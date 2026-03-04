#include "core/world.hpp"
#include <cmath>
#include <stdexcept>

namespace pbg {

World::World(uint32_t width, uint32_t height, uint64_t seed)
    : width_(width)
    , height_(height)
    , seed_(seed)
    , rng_(seed)
    , water_perlin_(seed)
    , nutrient_perlin_(seed + 12345)  // Different seed for nutrients
{
    cells_.resize(static_cast<size_t>(width_) * height_);
    initialize_terrain();
}

size_t World::index(int32_t x, int32_t y) const {
    return static_cast<size_t>(y) * width_ + static_cast<size_t>(x);
}

bool World::in_bounds(int32_t x, int32_t y) const {
    return x >= 0 && x < static_cast<int32_t>(width_) &&
           y >= 0 && y < static_cast<int32_t>(height_);
}

bool World::in_bounds(const GridCoord& coord) const {
    return in_bounds(coord.x, coord.y);
}

WorldCell& World::cell_at(int32_t x, int32_t y) {
    if (!in_bounds(x, y)) {
        throw std::out_of_range("World::cell_at: coordinates out of bounds");
    }
    return cells_[index(x, y)];
}

const WorldCell& World::cell_at(int32_t x, int32_t y) const {
    if (!in_bounds(x, y)) {
        throw std::out_of_range("World::cell_at: coordinates out of bounds");
    }
    return cells_[index(x, y)];
}

WorldCell& World::cell_at(const GridCoord& coord) {
    return cell_at(coord.x, coord.y);
}

const WorldCell& World::cell_at(const GridCoord& coord) const {
    return cell_at(coord.x, coord.y);
}

void World::initialize_terrain() {
    const auto& cfg = get_config();

    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            WorldCell& cell = cells_[index(x, y)];

            cell.water_level = water_perlin_.scaled_noise(
                static_cast<float>(x),
                static_cast<float>(y),
                cfg.water_perlin_scale,
                cfg.water_base,
                cfg.water_amplitude
            );
            
            // Set minimum water level
            if (cell.water_level < cfg.water_min) {
                cell.water_level = cfg.water_min;
            }

            cell.nutrient_level = nutrient_perlin_.scaled_noise(
                static_cast<float>(x),
                static_cast<float>(y),
                cfg.nutrient_perlin_scale,
                cfg.nutrient_base,
                cfg.nutrient_amplitude
            );

            // Clamp to non-negative values
            cell.water_level = std::max(0.0f, cell.water_level);
            cell.nutrient_level = std::max(0.0f, cell.nutrient_level);

            cell.light_level = current_light_multiplier();
            cell.fire_ticks = 0;
            cell.plant_id = 0;
        }
    }
}

void World::regenerate_terrain() {
    // Reset RNG and Perlin generators
    rng_.seed(seed_);
    water_perlin_.reseed(seed_);
    nutrient_perlin_.reseed(seed_ + 12345);

    initialize_terrain();
}

float World::current_light_multiplier() const {
    const auto& cfg = get_config();
    float phase = 2.0f * 3.14159265358979f * static_cast<float>(tick_) /
                  static_cast<float>(cfg.season_length);
    return cfg.base_light + cfg.light_amplitude * std::sin(phase);
}

void World::update_season() {
    float light = current_light_multiplier();
    for (auto& cell : cells_) {
        cell.light_level = light;
    }
}

void World::ignite(const GridCoord& coord) {
    if (!in_bounds(coord)) return;

    WorldCell& cell = cell_at(coord);
    const auto& cfg = get_config();

    // Only ignite cells that have something to burn
    if (!cell.is_occupied()) return;
    // Don't ignite if already on fire or too wet
    if (cell.fire_ticks > 0) return;
    if (cell.water_level >= cfg.fire_water_threshold) return;

    cell.fire_ticks = cfg.fire_destroy_ticks;
}

void World::update_fire() {
    const auto& cfg = get_config();

    burned_out_positions_.clear();
    std::vector<GridCoord> spread_sources;

    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            WorldCell& cell = cells_[index(x, y)];

            if (cell.fire_ticks == 0) continue;

            // Fire on a tile with no cell immediately goes out
            if (!cell.is_occupied()) {
                cell.fire_ticks = 0;
                continue;
            }

            --cell.fire_ticks;

            if (cell.fire_ticks == 0) {
                burned_out_positions_.push_back({static_cast<int32_t>(x), static_cast<int32_t>(y)});
            } else {
                uint16_t ticks_burned = cfg.fire_destroy_ticks - cell.fire_ticks;
                if (ticks_burned >= cfg.fire_spread_ticks) {
                    spread_sources.push_back({static_cast<int32_t>(x), static_cast<int32_t>(y)});
                }
            }
        }
    }

    // Spread to adjacent occupied, non-fireproof cells only
    static const GridCoord offsets[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (const auto& src : spread_sources) {
        for (const auto& off : offsets) {
            GridCoord neighbor = src + off;
            if (!in_bounds(neighbor)) continue;
            const WorldCell& ncell = cell_at(neighbor);
            if (!ncell.is_occupied() || ncell.is_fireproof()) continue;
            ignite(neighbor);
        }
    }
}

void World::advance_tick() {
    ++tick_;
    update_season();
    update_fire();
}

uint64_t World::rng_state() const {
    // Note: std::mt19937_64 doesn't have a simple state getter
    // For full determinism, we'd need to serialize the full state
    // This is a simplified version
    return tick_;  // Placeholder - full implementation would serialize RNG state
}

void World::set_rng_state(uint64_t state) {
    // Placeholder - full implementation would deserialize RNG state
    (void)state;
}

}  // namespace pbg
