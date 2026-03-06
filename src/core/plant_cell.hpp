#pragma once

#include "core/types.hpp"
#include "core/config.hpp"
#include <cstdint>

namespace pbg {

struct PlantCell {
    CellType type = CellType::Empty;
    GridCoord position{0, 0};
    bool enabled = true;
    uint64_t plant_id = 0;  // Owner plant ID
    uint64_t age_ticks = 0; // How many ticks this cell has been alive

    PlantCell() = default;
    PlantCell(CellType type_, const GridCoord& pos_)
        : type(type_), position(pos_) {}

    bool is_anther() const {
        return type == CellType::Anther;
    }

    bool is_leaf() const {
        return type == CellType::SmallLeaf || type == CellType::BigLeaf;
    }

    bool is_fireproof() const {
        return type == CellType::Bark;
    }

    bool blocks_placement() const {
        return type == CellType::Thorn;
    }
};

// Returns build and maintenance costs for a given cell type
const CellCosts& get_cell_costs(CellType type);

}  // namespace pbg
