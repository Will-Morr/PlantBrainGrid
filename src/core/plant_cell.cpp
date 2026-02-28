#include "core/plant_cell.hpp"
#include "core/config.hpp"

namespace pbg {

const CellCosts& get_cell_costs(CellType type) {
    const auto& cfg = get_config();

    switch (type) {
        case CellType::Primary:        return cfg.primary_costs;
        case CellType::SmallLeaf:      return cfg.small_leaf_costs;
        case CellType::BigLeaf:        return cfg.big_leaf_costs;
        case CellType::Root:           return cfg.root_costs;
        case CellType::Xylem:          return cfg.xylem_costs;
        case CellType::FireproofXylem: return cfg.fireproof_xylem_costs;
        case CellType::Thorn:          return cfg.thorn_costs;
        case CellType::FireStarter:    return cfg.fire_starter_costs;
        default: {
            static const CellCosts empty{};
            return empty;
        }
    }
}

}  // namespace pbg
