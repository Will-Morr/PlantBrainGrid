#include <catch2/catch_test_macros.hpp>
#include "core/brain.hpp"
#include "core/brain_ops.hpp"
#include "core/plant.hpp"
#include "core/world.hpp"

using namespace pbg;

// Helper to create a plant for testing
static Plant make_test_plant(const std::vector<uint8_t>& genome) {
    Plant plant(1, {50, 50}, genome);
    plant.resources() = Resources{1000.0f, 1000.0f, 1000.0f};
    return plant;
}

TEST_CASE("Brain construction", "[brain]") {
    SECTION("Constructs from genome") {
        std::vector<uint8_t> genome = {1, 2, 3, 4, 5};
        Brain brain(genome);

        REQUIRE(brain.read(0) == 1);
        REQUIRE(brain.read(1) == 2);
        REQUIRE(brain.read(4) == 5);
    }

    SECTION("Pads genome to brain_size") {
        std::vector<uint8_t> genome = {1, 2, 3};
        Brain brain(genome);

        REQUIRE(brain.size() >= get_config().brain_size);
    }

    SECTION("Default construction creates zeroed memory") {
        Brain brain(256);
        for (size_t i = 0; i < 256; ++i) {
            REQUIRE(brain.read(i) == 0);
        }
    }
}

TEST_CASE("Brain memory operations", "[brain]") {
    Brain brain(256);

    SECTION("Write and read") {
        brain.write(100, 42);
        REQUIRE(brain.read(100) == 42);
    }

    SECTION("OOB read returns 0") {
        REQUIRE(brain.read(10000) == 0);
    }

    SECTION("OOB write is ignored") {
        brain.write(10000, 99);
        // No crash, no effect
        REQUIRE(brain.read(10000) == 0);
    }
}

TEST_CASE("Brain control flow instructions", "[brain]") {
    World world(100, 100, 42);

    SECTION("NOP advances IP") {
        std::vector<uint8_t> genome = {OP_NOP, OP_NOP, OP_HALT};
        auto plant = make_test_plant(genome);

        plant.brain().execute_tick(plant, world);
        // Should have executed all 3 instructions
        REQUIRE(plant.brain().is_halted());
    }

    SECTION("HALT stops execution") {
        std::vector<uint8_t> genome = {OP_HALT, OP_NOP, OP_NOP};
        auto plant = make_test_plant(genome);

        auto actions = plant.brain().execute_tick(plant, world);
        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == 1);  // Stopped after HALT
    }

    SECTION("JUMP goes to address") {
        std::vector<uint8_t> genome(20, OP_NOP);
        genome[0] = OP_JUMP;
        genome[1] = 10;  // Low byte of address
        genome[2] = 0;   // High byte of address
        genome[10] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == 11);
    }

    SECTION("JUMP_REL with positive offset") {
        std::vector<uint8_t> genome(20, OP_NOP);
        genome[0] = OP_JUMP_REL;
        genome[1] = 5;  // Jump forward 5
        genome[7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().is_halted());
    }

    SECTION("JUMP_IF_ZERO jumps when zero") {
        std::vector<uint8_t> genome(30, OP_NOP);
        // mem[20] = 0 (default), so should jump
        genome[0] = OP_JUMP_IF_ZERO;
        genome[1] = 20;  // Test address low
        genome[2] = 0;   // Test address high
        genome[3] = 15;  // Jump address low
        genome[4] = 0;   // Jump address high
        genome[15] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == 16);
    }

    SECTION("JUMP_IF_ZERO does not jump when non-zero") {
        std::vector<uint8_t> genome(30, OP_NOP);
        genome[20] = 1;  // Non-zero value
        genome[0] = OP_JUMP_IF_ZERO;
        genome[1] = 20;
        genome[2] = 0;
        genome[3] = 15;
        genome[4] = 0;
        genome[5] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == 6);  // Continued past jump
    }

    SECTION("CALL and RET") {
        std::vector<uint8_t> genome(30, OP_NOP);
        genome[0] = OP_CALL;
        genome[1] = 10;  // Call address
        genome[2] = 0;
        genome[3] = OP_HALT;  // Return here
        genome[10] = OP_RET;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == 4);  // After HALT following return
    }
}

TEST_CASE("Brain arithmetic instructions", "[brain]") {
    World world(100, 100, 42);

    SECTION("LOAD_IMM stores value") {
        std::vector<uint8_t> genome(50, 0);
        genome[0] = OP_LOAD_IMM;
        genome[1] = 20;  // Address low
        genome[2] = 0;   // Address high
        genome[3] = 42;  // Value
        genome[4] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().read(20) == 42);
    }

    SECTION("ADD") {
        std::vector<uint8_t> genome(50, 0);
        genome[30] = 10;  // First operand
        genome[32] = 5;   // Second operand

        genome[0] = OP_ADD;
        genome[1] = 40;  // Dest low
        genome[2] = 0;
        genome[3] = 30;  // A low
        genome[4] = 0;
        genome[5] = 32;  // B low
        genome[6] = 0;
        genome[7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().read(40) == 15);
    }

    SECTION("SUB") {
        std::vector<uint8_t> genome(50, 0);
        genome[30] = 20;
        genome[32] = 8;

        genome[0] = OP_SUB;
        genome[1] = 40; genome[2] = 0;  // dest
        genome[3] = 30; genome[4] = 0;  // a
        genome[5] = 32; genome[6] = 0;  // b
        genome[7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().read(40) == 12);
    }

    SECTION("DIV by zero returns zero") {
        std::vector<uint8_t> genome(50, 0);
        genome[30] = 100;
        genome[32] = 0;  // Divisor is zero

        genome[0] = OP_DIV;
        genome[1] = 40; genome[2] = 0;
        genome[3] = 30; genome[4] = 0;
        genome[5] = 32; genome[6] = 0;
        genome[7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().read(40) == 0);
    }

    SECTION("CMP_LT") {
        std::vector<uint8_t> genome(50, 0);
        genome[30] = 5;
        genome[32] = 10;

        genome[0] = OP_CMP_LT;
        genome[1] = 40; genome[2] = 0;
        genome[3] = 30; genome[4] = 0;  // 5
        genome[5] = 32; genome[6] = 0;  // 10
        genome[7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().read(40) == 1);  // 5 < 10
    }
}

TEST_CASE("Brain sensing instructions", "[brain]") {
    World world(100, 100, 42);

    SECTION("SENSE_SELF_ENERGY") {
        std::vector<uint8_t> genome(50, 0);
        genome[0] = OP_SENSE_SELF_ENERGY;
        genome[1] = 20; genome[2] = 0;  // Dest
        genome[3] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.resources().energy = 50.0f;

        plant.brain().execute_tick(plant, world);

        // 50 * 2.55 = 127.5 -> 127
        uint8_t expected = static_cast<uint8_t>(50.0f * get_config().resource_sense_scale);
        REQUIRE(plant.brain().read(20) == expected);
    }

    SECTION("SENSE_CELL_COUNT") {
        std::vector<uint8_t> genome(50, 0);
        genome[0] = OP_SENSE_CELL_COUNT;
        genome[1] = 20; genome[2] = 0;
        genome[3] = OP_HALT;

        auto plant = make_test_plant(genome);
        // Plant starts with 1 cell (primary)

        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.brain().read(20) == 1);
    }
}

TEST_CASE("Brain action queueing", "[brain]") {
    World world(100, 100, 42);

    SECTION("PLACE_CELL queues action") {
        std::vector<uint8_t> genome(50, 0);
        genome[0] = OP_PLACE_CELL;
        genome[1] = static_cast<uint8_t>(CellType::SmallLeaf);
        genome[2] = 1;   // dx
        genome[3] = 0;   // dy
        genome[4] = 0;   // direction (North)
        genome[5] = OP_HALT;

        auto plant = make_test_plant(genome);
        auto actions = plant.brain().execute_tick(plant, world);

        REQUIRE(actions.size() == 1);
        REQUIRE(actions[0].type == ActionType::PlaceCell);
        REQUIRE(actions[0].cell_type == CellType::SmallLeaf);
        REQUIRE(actions[0].position == GridCoord{51, 50});
        REQUIRE(actions[0].direction == Direction::North);
    }

    SECTION("LAUNCH_SEED queues action with params") {
        std::vector<uint8_t> genome(50, 0);
        genome[0] = OP_LAUNCH_SEED;
        genome[1] = 0;   // recomb method
        genome[2] = 100; // energy
        genome[3] = 50;  // water
        genome[4] = 25;  // nutrients
        genome[5] = 10;  // power
        genome[6] = 5;   // dx
        genome[7] = static_cast<uint8_t>(-3);  // dy (signed)
        genome[8] = 1;   // placement mode (random)
        genome[9] = OP_HALT;

        auto plant = make_test_plant(genome);
        auto actions = plant.brain().execute_tick(plant, world);

        REQUIRE(actions.size() == 1);
        REQUIRE(actions[0].type == ActionType::LaunchSeed);
        REQUIRE(actions[0].seed_params.has_value());
        REQUIRE(actions[0].seed_params->energy == 100);
        REQUIRE(actions[0].seed_params->water == 50);
        REQUIRE(actions[0].seed_params->nutrients == 25);
        REQUIRE(actions[0].seed_params->launch_power == 10);
        REQUIRE(actions[0].seed_params->dx == 5);
        REQUIRE(actions[0].seed_params->dy == -3);
        REQUIRE(actions[0].seed_params->placement_mode == SeedPlacementMode::Random);
    }
}

TEST_CASE("Brain error penalties", "[brain]") {
    World world(100, 100, 42);
    const auto& cfg = get_config();

    SECTION("Instruction limit penalty") {
        // Create infinite loop
        std::vector<uint8_t> genome(50, 0);
        genome[0] = OP_JUMP;
        genome[1] = 0;
        genome[2] = 0;  // Jump to 0 forever

        auto plant = make_test_plant(genome);
        float initial_energy = plant.resources().energy;

        plant.brain().execute_tick(plant, world);

        REQUIRE(plant.resources().energy < initial_energy);
        float penalty = initial_energy - plant.resources().energy;
        REQUIRE(penalty >= cfg.instruction_limit_penalty);
    }
}

TEST_CASE("Brain randomize instruction", "[brain]") {
    World world(100, 100, 42);

    SECTION("RANDOMIZE modifies memory range") {
        std::vector<uint8_t> genome(100, 0);
        genome[0] = OP_RANDOMIZE;
        genome[1] = 50;  // Start address low
        genome[2] = 0;   // Start address high
        genome[3] = 10;  // Length
        genome[4] = OP_HALT;

        auto plant = make_test_plant(genome);

        // Verify initially zero
        for (int i = 50; i < 60; ++i) {
            REQUIRE(plant.brain().read(i) == 0);
        }

        plant.brain().execute_tick(plant, world);

        // At least some values should have changed
        int changed = 0;
        for (int i = 50; i < 60; ++i) {
            if (plant.brain().read(i) != 0) {
                ++changed;
            }
        }
        REQUIRE(changed > 0);
    }
}

TEST_CASE("Brain stack operations", "[brain]") {
    Brain brain(256);

    SECTION("Push and pop") {
        brain.push_stack(100);
        brain.push_stack(200);

        REQUIRE(brain.pop_stack() == 200);
        REQUIRE(brain.pop_stack() == 100);
    }

    SECTION("Pop empty stack returns 0") {
        REQUIRE(brain.stack_empty());
        REQUIRE(brain.pop_stack() == 0);
    }
}
