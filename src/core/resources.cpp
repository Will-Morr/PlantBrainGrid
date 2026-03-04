#include "core/resources.hpp"
#include "core/world.hpp"
#include "core/config.hpp"
#include <queue>
#include <unordered_set>
#include <algorithm>

namespace pbg {

ResourceTickResult ResourceSystem::process_tick(Plant& plant, World& world) {
    ResourceTickResult result;

    if (!plant.is_alive()) {
        return result;
    }

    // 1. Generate resources from leaves and roots
    result.energy_generated = calculate_leaf_energy(plant, world);
    result.water_extracted = calculate_root_water(plant, world);
    result.nutrients_extracted = calculate_root_nutrients(plant, world);

    // Primary cell draws a small amount of water from its position
    const auto& cfg_tick = get_config();
    if (world.in_bounds(plant.primary_position())) {
        WorldCell& pwc = world.cell_at(plant.primary_position());
        result.water_extracted += pwc.water_level*cfg_tick.primary_water_rate;
    }

    // 2. Add generated resources to plant pool
    plant.resources().energy += result.energy_generated;
    plant.resources().water += result.water_extracted;
    plant.resources().nutrients += result.nutrients_extracted;

    // 3. Process xylem flow (resources move through enabled xylem)
    // Note: In this simplified model, we assume resources generated at
    // leaves/roots are added directly to the plant pool. Xylem affects
    // efficiency based on connectivity.
    Resources xylem_bonus = process_xylem_flow(plant, world);
    result.xylem_transfer_loss = xylem_bonus.energy;  // Track loss, not bonus

    // 4. Pay maintenance costs
    Resources maintenance = calculate_maintenance(plant);
    result.energy_maintenance = maintenance.energy;
    result.water_maintenance = maintenance.water;
    result.nutrients_maintenance = maintenance.nutrients;

    plant.resources().energy -= maintenance.energy;
    plant.resources().water -= maintenance.water;
    plant.resources().nutrients -= maintenance.nutrients;

    // Calculate net changes
    result.net_energy = result.energy_generated - result.energy_maintenance;
    result.net_water = result.water_extracted - result.water_maintenance;
    result.net_nutrients = result.nutrients_extracted - result.nutrients_maintenance;

    return result;
}

float ResourceSystem::calculate_leaf_energy(const Plant& plant, const World& world) {
    const auto& cfg = get_config();
    float total_energy = 0.0f;
    float light = world.current_light_multiplier();

    for (const auto& cell : plant.cells()) {
        if (!cell.enabled) continue;

        if (cell.type == CellType::SmallLeaf) {
            total_energy += cfg.small_leaf_energy_rate * light;
        } else if (cell.type == CellType::BigLeaf) {
            total_energy += cfg.big_leaf_energy_rate * light;
        }
    }

    return total_energy;
}

float ResourceSystem::calculate_root_water(const Plant& plant, World& world) {
    const auto& cfg = get_config();
    float total_water = 0.0f;

    for (const auto& cell : plant.cells()) {
        if (!cell.enabled) continue;

        if (cell.type == CellType::FiberRoot) {
            if (world.in_bounds(cell.position)) {
                WorldCell& wc = world.cell_at(cell.position);
                total_water += wc.water_level*cfg.fiber_root_water_rate;
            }
        } else if (cell.type == CellType::TapRoot) {
            if (world.in_bounds(cell.position)) {
                WorldCell& wc = world.cell_at(cell.position);
                total_water += wc.water_level*cfg.tap_root_water_rate;
            }
        }
    }

    return total_water;
}

float ResourceSystem::calculate_root_nutrients(const Plant& plant, World& world) {
    const auto& cfg = get_config();
    float total_nutrients = 0.0f;

    for (const auto& cell : plant.cells()) {
        if (!cell.enabled) continue;
        if (cell.type != CellType::FiberRoot) continue;  // TapRoot does not draw nutrients

        if (world.in_bounds(cell.position)) {
            WorldCell& wc = world.cell_at(cell.position);
            float extract = wc.nutrient_level*cfg.fiber_root_nutrient_rate;
            total_nutrients += extract;
        }
    }

    return total_nutrients;
}

Resources ResourceSystem::calculate_maintenance(const Plant& plant) {
    Resources total;

    for (const auto& cell : plant.cells()) {
        // All cells pay maintenance, even if disabled
        const CellCosts& cost = get_cell_costs(cell.type);
        total.energy += cost.maintain_energy;
        total.water += cost.maintain_water;
        total.nutrients += cost.maintain_nutrients;
    }

    return total;
}

std::unordered_map<GridCoord, std::vector<GridCoord>>
ResourceSystem::build_adjacency_graph(const Plant& plant) {
    std::unordered_map<GridCoord, std::vector<GridCoord>> adj;
    std::unordered_set<GridCoord> cell_positions;

    // Collect all cell positions
    for (const auto& cell : plant.cells()) {
        cell_positions.insert(cell.position);
        adj[cell.position] = {};
    }

    // Build adjacency
    static const GridCoord offsets[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (const auto& cell : plant.cells()) {
        for (const auto& offset : offsets) {
            GridCoord neighbor = cell.position + offset;
            if (cell_positions.count(neighbor)) {
                adj[cell.position].push_back(neighbor);
            }
        }
    }

    return adj;
}

std::vector<GridCoord> ResourceSystem::find_path_to_primary(
    const GridCoord& start,
    const std::unordered_set<GridCoord>& anchors,
    const std::unordered_map<GridCoord, std::vector<GridCoord>>& adj)
{
    // Anchors are the primary cell plus all xylem cells — xylem acts as relay
    // nodes equivalent to the primary for distance calculation.
    if (anchors.count(start)) {
        return {start};
    }

    std::unordered_map<GridCoord, GridCoord> parent;
    std::queue<GridCoord> queue;
    std::unordered_set<GridCoord> visited;

    queue.push(start);
    visited.insert(start);

    while (!queue.empty()) {
        GridCoord current = queue.front();
        queue.pop();

        if (anchors.count(current)) {
            // Reconstruct path
            std::vector<GridCoord> path;
            GridCoord node = current;
            while (node != start) {
                path.push_back(node);
                node = parent[node];
            }
            path.push_back(start);
            std::reverse(path.begin(), path.end());
            return path;
        }

        auto it = adj.find(current);
        if (it != adj.end()) {
            for (const auto& neighbor : it->second) {
                if (visited.count(neighbor) == 0) {
                    visited.insert(neighbor);
                    parent[neighbor] = current;
                    queue.push(neighbor);
                }
            }
        }
    }

    // No path found
    return {};
}

Resources ResourceSystem::process_xylem_flow(Plant& plant, World& /*world*/) {
    const auto& cfg = get_config();
    Resources total_loss;

    // For each xylem cell that is enabled, calculate flow efficiency
    // Xylem improves resource transport but costs some resources per hop
    int enabled_xylem_count = 0;
    for (const auto& cell : plant.cells()) {
        if (cell.enabled && cell.is_xylem()) {
            ++enabled_xylem_count;
            // Each active xylem costs a small amount of the throughput
            total_loss.energy += cfg.xylem_transfer_cost;
        }
    }

    // Deduct xylem operation cost from plant resources
    plant.resources().energy -= total_loss.energy;

    return total_loss;
}

}  // namespace pbg
