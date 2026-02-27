#pragma once

#include <cstdint>
#include <array>

namespace pbg {

class PerlinNoise {
public:
    explicit PerlinNoise(uint64_t seed = 0);

    // Get noise value at position, returns value in range [-1, 1]
    float noise(float x, float y) const;

    // Get noise value scaled and offset: base + amplitude * noise(x * scale, y * scale)
    float scaled_noise(float x, float y, float scale, float base, float amplitude) const;

    // Reseed the permutation table
    void reseed(uint64_t seed);

private:
    std::array<uint8_t, 512> perm_;

    float fade(float t) const;
    float lerp(float a, float b, float t) const;
    float grad(int hash, float x, float y) const;
};

}  // namespace pbg
