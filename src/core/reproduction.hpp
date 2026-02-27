#pragma once

#include "core/types.hpp"
#include "core/plant.hpp"
#include "core/brain.hpp"
#include <vector>
#include <random>
#include <optional>

namespace pbg {

class World;

struct Seed {
    std::vector<uint8_t> genome;
    float energy = 0.0f;
    float water = 0.0f;
    float nutrients = 0.0f;
    GridCoord position{0, 0};
    Vec2 velocity{0.0f, 0.0f};
    uint64_t mother_id = 0;
    uint64_t father_id = 0;
    bool in_flight = false;
    uint16_t flight_ticks_remaining = 0;

    Seed() = default;
};

// Mate selection result
struct MateCandidate {
    uint64_t plant_id = 0;
    float score = 0.0f;
    float distance = 0.0f;
};

class ReproductionSystem {
public:
    // Perform mate selection for a plant based on its brain's search state
    // Returns the selected mate ID, or 0 if none found
    static uint64_t select_mate(
        const Plant& mother,
        const std::vector<Plant>& all_plants,
        const MateSearchState& search_state);

    // Create offspring genome using recombination
    static std::vector<uint8_t> recombine_genomes(
        const std::vector<uint8_t>& mother_genome,
        const std::vector<uint8_t>& father_genome,
        RecombinationMethod method,
        std::mt19937_64& rng);

    // Apply mutations to a genome
    static void apply_mutations(
        std::vector<uint8_t>& genome,
        float mutation_rate,
        uint8_t mutation_magnitude,
        std::mt19937_64& rng);

    // Create a seed from reproduction parameters
    static std::optional<Seed> create_seed(
        Plant& mother,
        const Plant* father,  // Can be null for asexual reproduction
        const QueuedAction::SeedParams& params,
        std::mt19937_64& rng);

    // Calculate landing position for a seed
    static GridCoord calculate_landing_position(
        const GridCoord& launch_pos,
        const QueuedAction::SeedParams& params,
        std::mt19937_64& rng);

    // Update seed position (for in-flight seeds)
    static void update_seed_flight(Seed& seed);

    // Try to germinate a seed at its current position
    // Returns new plant if successful, nullopt otherwise
    static std::optional<Plant> try_germinate(
        const Seed& seed,
        uint64_t new_plant_id,
        World& world);

private:
    // Calculate score for a potential mate
    static float calculate_mate_score(
        const Plant& mother,
        const Plant& candidate,
        const MateSearchState& search_state);

    // Get criterion value from a plant
    static float get_criterion_value(
        const Plant& plant,
        uint8_t criterion);
};

}  // namespace pbg
