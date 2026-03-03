#pragma once

#include "core/types.hpp"
#include "core/config.hpp"
#include <cstdint>

namespace pbg {

struct PlantCell {
    CellType type = CellType::Empty;
    GridCoord position{0, 0};
    bool enabled = true;
    Direction direction = Direction::North;  // For xylem: resource flow direction
    uint64_t plant_id = 0;  // Owner plant ID
    uint64_t age_ticks = 0; // How many ticks this cell has been alive

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

// Returns build and maintenance costs for a given cell type
const CellCosts& get_cell_costs(CellType type);

}  // namespace pbg
