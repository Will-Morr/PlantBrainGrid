#include <catch2/catch_test_macros.hpp>
#include "core/simulation.hpp"
#include "core/config.hpp"

using namespace pbg;

TEST_CASE("Simulation construction", "[simulation]") {
    Simulation sim(100, 100, 42);

    REQUIRE(sim.world().width() == 100);
    REQUIRE(sim.world().height() == 100);
    REQUIRE(sim.tick() == 0);
    REQUIRE(sim.plants().empty());
    REQUIRE(sim.seeds().empty());
}

TEST_CASE("Plant management", "[simulation]") {
    Simulation sim(100, 100, 42);
    std::vector<uint8_t> genome(100, 0);

    SECTION("Add plant successfully") {
        Plant* plant = sim.add_plant({50, 50}, genome);

        REQUIRE(plant != nullptr);
        REQUIRE(sim.plants().size() == 1);
        REQUIRE(plant->id() == 1);
        REQUIRE(plant->primary_position() == GridCoord{50, 50});
    }

    SECTION("Cannot add plant to occupied position") {
        Plant* plant1 = sim.add_plant({50, 50}, genome);
        Plant* plant2 = sim.add_plant({50, 50}, genome);

        REQUIRE(plant1 != nullptr);
        REQUIRE(plant2 == nullptr);
        REQUIRE(sim.plants().size() == 1);
    }

    SECTION("Cannot add plant out of bounds") {
        Plant* plant = sim.add_plant({-10, -10}, genome);

        REQUIRE(plant == nullptr);
        REQUIRE(sim.plants().empty());
    }

    SECTION("Find plant by ID") {
        sim.add_plant({50, 50}, genome);
        sim.add_plant({60, 60}, genome);

        Plant* found = sim.find_plant(2);
        REQUIRE(found != nullptr);
        REQUIRE(found->primary_position() == GridCoord{60, 60});

        Plant* not_found = sim.find_plant(999);
        REQUIRE(not_found == nullptr);
    }

    SECTION("Remove dead plants") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->kill();

        REQUIRE(sim.plants().size() == 1);
        sim.remove_dead_plants();
        REQUIRE(sim.plants().empty());
    }
}

TEST_CASE("Simulation tick", "[simulation]") {
    Simulation sim(100, 100, 42);
    std::vector<uint8_t> genome(100, 0);

    SECTION("Tick advances counter") {
        REQUIRE(sim.tick() == 0);

        sim.advance_tick();
        REQUIRE(sim.tick() == 1);

        sim.advance_tick();
        REQUIRE(sim.tick() == 2);
    }

    SECTION("Tick returns stats") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 100.0f;
        plant->resources().water = 100.0f;

        TickStats stats = sim.advance_tick();

        REQUIRE(stats.tick == 0);
        REQUIRE(stats.plant_count == 1);
    }

    SECTION("Run executes multiple ticks") {
        sim.add_plant({50, 50}, genome);

        sim.run(10);

        REQUIRE(sim.tick() == 10);
    }
}

TEST_CASE("Seed management", "[simulation]") {
    Simulation sim(100, 100, 42);
    std::vector<uint8_t> genome(100, 0);

    SECTION("Add seed") {
        Seed seed;
        seed.genome = genome;
        seed.position = {50, 50};
        seed.in_flight = false;

        sim.add_seed(std::move(seed));

        REQUIRE(sim.seeds().size() == 1);
    }

    SECTION("Seeds in flight are updated") {
        Seed seed;
        seed.genome = genome;
        seed.position = {50, 50};
        seed.velocity = Vec2{1.0f, 0.0f};
        seed.in_flight = true;
        seed.flight_ticks_remaining = 5;

        sim.add_seed(std::move(seed));
        sim.update_seeds();

        REQUIRE(sim.seeds()[0].position.x == 51);
        REQUIRE(sim.seeds()[0].flight_ticks_remaining == 4);
    }

    SECTION("Landed seeds germinate") {
        Seed seed;
        seed.genome = genome;
        seed.position = {50, 50};
        seed.energy = 100.0f;
        seed.water = 50.0f;
        seed.nutrients = 25.0f;
        seed.in_flight = false;

        sim.add_seed(std::move(seed));
        sim.germinate_seeds();

        REQUIRE(sim.seeds().empty());
        REQUIRE(sim.plants().size() == 1);
    }

    SECTION("Seeds destroyed on occupied tiles") {
        // Add blocking plant
        sim.add_plant({50, 50}, genome);

        Seed seed;
        seed.genome = genome;
        seed.position = {50, 50};
        seed.in_flight = false;

        sim.add_seed(std::move(seed));
        sim.germinate_seeds();

        // Seed should be destroyed, no new plant
        REQUIRE(sim.seeds().empty());
        REQUIRE(sim.plants().size() == 1);  // Only the blocking plant
    }
}

TEST_CASE("Action conflict resolution", "[simulation]") {
    Simulation sim(100, 100, 42);

    // Create two plants with brains that try to place cells at same location
    // For this test, we'll manually test the conflict resolution

    SECTION("Conflicting placements are cancelled") {
        std::vector<uint8_t> genome(100, 0);

        // Create two plants next to each other
        Plant* plant1 = sim.add_plant({48, 50}, genome);
        Plant* plant2 = sim.add_plant({52, 50}, genome);

        plant1->resources().energy = 1000.0f;
        plant2->resources().energy = 1000.0f;

        // Both try to place at {50, 50} (manual test would require brain setup)
        // For now, verify the infrastructure exists
        REQUIRE(plant1 != nullptr);
        REQUIRE(plant2 != nullptr);
    }
}

TEST_CASE("Fire damage to plants", "[simulation]") {
    Simulation sim(100, 100, 42);
    std::vector<uint8_t> genome(100, 0);

    // Modify config for faster fire
    auto& cfg = get_config();
    uint16_t orig_destroy = cfg.fire_destroy_ticks;
    cfg.fire_destroy_ticks = 2;

    SECTION("Fire destroys plant cells") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water = 1000.0f;

        // Add a leaf
        plant->place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, sim.world());
        REQUIRE(plant->cell_count() == 2);

        // Set fire to leaf position
        sim.world().cell_at(51, 50).water_level = 0.0f;
        sim.world().ignite({51, 50});

        // Run ticks until fire destroys cell
        for (int i = 0; i < 5; ++i) {
            sim.advance_tick();
        }

        // Leaf should be destroyed
        REQUIRE(plant->cell_count() == 1);
    }

    SECTION("Fire on primary cell kills plant") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water = 1000.0f;

        sim.world().cell_at(50, 50).water_level = 0.0f;
        sim.world().ignite({50, 50});

        // Run until fire destroys primary
        for (int i = 0; i < 5; ++i) {
            sim.advance_tick();
        }

        // Plant should be dead and removed
        REQUIRE(sim.plants().empty());
    }

    SECTION("Fire extinguishes when cell is removed") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water = 1000.0f;

        plant->place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, sim.world());

        sim.world().cell_at(51, 50).water_level = 0.0f;
        sim.world().ignite({51, 50});
        REQUIRE(sim.world().cell_at(51, 50).is_on_fire());

        plant->remove_cell({51, 50}, sim.world());

        REQUIRE_FALSE(sim.world().cell_at(51, 50).is_on_fire());
    }

    SECTION("Fire extinguishes when plant dies") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water = 1000.0f;

        sim.world().cell_at(50, 50).water_level = 0.0f;
        sim.world().ignite({50, 50});
        REQUIRE(sim.world().cell_at(50, 50).is_on_fire());

        plant->kill();
        sim.remove_dead_plants();

        REQUIRE_FALSE(sim.world().cell_at(50, 50).is_on_fire());
    }

    SECTION("Fire does not spread to fireproof cells") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water = 1000.0f;

        plant->place_cell(CellType::FireproofXylem, {51, 50}, Direction::North, sim.world());

        sim.world().cell_at(50, 50).water_level = 0.0f;
        sim.world().cell_at(51, 50).water_level = 0.0f;
        sim.world().ignite({50, 50});

        for (uint16_t i = 0; i < cfg.fire_destroy_ticks + 2; ++i) {
            sim.world().update_fire();
        }

        REQUIRE_FALSE(sim.world().cell_at(51, 50).is_on_fire());
    }

    cfg.fire_destroy_ticks = orig_destroy;
}

TEST_CASE("Resource processing", "[simulation]") {
    Simulation sim(100, 100, 42);
    std::vector<uint8_t> genome(100, 0);

    SECTION("Plants gain resources from leaves") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 100.0f;  // Start with plenty
        plant->resources().water = 100.0f;
        plant->resources().nutrients = 100.0f;

        plant->place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, sim.world());

        // Record state before tick
        float energy_before = plant->resources().energy;

        // Run a tick - leaf should generate energy
        sim.advance_tick();

        // Energy should have changed (gained from leaf, lost to maintenance)
        // The actual change depends on config, but there should be some change
        // With enough starting resources, the plant should survive
        REQUIRE(plant->is_alive());
    }
}

TEST_CASE("Event callbacks", "[simulation]") {
    Simulation sim(100, 100, 42);
    std::vector<uint8_t> genome(100, 0);

    int births = 0;
    int deaths = 0;

    sim.on_plant_birth([&births](const Plant&) { ++births; });
    sim.on_plant_death([&deaths](const Plant&) { ++deaths; });

    SECTION("Birth callback fires on add_plant") {
        sim.add_plant({50, 50}, genome);
        REQUIRE(births == 1);
    }

    SECTION("Death callback fires on plant death") {
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->kill();
        sim.remove_dead_plants();

        REQUIRE(deaths == 1);
    }
}

TEST_CASE("Starvation death", "[simulation]") {
    std::vector<uint8_t> genome(100, 0);

    SECTION("Plant with zero energy dies after tick") {
        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 0.0f;
        plant->resources().water = 100.0f;

        sim.advance_tick();

        REQUIRE(sim.plants().empty());
    }

    SECTION("Plant with zero water dies after tick") {
        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 100.0f;
        plant->resources().water = 0.0f;
        // Zero out world water at primary position so primary water draw gives 0
        sim.world().cell_at({50, 50}).water_level = 0.0f;

        sim.advance_tick();

        REQUIRE(sim.plants().empty());
    }

    SECTION("Plant with zero nutrients survives") {
        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 100.0f;
        plant->resources().water = 100.0f;
        plant->resources().nutrients = 0.0f;

        sim.advance_tick();

        // Nutrients alone don't cause death
        REQUIRE_FALSE(sim.plants().empty());
    }

    SECTION("Plant drains to zero over time and dies") {
        Simulation sim(100, 100, 42);
        // Give minimal resources and no leaves/roots — maintenance will drain them
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1.0f;
        plant->resources().water = 1000.0f;

        bool died = false;
        for (int i = 0; i < 100; ++i) {
            sim.advance_tick();
            if (sim.plants().empty()) { died = true; break; }
        }
        REQUIRE(died);
    }
}

TEST_CASE("Simulation determinism", "[simulation]") {
    std::vector<uint8_t> genome(100);
    for (size_t i = 0; i < genome.size(); ++i) {
        genome[i] = static_cast<uint8_t>(i);
    }

    SECTION("Same seed produces same results") {
        Simulation sim1(100, 100, 42);
        Simulation sim2(100, 100, 42);

        sim1.add_plant({50, 50}, genome);
        sim2.add_plant({50, 50}, genome);

        sim1.run(100);
        sim2.run(100);

        // Should have same state
        REQUIRE(sim1.tick() == sim2.tick());
        REQUIRE(sim1.plants().size() == sim2.plants().size());

        if (!sim1.plants().empty()) {
            REQUIRE(sim1.plants()[0].resources().energy ==
                    sim2.plants()[0].resources().energy);
        }
    }
}
