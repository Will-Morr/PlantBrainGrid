#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pbg {

// A discrete season with multipliers for light, water, and nutrients.
// Seasons are ordered; start_tick is the offset within the cycle.
struct SeasonDef {
    std::string name;
    uint32_t start_tick = 0;
    float light_mult = 1.0f;
    float water_mult = 1.0f;
    float nutrient_mult = 1.0f;
};

struct CellCosts {
    float build_energy = 0.0f;
    float build_water = 0.0f;
    float build_nutrients = 0.0f;
    float maintain_energy = 0.0f;
    float maintain_water = 0.0f;
    float maintain_nutrients = 0.0f;
};

struct Config {
    // World
    uint32_t world_width = 512;
    uint32_t world_height = 512;
    float water_perlin_scale = 0.02f;
    float nutrient_perlin_scale = 0.015f;
    float water_base = 0.8f;
    float water_amplitude = 2.0f;
    float water_min = 0.15f;
    float nutrient_base = 0.0f;
    float nutrient_amplitude = 3.0f;

    // Plants
    uint16_t brain_size = 1024;
    uint8_t vision_radius = 16;
    uint16_t max_instructions_per_tick = 1000;

    // Brain error penalties (non-fatal)
    float oob_memory_penalty = 0.5f;
    float instruction_limit_penalty = 5.0f;

    // Resources (income rates)
    float small_leaf_energy_rate = 1.0f;
    float big_leaf_energy_rate = 5.0f;
    float big_leaf_water_cost = 2.0f;
    float big_leaf_nutrient_cost = 1.5f;
    float primary_water_rate = 0.2f;       // small water draw from primary cell
    float fiber_root_water_rate = 1.2f;
    float fiber_root_nutrient_rate = 1.0f;
    float tap_root_water_rate = 3.5f;      // tap root draws more water, no nutrients

    // Cell costs (build and maintenance per cell type)
    //                                    build                  maintain
    //                              energy  water  nutrients  energy  water  nutrients
    CellCosts primary_costs         = { 10,  0,   0,   0.1,   0,    0   };
    CellCosts small_leaf_costs      = { 10,  0,   0,   0,     0.2,  0   };
    CellCosts big_leaf_costs        = { 25,  0,   10,  0,     0.4,  0.3 };
    CellCosts fiber_root_costs      = { 8,   0,   0,   0.2,   0,    0   };
    CellCosts tap_root_costs        = { 12,  0,   0,   0.1,   0,    0   };
    CellCosts anther_costs          = { 10,  0,   0,   0.2,   0,    0   };
    CellCosts bark_costs            = { 0,   1,   1,   0,     0.01, 0.01};
    CellCosts thorn_costs           = { 5,   0,   0,   0,     0.01, 0   };
    CellCosts fire_starter_costs    = { 30,  0,   0,   0,     0,    0   };
    CellCosts store_energy_costs   = { 10,  0,   0,   0,     0.02, 0   };
    CellCosts store_water_costs    = { 10,  0,   0,   0,     0.02, 0   };
    CellCosts store_nutrients_costs= { 10,  0,   0,   0,     0.02, 0   };
    CellCosts haustorium_costs     = { 10,  0,   0,   0.1,  0,    0   };

    // Resource storage caps
    float base_resource_cap = 300.0f;         // Base max for each resource
    float store_capacity_bonus = 300.0f;      // Extra cap per store cell

    // Haustorium
    float haustorium_steal_rate = 0.5f;       // Resources stolen per adjacent enemy per tick

    // Reproduction
    float mutation_rate = 0.001f;
    uint16_t mutation_block_min_size = 1;   // min bytes in per-seed block mutation
    uint16_t mutation_block_max_size = 8;  // max bytes in per-seed block mutation
    float max_mate_distance = 100.0f;
    float mate_distance_bias = 1.0f;   // Score penalty per unit of distance (always applied)
    float mate_selection_noise = 5.0f; // Random noise added to each candidate's score
    float seed_launch_distance_per_energy = 2.0f;

    // Fire
    uint16_t fire_spread_ticks = 6;
    uint16_t fire_destroy_ticks = 12;
    float fire_water_threshold = 999.9f;

    // Old age
    uint32_t max_cell_age = 1000;   // ticks before a cell dies of old age (0 = disabled)
    uint32_t max_plant_age = 2500; // ticks before a plant dies of old age (0 = disabled)

    // Seasons (discrete)
    uint32_t season_cycle_length = 800;
    std::vector<SeasonDef> seasons = {
        {"Spring",  0,   1.0f, 2.0f, 1.0f},
        {"Summer",  200, 2.0f, 1.0f, 1.0f},
        {"Fall",    400, 0.7f, 1.0f, 1.0f},
        {"Winter",  600, 0.3f, 0.0f, 1.0f},
    };

    // Scaling for brain sensing (convert float resources to 0-255 byte)
    float resource_sense_scale = 2.55f;  // 100 resource = 255 byte value
};

// Global config instance (can be modified before simulation starts)
inline Config& get_config() {
    static Config config;
    return config;
}

}  // namespace pbg
