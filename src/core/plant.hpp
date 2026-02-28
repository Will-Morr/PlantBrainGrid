#pragma once

#include "core/types.hpp"
#include "core/plant_cell.hpp"
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace pbg {

// Forward declarations
class World;
class Brain;

struct Resources {
    float energy = 0.0f;
    float water = 0.0f;
    float nutrients = 0.0f;

    Resources() = default;
    Resources(float e, float w, float n) : energy(e), water(w), nutrients(n) {}

    Resources operator+(const Resources& other) const {
        return {energy + other.energy, water + other.water, nutrients + other.nutrients};
    }

    Resources& operator+=(const Resources& other) {
        energy += other.energy;
        water += other.water;
        nutrients += other.nutrients;
        return *this;
    }

    Resources& operator-=(const Resources& other) {
        energy -= other.energy;
        water -= other.water;
        nutrients -= other.nutrients;
        return *this;
    }

    void clamp_non_negative() {
        if (energy < 0) energy = 0;
        if (water < 0) water = 0;
        if (nutrients < 0) nutrients = 0;
    }
};

class Plant {
public:
    Plant(uint64_t id, const GridCoord& primary_pos, const std::vector<uint8_t>& genome);
    ~Plant();

    // Disable copying (plants own resources)
    Plant(const Plant&) = delete;
    Plant& operator=(const Plant&) = delete;

    // Enable moving
    Plant(Plant&&) noexcept;
    Plant& operator=(Plant&&) noexcept;

    // Accessors
    uint64_t id() const { return id_; }
    const GridCoord& primary_position() const { return primary_pos_; }
    bool is_alive() const { return alive_; }
    uint64_t age() const { return age_; }
    const Resources& resources() const { return resources_; }
    Resources& resources() { return resources_; }
    const std::vector<PlantCell>& cells() const { return cells_; }
    size_t cell_count() const { return cells_.size(); }

    // Brain access
    Brain& brain();
    const Brain& brain() const;

    // Cell management
    bool can_place_cell(CellType type, const GridCoord& pos, const World& world) const;
    bool place_cell(CellType type, const GridCoord& pos, Direction dir, World& world);
    // Place cell without affordability check or cost payment (cost must be pre-paid externally)
    bool place_cell_free(CellType type, const GridCoord& pos, Direction dir, World& world);
    bool remove_cell(const GridCoord& pos, World& world);
    bool toggle_cell(const GridCoord& pos, bool enabled);
    bool rotate_cell(const GridCoord& pos, int rotation);

    // Force-deduct placement cost regardless of current resources (may go negative)
    void force_deduct_placement_cost(CellType type);

    // Find cell at position (returns nullptr if not found)
    PlantCell* find_cell(const GridCoord& pos);
    const PlantCell* find_cell(const GridCoord& pos) const;

    // Check if position is adjacent to any existing cell
    bool is_adjacent_to_plant(const GridCoord& pos) const;

    // Check if position is blocked by thorns (from other plants)
    static bool is_blocked_by_thorn(const GridCoord& pos, const World& world, uint64_t self_id);

    // Kill the plant
    void kill();

    // Tick age
    void advance_age() { ++age_; }

    // Pay resource cost
    bool pay_cost(const CellCosts& cost);
    bool pay_cost(float energy, float water, float nutrients);

private:
    uint64_t id_;
    GridCoord primary_pos_;
    bool alive_ = true;
    uint64_t age_ = 0;

    Resources resources_;
    std::vector<PlantCell> cells_;
    std::unordered_set<GridCoord> cell_positions_;  // For O(1) lookup

    std::unique_ptr<Brain> brain_;

    void add_cell_internal(const PlantCell& cell);
    void remove_cell_internal(const GridCoord& pos);
    bool place_cell_shape_ok(CellType type, const GridCoord& pos, const World& world) const;
    void do_place_cell_internal(CellType type, const GridCoord& pos, Direction dir, World& world);
};

}  // namespace pbg
