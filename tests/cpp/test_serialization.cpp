#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/simulation.hpp"
#include "core/config.hpp"
#include <fstream>
#include <cstdio>

using namespace pbg;

namespace {

// Helper: create a genome with some content
std::vector<uint8_t> test_genome() {
    std::vector<uint8_t> genome(256);
    for (size_t i = 0; i < genome.size(); ++i) {
        genome[i] = static_cast<uint8_t>(i & 0xFF);
    }
    return genome;
}

// Helper: run sim and save, return filename
std::string save_sim(const std::string& path) {
    Simulation sim(100, 100, 42);
    auto genome = test_genome();

    Plant* plant = sim.add_plant({50, 50}, genome);
    plant->resources().energy = 200.0f;
    plant->resources().water = 100.0f;
    plant->resources().nutrients = 50.0f;

    sim.run(10);
    sim.save_state(path);
    return path;
}

}  // namespace

TEST_CASE("Serialization round-trip", "[serialization]") {
    const std::string path = "/tmp/pbg_test_save.bin";

    SECTION("Save and load produces matching tick count") {
        Simulation sim(100, 100, 42);
        auto genome = test_genome();
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 200.0f;

        sim.run(20);
        uint64_t saved_tick = sim.tick();
        size_t saved_plant_count = sim.plants().size();

        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        REQUIRE(sim2.tick() == saved_tick);
        REQUIRE(sim2.plants().size() == saved_plant_count);

        std::remove(path.c_str());
    }

    SECTION("Loaded simulation can continue running") {
        Simulation sim(100, 100, 42);
        auto genome = test_genome();
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 500.0f;

        sim.run(5);
        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        uint64_t loaded_tick = sim2.tick();
        sim2.run(5);

        REQUIRE(sim2.tick() == loaded_tick + 5);

        std::remove(path.c_str());
    }

    SECTION("Plant resources preserved through save/load") {
        Simulation sim(100, 100, 42);
        auto genome = test_genome();
        Plant* plant = sim.add_plant({50, 50}, genome);
        plant->resources().energy = 123.4f;
        plant->resources().water = 56.7f;
        plant->resources().nutrients = 89.0f;

        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        REQUIRE(!sim2.plants().empty());
        const Plant& loaded = sim2.plants()[0];
        REQUIRE(loaded.resources().energy == Catch::Approx(123.4f).epsilon(0.01));
        REQUIRE(loaded.resources().water == Catch::Approx(56.7f).epsilon(0.01));
        REQUIRE(loaded.resources().nutrients == Catch::Approx(89.0f).epsilon(0.01));

        std::remove(path.c_str());
    }

    SECTION("Plant genome preserved through save/load") {
        Simulation sim(100, 100, 42);
        auto genome = test_genome();
        sim.add_plant({50, 50}, genome);

        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        REQUIRE(!sim2.plants().empty());
        const Brain& brain = sim2.plants()[0].brain();

        // Check a few genome bytes
        REQUIRE(brain.read(0) == genome[0]);
        REQUIRE(brain.read(100) == genome[100]);
        REQUIRE(brain.read(200) == genome[200]);

        std::remove(path.c_str());
    }

    SECTION("Plant position preserved through save/load") {
        Simulation sim(100, 100, 42);
        auto genome = test_genome();
        sim.add_plant({37, 62}, genome);

        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        REQUIRE(!sim2.plants().empty());
        REQUIRE(sim2.plants()[0].primary_position() == GridCoord{37, 62});

        std::remove(path.c_str());
    }

    SECTION("Multiple plants preserved through save/load") {
        Simulation sim(100, 100, 42);
        auto genome = test_genome();

        sim.add_plant({20, 20}, genome);
        sim.add_plant({50, 50}, genome);
        sim.add_plant({80, 80}, genome);

        size_t plant_count = sim.plants().size();
        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        REQUIRE(sim2.plants().size() == plant_count);

        std::remove(path.c_str());
    }
}

TEST_CASE("Serialization file format", "[serialization]") {
    const std::string path = "/tmp/pbg_test_format.bin";

    SECTION("Invalid file is handled gracefully") {
        // Write garbage to file
        {
            std::ofstream f(path, std::ios::binary);
            const char garbage[] = "not a valid save file";
            f.write(garbage, sizeof(garbage));
        }

        Simulation sim(100, 100, 42);
        // Should not crash or throw
        REQUIRE_NOTHROW(sim.load_state(path));

        std::remove(path.c_str());
    }

    SECTION("Missing file is handled gracefully") {
        Simulation sim(100, 100, 42);
        // Should not crash or throw
        REQUIRE_NOTHROW(sim.load_state("/tmp/pbg_nonexistent_file.bin"));
    }

    SECTION("Empty state saves and loads") {
        Simulation sim(100, 100, 42);
        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        REQUIRE(sim2.tick() == 0);
        REQUIRE(sim2.plants().empty());

        std::remove(path.c_str());
    }
}

TEST_CASE("Serialization next_plant_id", "[serialization]") {
    const std::string path = "/tmp/pbg_test_id.bin";

    SECTION("Plant IDs continue from saved state after load") {
        Simulation sim(100, 100, 42);
        auto genome = test_genome();

        Plant* p1 = sim.add_plant({20, 20}, genome);
        REQUIRE(p1->id() == 1);
        Plant* p2 = sim.add_plant({40, 40}, genome);
        REQUIRE(p2->id() == 2);

        sim.save_state(path);

        Simulation sim2(100, 100, 42);
        sim2.load_state(path);

        // After loading, new plants should not reuse IDs
        Plant* p3 = sim2.add_plant({60, 60}, genome);
        REQUIRE(p3 != nullptr);
        REQUIRE(p3->id() > 2);

        std::remove(path.c_str());
    }
}
