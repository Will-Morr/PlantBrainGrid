#include <catch2/catch_test_macros.hpp>
#include "core/simulation.hpp"
#include "core/reproduction.hpp"
#include "core/config.hpp"

using namespace pbg;

TEST_CASE("Mutation full randomization", "[reproduction]") {
    std::mt19937_64 rng(42);

    SECTION("Mutated bytes are fully random, not small deltas") {
        // Fill genome with a distinctive pattern
        std::vector<uint8_t> genome(1000, 42);

        // Apply 100% mutation rate
        ReproductionSystem::apply_mutations(genome, 1.0f, 16, rng);

        // With full randomization, values should spread across the full [0,255] range
        uint8_t min_val = 255, max_val = 0;
        for (auto b : genome) {
            min_val = std::min(min_val, b);
            max_val = std::max(max_val, b);
        }
        // Small-delta mutations from 42 with magnitude 16 would cap at [26, 58].
        // Full randomization should produce values well outside that range.
        REQUIRE(max_val > 100);
        REQUIRE(min_val < 20);
    }

    SECTION("Partial mutation rate leaves some bytes unchanged") {
        std::vector<uint8_t> genome(1000, 200);
        ReproductionSystem::apply_mutations(genome, 0.1f, 16, rng);

        int unchanged = 0;
        for (auto b : genome) {
            if (b == 200) ++unchanged;
        }
        // At 10% mutation rate, ~900 bytes should stay at 200
        // (some mutated bytes might randomly hit 200, but statistically negligible)
        REQUIRE(unchanged > 700);
        REQUIRE(unchanged < 1000);
    }
}

TEST_CASE("Auto-spawn disabled by default", "[simulation]") {
    Simulation sim(100, 100, 42);
    REQUIRE_FALSE(sim.auto_spawn_enabled());
}

TEST_CASE("Auto-spawn populates empty simulation", "[simulation]") {
    Simulation sim(100, 100, 42);
    sim.enable_auto_spawn(true, 5);

    REQUIRE(sim.auto_spawn_enabled());
    REQUIRE(sim.auto_spawn_min_population() == 5);

    // After one tick, auto-spawn should have triggered
    sim.advance_tick();

    size_t living = 0;
    for (const auto& p : sim.plants()) {
        if (p.is_alive()) ++living;
    }
    REQUIRE(living >= 5);
}

TEST_CASE("Auto-spawn respects minimum population", "[simulation]") {
    Simulation sim(200, 200, 99);
    sim.enable_auto_spawn(true, 10);

    // Run several ticks; population should never drop below threshold
    for (int i = 0; i < 20; ++i) {
        sim.advance_tick();

        size_t living = 0;
        for (const auto& p : sim.plants()) {
            if (p.is_alive()) ++living;
        }
        REQUIRE(living >= 10);
    }
}

TEST_CASE("Auto-spawn gives plants initial resources", "[simulation]") {
    Simulation sim(100, 100, 7);
    sim.enable_auto_spawn(true, 1, 150.0f, 80.0f, 40.0f);

    sim.advance_tick();

    REQUIRE_FALSE(sim.plants().empty());
    const Plant& p = sim.plants()[0];
    REQUIRE(p.resources().energy == 150.0f);
    REQUIRE(p.resources().water == 80.0f);
    REQUIRE(p.resources().nutrients == 40.0f);
}

TEST_CASE("Auto-spawn can be toggled off", "[simulation]") {
    Simulation sim(100, 100, 42);
    sim.enable_auto_spawn(true, 5);
    sim.advance_tick();

    size_t living_after_on = 0;
    for (const auto& p : sim.plants()) {
        if (p.is_alive()) ++living_after_on;
    }
    REQUIRE(living_after_on >= 5);

    // Disable and kill all plants
    sim.enable_auto_spawn(false);
    for (auto& p : const_cast<std::vector<Plant>&>(sim.plants())) {
        p.kill();
    }
    sim.remove_dead_plants();
    REQUIRE(sim.plants().empty());

    // A tick should NOT spawn new plants now
    sim.advance_tick();
    REQUIRE(sim.plants().empty());
}

TEST_CASE("Auto-spawn spawned genomes are random", "[simulation]") {
    Simulation sim(200, 200, 1234);
    sim.enable_auto_spawn(true, 3);
    sim.advance_tick();

    REQUIRE(sim.plants().size() >= 3);

    // Each spawned plant should have a different genome
    const auto& g0 = sim.plants()[0].brain().memory();
    const auto& g1 = sim.plants()[1].brain().memory();

    bool differ = false;
    for (size_t i = 0; i < g0.size(); ++i) {
        if (g0[i] != g1[i]) { differ = true; break; }
    }
    REQUIRE(differ);
}
