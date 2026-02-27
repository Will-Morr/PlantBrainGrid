#include "core/perlin.hpp"
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>

namespace pbg {

PerlinNoise::PerlinNoise(uint64_t seed) {
    reseed(seed);
}

void PerlinNoise::reseed(uint64_t seed) {
    // Fill first 256 entries with 0-255
    std::array<uint8_t, 256> base;
    std::iota(base.begin(), base.end(), 0);

    // Shuffle using the seed
    std::mt19937_64 rng(seed);
    std::shuffle(base.begin(), base.end(), rng);

    // Duplicate to avoid index wrapping
    for (size_t i = 0; i < 256; ++i) {
        perm_[i] = base[i];
        perm_[i + 256] = base[i];
    }
}

float PerlinNoise::fade(float t) const {
    // 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float PerlinNoise::lerp(float a, float b, float t) const {
    return a + t * (b - a);
}

float PerlinNoise::grad(int hash, float x, float y) const {
    // Use 4 gradient directions for 2D
    int h = hash & 3;
    switch (h) {
        case 0: return  x + y;
        case 1: return -x + y;
        case 2: return  x - y;
        case 3: return -x - y;
    }
    return 0.0f;
}

float PerlinNoise::noise(float x, float y) const {
    // Find unit grid cell containing point
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    // Relative position within cell
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    // Fade curves
    float u = fade(xf);
    float v = fade(yf);

    // Hash coordinates of the 4 corners
    int aa = perm_[perm_[xi] + yi];
    int ab = perm_[perm_[xi] + yi + 1];
    int ba = perm_[perm_[xi + 1] + yi];
    int bb = perm_[perm_[xi + 1] + yi + 1];

    // Gradient dot products and interpolation
    float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u);
    float x2 = lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u);

    return lerp(x1, x2, v);
}

float PerlinNoise::scaled_noise(float x, float y, float scale, float base, float amplitude) const {
    return base + amplitude * noise(x * scale, y * scale);
}

}  // namespace pbg
