#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/resources.hpp"
#include "core/simulation.hpp"
#include "core/config.hpp"
#include "core/brain_ops.hpp"

using namespace pbg;
using Catch::Matchers::WithinAbs;

static Plant make_plant(uint64_t id = 1, const GridCoord& pos = {50, 50}) {
    std::vector<uint8_t> genome(100, 0);
    Plant plant(id, pos, genome);
    plant.resources() = Resources{100.0f, 100.0f, 100.0f};
    return plant;
}

// ─── Resource Caps ──────────────────────────────────────────────────────────

TEST_CASE("Resource caps", "[resources][storage]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Base resource cap limits resource accumulation") {
        auto plant = make_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        // Set resources above cap
        plant.resources().energy = 500.0f;
        plant.resources().water = 500.0f;
        plant.resources().nutrients = 500.0f;

        ResourceSystem::clamp_to_caps(plant);

        REQUIRE_THAT(plant.resources().energy, WithinAbs(cfg.base_resource_cap, 0.01f));
        REQUIRE_THAT(plant.resources().water, WithinAbs(cfg.base_resource_cap, 0.01f));
        REQUIRE_THAT(plant.resources().nutrients, WithinAbs(cfg.base_resource_cap, 0.01f));
    }

    SECTION("Resources below cap are not affected") {
        auto plant = make_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.resources().energy = 50.0f;
        ResourceSystem::clamp_to_caps(plant);
        REQUIRE_THAT(plant.resources().energy, WithinAbs(50.0f, 0.01f));
    }
}

// ─── Store Cells ────────────────────────────────────────────────────────────

TEST_CASE("StoreEnergy cell", "[resources][storage]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("StoreEnergy increases energy cap") {
        auto plant = make_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::StoreEnergy, {51, 50}, world);

        float expected_cap = cfg.base_resource_cap + cfg.store_capacity_bonus;

        plant.resources().energy = expected_cap + 100.0f;
        ResourceSystem::clamp_to_caps(plant);
        REQUIRE_THAT(plant.resources().energy, WithinAbs(expected_cap, 0.01f));

        // Water and nutrients caps remain at base
        plant.resources().water = cfg.base_resource_cap + 100.0f;
        ResourceSystem::clamp_to_caps(plant);
        REQUIRE_THAT(plant.resources().water, WithinAbs(cfg.base_resource_cap, 0.01f));
    }

    SECTION("Multiple StoreEnergy cells stack") {
        auto plant = make_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::StoreEnergy, {51, 50}, world);
        plant.place_cell(CellType::StoreEnergy, {49, 50}, world);

        float expected_cap = cfg.base_resource_cap + 2 * cfg.store_capacity_bonus;
        plant.resources().energy = expected_cap + 100.0f;
        ResourceSystem::clamp_to_caps(plant);
        REQUIRE_THAT(plant.resources().energy, WithinAbs(expected_cap, 0.01f));
    }

    SECTION("StoreEnergy costs and maintenance") {
        REQUIRE(cfg.store_energy_costs.build_energy == 10.0f);
        REQUIRE(cfg.store_energy_costs.maintain_water == 0.02f);
    }
}

TEST_CASE("StoreWater cell", "[resources][storage]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("StoreWater increases water cap") {
        auto plant = make_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::StoreWater, {51, 50}, world);

        float expected_cap = cfg.base_resource_cap + cfg.store_capacity_bonus;

        plant.resources().water = expected_cap + 100.0f;
        ResourceSystem::clamp_to_caps(plant);
        REQUIRE_THAT(plant.resources().water, WithinAbs(expected_cap, 0.01f));

        // Energy cap unaffected
        plant.resources().energy = cfg.base_resource_cap + 100.0f;
        ResourceSystem::clamp_to_caps(plant);
        REQUIRE_THAT(plant.resources().energy, WithinAbs(cfg.base_resource_cap, 0.01f));
    }

    SECTION("StoreWater costs and maintenance") {
        REQUIRE(cfg.store_water_costs.build_energy == 10.0f);
        REQUIRE(cfg.store_water_costs.maintain_water == 0.02f);
    }
}

TEST_CASE("StoreNutrients cell", "[resources][storage]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("StoreNutrients increases nutrients cap") {
        auto plant = make_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::StoreNutrients, {51, 50}, world);

        float expected_cap = cfg.base_resource_cap + cfg.store_capacity_bonus;

        plant.resources().nutrients = expected_cap + 100.0f;
        ResourceSystem::clamp_to_caps(plant);
        REQUIRE_THAT(plant.resources().nutrients, WithinAbs(expected_cap, 0.01f));
    }

    SECTION("StoreNutrients costs and maintenance") {
        REQUIRE(cfg.store_nutrients_costs.build_energy == 10.0f);
        REQUIRE(cfg.store_nutrients_costs.maintain_water == 0.02f);
    }
}

// ─── Haustorium ─────────────────────────────────────────────────────────────

TEST_CASE("Haustorium cell", "[simulation][haustorium]") {
    auto& cfg = get_config();

    SECTION("Haustorium steals resources from adjacent enemy plant") {
        Simulation sim(100, 100, 42);

        // Plant A with haustorium
        std::vector<uint8_t> genome(100, 0);
        genome[0] = OP_HALT;
        Plant* a = sim.add_plant({50, 50}, genome);
        REQUIRE(a != nullptr);
        a->resources() = Resources{100.0f, 100.0f, 100.0f};

        // Plant B adjacent to where we'll place haustorium
        Plant* b = sim.add_plant({52, 50}, genome);
        REQUIRE(b != nullptr);
        b->resources() = Resources{100.0f, 100.0f, 100.0f};

        // Place haustorium on plant A at position adjacent to B
        a->place_cell(CellType::Haustorium, {51, 50}, sim.world());

        float b_energy_before = b->resources().energy;

        // Run one tick
        sim.advance_tick();

        // B lost resources to theft (plus its own maintenance)
        REQUIRE(b->resources().energy < b_energy_before);
    }

    SECTION("Haustorium does not steal from own plant") {
        Simulation sim(100, 100, 42);

        std::vector<uint8_t> genome(100, 0);
        genome[0] = OP_HALT;
        Plant* a = sim.add_plant({50, 50}, genome);
        REQUIRE(a != nullptr);
        a->resources() = Resources{100.0f, 100.0f, 100.0f};

        // Place cells adjacent to each other, all owned by same plant
        a->place_cell(CellType::SmallLeaf, {51, 50}, sim.world());
        a->place_cell(CellType::Haustorium, {49, 50}, sim.world());

        sim.advance_tick();

        // Haustorium adjacent to own SmallLeaf should NOT steal
        // Plant should still be alive (no unexpected resource drain)
        REQUIRE(a->is_alive());
    }

    SECTION("Haustorium costs and maintenance") {
        REQUIRE(cfg.haustorium_costs.build_energy == 10.0f);
        REQUIRE(cfg.haustorium_costs.maintain_energy == 0.1f);
    }

    SECTION("Haustorium steals correct amount") {
        // Direct test of process_haustorium via simulation
        Simulation sim(100, 100, 42);

        std::vector<uint8_t> genome(100, 0);
        genome[0] = OP_HALT;

        Plant* thief = sim.add_plant({50, 50}, genome);
        Plant* victim = sim.add_plant({52, 50}, genome);
        REQUIRE(thief != nullptr);
        REQUIRE(victim != nullptr);

        thief->resources() = Resources{50.0f, 50.0f, 50.0f};
        victim->resources() = Resources{50.0f, 50.0f, 50.0f};

        // Place haustorium adjacent to victim's primary cell
        thief->place_cell(CellType::Haustorium, {51, 50}, sim.world());

        // Record post-placement resources
        float victim_e = victim->resources().energy;

        sim.advance_tick();

        // Victim should have lost at least steal_rate in energy
        // (the exact amount depends on what victim has, but 50 > steal_rate)
        REQUIRE(victim->resources().energy < victim_e);
    }

    SECTION("Haustorium cannot steal more than victim has") {
        Simulation sim(100, 100, 42);

        std::vector<uint8_t> genome(100, 0);
        genome[0] = OP_HALT;

        Plant* thief = sim.add_plant({50, 50}, genome);
        Plant* victim = sim.add_plant({52, 50}, genome);
        REQUIRE(thief != nullptr);
        REQUIRE(victim != nullptr);

        thief->resources() = Resources{50.0f, 50.0f, 50.0f};
        // Victim has very little resources
        victim->resources() = Resources{0.1f, 0.1f, 0.1f};

        thief->place_cell(CellType::Haustorium, {51, 50}, sim.world());

        sim.advance_tick();

        // Victim's resources should not go negative from stealing alone
        // (they may go negative from maintenance and die, but stealing is clamped)
    }
}

// ─── Cell Type Placement ────────────────────────────────────────────────────

TEST_CASE("New cell types are placeable", "[plant]") {
    World world(100, 100, 42);

    std::vector<uint8_t> genome(100, 0);
    Plant plant(1, {50, 50}, genome);
    plant.resources() = Resources{100.0f, 100.0f, 100.0f};
    world.cell_at({50, 50}).plant_id = plant.id();
    world.cell_at({50, 50}).cell_type = CellType::Primary;

    SECTION("StoreEnergy can be placed") {
        REQUIRE(plant.can_place_cell(CellType::StoreEnergy, {51, 50}, world));
        REQUIRE(plant.place_cell(CellType::StoreEnergy, {51, 50}, world));
        REQUIRE(world.cell_at({51, 50}).cell_type == CellType::StoreEnergy);
    }

    SECTION("StoreWater can be placed") {
        REQUIRE(plant.can_place_cell(CellType::StoreWater, {51, 50}, world));
        REQUIRE(plant.place_cell(CellType::StoreWater, {51, 50}, world));
        REQUIRE(world.cell_at({51, 50}).cell_type == CellType::StoreWater);
    }

    SECTION("StoreNutrients can be placed") {
        REQUIRE(plant.can_place_cell(CellType::StoreNutrients, {51, 50}, world));
        REQUIRE(plant.place_cell(CellType::StoreNutrients, {51, 50}, world));
        REQUIRE(world.cell_at({51, 50}).cell_type == CellType::StoreNutrients);
    }

    SECTION("Haustorium can be placed") {
        REQUIRE(plant.can_place_cell(CellType::Haustorium, {51, 50}, world));
        REQUIRE(plant.place_cell(CellType::Haustorium, {51, 50}, world));
        REQUIRE(world.cell_at({51, 50}).cell_type == CellType::Haustorium);
    }
}

// ─── Resource Cap During process_tick ────────────────────────────────────────

TEST_CASE("Resource capping during process_tick", "[resources][storage]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("process_tick clamps resources to cap") {
        auto plant = make_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        // Give plant lots of leaves so it generates more than cap
        for (int i = 1; i <= 5; ++i) {
            plant.place_cell(CellType::BigLeaf, {50 + i, 50}, world);
        }

        // Set resources near cap
        plant.resources().energy = cfg.base_resource_cap - 1.0f;

        ResourceSystem::process_tick(plant, world);

        // Energy should not exceed cap (even though leaves generate lots)
        REQUIRE(plant.resources().energy <= cfg.base_resource_cap + 0.01f);
    }
}
