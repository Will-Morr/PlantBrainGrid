#include <catch2/catch_test_macros.hpp>
#include "core/brain.hpp"
#include "core/brain_ops.hpp"
#include "core/plant.hpp"
#include "core/world.hpp"
#include <random>

using namespace pbg;

// Program start offset (first 8 bytes are registers)
static constexpr int P = NUM_REGISTERS;

// Helper to create a plant for testing
static Plant make_test_plant(const std::vector<uint8_t>& genome) {
    Plant plant(1, {50, 50}, genome);
    plant.resources() = Resources{1000.0f, 1000.0f, 1000.0f};
    return plant;
}

// Shared test RNG — seeded deterministically
static std::mt19937_64 test_rng(12345);

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

    SECTION("IP starts at NUM_REGISTERS") {
        Brain brain(256);
        REQUIRE(brain.ip() == NUM_REGISTERS);
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
        std::vector<uint8_t> genome(P + 3, 0);
        genome[P]     = OP_NOP;
        genome[P + 1] = OP_NOP;
        genome[P + 2] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);
        REQUIRE(plant.brain().is_halted());
    }

    SECTION("HALT stops execution") {
        std::vector<uint8_t> genome(P + 3, 0);
        genome[P]     = OP_HALT;
        genome[P + 1] = OP_NOP;
        genome[P + 2] = OP_NOP;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);
        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == P + 1);
    }

    SECTION("JUMP goes to address") {
        std::vector<uint8_t> genome(30, OP_NOP);
        genome[P]     = OP_JUMP;
        genome[P + 1] = 20;  // Low byte of address
        genome[P + 2] = 0;   // High byte of address
        genome[20]    = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == 21);
    }

    SECTION("JUMP_REL with positive offset") {
        std::vector<uint8_t> genome(30, OP_NOP);
        genome[P]     = OP_JUMP_REL;
        genome[P + 1] = 5;  // Jump forward 5 from ip=(P+2) → target=P+7
        genome[P + 7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().is_halted());
    }

    SECTION("JUMP_IF_ZERO jumps when zero") {
        std::vector<uint8_t> genome(40, OP_NOP);
        // mem[30] = 0 (default), so should jump
        genome[P]     = OP_JUMP_IF_ZERO;
        genome[P + 1] = 30;  // Test address low
        genome[P + 2] = 0;   // Test address high
        genome[P + 3] = 25;  // Jump address low
        genome[P + 4] = 0;   // Jump address high
        genome[25]    = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == 26);
    }

    SECTION("JUMP_IF_ZERO does not jump when non-zero") {
        std::vector<uint8_t> genome(40, OP_NOP);
        genome[30]    = 1;  // Non-zero value
        genome[P]     = OP_JUMP_IF_ZERO;
        genome[P + 1] = 30;
        genome[P + 2] = 0;
        genome[P + 3] = 25;
        genome[P + 4] = 0;
        genome[P + 5] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == P + 6);
    }

    SECTION("CALL and RET") {
        std::vector<uint8_t> genome(30, OP_NOP);
        genome[P]     = OP_CALL;
        genome[P + 1] = 20;  // Call address
        genome[P + 2] = 0;
        genome[P + 3] = OP_HALT;  // Return here
        genome[20]    = OP_RET;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().is_halted());
        REQUIRE(plant.brain().ip() == P + 4);
    }
}

TEST_CASE("Brain arithmetic instructions", "[brain]") {
    World world(100, 100, 42);

    SECTION("LOAD_IMM stores value") {
        std::vector<uint8_t> genome(50, 0);
        genome[P]     = OP_LOAD_IMM;
        genome[P + 1] = 30;  // Address low
        genome[P + 2] = 0;   // Address high
        genome[P + 3] = 42;  // Value
        genome[P + 4] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(30) == 42);
    }

    SECTION("ADD") {
        std::vector<uint8_t> genome(50, 0);
        genome[35] = 10;  // First operand
        genome[37] = 5;   // Second operand

        genome[P]     = OP_ADD;
        genome[P + 1] = 40; genome[P + 2] = 0;   // Dest
        genome[P + 3] = 35; genome[P + 4] = 0;   // A
        genome[P + 5] = 37; genome[P + 6] = 0;   // B
        genome[P + 7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(40) == 15);
    }

    SECTION("SUB") {
        std::vector<uint8_t> genome(50, 0);
        genome[35] = 20;
        genome[37] = 8;

        genome[P]     = OP_SUB;
        genome[P + 1] = 40; genome[P + 2] = 0;
        genome[P + 3] = 35; genome[P + 4] = 0;
        genome[P + 5] = 37; genome[P + 6] = 0;
        genome[P + 7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(40) == 12);
    }

    SECTION("DIV by zero returns zero") {
        std::vector<uint8_t> genome(50, 0);
        genome[35] = 100;
        genome[37] = 0;

        genome[P]     = OP_DIV;
        genome[P + 1] = 40; genome[P + 2] = 0;
        genome[P + 3] = 35; genome[P + 4] = 0;
        genome[P + 5] = 37; genome[P + 6] = 0;
        genome[P + 7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(40) == 0);
    }

    SECTION("CMP_LT") {
        std::vector<uint8_t> genome(50, 0);
        genome[35] = 5;
        genome[37] = 10;

        genome[P]     = OP_CMP_LT;
        genome[P + 1] = 40; genome[P + 2] = 0;
        genome[P + 3] = 35; genome[P + 4] = 0;
        genome[P + 5] = 37; genome[P + 6] = 0;
        genome[P + 7] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(40) == 1);  // 5 < 10
    }
}

TEST_CASE("Brain sensing instructions", "[brain]") {
    World world(100, 100, 42);

    SECTION("SENSE_SELF_ENERGY") {
        std::vector<uint8_t> genome(50, 0);
        genome[P]     = OP_SENSE_SELF_ENERGY;
        genome[P + 1] = 30; genome[P + 2] = 0;
        genome[P + 3] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.resources().energy = 5.0f;

        plant.brain().execute_tick(plant, world, test_rng);

        uint8_t expected = static_cast<uint8_t>(std::min(255.0f, 5.0f * get_config().resource_sense_scale));
        REQUIRE(plant.brain().read(30) == expected);
    }

    SECTION("SENSE_CELL_COUNT") {
        std::vector<uint8_t> genome(50, 0);
        genome[P]     = OP_SENSE_CELL_COUNT;
        genome[P + 1] = 30; genome[P + 2] = 0;
        genome[P + 3] = OP_HALT;

        auto plant = make_test_plant(genome);

        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(30) == 1);
    }
}

TEST_CASE("Brain action queueing", "[brain]") {
    World world(100, 100, 42);

    SECTION("PLACE_CELL queues action") {
        std::vector<uint8_t> genome(50, 0);
        genome[P]     = OP_PLACE_CELL;
        genome[P + 1] = static_cast<uint8_t>(CellType::SmallLeaf);
        genome[P + 2] = 1;   // dx
        genome[P + 3] = 0;   // dy
        genome[P + 4] = OP_HALT;

        auto plant = make_test_plant(genome);
        auto actions = plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(actions.size() == 1);
        REQUIRE(actions[0].type == ActionType::PlaceCell);
        REQUIRE(actions[0].cell_type == CellType::SmallLeaf);
        REQUIRE(actions[0].position == GridCoord{51, 50});
    }

    SECTION("LAUNCH_SEED queues action with params") {
        std::vector<uint8_t> genome(50, 0);
        genome[P]     = OP_LAUNCH_SEED;
        genome[P + 1] = 0;   // recomb method
        genome[P + 2] = 100; // energy
        genome[P + 3] = 50;  // water
        genome[P + 4] = 25;  // nutrients
        genome[P + 5] = 10;  // power
        genome[P + 6] = 5;   // dx
        genome[P + 7] = static_cast<uint8_t>(-3);  // dy (signed)
        genome[P + 8] = 1;   // placement mode (random)
        genome[P + 9] = OP_HALT;

        auto plant = make_test_plant(genome);
        auto actions = plant.brain().execute_tick(plant, world, test_rng);

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
        genome[P]     = OP_JUMP;
        genome[P + 1] = P;   // Jump back to P forever
        genome[P + 2] = 0;

        auto plant = make_test_plant(genome);
        float initial_energy = plant.resources().energy;

        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.resources().energy < initial_energy);
        float penalty = initial_energy - plant.resources().energy;
        REQUIRE(penalty >= cfg.instruction_limit_penalty);
    }
}

TEST_CASE("Brain randomize instruction", "[brain]") {
    World world(100, 100, 42);

    SECTION("RANDOMIZE modifies memory range") {
        std::vector<uint8_t> genome(100, 0);
        genome[P]     = OP_RANDOMIZE;
        genome[P + 1] = 50;  // Start address low
        genome[P + 2] = 0;   // Start address high
        genome[P + 3] = 10;  // Length
        genome[P + 4] = OP_HALT;

        auto plant = make_test_plant(genome);

        for (int i = 50; i < 60; ++i) {
            REQUIRE(plant.brain().read(i) == 0);
        }

        plant.brain().execute_tick(plant, world, test_rng);

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

TEST_CASE("Brain register addressing", "[brain]") {
    World world(100, 100, 42);

    SECTION("MSB clear — normal address") {
        // LOAD_IMM to address 30 (MSB clear) should work normally
        std::vector<uint8_t> genome(50, 0);
        genome[P]     = OP_LOAD_IMM;
        genome[P + 1] = 30;  // addr low  (0x001E — MSB clear)
        genome[P + 2] = 0;   // addr high
        genome[P + 3] = 99;  // value
        genome[P + 4] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(30) == 99);
    }

    SECTION("MSB set, second bit clear — register via modulo") {
        // Address with MSB set and bit 14 clear → register = addr % 8
        // 0x8003 = 1000 0000 0000 0011 → MSB=1, bit14=0, addr%8 = 3 → register 3
        std::vector<uint8_t> genome(50, 0);
        genome[3] = 0;  // Register 3 starts at 0
        genome[P]     = OP_LOAD_IMM;
        genome[P + 1] = 0x03;  // addr low byte
        genome[P + 2] = 0x80;  // addr high byte (MSB set, bit14 clear)
        genome[P + 3] = 77;    // value to store
        genome[P + 4] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        // Should have written to register 3 (memory address 3)
        REQUIRE(plant.brain().read(3) == 77);
    }

    SECTION("MSB set, second bit set — last referenced register") {
        // First: write to register 5 via modulo (0x8005 → reg 5)
        // Then:  write via last-register mode (0xC000 → bit14 set → last reg = 5)
        std::vector<uint8_t> genome(50, 0);

        // LOAD_IMM to register 5 via modulo addressing
        genome[P]     = OP_LOAD_IMM;
        genome[P + 1] = 0x05;  // addr low
        genome[P + 2] = 0x80;  // addr high (MSB=1, bit14=0) → reg 5
        genome[P + 3] = 42;    // value

        // LOAD_IMM via last-referenced register (should hit reg 5 again)
        genome[P + 4] = OP_LOAD_IMM;
        genome[P + 5] = 0x00;  // addr low  (doesn't matter for last-reg mode)
        genome[P + 6] = 0xC0;  // addr high (MSB=1, bit14=1) → last register
        genome[P + 7] = 99;    // overwrite with 99

        genome[P + 8] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        // Register 5 should now contain 99 (overwritten by second LOAD_IMM)
        REQUIRE(plant.brain().read(5) == 99);
    }

    SECTION("Register read via safe_read") {
        // Store a value in register 2, then use ADD to read it
        std::vector<uint8_t> genome(50, 0);
        genome[2] = 10;   // Register 2 = 10
        genome[40] = 5;   // Normal memory at 40 = 5

        // ADD dest=45 a=reg2(0x8002) b=addr40
        genome[P]      = OP_ADD;
        genome[P + 1]  = 45; genome[P + 2] = 0;     // dest (normal)
        genome[P + 3]  = 0x02; genome[P + 4] = 0x80; // a = register 2
        genome[P + 5]  = 40; genome[P + 6] = 0;      // b = normal addr 40
        genome[P + 7]  = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(45) == 15);  // 10 + 5
    }

    SECTION("Last register defaults to 0") {
        // Without any prior register access, last_register_ = 0
        // Using last-register mode should access register 0
        std::vector<uint8_t> genome(50, 0);
        genome[0] = 123;  // Register 0 = 123

        // LOAD_IMM: read dest via last-register mode
        // Actually LOAD_IMM writes — let's use COPY to read reg 0 → addr 40
        genome[P]     = OP_COPY;
        genome[P + 1] = 40; genome[P + 2] = 0;     // dest = normal addr 40
        genome[P + 3] = 0x00; genome[P + 4] = 0xC0; // src = last register (0)
        genome[P + 5] = OP_HALT;

        auto plant = make_test_plant(genome);
        plant.brain().execute_tick(plant, world, test_rng);

        REQUIRE(plant.brain().read(40) == 123);
    }
}
