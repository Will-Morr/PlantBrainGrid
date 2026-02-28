#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/resources.hpp"
#include "core/world.hpp"
#include "core/config.hpp"

using namespace pbg;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Helper to create a test plant with resources
static Plant make_test_plant(const GridCoord& pos = {50, 50}) {
    std::vector<uint8_t> genome(100, 0);
    Plant plant(1, pos, genome);
    plant.resources() = Resources{100.0f, 100.0f, 100.0f};
    return plant;
}

TEST_CASE("Leaf energy generation", "[resources]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Small leaf generates energy based on light") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        // Place a small leaf
        plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);

        float energy = ResourceSystem::calculate_leaf_energy(plant, world);
        float expected = cfg.small_leaf_energy_rate * world.current_light_multiplier();

        REQUIRE_THAT(energy, WithinRel(expected, 0.01f));
    }

    SECTION("Big leaf generates more energy than small leaf") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::BigLeaf, {51, 50}, Direction::North, world);

        float energy = ResourceSystem::calculate_leaf_energy(plant, world);
        float expected = cfg.big_leaf_energy_rate * world.current_light_multiplier();

        REQUIRE_THAT(energy, WithinRel(expected, 0.01f));
        REQUIRE(cfg.big_leaf_energy_rate > cfg.small_leaf_energy_rate);
    }

    SECTION("Disabled leaves don't generate energy") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);
        plant.toggle_cell({51, 50}, false);

        float energy = ResourceSystem::calculate_leaf_energy(plant, world);
        REQUIRE(energy == 0.0f);
    }

    SECTION("Multiple leaves stack energy") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);
        plant.place_cell(CellType::SmallLeaf, {49, 50}, Direction::North, world);
        plant.place_cell(CellType::SmallLeaf, {50, 51}, Direction::North, world);

        float energy = ResourceSystem::calculate_leaf_energy(plant, world);
        float expected = 3.0f * cfg.small_leaf_energy_rate * world.current_light_multiplier();

        REQUIRE_THAT(energy, WithinRel(expected, 0.01f));
    }
}

TEST_CASE("Root water extraction", "[resources]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Root extracts water from ground") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        GridCoord root_pos{51, 50};
        float initial_water = 50.0f;
        world.cell_at(root_pos).water_level = initial_water;

        plant.place_cell(CellType::Root, root_pos, Direction::North, world);

        float extracted = ResourceSystem::calculate_root_water(plant, world);

        REQUIRE(extracted > 0.0f);
        REQUIRE(extracted <= cfg.root_water_rate);
        // World water is infinite — cell level is not depleted
        REQUIRE(world.cell_at(root_pos).water_level == initial_water);
    }

    SECTION("Root extracts limited by available water") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        GridCoord root_pos{51, 50};
        float scarce_water = 0.5f;  // Less than root_water_rate
        world.cell_at(root_pos).water_level = scarce_water;

        plant.place_cell(CellType::Root, root_pos, Direction::North, world);

        float extracted = ResourceSystem::calculate_root_water(plant, world);

        // Extraction is capped by the cell's water level
        REQUIRE_THAT(extracted, WithinAbs(scarce_water, 0.01f));
        // Cell level is not depleted (infinite supply)
        REQUIRE_THAT(world.cell_at(root_pos).water_level, WithinAbs(scarce_water, 0.01f));
    }

    SECTION("Disabled roots don't extract water") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        GridCoord root_pos{51, 50};
        float initial_water = 50.0f;
        world.cell_at(root_pos).water_level = initial_water;

        plant.place_cell(CellType::Root, root_pos, Direction::North, world);
        plant.toggle_cell(root_pos, false);

        float extracted = ResourceSystem::calculate_root_water(plant, world);

        REQUIRE(extracted == 0.0f);
        REQUIRE(world.cell_at(root_pos).water_level == initial_water);
    }
}

TEST_CASE("Root nutrient extraction", "[resources]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Root extracts nutrients from ground") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        GridCoord root_pos{51, 50};
        float initial_nutrients = 30.0f;
        world.cell_at(root_pos).nutrient_level = initial_nutrients;

        plant.place_cell(CellType::Root, root_pos, Direction::North, world);

        float extracted = ResourceSystem::calculate_root_nutrients(plant, world);

        REQUIRE(extracted > 0.0f);
        REQUIRE(extracted <= cfg.root_nutrient_rate);
        // World nutrients are infinite — cell level is not depleted
        REQUIRE(world.cell_at(root_pos).nutrient_level == initial_nutrients);
    }
}

TEST_CASE("Maintenance costs", "[resources]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Primary cell has maintenance cost") {
        auto plant = make_test_plant();

        Resources maintenance = ResourceSystem::calculate_maintenance(plant);

        REQUIRE(maintenance.energy >= cfg.primary_maintenance_energy);
    }

    SECTION("Leaves have water maintenance cost") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);

        Resources maintenance = ResourceSystem::calculate_maintenance(plant);

        REQUIRE(maintenance.water >= cfg.small_leaf_maintenance_water);
    }

    SECTION("Big leaves cost more to maintain") {
        auto plant1 = make_test_plant({40, 50});
        auto plant2 = make_test_plant({60, 50});

        world.cell_at(plant1.primary_position()).plant_id = plant1.id();
        world.cell_at(plant1.primary_position()).cell_type = CellType::Primary;
        world.cell_at(plant2.primary_position()).plant_id = plant2.id();
        world.cell_at(plant2.primary_position()).cell_type = CellType::Primary;

        plant1.place_cell(CellType::SmallLeaf, {41, 50}, Direction::North, world);
        plant2.place_cell(CellType::BigLeaf, {61, 50}, Direction::North, world);

        Resources m1 = ResourceSystem::calculate_maintenance(plant1);
        Resources m2 = ResourceSystem::calculate_maintenance(plant2);

        REQUIRE(m2.water > m1.water);
        REQUIRE(m2.nutrients > m1.nutrients);
    }

    SECTION("Maintenance applies even to disabled cells") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);
        Resources m1 = ResourceSystem::calculate_maintenance(plant);

        plant.toggle_cell({51, 50}, false);
        Resources m2 = ResourceSystem::calculate_maintenance(plant);

        // Maintenance should be the same whether enabled or not
        REQUIRE(m1.water == m2.water);
    }
}

TEST_CASE("Full resource tick", "[resources]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Resource tick updates plant resources") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        // Add a leaf and root
        GridCoord leaf_pos{51, 50};
        GridCoord root_pos{49, 50};
        world.cell_at(root_pos).water_level = 100.0f;
        world.cell_at(root_pos).nutrient_level = 50.0f;

        plant.place_cell(CellType::SmallLeaf, leaf_pos, Direction::North, world);
        plant.place_cell(CellType::Root, root_pos, Direction::North, world);

        float initial_energy = plant.resources().energy;
        float initial_water = plant.resources().water;

        ResourceTickResult result = ResourceSystem::process_tick(plant, world);

        // Should have generated energy and extracted water
        REQUIRE(result.energy_generated > 0.0f);
        REQUIRE(result.water_extracted > 0.0f);

        // Should have paid maintenance
        REQUIRE(result.energy_maintenance > 0.0f);
        REQUIRE(result.water_maintenance > 0.0f);
    }

    SECTION("Dead plant doesn't process resources") {
        auto plant = make_test_plant();
        plant.kill();

        float initial_energy = plant.resources().energy;

        ResourceTickResult result = ResourceSystem::process_tick(plant, world);

        REQUIRE(result.energy_generated == 0.0f);
        REQUIRE(plant.resources().energy == initial_energy);
    }

    SECTION("Resources don't go negative") {
        auto plant = make_test_plant();
        plant.resources() = Resources{0.0f, 0.0f, 0.0f};  // Start with nothing

        ResourceSystem::process_tick(plant, world);

        REQUIRE(plant.resources().energy >= 0.0f);
        REQUIRE(plant.resources().water >= 0.0f);
        REQUIRE(plant.resources().nutrients >= 0.0f);
    }
}

TEST_CASE("Xylem flow", "[resources]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Enabled xylem costs resources to operate") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        // Add xylem
        plant.place_cell(CellType::Xylem, {51, 50}, Direction::North, world);

        float initial_energy = plant.resources().energy;

        Resources loss = ResourceSystem::process_xylem_flow(plant, world);

        REQUIRE(plant.resources().energy < initial_energy);
    }

    SECTION("Disabled xylem doesn't cost resources") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::Xylem, {51, 50}, Direction::North, world);
        plant.toggle_cell({51, 50}, false);

        float initial_energy = plant.resources().energy;

        Resources loss = ResourceSystem::process_xylem_flow(plant, world);

        REQUIRE(plant.resources().energy == initial_energy);
    }

    SECTION("FireproofXylem also costs resources") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::FireproofXylem, {51, 50}, Direction::North, world);

        float initial_energy = plant.resources().energy;

        Resources loss = ResourceSystem::process_xylem_flow(plant, world);

        REQUIRE(plant.resources().energy < initial_energy);
    }
}

TEST_CASE("Seasonal light affects energy", "[resources]") {
    World world(100, 100, 42);
    auto& cfg = get_config();

    SECTION("Energy varies with season") {
        auto plant = make_test_plant();
        world.cell_at(plant.primary_position()).plant_id = plant.id();
        world.cell_at(plant.primary_position()).cell_type = CellType::Primary;

        plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);

        // Get energy at start
        float energy_start = ResourceSystem::calculate_leaf_energy(plant, world);

        // Advance to different season (quarter cycle)
        for (uint32_t i = 0; i < cfg.season_length / 4; ++i) {
            world.advance_tick();
        }

        float energy_quarter = ResourceSystem::calculate_leaf_energy(plant, world);

        // Energy should be different at different seasons
        REQUIRE(energy_start != energy_quarter);
    }
}
