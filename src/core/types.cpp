#include "core/types.hpp"
#include <cmath>

namespace pbg {

float Vec2::length() const {
    return std::sqrt(x * x + y * y);
}

Vec2 Vec2::normalized() const {
    float len = length();
    if (len < 1e-6f) {
        return {0.0f, 0.0f};
    }
    return {x / len, y / len};
}

}  // namespace pbg
