#pragma once

#include "core/types.hpp"
#include <cstdint>

namespace pbg {

struct PlantCell {
    CellType type = CellType::Empty;
    GridCoord position{0, 0};
    bool enabled = true;
    Direction direction = Direction::North;  // For xylem: resource flow direction
    uint64_t plant_id = 0;  // Owner plant ID

    PlantCell() = default;
    PlantCell(CellType type_, const GridCoord& pos_, Direction dir_ = Direction::North)
        : type(type_), position(pos_), direction(dir_) {}

    bool is_xylem() const {
        return type == CellType::Xylem || type == CellType::FireproofXylem;
    }

    bool is_leaf() const {
        return type == CellType::SmallLeaf || type == CellType::BigLeaf;
    }

    bool is_fireproof() const {
        return type == CellType::FireproofXylem;
    }

    bool blocks_placement() const {
        return type == CellType::Thorn;
    }
};

// Cell placement cost calculation
struct PlacementCost {
    float energy = 0.0f;
    float water = 0.0f;
    float nutrients = 0.0f;

    bool can_afford(float e, float w, float n) const {
        return e >= energy && w >= water && n >= nutrients;
    }
};

PlacementCost get_placement_cost(CellType type);

// Cell maintenance cost (per tick)
struct MaintenanceCost {
    float energy = 0.0f;
    float water = 0.0f;
    float nutrients = 0.0f;
};

MaintenanceCost get_maintenance_cost(CellType type);

}  // namespace pbg
