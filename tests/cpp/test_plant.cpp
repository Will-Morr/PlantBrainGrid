#include <catch2/catch_test_macros.hpp>
#include "core/plant.hpp"
#include "core/world.hpp"
#include "core/brain.hpp"

using namespace pbg;

TEST_CASE("Plant construction", "[plant]") {
    std::vector<uint8_t> genome(100, 0);
    World world(100, 100, 42);

    SECTION("Creates plant with primary cell") {
        Plant plant(1, {50, 50}, genome);

        REQUIRE(plant.id() == 1);
        REQUIRE(plant.primary_position() == GridCoord{50, 50});
        REQUIRE(plant.is_alive());
        REQUIRE(plant.age() == 0);
        REQUIRE(plant.cell_count() == 1);

        const PlantCell* primary = plant.find_cell({50, 50});
        REQUIRE(primary != nullptr);
        REQUIRE(primary->type == CellType::Primary);
    }

    SECTION("Plant has brain") {
        Plant plant(1, {50, 50}, genome);
        REQUIRE(plant.brain().size() >= genome.size());
    }
}

TEST_CASE("Plant cell placement", "[plant]") {
    std::vector<uint8_t> genome(100, 0);
    World world(100, 100, 42);

    Plant plant(1, {50, 50}, genome);
    plant.resources().energy = 1000.0f;
    plant.resources().water = 1000.0f;
    plant.resources().nutrients = 1000.0f;

    // Register primary cell with world
    world.cell_at(50, 50).plant_id = plant.id();
    world.cell_at(50, 50).cell_type = CellType::Primary;

    SECTION("Can place cell adjacent to existing") {
        REQUIRE(plant.can_place_cell(CellType::SmallLeaf, {51, 50}, world));
        REQUIRE(plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world));
        REQUIRE(plant.cell_count() == 2);
        REQUIRE(world.cell_at(51, 50).is_occupied());
    }

    SECTION("Cannot place cell not adjacent") {
        REQUIRE_FALSE(plant.can_place_cell(CellType::SmallLeaf, {55, 50}, world));
        REQUIRE_FALSE(plant.place_cell(CellType::SmallLeaf, {55, 50}, Direction::North, world));
    }

    SECTION("Cannot place on occupied cell") {
        // Place a cell first
        plant.place_cell(CellType::Root, {51, 50}, Direction::North, world);

        // Try to place another on same spot
        REQUIRE_FALSE(plant.can_place_cell(CellType::SmallLeaf, {51, 50}, world));
    }

    SECTION("Cannot place out of bounds") {
        // Move plant to edge
        Plant edge_plant(2, {0, 0}, genome);
        edge_plant.resources().energy = 1000.0f;

        REQUIRE_FALSE(edge_plant.can_place_cell(CellType::SmallLeaf, {-1, 0}, world));
    }

    SECTION("Placement costs resources") {
        float initial_energy = plant.resources().energy;
        plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);
        REQUIRE(plant.resources().energy < initial_energy);
    }

    SECTION("Cannot place without sufficient resources") {
        plant.resources().energy = 0.0f;
        REQUIRE_FALSE(plant.can_place_cell(CellType::SmallLeaf, {51, 50}, world));
    }
}

TEST_CASE("Plant cell removal", "[plant]") {
    std::vector<uint8_t> genome(100, 0);
    World world(100, 100, 42);

    Plant plant(1, {50, 50}, genome);
    plant.resources().energy = 1000.0f;

    // Setup
    world.cell_at(50, 50).plant_id = plant.id();
    world.cell_at(50, 50).cell_type = CellType::Primary;
    plant.place_cell(CellType::SmallLeaf, {51, 50}, Direction::North, world);

    SECTION("Can remove non-primary cell") {
        REQUIRE(plant.cell_count() == 2);
        REQUIRE(plant.remove_cell({51, 50}, world));
        REQUIRE(plant.cell_count() == 1);
        REQUIRE_FALSE(world.cell_at(51, 50).is_occupied());
    }

    SECTION("Cannot remove primary cell") {
        REQUIRE_FALSE(plant.remove_cell({50, 50}, world));
        REQUIRE(plant.cell_count() == 2);
    }

    SECTION("Cannot remove non-existent cell") {
        REQUIRE_FALSE(plant.remove_cell({60, 60}, world));
    }
}

TEST_CASE("Plant cell toggle", "[plant]") {
    std::vector<uint8_t> genome(100, 0);
    World world(100, 100, 42);

    Plant plant(1, {50, 50}, genome);
    plant.resources().energy = 1000.0f;

    world.cell_at(50, 50).plant_id = plant.id();
    world.cell_at(50, 50).cell_type = CellType::Primary;
    plant.place_cell(CellType::Xylem, {51, 50}, Direction::East, world);

    SECTION("Can toggle cell enabled state") {
        PlantCell* cell = plant.find_cell({51, 50});
        REQUIRE(cell->enabled == true);

        REQUIRE(plant.toggle_cell({51, 50}, false));
        REQUIRE(cell->enabled == false);

        REQUIRE(plant.toggle_cell({51, 50}, true));
        REQUIRE(cell->enabled == true);
    }

    SECTION("Cannot toggle non-existent cell") {
        REQUIRE_FALSE(plant.toggle_cell({60, 60}, false));
    }
}

TEST_CASE("Plant cell rotation", "[plant]") {
    std::vector<uint8_t> genome(100, 0);
    World world(100, 100, 42);

    Plant plant(1, {50, 50}, genome);
    plant.resources().energy = 1000.0f;

    world.cell_at(50, 50).plant_id = plant.id();
    world.cell_at(50, 50).cell_type = CellType::Primary;
    plant.place_cell(CellType::Xylem, {51, 50}, Direction::North, world);

    SECTION("Can rotate xylem cell") {
        PlantCell* cell = plant.find_cell({51, 50});
        REQUIRE(cell->direction == Direction::North);

        REQUIRE(plant.rotate_cell({51, 50}, 1));
        REQUIRE(cell->direction == Direction::East);

        REQUIRE(plant.rotate_cell({51, 50}, 2));
        REQUIRE(cell->direction == Direction::West);
    }

    SECTION("Cannot rotate non-xylem cell") {
        plant.place_cell(CellType::SmallLeaf, {49, 50}, Direction::North, world);
        REQUIRE_FALSE(plant.rotate_cell({49, 50}, 1));
    }
}

TEST_CASE("Plant death", "[plant]") {
    std::vector<uint8_t> genome(100, 0);

    SECTION("Plant starts alive") {
        Plant plant(1, {50, 50}, genome);
        REQUIRE(plant.is_alive());
    }

    SECTION("Kill makes plant dead") {
        Plant plant(1, {50, 50}, genome);
        plant.kill();
        REQUIRE_FALSE(plant.is_alive());
    }

    SECTION("Dead plant cannot place cells") {
        World world(100, 100, 42);
        Plant plant(1, {50, 50}, genome);
        plant.resources().energy = 1000.0f;

        plant.kill();
        REQUIRE_FALSE(plant.can_place_cell(CellType::SmallLeaf, {51, 50}, world));
    }
}

TEST_CASE("Plant thorn blocking", "[plant]") {
    std::vector<uint8_t> genome(100, 0);
    World world(100, 100, 42);

    // Create first plant with thorn
    Plant plant1(1, {50, 50}, genome);
    plant1.resources().energy = 1000.0f;
    world.cell_at(50, 50).plant_id = plant1.id();
    world.cell_at(50, 50).cell_type = CellType::Primary;
    plant1.place_cell(CellType::Thorn, {51, 50}, Direction::North, world);

    // Create second plant nearby
    Plant plant2(2, {53, 50}, genome);
    plant2.resources().energy = 1000.0f;
    world.cell_at(53, 50).plant_id = plant2.id();
    world.cell_at(53, 50).cell_type = CellType::Primary;

    SECTION("Thorn blocks adjacent placement by other plants") {
        // Position (52, 50) is adjacent to the thorn at (51, 50)
        REQUIRE_FALSE(plant2.can_place_cell(CellType::SmallLeaf, {52, 50}, world));
    }

    SECTION("Thorn does not block owner's adjacent placement") {
        // plant1 can still place adjacent to its own thorn
        REQUIRE(plant1.can_place_cell(CellType::Root, {51, 51}, world));
    }
}

TEST_CASE("Plant resource management", "[plant]") {
    std::vector<uint8_t> genome(100, 0);

    Plant plant(1, {50, 50}, genome);
    plant.resources() = Resources{100.0f, 50.0f, 25.0f};

    SECTION("Can pay cost with sufficient resources") {
        REQUIRE(plant.pay_cost(50.0f, 25.0f, 10.0f));
        REQUIRE(plant.resources().energy == 50.0f);
        REQUIRE(plant.resources().water == 25.0f);
        REQUIRE(plant.resources().nutrients == 15.0f);
    }

    SECTION("Cannot pay cost with insufficient resources") {
        REQUIRE_FALSE(plant.pay_cost(200.0f, 0.0f, 0.0f));
        // Resources unchanged
        REQUIRE(plant.resources().energy == 100.0f);
    }
}
