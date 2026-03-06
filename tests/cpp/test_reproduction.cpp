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

        ReproductionSystem::apply_mutations(genome, 0.0f, rng);

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

        ReproductionSystem::apply_mutations(genome, 1.0f, rng);

        // Most bytes should have changed
        int changes = 0;
        for (size_t i = 0; i < 100; ++i) {
            if (genome[i] != original[i]) ++changes;
        }
        REQUIRE(changes > 50);
    }

    SECTION("Mutations stay within bounds") {
        std::vector<uint8_t> genome(100, 128);

        ReproductionSystem::apply_mutations(genome, 1.0f, rng);

        for (auto byte : genome) {
            REQUIRE(byte >= 0);
            REQUIRE(byte <= 255);
        }
    }
}

TEST_CASE("Seed creation", "[reproduction]") {
    World world(100, 100, 42);
    auto mother = make_test_plant(1, {50, 50});
    auto father = make_test_plant(2, {60, 60});

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

        auto seed = ReproductionSystem::create_seed(mother, father, params, world.rng());

        REQUIRE(seed.has_value());
        REQUIRE(mother.resources().energy < initial_energy);
    }

    SECTION("Seed creation with zero resources produces zero-cost seed") {
        mother.resources() = Resources{0.0f, 0.0f, 0.0f};

        QueuedAction::SeedParams params;
        params.energy = 100;
        params.water = 50;
        params.nutrients = 25;
        params.launch_power = 10;
        params.recomb_method = RecombinationMethod::MotherOnly;

        auto seed = ReproductionSystem::create_seed(mother, father, params, world.rng());

        // create_seed always produces a seed; costs are clamped to available resources
        REQUIRE(seed.has_value());
        REQUIRE(seed->energy == 0.0f);
        REQUIRE(seed->water == 0.0f);
        REQUIRE(seed->nutrients == 0.0f);
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

// Helper: create a plant and give it an Anther cell via a world.
static Plant make_plant_with_anther(uint64_t id, const GridCoord& pos, World& world) {
    auto plant = make_test_plant(id, pos);
    world.cell_at(pos).plant_id = id;
    world.cell_at(pos).cell_type = CellType::Primary;
    GridCoord anther_pos{pos.x + 1, pos.y};
    plant.place_cell(CellType::Anther, anther_pos, world);
    return plant;
}

TEST_CASE("Mate selection", "[reproduction]") {
    std::mt19937_64 rng(42);

    SECTION("Selects mate within distance") {
        World world(200, 200, 42);
        auto mother     = make_test_plant(1, {50, 50});
        auto candidate1 = make_plant_with_anther(2, {55, 50}, world);  // Distance 5
        auto candidate2 = make_plant_with_anther(3, {100, 100}, world); // Distance ~70

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(candidate1));
        all_plants.push_back(std::move(candidate2));

        MateSearchState search;
        search.max_distance = 20.0f;
        search.criteria.push_back({MATE_CRIT_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search, rng);

        // Should select candidate1 (within range), not candidate2 (out of range)
        REQUIRE(selected == 2);
    }

    SECTION("Returns 0 with no valid mates") {
        auto mother = make_test_plant(1, {50, 50});

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.criteria.push_back({MATE_CRIT_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search, rng);

        REQUIRE(selected == 0);
    }

    SECTION("Distance criterion favors closer mates") {
        World world(200, 200, 42);
        auto mother = make_test_plant(1, {50, 50});
        auto close  = make_plant_with_anther(2, {52, 50}, world);  // Distance 2
        auto far    = make_plant_with_anther(3, {60, 50}, world);  // Distance 10

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(close));
        all_plants.push_back(std::move(far));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.criteria.push_back({MATE_CRIT_DISTANCE, 10});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search, rng);

        REQUIRE(selected == 2);  // Should select closer mate
    }

    SECTION("Default distance bias favors closer mates without explicit criterion") {
        auto& cfg = get_config();
        float orig_bias = cfg.mate_distance_bias;
        float orig_noise = cfg.mate_selection_noise;
        cfg.mate_distance_bias = 1.0f;
        cfg.mate_selection_noise = 0.0f;  // Disable noise for deterministic test

        World world(200, 200, 42);
        auto mother = make_test_plant(1, {50, 50});
        auto close  = make_plant_with_anther(2, {52, 50}, world);  // Distance 2
        auto far    = make_plant_with_anther(3, {70, 50}, world);  // Distance 20

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(close));
        all_plants.push_back(std::move(far));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.criteria.push_back({MATE_CRIT_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search, rng);

        // Closer plant selected purely due to default distance bias
        REQUIRE(selected == 2);

        cfg.mate_distance_bias = orig_bias;
        cfg.mate_selection_noise = orig_noise;
    }
}

TEST_CASE("Anther gate in mate selection", "[reproduction]") {
    std::mt19937_64 rng(42);

    SECTION("Plant without anther cannot be selected as mate") {
        auto mother = make_test_plant(1, {50, 50});
        auto no_anther = make_test_plant(2, {55, 50});   // no anther cell

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(no_anther));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.criteria.push_back({MATE_CRIT_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search, rng);

        REQUIRE(selected == 0);  // No eligible mate
    }

    SECTION("Plant with anther can be selected as mate") {
        World world(100, 100, 42);
        auto mother = make_test_plant(1, {50, 50});
        auto with_anther = make_test_plant(2, {55, 50});

        // Register with world and add an anther cell
        world.cell_at(with_anther.primary_position()).plant_id = with_anther.id();
        world.cell_at(with_anther.primary_position()).cell_type = CellType::Primary;
        with_anther.place_cell(CellType::Anther, {56, 50}, world);

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(with_anther));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.criteria.push_back({MATE_CRIT_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search, rng);

        REQUIRE(selected == 2);  // Selects the plant with anther
    }

    SECTION("Disabled anther does not qualify") {
        World world(100, 100, 42);
        auto mother = make_test_plant(1, {50, 50});
        auto disabled_anther = make_test_plant(2, {55, 50});

        world.cell_at(disabled_anther.primary_position()).plant_id = disabled_anther.id();
        world.cell_at(disabled_anther.primary_position()).cell_type = CellType::Primary;
        disabled_anther.place_cell(CellType::Anther, {56, 50}, world);
        disabled_anther.toggle_cell({56, 50}, false);

        std::vector<Plant> all_plants;
        all_plants.push_back(std::move(mother));
        all_plants.push_back(std::move(disabled_anther));

        MateSearchState search;
        search.max_distance = 100.0f;
        search.criteria.push_back({MATE_CRIT_SIZE, 1});

        uint64_t selected = ReproductionSystem::select_mate(
            all_plants[0], all_plants, search, rng);

        REQUIRE(selected == 0);  // Disabled anther doesn't count
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
