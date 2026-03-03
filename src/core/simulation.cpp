#include "core/simulation.hpp"
#include "core/resources.hpp"
#include "core/config.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <random>
#include <thread>
#include <unordered_set>

namespace pbg {

Simulation::Simulation(uint32_t width, uint32_t height, uint64_t seed)
    : world_(width, height, seed)
{
}

Plant* Simulation::add_plant(const GridCoord& pos, const std::vector<uint8_t>& genome) {
    if (!world_.in_bounds(pos)) {
        return nullptr;
    }

    if (world_.cell_at(pos).is_occupied()) {
        return nullptr;
    }

    plants_.emplace_back(next_plant_id_++, pos, genome);
    Plant& plant = plants_.back();

    // Register primary cell with world
    world_.cell_at(pos).plant_id = plant.id();
    world_.cell_at(pos).cell_type = CellType::Primary;

    if (on_plant_birth_) {
        on_plant_birth_(plant);
    }

    return &plant;
}

Plant* Simulation::find_plant(uint64_t id) {
    auto it = std::find_if(plants_.begin(), plants_.end(),
        [id](const Plant& p) { return p.id() == id; });
    return it != plants_.end() ? &(*it) : nullptr;
}

const Plant* Simulation::find_plant(uint64_t id) const {
    auto it = std::find_if(plants_.begin(), plants_.end(),
        [id](const Plant& p) { return p.id() == id; });
    return it != plants_.end() ? &(*it) : nullptr;
}

void Simulation::remove_dead_plants() {
    // First, clear world references for dead plants
    for (auto& plant : plants_) {
        if (!plant.is_alive()) {
            for (const auto& cell : plant.cells()) {
                if (world_.in_bounds(cell.position)) {
                    WorldCell& wc = world_.cell_at(cell.position);
                    if (wc.plant_id == plant.id()) {
                        wc.plant_id = 0;
                        wc.fire_ticks = 0;
                    }
                }
            }

            if (on_plant_death_) {
                on_plant_death_(plant);
            }
        }
    }

    // Remove dead plants from vector
    plants_.erase(
        std::remove_if(plants_.begin(), plants_.end(),
            [](const Plant& p) { return !p.is_alive(); }),
        plants_.end());
}

void Simulation::add_seed(Seed seed) {
    seeds_.push_back(std::move(seed));

    if (on_seed_launch_) {
        on_seed_launch_(seeds_.back());
    }
}

void Simulation::update_seeds() {
    for (auto& seed : seeds_) {
        ReproductionSystem::update_seed_flight(seed);
    }
}

void Simulation::germinate_seeds() {
    std::vector<Seed> remaining_seeds;

    for (auto& seed : seeds_) {
        if (seed.in_flight) {
            remaining_seeds.push_back(std::move(seed));
            continue;
        }

        auto new_plant = ReproductionSystem::try_germinate(seed, next_plant_id_, world_);
        if (new_plant) {
            ++next_plant_id_;
            plants_.push_back(std::move(*new_plant));

            // Register with world
            Plant& plant = plants_.back();
            world_.cell_at(plant.primary_position()).plant_id = plant.id();
            world_.cell_at(plant.primary_position()).cell_type = CellType::Primary;

            if (on_plant_birth_) {
                on_plant_birth_(plant);
            }
        }
        // Seeds that fail to germinate are destroyed
    }

    seeds_ = std::move(remaining_seeds);
}

std::unordered_map<GridCoord, std::vector<std::pair<uint64_t, QueuedAction>>>
Simulation::collect_all_actions() {
    const size_t n = plants_.size();

    // Generate per-plant RNG seeds sequentially to preserve determinism.
    // Each brain gets its own independent RNG so execute_tick can safely
    // run in parallel without sharing mutable state.
    std::vector<uint64_t> plant_seeds(n);
    for (size_t i = 0; i < n; ++i) {
        plant_seeds[i] = world_.rng()();
    }

    // Phase 1 (parallel): execute every brain concurrently.
    // Brains only read from `world_` (sensing) and write only to their own
    // plant's brain memory and resource totals — no shared mutable state.
    std::vector<std::vector<QueuedAction>> plant_actions(n);
    const unsigned int nthreads = std::max(1u,
        std::min(static_cast<unsigned int>(n),
                 std::thread::hardware_concurrency()));

    std::atomic<size_t> next_idx{0};
    auto worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= n) break;
            auto& plant = plants_[idx];
            if (!plant.is_alive()) continue;
            std::mt19937_64 rng(plant_seeds[idx]);
            plant_actions[idx] = plant.brain().execute_tick(plant, world_, rng);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(nthreads);
    for (unsigned int t = 0; t < nthreads; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    // Phase 2 (sequential): process collected actions.
    std::unordered_map<GridCoord, std::vector<std::pair<uint64_t, QueuedAction>>> all_actions;

    for (size_t i = 0; i < n; ++i) {
        auto& plant = plants_[i];
        if (!plant.is_alive()) continue;

        for (auto& action : plant_actions[i]) {
            if (action.type == ActionType::PlaceCell ||
                action.type == ActionType::RemoveCell) {
                all_actions[action.position].emplace_back(plant.id(), std::move(action));
            } else if (action.type == ActionType::LaunchSeed) {
                if (action.seed_params) {
                    const Plant* father = nullptr;
                    if (plant.brain().mate_search().selected_mate_id != 0) {
                        father = find_plant(plant.brain().mate_search().selected_mate_id);
                    }
                    auto seed = ReproductionSystem::create_seed(
                        plant, father, *action.seed_params, world_.rng());
                    if (seed) {
                        add_seed(std::move(*seed));
                    }
                }
            } else {
                if (action.type == ActionType::ToggleCell) {
                    plant.toggle_cell(action.position, action.toggle_state);
                } else if (action.type == ActionType::RotateCell) {
                    plant.rotate_cell(action.position, action.rotation);
                }
            }
        }

        plant.advance_age();
        plant.advance_cell_ages();
    }

    return all_actions;
}

void Simulation::check_old_age() {
    const auto& cfg = get_config();

    for (auto& plant : plants_) {
        if (!plant.is_alive()) continue;

        // Plant-level age limit
        if (cfg.max_plant_age > 0 && plant.age() >= cfg.max_plant_age) {
            plant.kill();
            continue;
        }

        // Cell-level age limit (primary cell is exempt)
        if (cfg.max_cell_age > 0) {
            plant.cull_old_cells(cfg.max_cell_age, world_);
        }
    }
}

TickStats Simulation::apply_actions(
    std::unordered_map<GridCoord, std::vector<std::pair<uint64_t, QueuedAction>>>& actions)
{
    TickStats stats;
    stats.tick = tick_;

    for (auto& [pos, action_list] : actions) {
        // A conflict only occurs when two *different* plants queue an action
        // for the same position in the same tick. If the same plant's brain
        // loops and re-queues the same position, just use the first entry.
        uint64_t first_id = action_list[0].first;
        bool multi_plant = false;
        for (size_t i = 1; i < action_list.size(); ++i) {
            if (action_list[i].first != first_id) { multi_plant = true; break; }
        }
        if (multi_plant) {
            stats.placements_cancelled += action_list.size();
            // Charge each unique plant for wasting resources on a conflicting action
            std::unordered_set<uint64_t> charged;
            for (auto& [pid, act] : action_list) {
                if (act.type == ActionType::PlaceCell && charged.insert(pid).second) {
                    Plant* p = find_plant(pid);
                    if (p && p->is_alive()) {
                        p->force_deduct_placement_cost(act.cell_type);
                    }
                }
            }
            continue;
        }

        auto& [plant_id, action] = action_list[0];
        Plant* plant = find_plant(plant_id);
        if (!plant || !plant->is_alive()) continue;

        if (action.type == ActionType::PlaceCell) {
            if (!world_.in_bounds(action.position)) {
                // Out-of-bounds placement: charge full cost but don't place
                plant->force_deduct_placement_cost(action.cell_type);
                continue;
            }
            const WorldCell& wc = world_.cell_at(action.position);

            if (wc.is_occupied() && wc.plant_id == plant_id) {
                // Placing on own existing cell: charge 10% of build cost as penalty
                const CellCosts& cost = get_cell_costs(action.cell_type);
                plant->resources().energy    -= cost.build_energy    * 0.1f;
                plant->resources().water     -= cost.build_water     * 0.1f;
                plant->resources().nutrients -= cost.build_nutrients * 0.1f;
            } else {
                // Displace any cell from a different plant that occupies this tile
                if (wc.is_occupied()) {
                    Plant* other = find_plant(wc.plant_id);
                    if (other && other->is_alive()) {
                        if (action.position == other->primary_position()) {
                            other->kill();
                        } else {
                            other->remove_cell(action.position, world_);
                        }
                    }
                    // Clear world reference so place_cell_free sees an empty tile
                    // (covers both the killed-primary case and any stale ghost cells)
                    world_.cell_at(action.position).plant_id = 0;
                    world_.cell_at(action.position).fire_ticks = 0;
                }

                // Charge full cost and attempt placement
                plant->force_deduct_placement_cost(action.cell_type);
                if (plant->place_cell_free(action.cell_type, action.position, action.direction, world_)) {
                    ++stats.cells_placed;
                }
            }
        } else if (action.type == ActionType::RemoveCell) {
            if (plant->remove_cell(action.position, world_)) {
                ++stats.cells_removed;
            }
        }
    }

    return stats;
}

void Simulation::process_resources() {
    for (auto& plant : plants_) {
        if (plant.is_alive()) {
            ResourceSystem::process_tick(plant, world_);
        }
    }
}

void Simulation::check_starvation() {
    for (auto& plant : plants_) {
        if (!plant.is_alive()) continue;
        if (plant.resources().energy <= 0.0f || plant.resources().water <= 0.0f) {
            plant.kill();
        }
    }
}

void Simulation::process_fire_damage() {
    // Only destroy cells where fire just burned out this tick (fire_ticks hit 0)
    for (const GridCoord& pos : world_.burned_out_positions()) {
        const WorldCell& wc = world_.cell_at(pos);
        if (!wc.is_occupied() || wc.is_fireproof()) continue;
        uint64_t pid = wc.plant_id;

        for (auto& plant : plants_) {
            if (!plant.is_alive()) continue;
            if (pid != plant.id()) continue;

            if (pos == plant.primary_position()) {
                plant.kill();
            } else {
                plant.remove_cell(pos, world_);
            }
            break;
        }
    }
}

void Simulation::process_thorn_damage() {
    static const GridCoord offsets[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

    struct DamageEvent {
        uint64_t victim_id;
        GridCoord pos;
        bool is_primary;
    };
    std::vector<DamageEvent> events;

    for (const auto& attacker : plants_) {
        if (!attacker.is_alive()) continue;

        for (const auto& cell : attacker.cells()) {
            if (cell.type != CellType::Thorn) continue;

            for (const auto& offset : offsets) {
                GridCoord neighbor = cell.position + offset;
                if (!world_.in_bounds(neighbor)) continue;

                const WorldCell& wc = world_.cell_at(neighbor);
                if (!wc.is_occupied() || wc.plant_id == attacker.id()) continue;

                const Plant* victim = find_plant(wc.plant_id);
                if (!victim || !victim->is_alive()) continue;

                events.push_back({wc.plant_id, neighbor,
                                  neighbor == victim->primary_position()});
            }
        }
    }

    for (const auto& ev : events) {
        Plant* victim = find_plant(ev.victim_id);
        if (!victim || !victim->is_alive()) continue;

        if (ev.is_primary) {
            victim->kill();
        } else {
            victim->remove_cell(ev.pos, world_);
        }
    }
}

void Simulation::check_plant_deaths() {
    for (auto& plant : plants_) {
        if (!plant.is_alive()) continue;

        // Check if primary cell still exists in world
        const PlantCell* primary = plant.find_cell(plant.primary_position());
        if (!primary) {
            plant.kill();
        }
    }
}

TickStats Simulation::advance_tick() {
    TickStats stats;
    stats.tick = tick_;

    // 1. Update world (seasons, fire spread)
    world_.advance_tick();

    // 2. Process fire damage to plants
    process_fire_damage();

    // 3. Check for plant deaths
    check_plant_deaths();

    // 4. Process resources for all plants
    process_resources();

    // 5. Kill plants that have exhausted energy or water
    check_starvation();

    // 6. Execute all brains and collect actions (also advances plant and cell ages)
    auto actions = collect_all_actions();

    // 6.5 Kill plants/cells that have reached their maximum age
    check_old_age();

    // 7. Apply non-conflicting actions
    TickStats action_stats = apply_actions(actions);
    stats.cells_placed = action_stats.cells_placed;
    stats.cells_removed = action_stats.cells_removed;
    stats.placements_cancelled = action_stats.placements_cancelled;

    // 8. Thorns damage adjacent enemy cells
    process_thorn_damage();

    // 9. Update seeds in flight
    update_seeds();

    // 10. Germinate landed seeds
    size_t seeds_before = seeds_.size();
    germinate_seeds();
    stats.seeds_germinated = seeds_before - seeds_.size();

    // 11. Remove dead plants
    size_t plants_before = plants_.size();
    remove_dead_plants();
    stats.plants_died = plants_before - plants_.size();

    // 12. Auto-spawn if enabled and population is low
    check_auto_spawn();

    // Update stats
    stats.plant_count = plants_.size();
    stats.seed_count = seeds_.size();

    ++tick_;

    return stats;
}

void Simulation::run(uint64_t num_ticks) {
    for (uint64_t i = 0; i < num_ticks; ++i) {
        advance_tick();
    }
}

void Simulation::enable_auto_spawn(bool enable, size_t min_population,
                                    float energy, float water, float nutrients)
{
    auto_spawn_enabled_ = enable;
    auto_spawn_min_population_ = min_population;
    auto_spawn_energy_ = energy;
    auto_spawn_water_ = water;
    auto_spawn_nutrients_ = nutrients;
}

Plant* Simulation::spawn_random_plant() {
    const auto& cfg = get_config();
    std::uniform_int_distribution<int> x_dist(0, static_cast<int>(world_.width()) - 1);
    std::uniform_int_distribution<int> y_dist(0, static_cast<int>(world_.height()) - 1);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    for (int attempt = 0; attempt < 100; ++attempt) {
        GridCoord pos{x_dist(world_.rng()), y_dist(world_.rng())};

        if (!world_.in_bounds(pos)) continue;
        if (world_.cell_at(pos).is_occupied()) continue;
        if (world_.cell_at(pos).is_on_fire()) continue;

        std::vector<uint8_t> genome(cfg.brain_size);
        for (auto& b : genome) {
            b = static_cast<uint8_t>(byte_dist(world_.rng()));
        }

        Plant* plant = add_plant(pos, genome);
        if (plant) {
            plant->resources().energy = auto_spawn_energy_;
            plant->resources().water = auto_spawn_water_;
            plant->resources().nutrients = auto_spawn_nutrients_;
            return plant;
        }
    }
    return nullptr;
}

void Simulation::check_auto_spawn() {
    if (!auto_spawn_enabled_) return;

    size_t living = 0;
    for (const auto& plant : plants_) {
        if (plant.is_alive()) ++living;
    }

    while (living < auto_spawn_min_population_) {
        if (!spawn_random_plant()) break;
        ++living;
    }
}

void Simulation::save_state(const std::string& filename) const {
    // Basic binary serialization
    std::ofstream file(filename, std::ios::binary);
    if (!file) return;

    // Write header
    uint32_t magic = 0x50424753;  // "PBGS"
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write tick
    file.write(reinterpret_cast<const char*>(&tick_), sizeof(tick_));
    file.write(reinterpret_cast<const char*>(&next_plant_id_), sizeof(next_plant_id_));

    // Write world dimensions and seed
    uint32_t w = world_.width();
    uint32_t h = world_.height();
    uint64_t seed = world_.seed();
    file.write(reinterpret_cast<const char*>(&w), sizeof(w));
    file.write(reinterpret_cast<const char*>(&h), sizeof(h));
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));

    // Write plant count
    uint64_t plant_count = plants_.size();
    file.write(reinterpret_cast<const char*>(&plant_count), sizeof(plant_count));

    // Write each plant (simplified - would need full serialization for production)
    for (const auto& plant : plants_) {
        uint64_t id = plant.id();
        int32_t px = plant.primary_position().x;
        int32_t py = plant.primary_position().y;
        bool alive = plant.is_alive();
        uint64_t age = plant.age();
        float energy = plant.resources().energy;
        float water = plant.resources().water;
        float nutrients = plant.resources().nutrients;

        file.write(reinterpret_cast<const char*>(&id), sizeof(id));
        file.write(reinterpret_cast<const char*>(&px), sizeof(px));
        file.write(reinterpret_cast<const char*>(&py), sizeof(py));
        file.write(reinterpret_cast<const char*>(&alive), sizeof(alive));
        file.write(reinterpret_cast<const char*>(&age), sizeof(age));
        file.write(reinterpret_cast<const char*>(&energy), sizeof(energy));
        file.write(reinterpret_cast<const char*>(&water), sizeof(water));
        file.write(reinterpret_cast<const char*>(&nutrients), sizeof(nutrients));

        // Write genome
        const auto& genome = plant.brain().memory();
        uint64_t genome_size = genome.size();
        file.write(reinterpret_cast<const char*>(&genome_size), sizeof(genome_size));
        file.write(reinterpret_cast<const char*>(genome.data()), genome_size);
    }
}

void Simulation::load_state(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return;

    // Read and verify header
    uint32_t magic, version;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));

    if (magic != 0x50424753 || version != 1) {
        return;  // Invalid file
    }

    // Read tick
    file.read(reinterpret_cast<char*>(&tick_), sizeof(tick_));
    file.read(reinterpret_cast<char*>(&next_plant_id_), sizeof(next_plant_id_));

    // Read world info (we assume dimensions match)
    uint32_t w, h;
    uint64_t seed;
    file.read(reinterpret_cast<char*>(&w), sizeof(w));
    file.read(reinterpret_cast<char*>(&h), sizeof(h));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));

    // Clear existing state
    plants_.clear();
    seeds_.clear();

    // Read plants
    uint64_t plant_count;
    file.read(reinterpret_cast<char*>(&plant_count), sizeof(plant_count));

    for (uint64_t i = 0; i < plant_count; ++i) {
        uint64_t id;
        int32_t px, py;
        bool alive;
        uint64_t age;
        float energy, water, nutrients;

        file.read(reinterpret_cast<char*>(&id), sizeof(id));
        file.read(reinterpret_cast<char*>(&px), sizeof(px));
        file.read(reinterpret_cast<char*>(&py), sizeof(py));
        file.read(reinterpret_cast<char*>(&alive), sizeof(alive));
        file.read(reinterpret_cast<char*>(&age), sizeof(age));
        file.read(reinterpret_cast<char*>(&energy), sizeof(energy));
        file.read(reinterpret_cast<char*>(&water), sizeof(water));
        file.read(reinterpret_cast<char*>(&nutrients), sizeof(nutrients));

        uint64_t genome_size;
        file.read(reinterpret_cast<char*>(&genome_size), sizeof(genome_size));

        std::vector<uint8_t> genome(genome_size);
        file.read(reinterpret_cast<char*>(genome.data()), genome_size);

        // Recreate plant (simplified - doesn't restore cells)
        plants_.emplace_back(id, GridCoord{px, py}, genome);
        Plant& plant = plants_.back();
        plant.resources() = Resources{energy, water, nutrients};
        if (!alive) plant.kill();

        // Register with world
        if (world_.in_bounds(plant.primary_position())) {
            world_.cell_at(plant.primary_position()).plant_id = plant.id();
            world_.cell_at(plant.primary_position()).cell_type = CellType::Primary;
        }
    }
}

}  // namespace pbg
