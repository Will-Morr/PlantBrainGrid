#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/simulation.hpp"
#include "core/brain.hpp"
#include "core/brain_ops.hpp"
#include "core/config.hpp"
#include "core/resources.hpp"
#include <random>
#include <thread>
#include <vector>

using namespace pbg;
using Catch::Matchers::WithinAbs;

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::vector<uint8_t> make_halt_genome(size_t size = 256) {
    std::vector<uint8_t> g(size, 0);
    g[0] = OP_HALT;
    return g;
}

// Genome that loops forever (hits instruction limit every tick)
static std::vector<uint8_t> make_looping_genome(size_t size = 256) {
    std::vector<uint8_t> g(size, 0);
    // JUMP 0x0000 — loops back to itself
    g[0] = OP_JUMP;
    g[1] = 0; g[2] = 0;
    return g;
}

// Genome that tries to place a SmallLeaf at (+1, 0)
static std::vector<uint8_t> make_placer_genome(size_t size = 256) {
    std::vector<uint8_t> g(size, 0);
    g[0] = OP_PLACE_CELL;
    g[1] = static_cast<uint8_t>(CellType::SmallLeaf);
    g[2] = 1;   // dx = +1
    g[3] = 0;   // dy = 0
    g[4] = 0;   // direction North
    g[5] = OP_HALT;
    return g;
}

// ─── parallel brain determinism ──────────────────────────────────────────────

TEST_CASE("Parallel brain determinism", "[parallel][simulation]") {
    SECTION("Same seed + same genomes produce identical results across runs") {
        // Use a repeatable pattern so brains do meaningful work
        std::vector<uint8_t> genome(256);
        for (size_t i = 0; i < genome.size(); ++i) {
            genome[i] = static_cast<uint8_t>(i * 7 + 13);
        }

        auto run_sim = [&]() -> std::vector<float> {
            Simulation sim(50, 50, 42);
            // Place 10 plants
            int placed = 0;
            for (int x = 5; x < 50 && placed < 10; x += 5) {
                Plant* p = sim.add_plant({x, 25}, genome);
                if (p) {
                    p->resources() = Resources{500.f, 500.f, 100.f};
                    ++placed;
                }
            }
            sim.run(50);

            std::vector<float> state;
            for (const auto& p : sim.plants()) {
                state.push_back(p.resources().energy);
                state.push_back(p.resources().water);
                state.push_back(static_cast<float>(p.age()));
            }
            return state;
        };

        auto s1 = run_sim();
        auto s2 = run_sim();

        REQUIRE(s1.size() == s2.size());
        for (size_t i = 0; i < s1.size(); ++i) {
            REQUIRE_THAT(s1[i], WithinAbs(s2[i], 1e-4f));
        }
    }

    SECTION("Parallel result matches with many plants") {
        // 20 plants; hardware_concurrency will split them across threads
        std::vector<uint8_t> genome = make_halt_genome();

        auto run = [&](uint64_t seed) -> std::pair<size_t, float> {
            Simulation sim(100, 100, seed);
            int placed = 0;
            for (int x = 2; x < 100 && placed < 20; x += 5) {
                for (int y = 2; y < 100 && placed < 20; y += 5) {
                    Plant* p = sim.add_plant({x, y}, genome);
                    if (p) {
                        p->resources() = Resources{200.f, 200.f, 50.f};
                        ++placed;
                    }
                }
            }
            sim.run(20);
            float total_energy = 0.f;
            for (const auto& p : sim.plants()) total_energy += p.resources().energy;
            return {sim.plants().size(), total_energy};
        };

        auto [c1, e1] = run(99);
        auto [c2, e2] = run(99);

        REQUIRE(c1 == c2);
        REQUIRE_THAT(e1, WithinAbs(e2, 1e-3f));
    }
}

// ─── thread safety ────────────────────────────────────────────────────────────

TEST_CASE("Parallel brain thread safety", "[parallel][simulation]") {
    SECTION("Brain states are independent between plants") {
        // Two plants with different genomes; run in parallel.
        // Check that each brain's memory reflects only its own execution.
        std::vector<uint8_t> genome_a(256, 0);
        // Plant A: writes 0xAA to addr 100
        genome_a[0] = OP_LOAD_IMM; genome_a[1] = 100; genome_a[2] = 0; genome_a[3] = 0xAA;
        genome_a[4] = OP_HALT;

        std::vector<uint8_t> genome_b(256, 0);
        // Plant B: writes 0x55 to addr 100
        genome_b[0] = OP_LOAD_IMM; genome_b[1] = 100; genome_b[2] = 0; genome_b[3] = 0x55;
        genome_b[4] = OP_HALT;

        Simulation sim(50, 50, 1);
        uint64_t id_a = 0, id_b = 0;
        {
            Plant* pa = sim.add_plant({10, 25}, genome_a);
            REQUIRE(pa != nullptr);
            id_a = pa->id();
            pa->resources() = Resources{500.f, 500.f, 100.f};
        }
        {
            Plant* pb = sim.add_plant({20, 25}, genome_b);
            REQUIRE(pb != nullptr);
            id_b = pb->id();
            pb->resources() = Resources{500.f, 500.f, 100.f};
        }

        sim.advance_tick();

        // Each brain should have its own value, not overwritten by the other
        Plant* pa = sim.find_plant(id_a);
        Plant* pb = sim.find_plant(id_b);
        REQUIRE(pa != nullptr);
        REQUIRE(pb != nullptr);
        REQUIRE(pa->brain().read(100) == 0xAA);
        REQUIRE(pb->brain().read(100) == 0x55);
    }

    SECTION("Many brains running concurrently don't corrupt each other") {
        const int N = std::max(4, static_cast<int>(std::thread::hardware_concurrency()) * 2);

        // Each genome writes a unique marker to addr 200
        std::vector<std::vector<uint8_t>> genomes(N);
        for (int i = 0; i < N; ++i) {
            genomes[i].assign(256, 0);
            genomes[i][0] = OP_LOAD_IMM;
            genomes[i][1] = 200; genomes[i][2] = 0;
            genomes[i][3] = static_cast<uint8_t>(i + 1);  // unique marker 1..N
            genomes[i][4] = OP_HALT;
        }

        Simulation sim(200, 200, 7);
        // Collect plant IDs (not pointers — plants_ vector may reallocate)
        std::vector<uint64_t> plant_ids;
        int placed = 0;
        for (int x = 5; x < 200 && placed < N; x += 10) {
            Plant* p = sim.add_plant({x, 100}, genomes[placed]);
            if (p) {
                p->resources() = Resources{500.f, 500.f, 100.f};
                plant_ids.push_back(p->id());
                ++placed;
            }
        }

        sim.advance_tick();

        for (int i = 0; i < static_cast<int>(plant_ids.size()); ++i) {
            Plant* p = sim.find_plant(plant_ids[i]);
            REQUIRE(p != nullptr);
            REQUIRE(p->brain().read(200) == static_cast<uint8_t>(i + 1));
        }
    }
}

// ─── resource edge cases ─────────────────────────────────────────────────────

TEST_CASE("Resource edge cases", "[parallel][resources]") {
    SECTION("Plant at exactly zero energy after tick dies") {
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, make_halt_genome());
        REQUIRE(p != nullptr);
        // Set water high; zero world water so primary draw gives 0
        sim.world().cell_at({25, 25}).water_level = 0.f;
        p->resources() = Resources{0.0f, 500.f, 100.f};
        // primary_costs.maintain_energy = 0.1 per tick → plant should die
        sim.advance_tick();
        REQUIRE(sim.plants().empty());
    }

    SECTION("Plant at exactly zero water after tick dies") {
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, make_halt_genome());
        REQUIRE(p != nullptr);
        sim.world().cell_at({25, 25}).water_level = 0.f;
        p->resources() = Resources{500.f, 0.0f, 100.f};
        sim.advance_tick();
        REQUIRE(sim.plants().empty());
    }

    SECTION("Plant with exactly enough energy to survive survives") {
        const auto& cfg = get_config();
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, make_halt_genome());
        REQUIRE(p != nullptr);
        // Give just enough water so primary water draw keeps water positive
        sim.world().cell_at({25, 25}).water_level = 100.f;
        // Energy = maintenance + epsilon
        float epsilon = 0.001f;
        p->resources().energy = cfg.primary_costs.maintain_energy + epsilon;
        p->resources().water = 500.f;
        p->resources().nutrients = 100.f;
        sim.advance_tick();
        REQUIRE_FALSE(sim.plants().empty());
    }

    SECTION("Instruction limit penalty is applied in parallel execution") {
        const auto& cfg = get_config();
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, make_looping_genome());
        REQUIRE(p != nullptr);
        p->resources() = Resources{500.f, 500.f, 100.f};
        float energy_before = p->resources().energy;

        sim.advance_tick();

        // Energy should have decreased by at least instruction_limit_penalty
        // (also maintenance, but penalty is the dominant cost here)
        REQUIRE(p->resources().energy < energy_before - cfg.instruction_limit_penalty + 0.01f);
    }

    SECTION("OOB memory penalty accumulates correctly") {
        // Brain that reads from OOB address repeatedly
        std::vector<uint8_t> genome(256, 0);
        // SENSE_WATER at a world position using OOB dest address
        // Actually use LOAD_IND with an address pointing beyond memory
        // Simpler: write to addr 65535 (definitely OOB for 1024-byte brain)
        genome[0] = OP_LOAD_IMM;
        genome[1] = 0xFF; genome[2] = 0xFF;  // addr 65535 — OOB
        genome[3] = 42;
        genome[4] = OP_HALT;

        const auto& cfg = get_config();
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, genome);
        REQUIRE(p != nullptr);
        p->resources() = Resources{500.f, 500.f, 100.f};
        float energy_before = p->resources().energy;

        sim.advance_tick();

        // OOB penalty should have been charged
        REQUIRE(p->resources().energy < energy_before - cfg.oob_memory_penalty + 0.01f);
    }
}

// ─── placement conflict resolution ───────────────────────────────────────────

TEST_CASE("Parallel placement conflict resolution", "[parallel][simulation]") {
    SECTION("Two plants competing for same cell both get charged") {
        // Plant A at (20, 25) tries to place at (21, 25) with dx=+1
        // Plant B at (30, 25) tries to place at (21, 25)... but that's too far.
        // Instead: A at (20,25) → target (21,25), B at (22,25) → target (21,25)
        std::vector<uint8_t> genome_a(256, 0);
        genome_a[0] = OP_PLACE_CELL;
        genome_a[1] = static_cast<uint8_t>(CellType::SmallLeaf);
        genome_a[2] = 1;   // dx = +1 → (21, 25)
        genome_a[3] = 0; genome_a[4] = 0; genome_a[5] = OP_HALT;

        std::vector<uint8_t> genome_b(256, 0);
        genome_b[0] = OP_PLACE_CELL;
        genome_b[1] = static_cast<uint8_t>(CellType::SmallLeaf);
        genome_b[2] = static_cast<uint8_t>(-1);  // dx = -1 → (21, 25)
        genome_b[3] = 0; genome_b[4] = 0; genome_b[5] = OP_HALT;

        Simulation sim(50, 50, 42);
        uint64_t id_a, id_b;
        {
            Plant* pa = sim.add_plant({20, 25}, genome_a);
            REQUIRE(pa != nullptr);
            id_a = pa->id();
            pa->resources() = Resources{500.f, 500.f, 100.f};
        }
        {
            Plant* pb = sim.add_plant({22, 25}, genome_b);
            REQUIRE(pb != nullptr);
            id_b = pb->id();
            pb->resources() = Resources{500.f, 500.f, 100.f};
        }

        // Run until one of the plants actually queues a conflicting placement.
        // Since both plants are adjacent to (21,25), the conflict fires and
        // placements_cancelled should be non-zero.
        TickStats stats = sim.advance_tick();

        Plant* pa = sim.find_plant(id_a);
        Plant* pb = sim.find_plant(id_b);
        REQUIRE(pa != nullptr);
        REQUIRE(pb != nullptr);

        // At least one cancellation should have occurred (both competing for same cell)
        REQUIRE(stats.placements_cancelled >= 2);
        // Cell should not have been placed by either plant (conflict → neither proceeds)
        REQUIRE_FALSE(sim.world().cell_at({21, 25}).is_occupied());
    }

    SECTION("Same plant placing cell twice doesn't double-charge") {
        // A genome that queues two PlaceCell actions to the same position
        std::vector<uint8_t> genome(256, 0);
        genome[0] = OP_PLACE_CELL;
        genome[1] = static_cast<uint8_t>(CellType::SmallLeaf);
        genome[2] = 1; genome[3] = 0; genome[4] = 0;  // (+1, 0)
        // Second placement to same position
        genome[5] = OP_PLACE_CELL;
        genome[6] = static_cast<uint8_t>(CellType::SmallLeaf);
        genome[7] = 1; genome[8] = 0; genome[9] = 0;
        genome[10] = OP_HALT;

        const auto& cfg = get_config();
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, genome);
        REQUIRE(p != nullptr);
        p->resources() = Resources{500.f, 500.f, 100.f};
        float e0 = p->resources().energy;

        sim.advance_tick();

        // Should only be charged once (same plant, same position → no multi_plant conflict)
        float leaf_cost = cfg.small_leaf_costs.build_energy;
        float maintenance = cfg.primary_costs.maintain_energy;
        REQUIRE(p->resources().energy >= e0 - leaf_cost - maintenance - 0.5f);
    }
}

// ─── TapRoot vs FiberRoot ────────────────────────────────────────────────────

TEST_CASE("TapRoot water extraction", "[parallel][resources]") {
    World world(50, 50, 42);
    const auto& cfg = get_config();

    SECTION("TapRoot extracts more water than FiberRoot") {
        std::vector<uint8_t> genome(100, 0);

        Plant pa(1, {10, 25}, genome);
        pa.resources() = Resources{500.f, 500.f, 100.f};
        world.cell_at({10, 25}).plant_id = pa.id();
        world.cell_at({10, 25}).water_level = 100.f;
        world.cell_at({11, 25}).water_level = 100.f;
        pa.place_cell(CellType::FiberRoot, {11, 25}, Direction::North, world);

        Plant pb(2, {20, 25}, genome);
        pb.resources() = Resources{500.f, 500.f, 100.f};
        world.cell_at({20, 25}).plant_id = pb.id();
        world.cell_at({20, 25}).water_level = 100.f;
        world.cell_at({21, 25}).water_level = 100.f;
        pb.place_cell(CellType::TapRoot, {21, 25}, Direction::North, world);

        float water_fiber = ResourceSystem::calculate_root_water(pa, world);
        float water_tap   = ResourceSystem::calculate_root_water(pb, world);

        // Extraction = water_level * rate (multiplicative model)
        REQUIRE(water_tap > water_fiber);
        REQUIRE_THAT(water_tap,   WithinAbs(100.f * cfg.tap_root_water_rate,   0.1f));
        REQUIRE_THAT(water_fiber, WithinAbs(100.f * cfg.fiber_root_water_rate, 0.1f));
    }

    SECTION("TapRoot does not extract nutrients") {
        std::vector<uint8_t> genome(100, 0);

        Plant p(3, {30, 25}, genome);
        p.resources() = Resources{500.f, 500.f, 100.f};
        world.cell_at({30, 25}).plant_id = p.id();
        world.cell_at({31, 25}).nutrient_level = 100.f;
        world.cell_at({31, 25}).water_level = 100.f;
        p.place_cell(CellType::TapRoot, {31, 25}, Direction::North, world);

        float nutrients = ResourceSystem::calculate_root_nutrients(p, world);
        REQUIRE(nutrients == 0.0f);
    }

    SECTION("FiberRoot extracts both water and nutrients") {
        std::vector<uint8_t> genome(100, 0);

        Plant p(4, {40, 25}, genome);
        p.resources() = Resources{500.f, 500.f, 100.f};
        world.cell_at({40, 25}).plant_id = p.id();
        world.cell_at({41, 25}).water_level = 100.f;
        world.cell_at({41, 25}).nutrient_level = 100.f;
        p.place_cell(CellType::FiberRoot, {41, 25}, Direction::North, world);

        float water     = ResourceSystem::calculate_root_water(p, world);
        float nutrients = ResourceSystem::calculate_root_nutrients(p, world);

        REQUIRE(water > 0.0f);
        REQUIRE(nutrients > 0.0f);
    }
}

// ─── primary cell water draw ──────────────────────────────────────────────────

TEST_CASE("Primary cell water draw", "[parallel][resources]") {
    SECTION("Primary cell draws water proportional to world water level") {
        const auto& cfg = get_config();
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, make_halt_genome());
        REQUIRE(p != nullptr);

        // High world water → primary draws more
        sim.world().cell_at({25, 25}).water_level = 100.f;
        p->resources() = Resources{500.f, 0.f, 100.f};  // start at 0 water

        sim.advance_tick();

        // primary water draw = water_level * primary_water_rate (multiplicative model)
        float expected = 100.f * cfg.primary_water_rate;
        REQUIRE(p->resources().water > 0.0f);
        REQUIRE_THAT(p->resources().water, WithinAbs(expected, 0.5f));
    }

    SECTION("Primary cell draws zero water when world water is zero") {
        Simulation sim(50, 50, 42);
        Plant* p = sim.add_plant({25, 25}, make_halt_genome());
        REQUIRE(p != nullptr);
        sim.world().cell_at({25, 25}).water_level = 0.f;
        p->resources() = Resources{500.f, 10.f, 100.f};

        float water_before = p->resources().water;
        sim.advance_tick();

        // No primary water gained (cell water = 0); only maintenance subtracted
        // primary has no water maintenance, so water stays at 10
        REQUIRE_THAT(p->resources().water, WithinAbs(water_before, 0.01f));
    }
}

// ─── fire damage under load ──────────────────────────────────────────────────

TEST_CASE("Fire damage with concurrent brains", "[parallel][simulation]") {
    SECTION("Fire destroys cells while other brains run concurrently") {
        std::vector<uint8_t> genome = make_halt_genome();

        Simulation sim(50, 50, 42);
        const auto& cfg = get_config();

        uint64_t id_a, id_b;
        {
            Plant* pa = sim.add_plant({10, 25}, genome);
            REQUIRE(pa != nullptr);
            id_a = pa->id();
            pa->resources() = Resources{500.f, 500.f, 100.f};
        }
        {
            Plant* pb = sim.add_plant({40, 25}, genome);
            REQUIRE(pb != nullptr);
            id_b = pb->id();
            pb->resources() = Resources{500.f, 500.f, 100.f};
        }

        // Ignite plant A's primary cell
        sim.world().ignite({10, 25});

        // Advance until fire destroys plant A
        bool a_died = false;
        for (int i = 0; i < static_cast<int>(cfg.fire_destroy_ticks) + 5; ++i) {
            sim.advance_tick();
            if (sim.find_plant(id_a) == nullptr) { a_died = true; break; }
        }

        // plant A should have been killed by fire; plant B should still be alive
        REQUIRE(a_died);
        REQUIRE(sim.find_plant(id_b) != nullptr);
    }
}
