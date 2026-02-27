#include "core/plant_cell.hpp"
#include "core/config.hpp"

namespace pbg {

PlacementCost get_placement_cost(CellType type) {
    const auto& cfg = get_config();
    PlacementCost cost;

    switch (type) {
        case CellType::SmallLeaf:
            cost.energy = cfg.small_leaf_place_energy;
            break;
        case CellType::BigLeaf:
            cost.energy = cfg.big_leaf_place_energy;
            cost.nutrients = cfg.big_leaf_place_nutrients;
            break;
        case CellType::Root:
            cost.energy = cfg.root_place_energy;
            break;
        case CellType::Xylem:
            cost.energy = cfg.xylem_place_energy;
            break;
        case CellType::FireproofXylem:
            cost.energy = cfg.fireproof_xylem_place_energy;
            cost.nutrients = cfg.fireproof_xylem_place_nutrients;
            break;
        case CellType::Thorn:
            cost.energy = cfg.thorn_place_energy;
            break;
        case CellType::FireStarter:
            cost.energy = cfg.fire_starter_place_energy;
            break;
        default:
            break;
    }

    return cost;
}

MaintenanceCost get_maintenance_cost(CellType type) {
    const auto& cfg = get_config();
    MaintenanceCost cost;

    switch (type) {
        case CellType::Primary:
            cost.energy = cfg.primary_maintenance_energy;
            break;
        case CellType::SmallLeaf:
            cost.water = cfg.small_leaf_maintenance_water;
            break;
        case CellType::BigLeaf:
            cost.water = cfg.big_leaf_maintenance_water;
            cost.nutrients = cfg.big_leaf_maintenance_nutrients;
            break;
        case CellType::Root:
            cost.energy = cfg.root_maintenance_energy;
            break;
        case CellType::Xylem:
        case CellType::FireproofXylem:
            cost.energy = cfg.xylem_maintenance_energy;
            break;
        case CellType::Thorn:
            cost.energy = cfg.thorn_maintenance_energy;
            break;
        case CellType::FireStarter:
            // No maintenance - one time use
            break;
        default:
            break;
    }

    return cost;
}

}  // namespace pbg
