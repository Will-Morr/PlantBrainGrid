#pragma once

#include "core/types.hpp"
#include "core/world.hpp"
#include "core/plant.hpp"
#include "core/brain.hpp"
#include "core/reproduction.hpp"
#include <vector>
#include <unordered_map>
#include <functional>

namespace pbg {

// Statistics for a simulation tick
struct TickStats {
    uint64_t tick = 0;
    size_t plant_count = 0;
    size_t seed_count = 0;
    size_t cells_placed = 0;
    size_t cells_removed = 0;
    size_t placements_cancelled = 0;  // Due to conflicts
    size_t seeds_launched = 0;
    size_t seeds_germinated = 0;
    size_t plants_died = 0;
};

// Callback types for simulation events
using PlantCallback = std::function<void(const Plant&)>;
using SeedCallback = std::function<void(const Seed&)>;

class Simulation {
public:
    Simulation(uint32_t width, uint32_t height, uint64_t seed);

    // Accessors
    World& world() { return world_; }
    const World& world() const { return world_; }
    const std::vector<Plant>& plants() const { return plants_; }
    const std::vector<Seed>& seeds() const { return seeds_; }
    uint64_t tick() const { return tick_; }
    uint64_t next_plant_id() const { return next_plant_id_; }

    // Plant management
    Plant* add_plant(const GridCoord& pos, const std::vector<uint8_t>& genome);
    Plant* find_plant(uint64_t id);
    const Plant* find_plant(uint64_t id) const;
    void remove_dead_plants();

    // Seed management
    void add_seed(Seed seed);
    void update_seeds();
    void germinate_seeds();

    // Main simulation tick
    TickStats advance_tick();

    // Run multiple ticks
    void run(uint64_t num_ticks);

    // Auto-spawn: place plants with randomized brains when population falls below threshold
    void enable_auto_spawn(bool enable, size_t min_population = 10,
                           float energy = 100.0f, float water = 50.0f,
                           float nutrients = 30.0f);
    bool auto_spawn_enabled() const { return auto_spawn_enabled_; }
    size_t auto_spawn_min_population() const { return auto_spawn_min_population_; }

    // Event callbacks
    void on_plant_death(PlantCallback callback) { on_plant_death_ = callback; }
    void on_plant_birth(PlantCallback callback) { on_plant_birth_ = callback; }
    void on_seed_launch(SeedCallback callback) { on_seed_launch_ = callback; }

    // Serialization
    void save_state(const std::string& filename) const;
    void load_state(const std::string& filename);

private:
    World world_;
    std::vector<Plant> plants_;
    std::vector<Seed> seeds_;
    uint64_t tick_ = 0;
    uint64_t next_plant_id_ = 1;

    // Callbacks
    PlantCallback on_plant_death_;
    PlantCallback on_plant_birth_;
    SeedCallback on_seed_launch_;

    // Auto-spawn state
    bool auto_spawn_enabled_ = false;
    size_t auto_spawn_min_population_ = 10;
    float auto_spawn_energy_ = 100.0f;
    float auto_spawn_water_ = 50.0f;
    float auto_spawn_nutrients_ = 30.0f;

    // Process brain actions for all plants
    // Returns map of position -> list of (plant_id, action)
    std::unordered_map<GridCoord, std::vector<std::pair<uint64_t, QueuedAction>>>
        collect_all_actions();

    // Resolve conflicts and apply actions
    TickStats apply_actions(
        std::unordered_map<GridCoord, std::vector<std::pair<uint64_t, QueuedAction>>>& actions);

    // Process resource flow for all plants
    void process_resources();

    // Check for plant deaths (primary cell destroyed)
    void check_plant_deaths();

    // Process fire damage to plant cells
    void process_fire_damage();

    // Kill plants that have run out of energy or water
    void check_starvation();

    // Spawn a plant with a randomized genome at a random empty position
    Plant* spawn_random_plant();

    // Check if auto-spawn should fire and do so
    void check_auto_spawn();
};

}  // namespace pbg
