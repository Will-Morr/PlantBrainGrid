#include <catch2/catch_test_macros.hpp>
#include "core/reproduction.hpp"
#include "core/brain_ops.hpp"
#include "core/world.hpp"
#include "core/config.hpp"
#include <unordered_set>

using namespace pbg;

static Plant make_test_plant(uint64_t id, const GridCoord& pos) {
    std::vector<uint8_t> genome(256);
    for (size_t i = 0; i < genome.size(); ++i) {
        genome[i] = static_cast<uint8_t>(i * id);  // Different genome per plant
    }
    Plant plant(id, pos, genome);
    plant.resources() = Resources{1000.0f, 1000.0f, 1000.0f};
    return plant;
}

TEST_CASE("Genome recombination", "[reproduction]") {
    std::mt19937_64 rng(42);

    std::vector<uint8_t> mother_genome(100, 0xAA);
    std::vector<uint8_t> father_genome(100, 0x55);

    SECTION("MotherOnly returns mother's genome") {
        auto offspring = ReproductionSystem::recombine_genomes(
            mother_genome, father_genome, RecombinationMethod::MotherOnly, rng);

        for (size_t i = 0; i < 100; ++i) {
            REQUIRE(offspring[i] == 0xAA);
        }
    }

    SECTION("FatherOnly returns father's genome") {
        auto offspring = ReproductionSystem::recombine_genomes(
            mother_genome, father_genome, RecombinationMethod::FatherOnly, rng);

        for (size_t i = 0; i < 100; ++i) {
            REQUIRE(offspring[i] == 0x55);
        }
    }

    SECTION("HalfHalf splits genome") {
        auto offspring = ReproductionSystem::recombine_genomes(
            mother_genome, father_genome, RecombinationMethod::HalfHalf, rng);

        // First half should be mother
        for (size_t i = 0; i < 50; ++i) {
            REQUIRE(offspring[i] == 0xAA);
        }
        // Second half should be father
        for (size_t i = 50; i < 100; ++i) {
            REQUIRE(offspring[i] == 0x55);
        }
    }

    SECTION("Alternating alternates bytes") {
        auto offspring = ReproductionSystem::recombine_genomes(
            mother_genome, father_genome, RecombinationMethod::Alternating, rng);

        for (size_t i = 0; i < 100; ++i) {
            if (i % 2 == 0) {
                REQUIRE(offspring[i] == 0xAA);
            } else {
                REQUIRE(offspring[i] == 0x55);
            }
        }
    }

    SECTION("RandomMix produces mixed result") {
        auto offspring = ReproductionSystem::recombine_genomes(
            mother_genome, father_genome, RecombinationMethod::RandomMix, rng);

        int mother_count = 0, father_count = 0;
        for (auto byte : offspring) {
            if (byte == 0xAA) ++mother_count;
            if (byte == 0x55) ++father_count;
        }

        // Should have a mix (probabilistic, but very unlikely to be all one)
        REQUIRE(mother_count > 0);
        REQUIRE(father_count > 0);
    }

    SECTION("Mother75 favors mother") {
        // Run multiple times to get statistical significance
        int mother_total = 0, father_total = 0;

        for (int trial = 0; trial < 10; ++trial) {
            auto offspring = ReproductionSystem::recombine_genomes(
                mother_genome, father_genome, RecombinationMethod::Mother75, rng);

            for (auto byte : offspring) {
                if (byte == 0xAA) ++mother_total;
                if (byte == 0x55) ++father_total;
            }
        }

        // Mother should have significantly more
        REQUIRE(mother_total > father_total * 2);
    }
}

TEST_CASE("Mutation application", "[reproduction]") {
    std::mt19937_64 rng(42);

    SECTION("Zero mutation rate still applies one block mutation") {
        // Per-byte mutations are suppressed, but a block mutation always fires.
        std::vector<uint8_t> genome(100, 42);
        std::vector<uint8_t> original = genome;

        ReproductionSystem::apply_mutations(genome, 0.0f, 16, rng);

        // The block mutation must have changed at least one byte
        REQUIRE(genome != original);
        // But not all bytes should have changed (block << whole genome)
        int unchanged = 0;
        for (size_t i = 0; i < genome.size(); ++i) {
            if (genome[i] == original[i]) ++unchanged;
        }
        REQUIRE(unchanged > 0);
    }

    SECTION("High mutation rate changes genome") {
        std::vector<uint8_t> genome(100, 42);
        std::vector<uint8_t> original = genome;

        ReproductionSystem::apply_mutations(genome, 1.0f, 16, rng);

        // Most bytes should have changed
        int changes = 0;
        for (size_t i = 0; i < 100; ++i) {
            if (genome[i] != original[i]) ++changes;
        }
        REQUIRE(changes > 50);
    }

    SECTION("Mutations stay within bounds") {
        std::vector<uint8_t> genome(100, 128);

        ReproductionSystem::apply_mutations(genome, 1.0f, 200, rng);

        for (auto byte : genome) {
            REQUIRE(byte >= 0);
            REQUIRE(byte <= 255);
        }
    }
}

TEST_CASE("Seed creation", "[reproduction]") {
    World world(100, 100, 42);
    auto mother = make_test_plant(1, {50, 50});

    SECTION("Seed creation deducts resources") {
        QueuedAction::SeedParams params;
        params.recomb_method = RecombinationMethod::MotherOnly;
        params.energy = 100;  // Scaled value
        params.water = 50;
        params.nutrients = 25;
        params.launch_power = 10;
        params.dx = 1;
        params.dy = 0;
        params.placement_mode = SeedPlacementMode::Exact;

        float initial_energy = mother.resources().energy;

        auto seed = ReproductionSystem::create_seed(mother, nullptr, params, world.rng());

        REQUIRE(seed.has_value());
        REQUIRE(mother.resources().energy < initial_energy);
    }

    SECTION("Seed creation fails with insufficient resources") {
        mother.resources() = Resources{0.0f, 0.0f, 0.0f};

        QueuedAction::SeedParams params;
        params.energy = 100;
        params.water = 50;
        params.nutrients = 25;
        params.launch_power = 10;

        auto seed = ReproductionSystem::create_seed(mother, nullptr, params, world.rng());

        REQUIRE_FALSE(seed.has_value());
    }

    SECTION("Seed inherits mother's genome for asexual reproduction") {
        QueuedAction::SeedParams params;
        params.recomb_method = RecombinationMethod::MotherOnly;
        params.energy = 10;
        params.water = 10;
        params.nutrients = 10;
        params.launch_power = 5;

        auto seed = ReproductionSystem::create_seed(mother, nullptr, params, world.rng());

        REQUIRE(seed.has_value());
        // Genome should be similar to mother (with mutations)
        REQUIRE(seed->genome.size() == mother.brain().memory().size());
    }
}

TEST_CASE("Seed germination", "[reproduction]") {
    World world(100, 100, 42);

    SECTION("Seed germinates on empty tile") {
        Seed seed;
        seed.genome = std::vector<uint8_t>(100, 0);
        seed.energy = 50.0f;
        seed.water = 30.0f;
        seed.nutrients = 20.0f;
        seed.position = {50, 50};
        seed.in_flight = false;

        auto plant = ReproductionSystem::try_germinate(seed, 1, world);

        REQUIRE(plant.has_value());
        REQUIRE(plant->primary_position() == seed.position);
        REQUIRE(plant->resources().energy == seed.energy);
    }

    SECTION("Seed fails to germinate on occupied tile") {
        // Create occupying plant
        std::vector<uint8_t> genome(100, 0);
        Plant blocker(1, {50, 50}, genome);
        world.cell_at(50, 50).plant_id = blocker.id();
        world.cell_at(50, 50).cell_type = CellType::Primary;

        Seed seed;
        seed.genome = std::vector<uint8_t>(100, 0);
        seed.position = {50, 50};
        seed.in_flight = false;

        auto plant = ReproductionSystem::try_germinate(seed, 2, world);

        REQUIRE_FALSE(plant.has_value());
    }

    SECTION("Seed fails to germinate out of bounds") {
        Seed seed;
        seed.genome = std::vector<uint8_t>(100, 0);
        seed.position = {-10, -10};
        seed.in_flight = false;

        auto plant = ReproductionSystem::try_germinate(seed, 1, world);

        REQUIRE_FALSE(plant.has_value());
    }

    SECTION("Seed fails to germinate on fire") {
        // Set up an occupied cell so it can be ignited
        world.cell_at(50, 50).plant_id = 1;
        world.cell_at(50, 50).cell_type = CellType::SmallLeaf;
        world.cell_at(50, 50).water_level = 0.0f;
        world.ignite({50, 50});

        // Remove the occupant so the seed could theoretically germinate
        // (simulating the plant having been burned away but fire still active)
        world.cell_at(50, 50).plant_id = 0;

        Seed seed;
        seed.genome = std::vector<uint8_t>(100, 0);
        seed.position = {50, 50};
        seed.in_flight = false;

        auto plant = ReproductionSystem::try_germinate(seed, 1, world);

        REQUIRE_FALSE(plant.has_value());
    }
}

TEST_CASE("Mate selection", "[reproduction]") {
    SECTION("Selects mate within distance") {
        auto mother = make_test_plant(1, {50, 50});
        auto candidate1 = make_test_plant(2, {55, 50});  // Distance 5
        auto candidate2 = make_test_plant(3, {100, 100}); // Distance ~70

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(candidate1));
        all_plants.push_back(std::move(candidate2));

        MateSearchState search;
        search.max_distance = 20.0f;
        search.weights.push_back({MATE_CRITERION_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search);

        // Should select candidate1 (within range), not candidate2
        REQUIRE(selected == 2);
    }

    SECTION("Returns 0 with no valid mates") {
        auto mother = make_test_plant(1, {50, 50});

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.weights.push_back({MATE_CRITERION_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search);

        REQUIRE(selected == 0);
    }

    SECTION("Distance criterion favors closer mates") {
        auto mother = make_test_plant(1, {50, 50});
        auto close = make_test_plant(2, {52, 50});   // Distance 2
        auto far = make_test_plant(3, {60, 50});     // Distance 10

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(close));
        all_plants.push_back(std::move(far));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.weights.push_back({MATE_CRITERION_DISTANCE, 10});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search);

        REQUIRE(selected == 2);  // Should select closer mate
    }
}

TEST_CASE("Landing position calculation", "[reproduction]") {
    std::mt19937_64 rng(42);
    GridCoord launch{50, 50};

    SECTION("Exact mode respects direction") {
        QueuedAction::SeedParams params;
        params.launch_power = 10;
        params.dx = 1;
        params.dy = 0;
        params.placement_mode = SeedPlacementMode::Exact;

        GridCoord landing = ReproductionSystem::calculate_landing_position(
            launch, params, rng);

        // Should land to the right (positive x)
        REQUIRE(landing.x > launch.x);
        REQUIRE(landing.y == launch.y);
    }

    SECTION("Random mode produces varied positions") {
        QueuedAction::SeedParams params;
        params.launch_power = 20;
        params.placement_mode = SeedPlacementMode::Random;

        std::unordered_set<GridCoord> positions;
        for (int i = 0; i < 20; ++i) {
            GridCoord landing = ReproductionSystem::calculate_landing_position(
                launch, params, rng);
            positions.insert(landing);
        }

        // Should have variety
        REQUIRE(positions.size() > 5);
    }
}
