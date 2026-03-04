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
    const MateSearchState& search_state)
{
    if (search_state.weights.empty()) {
        return 0;  // No criteria specified
    }

    float best_score = -std::numeric_limits<float>::infinity();
    uint64_t best_mate_id = 0;

    for (const auto& candidate : all_plants) {
        // Skip self and dead plants
        if (candidate.id() == mother.id() || !candidate.is_alive()) {
            continue;
        }

        // Check distance
        float dx = static_cast<float>(candidate.primary_position().x - mother.primary_position().x);
        float dy = static_cast<float>(candidate.primary_position().y - mother.primary_position().y);
        float distance = std::sqrt(dx * dx + dy * dy);

        if (distance > search_state.max_distance) {
            continue;
        }

        float score = calculate_mate_score(mother, candidate, search_state);

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
    const MateSearchState& search_state)
{
    float score = 0.0f;

    float dx = static_cast<float>(candidate.primary_position().x - mother.primary_position().x);
    float dy = static_cast<float>(candidate.primary_position().y - mother.primary_position().y);
    float distance = std::sqrt(dx * dx + dy * dy);

    for (const auto& [criterion, weight] : search_state.weights) {
        float value = get_criterion_value(candidate, criterion);

        // For distance, invert so closer is better
        if (criterion == MATE_CRITERION_DISTANCE) {
            value = search_state.max_distance - distance;
        }

        // For similarity/difference, compare genomes
        if (criterion == MATE_CRITERION_SIMILARITY || criterion == MATE_CRITERION_DIFFERENCE) {
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
            value = (criterion == MATE_CRITERION_SIMILARITY) ? similarity : (1.0f - similarity);
            value *= 255.0f;  // Scale to match other criteria
        }

        score += value * static_cast<float>(weight);
    }

    // Always apply distance bias: closer candidates score higher
    score -= distance * get_config().mate_distance_bias;

    return score;
}

float ReproductionSystem::get_criterion_value(const Plant& plant, uint8_t criterion) {
    const auto& cfg = get_config();

    switch (criterion) {
        case MATE_CRITERION_SIZE:
            return static_cast<float>(std::min(255UL, plant.cell_count()));
        case MATE_CRITERION_AGE:
            return static_cast<float>(std::min(255UL, plant.age() / 100));
        case MATE_CRITERION_ENERGY:
            return std::min(255.0f, plant.resources().energy * cfg.resource_sense_scale);
        case MATE_CRITERION_WATER:
            return std::min(255.0f, plant.resources().water * cfg.resource_sense_scale);
        case MATE_CRITERION_NUTRIENTS:
            return std::min(255.0f, plant.resources().nutrients * cfg.resource_sense_scale);
        default:
            return 0.0f;
    }
}

std::vector<uint8_t> ReproductionSystem::recombine_genomes(
    const std::vector<uint8_t>& mother_genome,
    const std::vector<uint8_t>& father_genome,
    RecombinationMethod method,
    std::mt19937_64& rng)
{
    size_t size = std::max(mother_genome.size(), father_genome.size());
    std::vector<uint8_t> offspring(size, 0);

    std::uniform_real_distribution<float> prob(0.0f, 1.0f);

    for (size_t i = 0; i < size; ++i) {
        uint8_t m_byte = (i < mother_genome.size()) ? mother_genome[i] : 0;
        uint8_t f_byte = (i < father_genome.size()) ? father_genome[i] : 0;

        switch (method) {
            case RecombinationMethod::MotherOnly:
                offspring[i] = m_byte;
                break;

            case RecombinationMethod::FatherOnly:
                offspring[i] = f_byte;
                break;

            case RecombinationMethod::Mother75:
                offspring[i] = (prob(rng) < 0.75f) ? m_byte : f_byte;
                break;

            case RecombinationMethod::Father75:
                offspring[i] = (prob(rng) < 0.75f) ? f_byte : m_byte;
                break;

            case RecombinationMethod::HalfHalf:
                offspring[i] = (i < size / 2) ? m_byte : f_byte;
                break;

            case RecombinationMethod::RandomMix:
                offspring[i] = (prob(rng) < 0.5f) ? m_byte : f_byte;
                break;

            case RecombinationMethod::Alternating:
                offspring[i] = (i % 2 == 0) ? m_byte : f_byte;
                break;
        }
    }

    return offspring;
}

void ReproductionSystem::apply_mutations(
    std::vector<uint8_t>& genome,
    float mutation_rate,
    uint8_t /*mutation_magnitude*/,
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
    const Plant* father,
    const QueuedAction::SeedParams& params,
    std::mt19937_64& rng)
{
    const auto& cfg = get_config();

    // Scale byte values to actual resource amounts
    float energy_cost = static_cast<float>(params.energy) / cfg.resource_sense_scale;
    float water_cost = static_cast<float>(params.water) / cfg.resource_sense_scale;
    float nutrient_cost = static_cast<float>(params.nutrients) / cfg.resource_sense_scale;
    float launch_cost = static_cast<float>(params.launch_power);

    // Check if mother can afford it
    if (mother.resources().energy < energy_cost + launch_cost ||
        mother.resources().water < water_cost ||
        mother.resources().nutrients < nutrient_cost) {
        return std::nullopt;
    }

    // Deduct resources from mother
    mother.resources().energy -= energy_cost + launch_cost;
    mother.resources().water -= water_cost;
    mother.resources().nutrients -= nutrient_cost;

    // Create offspring genome
    std::vector<uint8_t> offspring_genome;
    if (father) {
        offspring_genome = recombine_genomes(
            mother.brain().memory(),
            father->brain().memory(),
            params.recomb_method,
            rng);
    } else {
        offspring_genome = mother.brain().memory();
    }

    // Apply mutations
    apply_mutations(offspring_genome, cfg.mutation_rate, cfg.mutation_magnitude, rng);

    // Create seed
    Seed seed;
    seed.genome = std::move(offspring_genome);
    seed.energy = energy_cost;
    seed.water = water_cost;
    seed.nutrients = nutrient_cost;
    seed.mother_id = mother.id();
    seed.father_id = father ? father->id() : 0;
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

    // Create new plant
    Plant new_plant(new_plant_id, seed.position, seed.genome);
    new_plant.resources().energy = seed.energy;
    new_plant.resources().water = seed.water;
    new_plant.resources().nutrients = seed.nutrients;

    return new_plant;
}

}  // namespace pbg
