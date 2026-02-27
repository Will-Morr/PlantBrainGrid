#pragma once

#include <cstdint>

namespace pbg {

struct Config {
    // World
    uint32_t world_width = 512;
    uint32_t world_height = 512;
    float water_perlin_scale = 0.08f;
    float nutrient_perlin_scale = 0.10f;
    float water_base = 50.0f;
    float water_amplitude = 40.0f;
    float nutrient_base = 30.0f;
    float nutrient_amplitude = 25.0f;

    // Plants
    uint16_t brain_size = 1024;
    uint8_t vision_radius = 16;
    uint16_t max_instructions_per_tick = 1000;

    // Brain error penalties (non-fatal)
    float oob_memory_penalty = 0.5f;
    float instruction_limit_penalty = 5.0f;

    // Resources
    float xylem_transfer_cost = 0.05f;
    float small_leaf_energy_rate = 1.0f;
    float big_leaf_energy_rate = 3.0f;
    float big_leaf_water_cost = 2.0f;
    float big_leaf_nutrient_cost = 1.5f;
    float root_water_rate = 1.5f;
    float root_nutrient_rate = 0.5f;

    // Cell placement costs
    float small_leaf_place_energy = 10.0f;
    float big_leaf_place_energy = 25.0f;
    float big_leaf_place_nutrients = 10.0f;
    float root_place_energy = 8.0f;
    float xylem_place_energy = 12.0f;
    float fireproof_xylem_place_energy = 15.0f;
    float fireproof_xylem_place_nutrients = 20.0f;
    float thorn_place_energy = 15.0f;
    float fire_starter_place_energy = 30.0f;

    // Cell maintenance costs (per tick)
    float primary_maintenance_energy = 0.1f;
    float small_leaf_maintenance_water = 0.2f;
    float big_leaf_maintenance_water = 1.0f;
    float big_leaf_maintenance_nutrients = 0.5f;
    float root_maintenance_energy = 0.1f;
    float xylem_maintenance_energy = 0.05f;
    float thorn_maintenance_energy = 0.1f;

    // Reproduction
    float mutation_rate = 0.01f;
    uint8_t mutation_magnitude = 16;
    float max_mate_distance = 100.0f;
    float seed_launch_distance_per_energy = 2.0f;

    // Fire
    uint16_t fire_spread_ticks = 10;
    uint16_t fire_destroy_ticks = 50;
    float fire_water_threshold = 99999.0f;

    // Seasons
    uint32_t season_length = 1000;
    float base_light = 0.5f;
    float light_amplitude = 0.4f;

    // Scaling for brain sensing (convert float resources to 0-255 byte)
    float resource_sense_scale = 2.55f;  // 100 resource = 255 byte value
};

// Global config instance (can be modified before simulation starts)
inline Config& get_config() {
    static Config config;
    return config;
}

}  // namespace pbg
