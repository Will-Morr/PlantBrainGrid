# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PlantBrainGrid is a plant evolution simulation where plants grow on a 2D grid, compete for resources, and evolve through reproduction. Each plant has a bytecode-based "brain" VM that controls its behavior.

**Stack**: C++17 core (simulation) + pybind11 bindings + Python/raylib visualization

## Build and Run Commands

```bash
# Build C++ core (without Python bindings)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_PYTHON_BINDINGS=OFF
make -j$(nproc)

# Run all C++ tests
ctest --output-on-failure

# Run specific test category
./plantbraingrid_tests "[brain]"
./plantbraingrid_tests "[world]"
./plantbraingrid_tests "[plant]"
./plantbraingrid_tests "[perlin]"

# Build with Python bindings (requires python3-dev)
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON_BINDINGS=ON
make -j$(nproc)
pip install -e ..

# Run Python tests (when bindings available)
pytest tests/python/ -v
```

## Architecture

See `ARCHITECTURE.md` for full details. Key components:

- **World Grid** (`src/core/world.*`): 2D grid with water/nutrients (Perlin noise), light levels, fire state
- **Plant** (`src/core/plant.*`): Entity with cells, resources, brain
- **Plant Cells** (`src/core/plant_cell.*`): Leaf, Root, Xylem, Thorn, FireStarter types
- **Brain VM** (`src/core/brain.*`): Bytecode interpreter with ~40 instructions
- **Resources** (`src/core/resources.*`): Flow calculation (roots→xylem→primary cell)
- **Reproduction** (`src/core/reproduction.*`): Seeds, mate selection, genetic recombination

## Brain VM Instruction Categories

- `0x00-0x1F`: Control flow (JUMP, CALL, RET, conditionals)
- `0x20-0x3F`: Memory operations (arithmetic, bitwise, indirect access)
- `0x40-0x5F`: World sensing (water, nutrients, light, cells, self-stats)
- `0x60-0x7F`: Plant actions (place/toggle/remove cells)
- `0x80-0x9F`: Reproduction (mate search, recombination, seed launch)

Brain execution: `opcode = memory[ip] % NUM_OPCODES`, then execute with args from subsequent bytes.

## Key Design Decisions

1. **Determinism**: All randomness uses seeded RNG; same seed = identical simulation
2. **Xylem flow**: Automatic each tick when enabled, costs resources per hop
3. **Plant death**: When primary cell is destroyed, or when energy or water reaches 0 after a resource tick. Nutrients at 0 do not cause death. Brain errors never cause death.
4. **Mate selection**: Brain iteratively adds weighted criteria (variable length), then selects best match
5. **Parallel brain execution**: All brains run in parallel, building action queues. Conflicting placements cancel out.
6. **Non-fatal errors**: Out-of-bounds memory and instruction limits incur resource penalties, not death
7. **Cell direction**: Specified at placement time (not separate instruction)
8. **Reproduction**: Fixed-order arguments in LAUNCH_SEED (8 bytes), only mate weighting is variable length

## Testing Approach

Each component should have unit tests before integration. Use Catch2 for C++, pytest for Python. Test determinism by running same seed twice and comparing state.

## Implementation Status

**Completed (Phase 1-3):**
- Core types, config, Perlin noise
- World grid with terrain, seasons, fire
- Plant with cells, resources, placement rules
- Brain VM with all instruction categories
- 26 unit tests passing

**Next Steps (Phase 4+):**
- Resource flow system
- Reproduction and seeds
- Simulation loop with parallel brain execution
- Python bindings and visualization

See `IMPLEMENTATION_PLAN.md` for full phase breakdown.
