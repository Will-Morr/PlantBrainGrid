#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/perlin.hpp"
#include <set>

using namespace pbg;
using Catch::Matchers::WithinAbs;

TEST_CASE("Perlin noise basic properties", "[perlin]") {
    PerlinNoise perlin(42);

    SECTION("Noise values are in range [-1, 1]") {
        for (float x = 0; x < 100; x += 0.7f) {
            for (float y = 0; y < 100; y += 0.7f) {
                float value = perlin.noise(x, y);
                REQUIRE(value >= -1.0f);
                REQUIRE(value <= 1.0f);
            }
        }
    }

    SECTION("Noise is continuous (nearby points have similar values)") {
        float x = 50.0f, y = 50.0f;
        float base = perlin.noise(x, y);
        float epsilon = 0.01f;

        // Small offset should produce similar value
        float nearby = perlin.noise(x + epsilon, y + epsilon);
        REQUIRE_THAT(nearby, WithinAbs(base, 0.1));
    }

    SECTION("Noise varies over distance") {
        // Use non-integer coordinates to get varied noise values
        std::set<int> unique_buckets;
        for (float x = 0.1f; x < 50; x += 3.7f) {
            float val = perlin.noise(x, x * 0.8f);
            unique_buckets.insert(static_cast<int>(val * 100));
        }
        REQUIRE(unique_buckets.size() > 3);
    }
}

TEST_CASE("Perlin noise determinism", "[perlin]") {
    SECTION("Same seed produces same values") {
        PerlinNoise p1(12345);
        PerlinNoise p2(12345);

        for (float x = 0; x < 20; x += 1.3f) {
            for (float y = 0; y < 20; y += 1.3f) {
                REQUIRE(p1.noise(x, y) == p2.noise(x, y));
            }
        }
    }

    SECTION("Different seeds produce different values") {
        PerlinNoise p1(100);
        PerlinNoise p2(200);

        int differences = 0;
        // Use non-integer coordinates
        for (float x = 0.3f; x < 20; x += 1.7f) {
            for (float y = 0.5f; y < 20; y += 1.3f) {
                if (p1.noise(x, y) != p2.noise(x, y)) {
                    ++differences;
                }
            }
        }
        REQUIRE(differences > 50);  // Most values should differ
    }

    SECTION("Reseed produces same results as new instance") {
        PerlinNoise p1(999);
        PerlinNoise p2(111);
        p2.reseed(999);

        for (float x = 0; x < 10; x += 0.5f) {
            REQUIRE(p1.noise(x, 5.0f) == p2.noise(x, 5.0f));
        }
    }
}

TEST_CASE("Perlin noise scaled_noise", "[perlin]") {
    PerlinNoise perlin(42);

    SECTION("Base and amplitude work correctly") {
        float base = 50.0f;
        float amplitude = 20.0f;
        float scale = 0.1f;

        for (float x = 0; x < 50; x += 3.0f) {
            for (float y = 0; y < 50; y += 3.0f) {
                float value = perlin.scaled_noise(x, y, scale, base, amplitude);
                // Should be in range [base - amplitude, base + amplitude]
                REQUIRE(value >= base - amplitude);
                REQUIRE(value <= base + amplitude);
            }
        }
    }

    SECTION("Scale affects frequency") {
        // Higher scale = more variation in same distance
        float base = 0.0f;
        float amplitude = 1.0f;

        std::set<int> low_freq_buckets;
        std::set<int> high_freq_buckets;

        for (float x = 0; x < 10; x += 0.5f) {
            float low = perlin.scaled_noise(x, 0, 0.01f, base, amplitude);
            float high = perlin.scaled_noise(x, 0, 0.5f, base, amplitude);
            low_freq_buckets.insert(static_cast<int>(low * 10));
            high_freq_buckets.insert(static_cast<int>(high * 10));
        }

        // High frequency should have more variation
        REQUIRE(high_freq_buckets.size() >= low_freq_buckets.size());
    }
}
