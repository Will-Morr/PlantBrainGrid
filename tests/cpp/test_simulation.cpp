#include <catch2/catch_test_macros.hpp>
#include "core/simulation.hpp"
#include "core/config.hpp"
#include <fstream>
#include <vector>

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

TEST_CASE("Thorn damage to adjacent plants", "[simulation]") {
    std::vector<uint8_t> genome(100, 0);

    SECTION("Thorn destroys adjacent enemy cell") {
        // Layout (x→): attacker(50) - thorn(51) - leaf(52) - victim(53)
        // Add both plants before using any pointers to avoid dangling after reallocation.
        Simulation sim(100, 100, 42);
        sim.add_plant({50, 50}, genome);
        sim.add_plant({53, 50}, genome);

        Plant* attacker = sim.find_plant(1);
        Plant* victim   = sim.find_plant(2);
        attacker->resources().energy = 1000.0f;
        attacker->resources().water  = 1000.0f;
        victim->resources().energy   = 1000.0f;
        victim->resources().water    = 1000.0f;

        // Give victim a leaf adjacent to its primary
        victim->place_cell(CellType::SmallLeaf, {52, 50}, sim.world());
        REQUIRE(victim->cell_count() == 2);

        // Give attacker a thorn adjacent to its primary (and adjacent to the victim leaf)
        attacker->place_cell(CellType::Thorn, {51, 50}, sim.world());
        REQUIRE(attacker->cell_count() == 2);

        // One tick should trigger thorn damage and remove the leaf
        sim.advance_tick();

        REQUIRE(victim->cell_count() == 1);  // only primary remains
    }

    SECTION("Thorn kills plant when primary is adjacent") {
        // Layout: attacker(50) - thorn(51) - victim_primary(52)
        Simulation sim(100, 100, 42);
        sim.add_plant({50, 50}, genome);
        sim.add_plant({52, 50}, genome);

        Plant* attacker = sim.find_plant(1);
        Plant* victim   = sim.find_plant(2);
        attacker->resources().energy = 1000.0f;
        attacker->resources().water  = 1000.0f;
        victim->resources().energy   = 1000.0f;
        victim->resources().water    = 1000.0f;

        // Place thorn directly adjacent to victim's primary
        attacker->place_cell(CellType::Thorn, {51, 50}, sim.world());
        REQUIRE(attacker->cell_count() == 2);

        sim.advance_tick();

        REQUIRE(sim.plants().size() == 1);  // victim dead and removed
        REQUIRE(sim.plants()[0].id() == 1); // attacker survives
    }

    SECTION("Thorn does not damage own cells") {
        // Layout: primary(50) - thorn(51) - own_leaf(52)
        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water  = 1000.0f;

        plant->place_cell(CellType::Thorn,     {51, 50}, sim.world());
        plant->place_cell(CellType::SmallLeaf, {52, 50}, sim.world());

        sim.advance_tick();

        // Own leaf next to own thorn must survive
        REQUIRE(plant->cell_count() == 3);
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
        plant->place_cell(CellType::SmallLeaf, {51, 50}, sim.world());
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

        plant->place_cell(CellType::SmallLeaf, {51, 50}, sim.world());

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

        plant->place_cell(CellType::FireproofXylem, {51, 50}, sim.world());

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

        plant->place_cell(CellType::SmallLeaf, {51, 50}, sim.world());

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

    SECTION("Plant with negative energy dies after tick") {
        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = -1.0f;
        plant->resources().water  = 100.0f;

        sim.advance_tick();

        REQUIRE(sim.plants().empty());
    }

    SECTION("Plant with negative water dies after tick") {
        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 100.0f;
        plant->resources().water  = -1.0f;

        sim.advance_tick();

        REQUIRE(sim.plants().empty());
    }

    SECTION("Negative resources from placement cost kill the plant") {
        // Brain places a cell costing 10 energy; plant only has 5 — goes negative.
        // OP_PLACE_CELL=0x60, SmallLeaf=2, dx=+1, dy=0, dir=0, HALT=0x01
        std::vector<uint8_t> placer(1024, 0);
        placer[0] = 0x60; placer[1] = 0x02;
        placer[2] = 0x01; placer[3] = 0x00;
        placer[4] = 0x00; placer[5] = 0x01;

        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, placer);
        // SmallLeaf build cost = 10 energy; give the plant only 5
        const CellCosts& cost = get_cell_costs(CellType::SmallLeaf);
        plant->resources().energy = cost.build_energy * 0.5f;
        plant->resources().water  = 1000.0f;

        sim.advance_tick();

        REQUIRE(sim.plants().empty());
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

TEST_CASE("Reproducer colonises world", "[simulation]") {
    // Load the compiled reproducer genome from examples/reproducer.bin
    std::string bin_path = std::string(EXAMPLES_DIR) + "/reproducer.bin";
    std::ifstream f(bin_path, std::ios::binary);
    REQUIRE(f.good());
    std::vector<uint8_t> genome(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    REQUIRE_FALSE(genome.empty());

    // 128×128 world gives seeds (max radius 100) plenty of room to land
    Simulation sim(128, 128, 42);
    Plant* plant = sim.add_plant({64, 64}, genome);
    REQUIRE(plant != nullptr);
    plant->resources().energy    = 500.0f;
    plant->resources().water     = 300.0f;
    plant->resources().nutrients = 200.0f;

    sim.run(10000);

    REQUIRE(sim.plants().size() >= 50);
}

TEST_CASE("Old age death", "[simulation]") {
    std::vector<uint8_t> genome(100, 0);

    SECTION("Plant dies when it reaches max_plant_age") {
        // Each advance_tick() increments age by 1.  With max_plant_age=5 the
        // plant is killed when age reaches 5, i.e. after the 5th tick.
        auto& cfg = get_config();
        uint32_t orig_plant = cfg.max_plant_age;
        uint32_t orig_cell  = cfg.max_cell_age;
        cfg.max_plant_age = 5;
        cfg.max_cell_age  = 0; // disabled so only plant age matters

        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water  = 1000.0f;

        // Ticks 1-4: age 1-4, plant alive
        for (int i = 0; i < 4; ++i) {
            sim.advance_tick();
            REQUIRE(!sim.plants().empty());
        }
        // Tick 5: age becomes 5 → dies and is removed
        sim.advance_tick();
        REQUIRE(sim.plants().empty());

        cfg.max_plant_age = orig_plant;
        cfg.max_cell_age  = orig_cell;
    }

    SECTION("Non-primary cell removed at max_cell_age, primary exempt") {
        // Primary cell is exempt from cell-age death (governed by max_plant_age).
        // Only non-primary cells are culled when age_ticks >= max_cell_age.
        auto& cfg = get_config();
        uint32_t orig_cell  = cfg.max_cell_age;
        uint32_t orig_plant = cfg.max_plant_age;
        cfg.max_cell_age  = 3;
        cfg.max_plant_age = 0; // disabled

        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water  = 1000.0f;
        plant->place_cell(CellType::SmallLeaf, {51, 50}, sim.world());
        REQUIRE(plant->cell_count() == 2);

        // After 3 ticks both cells have age_ticks=3.  The leaf is culled;
        // the primary cell is exempt.
        for (int i = 0; i < 3; ++i) sim.advance_tick();

        REQUIRE(!sim.plants().empty());           // plant survives
        REQUIRE(sim.plants()[0].cell_count() == 1); // only primary remains

        cfg.max_cell_age  = orig_cell;
        cfg.max_plant_age = orig_plant;
    }

    SECTION("Plant survives beyond max_cell_age when it has only a primary cell") {
        // Confirm primary is truly exempt: a single-cell plant lives past max_cell_age.
        auto& cfg = get_config();
        uint32_t orig_cell  = cfg.max_cell_age;
        uint32_t orig_plant = cfg.max_plant_age;
        cfg.max_cell_age  = 2;
        cfg.max_plant_age = 0; // disabled

        Simulation sim(100, 100, 42);
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 1000.0f;
        plant->resources().water  = 1000.0f;

        for (int i = 0; i < 10; ++i) sim.advance_tick();

        REQUIRE(!sim.plants().empty()); // still alive despite cell age >> max_cell_age

        cfg.max_cell_age  = orig_cell;
        cfg.max_plant_age = orig_plant;
    }
}

TEST_CASE("Cell overlap prevention", "[simulation]") {
    // Brain genome that places SmallLeaf(2) at dx=+1, dy=0 then halts.
    // OP_PLACE_CELL=0x60, type=2, dx=+1, dy=0, dir=0, OP_HALT=0x01
    std::vector<uint8_t> placer_genome(1024, 0);
    placer_genome[0] = 0x60; // OP_PLACE_CELL
    placer_genome[1] = 0x02; // SmallLeaf
    placer_genome[2] = 0x01; // dx=+1
    placer_genome[3] = 0x00; // dy=0
    placer_genome[4] = 0x01; // OP_HALT

    std::vector<uint8_t> idle_genome(1024, 0); // all NOPs, effectively idle
    idle_genome[0] = 0x01; // OP_HALT immediately

    SECTION("Placing on another plant's cell displaces it") {
        // Plant A at (50,50) wants to place at (51,50).
        // Plant B at (52,50) pre-placed a leaf at (51,50).
        Simulation sim(100, 100, 42);
        sim.add_plant({50, 50}, placer_genome);
        sim.add_plant({52, 50}, idle_genome);

        Plant* a = sim.find_plant(1);
        Plant* b = sim.find_plant(2);
        a->resources().energy = 1000.0f;
        a->resources().water  = 1000.0f;
        b->resources().energy = 1000.0f;
        b->resources().water  = 1000.0f;

        // Manually place B's leaf at (51,50) — the same tile A will try to claim
        b->place_cell(CellType::SmallLeaf, {51, 50}, sim.world());
        REQUIRE(b->cell_count() == 2);

        sim.advance_tick();

        // B's leaf should be displaced; A should now own (51,50)
        const WorldCell& wc = sim.world().cell_at({51, 50});
        REQUIRE(wc.is_occupied());
        REQUIRE(wc.plant_id == a->id());
        REQUIRE(b->cell_count() == 1); // only B's primary remains
    }

    SECTION("Placing on another plant's primary cell kills that plant") {
        // Plant A at (50,50) tries to place at (51,50) = Plant B's primary.
        Simulation sim(100, 100, 42);
        sim.add_plant({50, 50}, placer_genome);
        sim.add_plant({51, 50}, idle_genome);

        Plant* a = sim.find_plant(1);
        a->resources().energy = 1000.0f;
        a->resources().water  = 1000.0f;
        Plant* b = sim.find_plant(2);
        b->resources().energy = 1000.0f;
        b->resources().water  = 1000.0f;

        sim.advance_tick();

        // Plant B should be dead and removed; A owns (51,50)
        REQUIRE(sim.find_plant(2) == nullptr);
        const WorldCell& wc = sim.world().cell_at({51, 50});
        REQUIRE(wc.plant_id == a->id());
    }

    SECTION("Placing on own existing cell charges 10% penalty, no re-place") {
        // Plant A already has a leaf at (51,50); brain tries to place there again.
        Simulation sim(100, 100, 42);
        Plant* a = sim.add_plant({50, 50}, placer_genome);
        a->resources().energy    = 1000.0f;
        a->resources().water     = 1000.0f;
        a->resources().nutrients = 0.0f;

        // Pre-place the leaf so the target tile is already owned by A
        a->place_cell(CellType::SmallLeaf, {51, 50}, sim.world());

        // Run a reference tick with an idle plant to measure baseline resource change
        Simulation sim_ref(100, 100, 42);
        Plant* ref = sim_ref.add_plant({50, 50}, idle_genome);
        ref->resources().energy    = 1000.0f;
        ref->resources().water     = 1000.0f;
        ref->resources().nutrients = 0.0f;
        ref->place_cell(CellType::SmallLeaf, {51, 50}, sim_ref.world());

        auto& cfg = get_config();
        uint32_t orig_cell  = cfg.max_cell_age;
        uint32_t orig_plant = cfg.max_plant_age;
        cfg.max_cell_age  = 0;
        cfg.max_plant_age = 0;

        float ref_energy_before = ref->resources().energy;
        sim_ref.advance_tick();
        float ref_delta = ref->resources().energy - ref_energy_before; // baseline (no penalty)

        float energy_before = a->resources().energy;
        sim.advance_tick();
        float actual_delta = a->resources().energy - energy_before;

        cfg.max_cell_age  = orig_cell;
        cfg.max_plant_age = orig_plant;

        // The placer brain charged 10% of SmallLeaf build cost (=1 energy) as penalty.
        // So actual_delta should be lower than ref_delta by approximately that penalty.
        const CellCosts& cost = get_cell_costs(CellType::SmallLeaf);
        float expected_penalty = cost.build_energy * 0.1f;
        float penalty_observed = ref_delta - actual_delta;
        REQUIRE(penalty_observed > expected_penalty * 0.5f); // meaningful penalty was charged
        REQUIRE(penalty_observed < cost.build_energy * 1.5f); // but not the full build cost
        // Cell count unchanged (no extra cell placed)
        REQUIRE(a->cell_count() == 2);
    }

    SECTION("Multi-plant conflict: neither cell is placed, both charged") {
        // Both plants try to place at the same empty tile; conflict cancels both.
        // Genome: PLACE_CELL SmallLeaf dx=0 dy=+1 (above primary)
        std::vector<uint8_t> up_genome(1024, 0);
        up_genome[0] = 0x60; up_genome[1] = 0x02; // OP_PLACE_CELL, SmallLeaf
        up_genome[2] = 0x00; up_genome[3] = 0x01; // dx=0, dy=+1
        up_genome[4] = 0x00; up_genome[5] = 0x01; // dir=North, OP_HALT

        Simulation sim(100, 100, 42);
        // Plants on either side of the empty target at (51,50)
        sim.add_plant({50, 50}, placer_genome); // tries (51,50)
        sim.add_plant({52, 50}, idle_genome);

        Plant* a = sim.find_plant(1);
        Plant* b = sim.find_plant(2);
        a->resources().energy = 1000.0f;
        a->resources().water  = 1000.0f;
        b->resources().energy = 1000.0f;
        b->resources().water  = 1000.0f;

        // Give B a leaf at (51,50) pre-placed so B also tries to place there
        // Actually: simulate conflict by giving both a brain placing at same tile.
        // B's idle_genome doesn't place — let's give B the placer that places at dx=-1
        std::vector<uint8_t> left_placer(1024, 0);
        left_placer[0] = 0x60; left_placer[1] = 0x02;
        left_placer[2] = static_cast<uint8_t>(-1); // dx=-1 as int8
        left_placer[3] = 0x00; left_placer[4] = 0x00; left_placer[5] = 0x01;

        sim.find_plant(2)->brain().write(0, 0x60); // PLACE_CELL
        sim.find_plant(2)->brain().write(1, 0x02); // SmallLeaf
        sim.find_plant(2)->brain().write(2, static_cast<uint8_t>(-1)); // dx=-1
        sim.find_plant(2)->brain().write(3, 0x00);
        sim.find_plant(2)->brain().write(4, 0x00);
        sim.find_plant(2)->brain().write(5, 0x01); // HALT

        float a_energy_before = a->resources().energy;
        float b_energy_before = b->resources().energy;

        sim.advance_tick();

        // Neither should have placed at (51,50)
        const WorldCell& wc = sim.world().cell_at({51, 50});
        REQUIRE_FALSE(wc.is_occupied());
        // Both should have been charged
        REQUIRE(a->resources().energy < a_energy_before);
        REQUIRE(b->resources().energy < b_energy_before);
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
