#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/world.hpp"
#include "core/plant_cell.hpp"

using namespace pbg;
using Catch::Matchers::WithinAbs;

TEST_CASE("World construction", "[world]") {
    SECTION("Creates grid with correct dimensions") {
        World world(100, 200, 42);
        REQUIRE(world.width() == 100);
        REQUIRE(world.height() == 200);
        REQUIRE(world.seed() == 42);
        REQUIRE(world.tick() == 0);
    }

    SECTION("All cells are initialized") {
        World world(50, 50, 123);
        for (int32_t y = 0; y < 50; ++y) {
            for (int32_t x = 0; x < 50; ++x) {
                const WorldCell& cell = world.cell_at(x, y);
                REQUIRE(cell.water_level >= 0.0f);
                REQUIRE(cell.nutrient_level >= 0.0f);
                REQUIRE(cell.light_level > 0.0f);
                REQUIRE(cell.fire_ticks == 0);
                REQUIRE(cell.occupant == nullptr);
            }
        }
    }
}

TEST_CASE("World bounds checking", "[world]") {
    World world(100, 100, 42);

    SECTION("in_bounds returns true for valid coordinates") {
        REQUIRE(world.in_bounds(0, 0));
        REQUIRE(world.in_bounds(99, 99));
        REQUIRE(world.in_bounds(50, 50));
        REQUIRE(world.in_bounds(GridCoord{25, 75}));
    }

    SECTION("in_bounds returns false for invalid coordinates") {
        REQUIRE_FALSE(world.in_bounds(-1, 0));
        REQUIRE_FALSE(world.in_bounds(0, -1));
        REQUIRE_FALSE(world.in_bounds(100, 0));
        REQUIRE_FALSE(world.in_bounds(0, 100));
        REQUIRE_FALSE(world.in_bounds(GridCoord{-5, 50}));
    }

    SECTION("cell_at throws for out of bounds") {
        REQUIRE_THROWS(world.cell_at(-1, 0));
        REQUIRE_THROWS(world.cell_at(100, 0));
        REQUIRE_THROWS(world.cell_at(GridCoord{0, 100}));
    }
}

TEST_CASE("World terrain generation", "[world]") {
    SECTION("Same seed produces identical terrain") {
        World w1(100, 100, 12345);
        World w2(100, 100, 12345);

        for (int32_t y = 0; y < 100; y += 10) {
            for (int32_t x = 0; x < 100; x += 10) {
                REQUIRE(w1.cell_at(x, y).water_level == w2.cell_at(x, y).water_level);
                REQUIRE(w1.cell_at(x, y).nutrient_level == w2.cell_at(x, y).nutrient_level);
            }
        }
    }

    SECTION("Different seeds produce different terrain") {
        World w1(100, 100, 100);
        World w2(100, 100, 200);

        int differences = 0;
        for (int32_t y = 0; y < 100; y += 5) {
            for (int32_t x = 0; x < 100; x += 5) {
                if (w1.cell_at(x, y).water_level != w2.cell_at(x, y).water_level) {
                    ++differences;
                }
            }
        }
        REQUIRE(differences > 100);  // Most cells should differ
    }

    SECTION("Terrain has variation") {
        World world(100, 100, 42);

        float min_water = 1e9f, max_water = -1e9f;
        float min_nutrient = 1e9f, max_nutrient = -1e9f;

        for (int32_t y = 0; y < 100; ++y) {
            for (int32_t x = 0; x < 100; ++x) {
                const WorldCell& cell = world.cell_at(x, y);
                min_water = std::min(min_water, cell.water_level);
                max_water = std::max(max_water, cell.water_level);
                min_nutrient = std::min(min_nutrient, cell.nutrient_level);
                max_nutrient = std::max(max_nutrient, cell.nutrient_level);
            }
        }

        REQUIRE(max_water - min_water > 10.0f);  // Should have significant variation
        REQUIRE(max_nutrient - min_nutrient > 5.0f);
    }

    SECTION("regenerate_terrain restores initial state") {
        World world(50, 50, 999);

        // Store original values
        float orig_water = world.cell_at(25, 25).water_level;
        float orig_nutrient = world.cell_at(25, 25).nutrient_level;

        // Modify cell
        world.cell_at(25, 25).water_level = 999.0f;
        REQUIRE(world.cell_at(25, 25).water_level == 999.0f);

        // Regenerate
        world.regenerate_terrain();

        REQUIRE(world.cell_at(25, 25).water_level == orig_water);
        REQUIRE(world.cell_at(25, 25).nutrient_level == orig_nutrient);
    }
}

TEST_CASE("World seasons", "[world]") {
    World world(10, 10, 42);
    const auto& cfg = get_config();

    SECTION("Light varies with ticks") {
        float initial_light = world.current_light_multiplier();

        // Advance to quarter season
        for (uint32_t i = 0; i < cfg.season_length / 4; ++i) {
            world.advance_tick();
        }
        float quarter_light = world.current_light_multiplier();

        // Light should have changed
        REQUIRE(initial_light != quarter_light);
    }

    SECTION("Light completes full cycle") {
        float initial_light = world.current_light_multiplier();

        // Advance full season
        for (uint32_t i = 0; i < cfg.season_length; ++i) {
            world.advance_tick();
        }
        float final_light = world.current_light_multiplier();

        // Should return to approximately the same value
        REQUIRE_THAT(final_light, WithinAbs(initial_light, 0.01f));
    }

    SECTION("update_season updates all cells") {
        world.advance_tick();
        float expected_light = world.current_light_multiplier();

        for (int32_t y = 0; y < 10; ++y) {
            for (int32_t x = 0; x < 10; ++x) {
                REQUIRE(world.cell_at(x, y).light_level == expected_light);
            }
        }
    }
}

TEST_CASE("World fire system", "[world]") {
    // Use custom config for fire tests
    auto& cfg = get_config();
    uint16_t orig_spread = cfg.fire_spread_ticks;
    uint16_t orig_destroy = cfg.fire_destroy_ticks;
    float orig_threshold = cfg.fire_water_threshold;

    cfg.fire_spread_ticks = 2;
    cfg.fire_destroy_ticks = 5;
    cfg.fire_water_threshold = 100.0f;

    World world(20, 20, 42);

    SECTION("ignite starts fire") {
        GridCoord pos{10, 10};
        world.cell_at(pos).water_level = 0.0f;  // Ensure it can ignite

        REQUIRE_FALSE(world.cell_at(pos).is_on_fire());
        world.ignite(pos);
        REQUIRE(world.cell_at(pos).is_on_fire());
        REQUIRE(world.cell_at(pos).fire_ticks == cfg.fire_destroy_ticks);
    }

    SECTION("wet cells don't ignite") {
        GridCoord pos{10, 10};
        world.cell_at(pos).water_level = 150.0f;  // Above threshold

        world.ignite(pos);
        REQUIRE_FALSE(world.cell_at(pos).is_on_fire());
    }

    SECTION("fire decrements each tick") {
        GridCoord pos{10, 10};
        world.cell_at(pos).water_level = 0.0f;
        world.ignite(pos);

        uint16_t initial_ticks = world.cell_at(pos).fire_ticks;
        world.update_fire();

        REQUIRE(world.cell_at(pos).fire_ticks == initial_ticks - 1);
    }

    SECTION("fire spreads to occupied neighbors after spread_ticks") {
        GridCoord center{10, 10};
        GridCoord right{11, 10};
        GridCoord left{9, 10};

        // Place fake occupants on neighbors so fire can spread there
        PlantCell dummy_right(CellType::SmallLeaf, right);
        PlantCell dummy_left(CellType::SmallLeaf, left);
        world.cell_at(right).occupant = &dummy_right;
        world.cell_at(left).occupant = &dummy_left;

        // Ensure center and neighbors can burn
        world.cell_at(center).water_level = 0.0f;
        world.cell_at(right).water_level = 0.0f;
        world.cell_at(left).water_level = 0.0f;

        world.ignite(center);

        // Before spread_ticks, neighbors shouldn't be on fire
        for (uint16_t i = 0; i < cfg.fire_spread_ticks - 1; ++i) {
            world.update_fire();
        }
        REQUIRE_FALSE(world.cell_at(right).is_on_fire());

        // After spread_ticks, occupied neighbors should ignite
        world.update_fire();
        REQUIRE(world.cell_at(right).is_on_fire());
        REQUIRE(world.cell_at(left).is_on_fire());
    }

    SECTION("fire does not spread to empty (unoccupied) neighbors") {
        GridCoord center{10, 10};
        GridCoord right{11, 10};

        // No occupant on neighbor — fire must not spread there
        world.cell_at(center).water_level = 0.0f;
        world.cell_at(right).water_level = 0.0f;

        world.ignite(center);

        for (uint16_t i = 0; i < cfg.fire_spread_ticks + 2; ++i) {
            world.update_fire();
        }
        REQUIRE_FALSE(world.cell_at(right).is_on_fire());
    }

    SECTION("fire does not spread to fireproof neighbors") {
        GridCoord center{10, 10};
        GridCoord right{11, 10};

        PlantCell fireproof(CellType::FireproofXylem, right);
        world.cell_at(right).occupant = &fireproof;

        world.cell_at(center).water_level = 0.0f;
        world.cell_at(right).water_level = 0.0f;

        world.ignite(center);

        for (uint16_t i = 0; i < cfg.fire_spread_ticks + 2; ++i) {
            world.update_fire();
        }
        REQUIRE_FALSE(world.cell_at(right).is_on_fire());
    }

    SECTION("fire burns out after destroy_ticks") {
        GridCoord pos{10, 10};
        world.cell_at(pos).water_level = 0.0f;

        // Set adjacent cells to high water to prevent re-ignition from spread
        world.cell_at(pos.x + 1, pos.y).water_level = 1000.0f;
        world.cell_at(pos.x - 1, pos.y).water_level = 1000.0f;
        world.cell_at(pos.x, pos.y + 1).water_level = 1000.0f;
        world.cell_at(pos.x, pos.y - 1).water_level = 1000.0f;

        world.ignite(pos);

        for (uint16_t i = 0; i < cfg.fire_destroy_ticks; ++i) {
            REQUIRE(world.cell_at(pos).is_on_fire());
            world.update_fire();
        }
        REQUIRE_FALSE(world.cell_at(pos).is_on_fire());
    }

    SECTION("out of bounds ignite is safe") {
        REQUIRE_NOTHROW(world.ignite({-1, -1}));
        REQUIRE_NOTHROW(world.ignite({100, 100}));
    }

    // Restore original config
    cfg.fire_spread_ticks = orig_spread;
    cfg.fire_destroy_ticks = orig_destroy;
    cfg.fire_water_threshold = orig_threshold;
}

TEST_CASE("World tick advancement", "[world]") {
    World world(10, 10, 42);

    SECTION("advance_tick increments tick counter") {
        REQUIRE(world.tick() == 0);
        world.advance_tick();
        REQUIRE(world.tick() == 1);
        world.advance_tick();
        world.advance_tick();
        REQUIRE(world.tick() == 3);
    }
}
