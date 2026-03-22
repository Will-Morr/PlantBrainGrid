#include "core/resources.hpp"
#include "core/world.hpp"
#include "core/config.hpp"
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
        result.water_extracted += pwc.water_level * cfg_tick.primary_water_rate
                                 * world.current_water_multiplier();
    }

    // 2. Add generated resources to plant pool
    plant.resources().energy += result.energy_generated;
    plant.resources().water += result.water_extracted;
    plant.resources().nutrients += result.nutrients_extracted;

    // 2.5 Clamp resources to storage caps
    clamp_to_caps(plant);

    // 3. Pay maintenance costs
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
    float water_mult = world.current_water_multiplier();

    for (const auto& cell : plant.cells()) {
        if (!cell.enabled) continue;

        if (cell.type == CellType::FiberRoot) {
            if (world.in_bounds(cell.position)) {
                WorldCell& wc = world.cell_at(cell.position);
                total_water += wc.water_level * cfg.fiber_root_water_rate * water_mult;
            }
        } else if (cell.type == CellType::TapRoot) {
            if (world.in_bounds(cell.position)) {
                WorldCell& wc = world.cell_at(cell.position);
                total_water += wc.water_level * cfg.tap_root_water_rate * water_mult;
            }
        }
    }

    return total_water;
}

float ResourceSystem::calculate_root_nutrients(const Plant& plant, World& world) {
    const auto& cfg = get_config();
    float total_nutrients = 0.0f;
    float nutrient_mult = world.current_nutrient_multiplier();

    for (const auto& cell : plant.cells()) {
        if (!cell.enabled) continue;
        if (cell.type != CellType::FiberRoot) continue;  // TapRoot does not draw nutrients

        if (world.in_bounds(cell.position)) {
            WorldCell& wc = world.cell_at(cell.position);
            float extract = wc.nutrient_level * cfg.fiber_root_nutrient_rate * nutrient_mult;
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

Resources ResourceSystem::calculate_caps(const Plant& plant) {
    const auto& cfg = get_config();
    Resources caps(cfg.base_resource_cap, cfg.base_resource_cap, cfg.base_resource_cap);

    for (const auto& cell : plant.cells()) {
        if (cell.type == CellType::StoreEnergy) {
            caps.energy += cfg.store_capacity_bonus;
        } else if (cell.type == CellType::StoreWater) {
            caps.water += cfg.store_capacity_bonus;
        } else if (cell.type == CellType::StoreNutrients) {
            caps.nutrients += cfg.store_capacity_bonus;
        }
    }

    return caps;
}

void ResourceSystem::clamp_to_caps(Plant& plant) {
    Resources caps = calculate_caps(plant);
    if (plant.resources().energy > caps.energy) plant.resources().energy = caps.energy;
    if (plant.resources().water > caps.water) plant.resources().water = caps.water;
    if (plant.resources().nutrients > caps.nutrients) plant.resources().nutrients = caps.nutrients;
}

}  // namespace pbg
