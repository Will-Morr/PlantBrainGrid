#include "core/reproduction.hpp"
#include "core/world.hpp"
#include "core/config.hpp"
#include "core/brain_ops.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace pbg {

uint64_t ReproductionSystem::select_mate(
    const Plant& mother,
    const std::vector<Plant>& all_plants,
    const MateSearchState& search_state,
    std::mt19937_64& rng)
{
    const auto& cfg = get_config();

    // Use the larger of the brain-specified search radius and the config default.
    // This lets the distance-bias criterion always find nearby mates even when no
    // MATE_BY_* opcodes were executed.
    float effective_max_dist = std::max(search_state.max_distance, cfg.max_mate_distance);

    // If there is nothing to rank candidates by, skip mate selection entirely.
    if (search_state.criteria.empty() && cfg.mate_distance_bias <= 0.0f) {
        return 0;
    }

    float best_score = -std::numeric_limits<float>::infinity();
    uint64_t best_mate_id = 0;

    for (const auto& candidate : all_plants) {
        // Skip self and dead plants
        if (candidate.id() == mother.id() || !candidate.is_alive()) {
            continue;
        }

        // Only allow candidates that have at least one enabled Anther cell
        bool has_anther = false;
        for (const auto& cell : candidate.cells()) {
            if (cell.type == CellType::Anther && cell.enabled) {
                has_anther = true;
                break;
            }
        }
        if (!has_anther) {
            continue;
        }

        // Check distance
        float dx = static_cast<float>(candidate.primary_position().x - mother.primary_position().x);
        float dy = static_cast<float>(candidate.primary_position().y - mother.primary_position().y);
        float distance = std::sqrt(dx * dx + dy * dy);

        if (distance > effective_max_dist) {
            continue;
        }

        float score = calculate_mate_score(mother, candidate, search_state, rng);

        if (score > best_score) {
            best_score = score;
            best_mate_id = candidate.id();
        }
    }

    return best_mate_id;
}

float ReproductionSystem::calculate_mate_score(
    const Plant& mother,
    const Plant& candidate,
    const MateSearchState& search_state,
    std::mt19937_64& rng)
{
    const auto& cfg = get_config();
    float score = 0.0f;

    float dx = static_cast<float>(candidate.primary_position().x - mother.primary_position().x);
    float dy = static_cast<float>(candidate.primary_position().y - mother.primary_position().y);
    float distance = std::sqrt(dx * dx + dy * dy);

    for (const auto& criterion : search_state.criteria) {
        float value = 0.0f;

        if (criterion.type == MATE_CRIT_DISTANCE) {
            value = search_state.max_distance - distance;
        } else if (criterion.type == MATE_CRIT_SIMILARITY || criterion.type == MATE_CRIT_DIFFERENCE) {
            const auto& m_genome = mother.brain().memory();
            const auto& c_genome = candidate.brain().memory();
            size_t min_size = std::min(m_genome.size(), c_genome.size());

            int matches = 0;
            for (size_t i = 0; i < min_size; ++i) {
                if (m_genome[i] == c_genome[i]) {
                    ++matches;
                }
            }

            float similarity = static_cast<float>(matches) / static_cast<float>(min_size);
            value = (criterion.type == MATE_CRIT_SIMILARITY) ? similarity : (1.0f - similarity);
            value *= 255.0f;  // Scale to match other criteria
        } else {
            value = get_criterion_value(candidate, criterion);
        }

        score += value * static_cast<float>(criterion.magnitude);
    }

    // Always apply distance bias: closer candidates score higher
    score -= distance * cfg.mate_distance_bias;

    // Add small random noise to break ties and introduce selection stochasticity
    if (cfg.mate_selection_noise > 0.0f) {
        std::uniform_real_distribution<float> noise_dist(-cfg.mate_selection_noise,
                                                          cfg.mate_selection_noise);
        score += noise_dist(rng);
    }

    return score;
}

float ReproductionSystem::get_criterion_value(const Plant& plant, const MateCriterion& criterion) {
    const auto& cfg = get_config();

    switch (criterion.type) {
        case MATE_CRIT_SIZE:
            return static_cast<float>(std::min(size_t{255}, plant.cell_count()));
        case MATE_CRIT_AGE:
            return static_cast<float>(std::min(uint64_t{255}, plant.age() / 100));
        case MATE_CRIT_ENERGY:
            return std::min(255.0f, plant.resources().energy * cfg.resource_sense_scale);
        case MATE_CRIT_WATER:
            return std::min(255.0f, plant.resources().water * cfg.resource_sense_scale);
        case MATE_CRIT_NUTRIENTS:
            return std::min(255.0f, plant.resources().nutrients * cfg.resource_sense_scale);
        case MATE_CRIT_CELL_COUNT: {
            // Count cells of the requested type; score peaks when count == target
            CellType target_type = static_cast<CellType>(criterion.param1 % 10);
            size_t count = 0;
            for (const auto& cell : plant.cells()) {
                if (cell.type == target_type) ++count;
            }
            float deviation = std::abs(static_cast<float>(count)
                                       - static_cast<float>(criterion.param2));
            return std::max(0.0f, 255.0f - deviation);
        }
        default:
            return 0.0f;
    }
}

RecombinationResult ReproductionSystem::recombine_genomes(
    const std::vector<uint8_t>& mother_active,
    const std::vector<uint8_t>& mother_inactive,
    const std::vector<uint8_t>& father_active,
    const std::vector<uint8_t>& father_inactive,
    RecombinationMethod method,
    std::mt19937_64& rng)
{
    size_t size = std::max({mother_active.size(), mother_inactive.size(),
                           father_active.size(), father_inactive.size()});
    RecombinationResult result;
    result.active.resize(size, 0);
    result.inactive.resize(size, 0);

    auto safe_get = [](const std::vector<uint8_t>& v, size_t i) -> uint8_t {
        return (i < v.size()) ? v[i] : 0;
    };

    std::uniform_real_distribution<float> prob(0.0f, 1.0f);

    for (size_t i = 0; i < size; ++i) {
        // Each parent randomly sends from their active or inactive copy
        uint8_t m_byte = (prob(rng) < 0.5f)
            ? safe_get(mother_active, i) : safe_get(mother_inactive, i);
        uint8_t f_byte = (prob(rng) < 0.5f)
            ? safe_get(father_active, i) : safe_get(father_inactive, i);

        // Recombination method picks which parent's byte goes to active;
        // the other goes to inactive.
        bool pick_mother;
        switch (method) {
            case RecombinationMethod::Mother75:
                pick_mother = prob(rng) < 0.75f;
                break;
            case RecombinationMethod::Father75:
                pick_mother = prob(rng) < 0.25f;
                break;
            case RecombinationMethod::RandomMix:
                pick_mother = prob(rng) < 0.5f;
                break;
            case RecombinationMethod::Alternating:
                pick_mother = (i % 2 == 0);
                break;
        }

        if (pick_mother) {
            result.active[i] = m_byte;
            result.inactive[i] = f_byte;
        } else {
            result.active[i] = f_byte;
            result.inactive[i] = m_byte;
        }
    }

    return result;
}

void ReproductionSystem::apply_mutations(
    std::vector<uint8_t>& genome,
    float mutation_rate,
    std::mt19937_64& rng)
{
    if (genome.empty()) return;

    const auto& cfg = get_config();
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    // Per-byte point mutations
    for (auto& byte : genome) {
        if (prob(rng) < mutation_rate) {
            byte = static_cast<uint8_t>(byte_dist(rng));
        }
    }

    // Block mutation: every seed gets one contiguous block either randomized
    // or copied from another location within the genome.
    size_t genome_size = genome.size();
    size_t min_block = static_cast<size_t>(cfg.mutation_block_min_size);
    size_t max_block = std::min(static_cast<size_t>(cfg.mutation_block_max_size), genome_size);
    min_block = std::min(min_block, max_block);

    std::uniform_int_distribution<uint32_t> size_dist(
        static_cast<uint32_t>(min_block),
        static_cast<uint32_t>(max_block));
    size_t block_size = static_cast<size_t>(size_dist(rng));

    std::uniform_int_distribution<uint32_t> pos_dist(
        0u, static_cast<uint32_t>(genome_size - block_size));
    size_t dest = static_cast<size_t>(pos_dist(rng));

    if (prob(rng) < 0.5f) {
        // Randomize the block
        for (size_t i = 0; i < block_size; ++i) {
            genome[dest + i] = static_cast<uint8_t>(byte_dist(rng));
        }
    } else {
        // Copy from another location (use temp buffer to handle overlap)
        size_t src = static_cast<size_t>(pos_dist(rng));
        std::vector<uint8_t> tmp(genome.begin() + src,
                                 genome.begin() + src + block_size);
        std::copy(tmp.begin(), tmp.end(), genome.begin() + dest);
    }
}

std::optional<Seed> ReproductionSystem::create_seed(
    Plant& mother,
    const Plant& father,
    const QueuedAction::SeedParams& params,
    std::mt19937_64& rng)
{
    const auto& cfg = get_config();

    // Scale byte values to actual resource amounts
    float energy_cost = static_cast<float>(params.energy) / cfg.resource_sense_scale;
    float water_cost = static_cast<float>(params.water) / cfg.resource_sense_scale;
    float nutrient_cost = static_cast<float>(params.nutrients) / cfg.resource_sense_scale;
    float launch_cost = static_cast<float>(params.launch_power);

    // Reduce cost if mother can't afford full launch
    launch_cost = std::min(launch_cost, mother.resources().energy);
    mother.resources().energy -= launch_cost;

    // Reduce cost if mother can't afford full resource allocation
    energy_cost = std::min(energy_cost, mother.resources().energy);
    water_cost = std::min(water_cost, mother.resources().water);
    nutrient_cost = std::min(nutrient_cost, mother.resources().nutrients);

    // Deduct resources from mother
    mother.resources().energy -= energy_cost;
    mother.resources().water -= water_cost;
    mother.resources().nutrients -= nutrient_cost;

    // Create offspring genomes via recombination (both active and inactive)
    RecombinationResult offspring = recombine_genomes(
        mother.brain().memory(),
        mother.brain().inactive_memory(),
        father.brain().memory(),
        father.brain().inactive_memory(),
        params.recomb_method,
        rng);

    // Apply mutations to both active and inactive genomes
    apply_mutations(offspring.active, cfg.mutation_rate, rng);
    apply_mutations(offspring.inactive, cfg.mutation_rate, rng);

    // Create seed
    Seed seed;
    seed.genome = std::move(offspring.active);
    seed.inactive_genome = std::move(offspring.inactive);
    seed.energy = energy_cost;
    seed.water = water_cost;
    seed.nutrients = nutrient_cost;
    seed.mother_id = mother.id();
    seed.father_id = father.id();
    seed.position = mother.primary_position();

    // Calculate landing position
    GridCoord landing = calculate_landing_position(mother.primary_position(), params, rng);

    // Set up flight
    float dx = static_cast<float>(landing.x - seed.position.x);
    float dy = static_cast<float>(landing.y - seed.position.y);
    float distance = std::sqrt(dx * dx + dy * dy);

    if (distance > 0.1f) {
        seed.in_flight = true;
        seed.flight_ticks_remaining = static_cast<uint16_t>(std::max(1.0f, distance / 2.0f));
        seed.velocity = Vec2{dx / seed.flight_ticks_remaining, dy / seed.flight_ticks_remaining};
    } else {
        seed.in_flight = false;
        seed.position = landing;
    }

    return seed;
}

GridCoord ReproductionSystem::calculate_landing_position(
    const GridCoord& launch_pos,
    const QueuedAction::SeedParams& params,
    std::mt19937_64& rng)
{
    const auto& cfg = get_config();
    float max_distance = static_cast<float>(params.launch_power) * cfg.seed_launch_distance_per_energy;

    if (params.placement_mode == SeedPlacementMode::Random) {
        // Random position within radius
        std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159265f);
        std::uniform_real_distribution<float> dist_dist(0.0f, max_distance);

        float angle = angle_dist(rng);
        float distance = dist_dist(rng);

        int dx = static_cast<int>(std::round(distance * std::cos(angle)));
        int dy = static_cast<int>(std::round(distance * std::sin(angle)));

        return {launch_pos.x + dx, launch_pos.y + dy};
    } else {
        // Exact direction
        float dx = static_cast<float>(params.dx);
        float dy = static_cast<float>(params.dy);
        float len = std::sqrt(dx * dx + dy * dy);

        if (len < 0.1f) {
            return launch_pos;
        }

        // Normalize and scale by max distance
        dx = (dx / len) * max_distance;
        dy = (dy / len) * max_distance;

        return {
            launch_pos.x + static_cast<int>(std::round(dx)),
            launch_pos.y + static_cast<int>(std::round(dy))
        };
    }
}

void ReproductionSystem::update_seed_flight(Seed& seed) {
    if (!seed.in_flight || seed.flight_ticks_remaining == 0) {
        seed.in_flight = false;
        return;
    }

    seed.position.x += static_cast<int>(std::round(seed.velocity.x));
    seed.position.y += static_cast<int>(std::round(seed.velocity.y));
    --seed.flight_ticks_remaining;

    if (seed.flight_ticks_remaining == 0) {
        seed.in_flight = false;
    }
}

std::optional<Plant> ReproductionSystem::try_germinate(
    const Seed& seed,
    uint64_t new_plant_id,
    World& world)
{
    // Check if position is valid and unoccupied
    if (!world.in_bounds(seed.position)) {
        return std::nullopt;
    }

    if (world.cell_at(seed.position).is_occupied()) {
        return std::nullopt;
    }

    if (world.cell_at(seed.position).is_on_fire()) {
        return std::nullopt;
    }

    // Create new plant with both active and inactive genomes
    Plant new_plant(new_plant_id, seed.position, seed.genome, seed.inactive_genome);
    new_plant.resources().energy = seed.energy;
    new_plant.resources().water = seed.water;
    new_plant.resources().nutrients = seed.nutrients;

    return new_plant;
}

}  // namespace pbg
