# PlantBrainGrid

A plant evolution simulation where plants grow on a 2D grid, compete for resources, and evolve through reproduction. Each plant is controlled by a bytecode VM "brain" that encodes its behavior as a genome subject to mutation and recombination.

## Overview

- **World**: 2D grid with water/nutrients (Perlin noise), light levels varying by season, and fire propagation
- **Plants**: Multicellular organisms that place cells (leaves, roots, xylem, thorns, fire starters)
- **Brain VM**: 160-opcode bytecode interpreter — senses the world, queues cell placement/removal, launches seeds
- **Evolution**: Sexual/asexual reproduction with configurable recombination and per-byte random mutation

## Building

### Requirements

- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.16+
- For Python bindings: Python 3.8+ development headers and pip

### C++ core and tests only

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_PYTHON_BINDINGS=OFF
make -j$(nproc)
```

### With Python bindings

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON_BINDINGS=ON
make -j$(nproc)
# Copy the built module into the Python package
cp _plantbraingrid*.so ../src/python/plantbraingrid/
```

If Python development headers are not installed system-wide, point CMake at them directly:

```bash
cmake .. -DBUILD_PYTHON_BINDINGS=ON \
  -DPython3_INCLUDE_DIR=/path/to/include/python3.x \
  -DPython3_LIBRARY=/path/to/libpython3.x.so \
  -DPython3_EXECUTABLE=/usr/bin/python3
```

### Install as Python package (editable)

```bash
pip install -e .
```

## Running

### Headless simulation (no display required)

```bash
python -m plantbraingrid --headless --width 256 --height 256 --seed 42 --ticks 500
```

#### Headless with auto-spawn

Start from scratch — plants with random brains are automatically injected whenever the living population drops below 10:

```bash
python -m plantbraingrid --headless --auto-spawn --seed 42 --ticks 2000
```

### Visual simulation (requires `pip install raylib`)

```bash
python -m plantbraingrid --width 512 --height 512 --seed 42
```

Visual controls:
- **Click** a plant to select it and view its brain state
- **`.`** (period) — increase simulation speed (double ticks per frame)
- **`,`** (comma) — decrease simulation speed
- **`P`** — pause/unpause

#### Visual with auto-spawn

```bash
python -m plantbraingrid --auto-spawn --seed 42
```

### Command-line options

| Flag | Default | Description |
|------|---------|-------------|
| `--width N` | 256 | World width in cells |
| `--height N` | 256 | World height in cells |
| `--seed N` | random | RNG seed (same seed = identical simulation) |
| `--headless` | off | Run without visualization |
| `--ticks N` | 1000 | Ticks to run in headless mode |
| `--auto-spawn` | off | Spawn random-brain plants when population < 10 |

## Running Tests

### C++ unit tests (Catch2)

```bash
cd build
ctest --output-on-failure         # Run all 58 tests
./plantbraingrid_tests "[brain]"  # Brain VM tests only
./plantbraingrid_tests "[world]"  # World grid tests only
./plantbraingrid_tests "[plant]"  # Plant tests only
./plantbraingrid_tests "[reproduction]"
./plantbraingrid_tests "[simulation]"
./plantbraingrid_tests "[serialization]"
```

### Python tests (pytest)

Requires Python bindings to be built and copied into the package directory.

```bash
pytest tests/python/ -v
pytest tests/python/test_determinism.py -v   # Determinism tests only
pytest tests/python/test_integration.py -v   # Integration tests only
```

## Tools

### Brain Assembler

Converts human-readable assembly into 1024-byte brain bytecodes. Supports labels, `.define`, `.org`, `.db`, and `.fill` directives.

```bash
python tools/brain_assembler.py examples/simple_leaf_grower.asm -o my_genome.bin
python tools/brain_assembler.py examples/fire_starter.asm --hex   # Print hex dump
```

Assembly syntax:

```asm
.define ENERGY 0x00F0
.define THRESH 0x00F1

main:
    SENSE_SELF_ENERGY [ENERGY]
    LOAD_IMM [THRESH], 50
    CMP_LT [THRESH], [THRESH], [ENERGY]
    JUMP_IF_ZERO [THRESH], main
    PLACE_CELL SmallLeaf, +0, -1, North
    JUMP main
```

See `examples/` for complete working examples:

| File | Strategy |
|------|----------|
| `simple_leaf_grower.asm` | Grow leaves in all directions when energy is high |
| `root_farmer.asm` | Build a deep root network to maximise water extraction |
| `reproducer.asm` | Accumulate energy then launch clonal seeds |
| `fire_starter.asm` | Build fireproof xylem, burn neighbours, reproduce into cleared land |

### Genome Analyzer

Analyses an evolved or hand-written genome for statistical properties:

```bash
python tools/genome_analyzer.py my_genome.bin
python tools/genome_analyzer.py my_genome.bin --verbose  # Show instruction frequency table
```

Output includes:
- Shannon entropy (bits/byte)
- Instruction category breakdown (control flow, sensing, actions, reproduction)
- Loop detection (backward jumps)
- Complexity score (0–100)

## Simulation Loop

Each tick proceeds in order:

1. World tick — season/light update, fire spread
2. Fire damage — burn non-fireproof cells on burning tiles
3. Death check — kill plants whose primary cell is gone
4. Resource processing — leaves generate energy, roots extract water/nutrients, xylem transfers resources
5. Brain execution — all brains run in parallel (action queues built independently)
6. Action resolution — conflicting placements from different plants cancel out
7. Seed flight update
8. Seed germination
9. Dead plant removal
10. Auto-spawn (if enabled) — inject random plants if population < threshold

## Architecture

```
src/core/           C++17 simulation core
  brain.hpp/.cpp    Bytecode VM (160 opcodes, 1024-byte memory)
  world.hpp/.cpp    2D grid, terrain, seasons, fire
  plant.hpp/.cpp    Multicellular plant entity
  plant_cell.hpp    Cell types and placement rules
  resources.hpp/.cpp  Energy/water/nutrient flow
  reproduction.hpp/.cpp  Seeds, recombination, mutation
  simulation.hpp/.cpp   Main simulation loop

src/bindings/
  module.cpp        pybind11 Python bindings

src/python/plantbraingrid/
  main.py           CLI entry point
  visualization.py  raylib renderer
  brain_viewer.py   Hex dump and disassembler (pure Python)

tools/
  brain_assembler.py   .asm → bytecode
  genome_analyzer.py   Genome statistics

tests/cpp/          Catch2 unit tests (58 tests)
tests/python/       pytest tests (66 tests)
examples/           Example .asm brain programs
```

## Configuration

Global simulation parameters can be modified via the `Config` struct before the simulation starts. From Python:

```python
from _plantbraingrid import get_config
cfg = get_config()
cfg.mutation_rate = 0.02          # 2% per-byte mutation chance on reproduction
cfg.fire_destroy_ticks = 5        # Ticks before fire destroys a cell
cfg.season_length = 5000          # Ticks per season
cfg.max_instructions_per_tick = 500
```

From C++:

```cpp
auto& cfg = pbg::get_config();
cfg.mutation_rate = 0.02f;
cfg.brain_size = 512;
```

## Mutation

During reproduction, child genomes are built from the parents' genomes via the chosen recombination method (`MotherOnly`, `HalfHalf`, `RandomMix`, etc.), then each byte independently has a `mutation_rate` probability of being replaced with a uniformly random value (0–255). The default rate is 1% (`mutation_rate = 0.01`).

## Saving and Loading

```python
sim.save_state("checkpoint.bin")
sim.load_state("checkpoint.bin")
```

Save files are binary (magic `PBGS`, version 1) and store tick count, plant genomes, resources, and positions.
