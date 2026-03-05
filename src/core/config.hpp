#pragma once

#include <cstdint>

namespace pbg {

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
    float nutrient_base = 0.75f;
    float nutrient_amplitude = 3.0f;

    // Plants
    uint16_t brain_size = 1024;
    uint8_t vision_radius = 16;
    uint16_t max_instructions_per_tick = 1000;

    // Brain error penalties (non-fatal)
    float oob_memory_penalty = 0.5f;
    float instruction_limit_penalty = 5.0f;

    // Resources (income rates)
    float xylem_transfer_cost = 0.05f;
    float small_leaf_energy_rate = 1.0f;
    float big_leaf_energy_rate = 5.0f;
    float big_leaf_water_cost = 2.0f;
    float big_leaf_nutrient_cost = 1.5f;
    float primary_water_rate = 0.2f;       // small water draw from primary cell
    float fiber_root_water_rate = 1.5f;
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
    CellCosts xylem_costs           = { 12,  0,   0,   0.05,  0,    0   };
    CellCosts fireproof_xylem_costs = { 5,   0,   20,  0.05,  0,    0   };
    CellCosts thorn_costs           = { 5,   0,   0,   0,     0.01, 0   };
    CellCosts fire_starter_costs    = { 30,  0,   0,   0,     0,    0   };
    
    // Reproduction
    float mutation_rate = 0.01f;
    uint8_t mutation_magnitude = 16;
    uint16_t mutation_block_min_size = 4;   // min bytes in per-seed block mutation
    uint16_t mutation_block_max_size = 64;  // max bytes in per-seed block mutation
    float max_mate_distance = 100.0f;
    float mate_distance_bias = 1.0f;  // Score penalty per unit of distance (always applied)
    float seed_launch_distance_per_energy = 2.0f;

    // Fire
    uint16_t fire_spread_ticks = 6;
    uint16_t fire_destroy_ticks = 12;
    float fire_water_threshold = 999.9f;

    // Old age
    uint32_t max_cell_age = 1000;   // ticks before a cell dies of old age (0 = disabled)
    uint32_t max_plant_age = 5000; // ticks before a plant dies of old age (0 = disabled)

    // Seasons
    uint32_t season_length = 500;
    float base_light = 1.0f;
    float light_amplitude = 0.75f;

    // Scaling for brain sensing (convert float resources to 0-255 byte)
    float resource_sense_scale = 2.55f;  // 100 resource = 255 byte value
};

// Global config instance (can be modified before simulation starts)
inline Config& get_config() {
    static Config config;
    return config;
}

}  // namespace pbg
