#include "core/plant.hpp"
#include "core/brain.hpp"
#include "core/world.hpp"
#include "core/config.hpp"
#include <algorithm>

namespace pbg {

Plant::Plant(uint64_t id, const GridCoord& primary_pos, const std::vector<uint8_t>& genome)
    : id_(id)
    , primary_pos_(primary_pos)
    , brain_(std::make_unique<Brain>(genome))
{
    // Create primary cell
    PlantCell primary;
    primary.type = CellType::Primary;
    primary.position = primary_pos;
    primary.plant_id = id_;
    primary.enabled = true;

    add_cell_internal(primary);
}

Plant::~Plant() = default;

Plant::Plant(Plant&&) noexcept = default;
Plant& Plant::operator=(Plant&&) noexcept = default;

Brain& Plant::brain() {
    return *brain_;
}

const Brain& Plant::brain() const {
    return *brain_;
}

void Plant::add_cell_internal(const PlantCell& cell) {
    cells_.push_back(cell);
    cells_.back().plant_id = id_;
    cell_positions_.insert(cell.position);
}

void Plant::remove_cell_internal(const GridCoord& pos) {
    auto it = std::find_if(cells_.begin(), cells_.end(),
        [&pos](const PlantCell& c) { return c.position == pos; });

    if (it != cells_.end()) {
        cell_positions_.erase(pos);
        cells_.erase(it);
    }
}

PlantCell* Plant::find_cell(const GridCoord& pos) {
    auto it = std::find_if(cells_.begin(), cells_.end(),
        [&pos](const PlantCell& c) { return c.position == pos; });
    return it != cells_.end() ? &(*it) : nullptr;
}

const PlantCell* Plant::find_cell(const GridCoord& pos) const {
    auto it = std::find_if(cells_.begin(), cells_.end(),
        [&pos](const PlantCell& c) { return c.position == pos; });
    return it != cells_.end() ? &(*it) : nullptr;
}

bool Plant::is_adjacent_to_plant(const GridCoord& pos) const {
    static const GridCoord offsets[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

    for (const auto& offset : offsets) {
        GridCoord neighbor = pos + offset;
        if (cell_positions_.count(neighbor) > 0) {
            return true;
        }
    }
    return false;
}

bool Plant::is_blocked_by_thorn(const GridCoord& pos, const World& world, uint64_t self_id) {
    static const GridCoord offsets[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

    for (const auto& offset : offsets) {
        GridCoord neighbor = pos + offset;
        if (!world.in_bounds(neighbor)) continue;

        const WorldCell& wc = world.cell_at(neighbor);
        if (wc.occupant && wc.occupant->plant_id != self_id &&
            wc.occupant->blocks_placement()) {
            return true;
        }
    }
    return false;
}

bool Plant::place_cell_shape_ok(CellType type, const GridCoord& pos, const World& world) const {
    if (!alive_) return false;
    if (!is_valid_placeable_cell(type)) return false;
    if (!world.in_bounds(pos)) return false;
    if (world.cell_at(pos).is_occupied()) return false;
    if (!is_adjacent_to_plant(pos)) return false;
    if (is_blocked_by_thorn(pos, world, id_)) return false;
    return true;
}

void Plant::do_place_cell_internal(CellType type, const GridCoord& pos, Direction dir, World& world) {
    PlantCell cell(type, pos, dir);
    cell.plant_id = id_;
    add_cell_internal(cell);
    world.cell_at(pos).occupant = &cells_.back();

    if (type == CellType::FireStarter) {
        static const GridCoord offsets[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        for (const auto& offset : offsets) {
            world.ignite(pos + offset);
        }
    }
}

bool Plant::can_place_cell(CellType type, const GridCoord& pos, const World& world) const {
    if (!place_cell_shape_ok(type, pos, world)) return false;
    PlacementCost cost = get_placement_cost(type);
    return cost.can_afford(resources_.energy, resources_.water, resources_.nutrients);
}

bool Plant::place_cell(CellType type, const GridCoord& pos, Direction dir, World& world) {
    if (!can_place_cell(type, pos, world)) return false;
    PlacementCost cost = get_placement_cost(type);
    if (!pay_cost(cost)) return false;
    do_place_cell_internal(type, pos, dir, world);
    return true;
}

bool Plant::place_cell_free(CellType type, const GridCoord& pos, Direction dir, World& world) {
    if (!place_cell_shape_ok(type, pos, world)) return false;
    do_place_cell_internal(type, pos, dir, world);
    return true;
}

void Plant::force_deduct_placement_cost(CellType type) {
    PlacementCost cost = get_placement_cost(type);
    resources_.energy -= cost.energy;
    resources_.water -= cost.water;
    resources_.nutrients -= cost.nutrients;
}

bool Plant::remove_cell(const GridCoord& pos, World& world) {
    if (!alive_) return false;

    // Can't remove primary cell
    if (pos == primary_pos_) return false;

    PlantCell* cell = find_cell(pos);
    if (!cell) return false;

    // Clear world grid reference and extinguish fire
    if (world.in_bounds(pos)) {
        WorldCell& wc = world.cell_at(pos);
        wc.occupant = nullptr;
        wc.fire_ticks = 0;
    }

    remove_cell_internal(pos);
    return true;
}

bool Plant::toggle_cell(const GridCoord& pos, bool enabled) {
    if (!alive_) return false;

    PlantCell* cell = find_cell(pos);
    if (!cell) return false;

    cell->enabled = enabled;
    return true;
}

bool Plant::rotate_cell(const GridCoord& pos, int rotation) {
    if (!alive_) return false;

    PlantCell* cell = find_cell(pos);
    if (!cell) return false;

    // Only xylem cells have meaningful direction
    if (!cell->is_xylem()) return false;

    int current = static_cast<int>(cell->direction);
    int new_dir = (current + rotation) % 4;
    if (new_dir < 0) new_dir += 4;
    cell->direction = static_cast<Direction>(new_dir);

    return true;
}

void Plant::kill() {
    alive_ = false;
}

bool Plant::pay_cost(const PlacementCost& cost) {
    return pay_cost(cost.energy, cost.water, cost.nutrients);
}

bool Plant::pay_cost(float energy, float water, float nutrients) {
    if (resources_.energy < energy ||
        resources_.water < water ||
        resources_.nutrients < nutrients) {
        return false;
    }

    resources_.energy -= energy;
    resources_.water -= water;
    resources_.nutrients -= nutrients;
    return true;
}

}  // namespace pbg
