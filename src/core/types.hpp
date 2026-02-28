#pragma once

#include <cstdint>
#include <functional>

namespace pbg {

struct GridCoord {
    int32_t x = 0;
    int32_t y = 0;

    GridCoord() = default;
    GridCoord(int32_t x_, int32_t y_) : x(x_), y(y_) {}

    GridCoord operator+(const GridCoord& other) const {
        return {x + other.x, y + other.y};
    }

    GridCoord operator-(const GridCoord& other) const {
        return {x - other.x, y - other.y};
    }

    bool operator==(const GridCoord& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const GridCoord& other) const {
        return !(*this == other);
    }
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& other) const {
        return {x + other.x, y + other.y};
    }

    Vec2 operator*(float scalar) const {
        return {x * scalar, y * scalar};
    }

    float length() const;
    Vec2 normalized() const;
};

enum class Direction : uint8_t {
    North = 0,
    East = 1,
    South = 2,
    West = 3
};

inline GridCoord direction_offset(Direction dir) {
    switch (dir) {
        case Direction::North: return {0, -1};
        case Direction::East:  return {1, 0};
        case Direction::South: return {0, 1};
        case Direction::West:  return {-1, 0};
    }
    return {0, 0};
}

inline Direction direction_from_byte(uint8_t byte) {
    return static_cast<Direction>(byte % 4);
}

enum class CellType : uint8_t {
    Empty = 0,
    Primary = 1,
    SmallLeaf = 2,
    BigLeaf = 3,
    FiberRoot = 4,
    Xylem = 5,
    FireproofXylem = 6,
    Thorn = 7,
    FireStarter = 8,
    TapRoot = 9
};

inline bool is_valid_placeable_cell(CellType type) {
    return type != CellType::Empty && type != CellType::Primary;
}

enum class RecombinationMethod : uint8_t {
    MotherOnly = 0,
    FatherOnly = 1,
    Mother75 = 2,
    Father75 = 3,
    HalfHalf = 4,
    RandomMix = 5,
    Alternating = 6
};

enum class SeedPlacementMode : uint8_t {
    Exact = 0,
    Random = 1
};

}  // namespace pbg

// Hash support for GridCoord
namespace std {
template <>
struct hash<pbg::GridCoord> {
    size_t operator()(const pbg::GridCoord& coord) const {
        return hash<int64_t>()(static_cast<int64_t>(coord.x) << 32 |
                               static_cast<uint32_t>(coord.y));
    }
};
}  // namespace std
