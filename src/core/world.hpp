#pragma once

#include "core/types.hpp"
#include "core/config.hpp"
#include "core/perlin.hpp"
#include <vector>
#include <random>
#include <cstdint>

namespace pbg {

struct WorldCell {
    float water_level = 0.0f;
    float nutrient_level = 0.0f;
    float light_level = 1.0f;
    uint16_t fire_ticks = 0;  // 0 = not on fire
    uint64_t plant_id = 0;    // 0 = unoccupied
    CellType cell_type = CellType::Primary;  // valid only when plant_id != 0

    bool is_on_fire() const { return fire_ticks > 0; }
    bool is_occupied() const { return plant_id != 0; }
    bool is_fireproof() const { return cell_type == CellType::FireproofXylem; }
    bool blocks_placement() const { return cell_type == CellType::Thorn; }
};

class World {
public:
    World(uint32_t width, uint32_t height, uint64_t seed);

    // Accessors
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint64_t seed() const { return seed_; }
    uint64_t tick() const { return tick_; }

    // Cell access
    WorldCell& cell_at(int32_t x, int32_t y);
    const WorldCell& cell_at(int32_t x, int32_t y) const;
    WorldCell& cell_at(const GridCoord& coord);
    const WorldCell& cell_at(const GridCoord& coord) const;

    // Bounds checking
    bool in_bounds(int32_t x, int32_t y) const;
    bool in_bounds(const GridCoord& coord) const;

    // Season/light
    void update_season();
    float current_light_multiplier() const;

    // Fire
    void ignite(const GridCoord& coord);
    void update_fire();
    const std::vector<GridCoord>& burned_out_positions() const { return burned_out_positions_; }

    // Simulation tick
    void advance_tick();

    // Random number generation (for determinism)
    std::mt19937_64& rng() { return rng_; }
    uint64_t rng_state() const;
    void set_rng_state(uint64_t state);

    // Reset/regenerate terrain
    void regenerate_terrain();

private:
    uint32_t width_;
    uint32_t height_;
    uint64_t seed_;
    uint64_t tick_ = 0;

    std::vector<WorldCell> cells_;
    std::vector<GridCoord> burned_out_positions_;
    std::mt19937_64 rng_;
    PerlinNoise water_perlin_;
    PerlinNoise nutrient_perlin_;

    size_t index(int32_t x, int32_t y) const;
    void initialize_terrain();
};

}  // namespace pbg
