# PlantBrainGrid

A plant evolution simulation where plants grow on a 2D grid, compete for resources, and evolve through reproduction. Each plant is controlled by a bytecode VM "brain" that encodes its behavior as a genome subject to mutation and recombination.

Fair warning, this is my first real vibe code project (if that isn't obvious from looking at it)

## Overview

- **World**: 2D grid with water/nutrients (Perlin noise), light levels varying by season, and fire propagation
- **Plants**: Multicellular organisms that place cells (leaves, roots, bark, thorns, fire starters, storage organs, haustoria)
- **Brain VM**: 160-opcode bytecode interpreter — senses the world, queues cell placement/removal, launches seeds
- **Evolution**: Sexual reproduction with configurable recombination and per-byte random mutation

## Cell Types

Each plant is made of cells placed on adjacent grid tiles. The primary cell is created at birth; all others are placed by the brain VM.

| Cell | Description | Key stats |
|------|-------------|-----------|
| **Primary** | The plant's origin cell. Destroying it kills the plant. Draws a small amount of water passively. | water +0.2/tick |
| **SmallLeaf** | Generates energy from light. Cheap to build and maintain. | energy +1.0/tick, costs 10E to build |
| **BigLeaf** | High energy output but consumes water and nutrients. | energy +5.0/tick, costs 25E+10N to build |
| **FiberRoot** | Extracts water and nutrients from the soil. | water +1.2/tick, nutrients +1.0/tick |
| **TapRoot** | Deep root that draws more water than FiberRoot but no nutrients. | water +3.5/tick |
| **Anther** | Required for reproduction. Must be present to launch seeds. | costs 10E to build |
| **Bark** | Fireproof armor. Protects the tile from fire damage. | costs 1W+1N to build |
| **Thorn** | Blocks other plants from placing cells on this tile. | costs 5E to build |
| **FireStarter** | Ignites the tile it occupies. Expensive, one-shot offensive cell. | costs 30E to build |
| **StoreEnergy** | Increases the plant's energy storage cap by 300. | costs 10E to build |
| **StoreWater** | Increases the plant's water storage cap by 300. | costs 10E to build |
| **StoreNutrients** | Increases the plant's nutrients storage cap by 300. | costs 10E to build |
| **Haustorium** | Parasitic cell that steals resources from adjacent enemy plants. | steals 0.5/tick per neighbor |

Base resource cap is 300 for each resource. Store cells stack additively.

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
- **Arrow keys / WASD** — pan the camera
- **Mouse wheel / +/-** — zoom in/out
- **`.`** (period) — increase simulation speed (double ticks per frame)
- **`,`** (comma) — decrease simulation speed
- **Space / P** — pause/unpause
- **N** — advance exactly one tick (while paused)
- **1** — toggle water overlay
- **2** — toggle nutrient overlay
- **M** — toggle brain memory hex dump (selected plant)
- **F** — toggle fullscreen
- **Escape** — deselect plant

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
ctest --output-on-failure         # Run all tests
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
| `single_reproducer.py` | Python script — auto-spawn random plants and watch them evolve |

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

### Logged Simulation Runner

Runs a simulation and writes per-tick and per-plant data to Parquet files for analysis. Requires `pyarrow`.

```bash
python tools/run_logged.py --ticks 5000
python tools/run_logged.py --ticks 2000 --genome examples/reproducer.bin --output runs/exp1
python tools/run_logged.py --ticks 1000 --trace --log-every 10 --width 128 --height 128
```

Produces:
- `tick_stats.parquet` — one row per tick (population, wall time)
- `plant_ticks.parquet` — per-plant snapshots every `--log-every` ticks
- `plant_events.parquet` — birth and death records
- `reproduction_events.parquet` — every seed launch (parents, genome hash)
- `genomes.parquet` — full genome bytes captured at birth

### Simulation Overview Report

Generates a Markdown report with charts from logged simulation data. Requires `pyarrow`, `numpy`, `matplotlib`, `scikit-learn`.

```bash
python tools/generate_overview.py logs/
python tools/generate_overview.py logs/ --eps 200 --min-samples 5 --output report/
```

Includes population graphs, water/nutrient maps, species clusters (DBSCAN on genome Hamming distance), per-cluster spatial heatmaps, lifespan histograms, body plans, and opcode usage.

### Genome Heatmap

Plots a pairwise genome-difference heatmap from logged data. Optionally reorders by hierarchical clustering. Requires `pyarrow`, `numpy`, `matplotlib`, and optionally `scipy`.

```bash
python tools/plot_genome_heatmap.py logs/
python tools/plot_genome_heatmap.py logs/ --max-plants 300 --no-cluster
```

### Species Map

Clusters plants by genome similarity (DBSCAN) and plots them by world position, colored by species. Requires `pyarrow`, `numpy`, `matplotlib`, `scikit-learn`.

```bash
python tools/plot_species_map.py logs/
python tools/plot_species_map.py logs/ --eps 150 --min-ticks-lived 100 --output species.png
```

## Loading Plants into the Simulation

Plants can be loaded from hand-written assembly, saved genomes, or generated randomly. All three approaches use the same `Simulation.add_plant` entry point.

### From an assembled brain

Assemble a `.asm` file to a binary genome, then load it:

```python
from _plantbraingrid import Simulation, GridCoord
import sys
sys.path.insert(0, 'tools')
from brain_assembler import BrainAssembler

genome = list(BrainAssembler().assemble(open('examples/reproducer.asm').read()))

sim = Simulation(256, 256, 42)
plant = sim.add_plant(GridCoord(128, 128), genome)
plant.resources().energy = 200.0
plant.resources().water = 100.0
plant.resources().nutrients = 50.0
```

Or load a pre-assembled `.bin` file:

```python
genome = list(open('my_genome.bin', 'rb').read())
plant = sim.add_plant(GridCoord(64, 64), genome)
```

### Placing multiple plants at once

```python
import random

rng = random.Random(42)
positions = [(rng.randint(10, 246), rng.randint(10, 246)) for _ in range(20)]

for x, y in positions:
    plant = sim.add_plant(GridCoord(x, y), genome)
    if plant:  # None if position was occupied or out of bounds
        plant.resources().energy = 150.0
        plant.resources().water = 75.0
        plant.resources().nutrients = 40.0
```

### Competing species

Load different genomes to pit strategies against each other:

```python
assembler = BrainAssembler()
genome_a = list(assembler.assemble(open('examples/reproducer.asm').read()))
genome_b = list(assembler.assemble(open('examples/fire_starter.asm').read()))

sim = Simulation(512, 512, 1337)

for i in range(10):
    p = sim.add_plant(GridCoord(50 + i * 5, 256), genome_a)
    if p:
        p.resources().energy = 150.0; p.resources().water = 75.0

for i in range(10):
    p = sim.add_plant(GridCoord(400 + i * 5, 256), genome_b)
    if p:
        p.resources().energy = 150.0; p.resources().water = 75.0
```

### From a saved simulation

Plants are serialised as part of the simulation state and restore automatically:

```python
sim = Simulation(256, 256, 42)
sim.load_state('checkpoint.bin')
# Plants, resources, tick count, and RNG state are all restored
sim.run(1000)
```

### Auto-spawn (no manual placement needed)

Enable auto-spawn to let the simulation seed itself with random-brain plants whenever the population falls below a threshold. Plants die when energy or water reaches 0, so auto-spawn ensures the world never stays empty:

```python
sim = Simulation(256, 256, 42)
sim.enable_auto_spawn(True, min_population=10, energy=100.0, water=50.0, nutrients=30.0)
sim.run(5000)
```

### Plant death

Plants die when:
- Their **primary cell** is destroyed (by fire or another plant)
- Their **energy** or **water** reaches 0 after a resource tick

Nutrients reaching 0 does not cause death. Make sure newly placed plants have enough energy and water to survive their first few ticks before their leaves and roots begin producing.

## Simulation Loop

Each tick proceeds in order:

1. World tick — season/light update, fire spread
2. Fire damage — burn non-fireproof cells on burning tiles
3. Death check — kill plants whose primary cell is gone
4. Resource processing — leaves generate energy, roots extract water/nutrients, xylem transfers resources
5. Starvation check — kill plants whose energy or water reached 0
6. Brain execution — all brains run in parallel (action queues built independently)
7. Action resolution — conflicting placements from different plants cancel out
8. Seed flight update
9. Seed germination
10. Dead plant removal
11. Auto-spawn (if enabled) — inject random plants if population < threshold

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
  brain_assembler.py      .asm → bytecode
  genome_analyzer.py      Genome statistics
  run_logged.py           Logged simulation runner (→ Parquet)
  generate_overview.py    Markdown report from logs
  plot_genome_heatmap.py  Pairwise genome-difference heatmap
  plot_species_map.py     Species cluster scatter plot

tests/cpp/          Catch2 unit tests
tests/python/       pytest tests
examples/           Example .asm brain programs and scripts
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

## Brain VM Instructions

The brain is a 1024-byte memory that doubles as code and data. Each tick the VM executes up to `max_instructions_per_tick` (default 1000) instructions starting from the instruction pointer. Opcodes are decoded as `memory[ip] % 160`.

### Control Flow (0x00-0x0A)

| Opcode | Mnemonic | Args | Description |
|--------|----------|------|-------------|
| 0x00 | NOP | — | No operation |
| 0x01 | HALT | — | Stop execution for this tick |
| 0x02 | JUMP | addr16 | Jump to absolute address |
| 0x03 | JUMP_REL | offset8 | Jump relative (signed) |
| 0x04 | JUMP_IF_ZERO | addr16, dest16 | Jump to dest if mem[addr] == 0 |
| 0x05 | JUMP_IF_NEQ | a16, b16, dest16 | Jump if mem[a] != mem[b] |
| 0x06 | CALL | addr16 | Push return address, jump to addr |
| 0x07 | RET | — | Pop return address, jump back |
| 0x08 | JUMP_GT | a16, b16, dest16 | Jump if mem[a] > mem[b] |
| 0x09 | JUMP_EQ | a16, b16, dest16 | Jump if mem[a] == mem[b] |
| 0x0A | JUMP_LT | a16, b16, dest16 | Jump if mem[a] < mem[b] |

### Memory Operations (0x20-0x31)

| Opcode | Mnemonic | Args | Description |
|--------|----------|------|-------------|
| 0x20 | LOAD_IMM | addr16, value8 | Store immediate value into memory |
| 0x21 | COPY | dst16, src16 | Copy mem[src] to mem[dst] |
| 0x22 | ADD | dst16, a16, b16 | mem[dst] = mem[a] + mem[b] |
| 0x23 | SUB | dst16, a16, b16 | mem[dst] = mem[a] - mem[b] |
| 0x24 | MUL | dst16, a16, b16 | mem[dst] = mem[a] * mem[b] |
| 0x25 | DIV | dst16, a16, b16 | mem[dst] = mem[a] / mem[b] (0 if div by 0) |
| 0x26 | MOD | dst16, a16, b16 | mem[dst] = mem[a] % mem[b] |
| 0x27 | AND | dst16, a16, b16 | Bitwise AND |
| 0x28 | OR | dst16, a16, b16 | Bitwise OR |
| 0x29 | XOR | dst16, a16, b16 | Bitwise XOR |
| 0x2A | NOT | dst16, src16 | Bitwise NOT |
| 0x2B | SHL | dst16, a16, b16 | Shift left |
| 0x2C | SHR | dst16, a16, b16 | Shift right |
| 0x2D | CMP_LT | dst16, a16, b16 | mem[dst] = 1 if mem[a] < mem[b], else 0 |
| 0x2E | CMP_EQ | dst16, a16, b16 | mem[dst] = 1 if mem[a] == mem[b], else 0 |
| 0x2F | LOAD_IND | dst16, ptr16 | mem[dst] = mem[mem[ptr]] |
| 0x30 | STORE_IND | ptr16, src16 | mem[mem[ptr]] = mem[src] |
| 0x31 | RANDOMIZE | dst16 | mem[dst] = random byte |

### World Sensing (0x40-0x4B)

| Opcode | Mnemonic | Args | Description |
|--------|----------|------|-------------|
| 0x40 | SENSE_WATER | dst16 | Water level at primary cell |
| 0x41 | SENSE_NUTRIENTS | dst16 | Nutrient level at primary cell |
| 0x42 | SENSE_LIGHT | dst16 | Light level at primary cell |
| 0x43 | SENSE_CELL | dst16, dx8, dy8 | Cell type at offset (dx,dy) |
| 0x44 | SENSE_FIRE | dst16, dx8, dy8 | Fire state at offset |
| 0x45 | SENSE_OWNED | dst16, dx8, dy8 | 1 if own cell at offset, else 0 |
| 0x46 | SENSE_SELF_ENERGY | dst16 | Plant's current energy |
| 0x47 | SENSE_SELF_WATER | dst16 | Plant's current water |
| 0x48 | SENSE_SELF_NUTRIENTS | dst16 | Plant's current nutrients |
| 0x49 | SENSE_CELL_COUNT | dst16 | Number of cells this plant has |
| 0x4A | SENSE_AGE | dst16 | Plant's age in ticks |
| 0x4B | SENSE_SEASON | dst16 | Current season index (0-3) |

### Plant Actions (0x60-0x63)

| Opcode | Mnemonic | Args | Description |
|--------|----------|------|-------------|
| 0x60 | PLACE_CELL | type8, dx8, dy8, dir8 | Place a cell at offset. Type is modulo 14 |
| 0x61 | ROTATE_CELL | dx8, dy8, dir8 | Rotate an existing cell |
| 0x62 | TOGGLE_CELL | dx8, dy8 | Enable/disable a cell |
| 0x63 | REMOVE_CELL | dx8, dy8 | Remove own cell at offset |

### Reproduction (0x80-0x89)

Mate selection works by accumulating weighted criteria, then launching a seed. Each `MATE_BY_*` instruction adds one criterion to the current mate search.

| Opcode | Mnemonic | Args | Description |
|--------|----------|------|-------------|
| 0x80 | MATE_BY_SIZE | max_dist8, magnitude8 | Prefer mates by cell count |
| 0x81 | MATE_BY_AGE | max_dist8, magnitude8 | Prefer older/younger mates |
| 0x82 | MATE_BY_ENERGY | max_dist8, magnitude8 | Prefer mates with more energy |
| 0x83 | MATE_BY_WATER | max_dist8, magnitude8 | Prefer mates with more water |
| 0x84 | MATE_BY_NUTRIENTS | max_dist8, magnitude8 | Prefer mates with more nutrients |
| 0x85 | MATE_BY_DISTANCE | max_dist8, magnitude8 | Prefer closer/farther mates |
| 0x86 | MATE_BY_SIMILARITY | max_dist8, magnitude8 | Prefer genetically similar mates |
| 0x87 | MATE_BY_DIFFERENCE | max_dist8, magnitude8 | Prefer genetically different mates |
| 0x88 | MATE_BY_CELL_COUNT | max_dist8, cell_type8, target8, magnitude8 | Prefer mates by count of a specific cell type |
| 0x89 | LAUNCH_SEED | energy8, water8, nutrients8, dx8, dy8, dist8, recomb8, mutations8 | Launch a seed with the selected mate |

Recombination methods for LAUNCH_SEED (recomb byte % 4): RandomMix (0), Alternating (1), Mother75 (2), Father75 (3).

## Mutation

During reproduction, child genomes are built from the parents' genomes via the chosen recombination method (RandomMix, Alternating, Mother75, Father75), then each byte independently has a `mutation_rate` probability of being replaced with a uniformly random value (0-255). The default rate is 0.1% (`mutation_rate = 0.001`).

## Saving and Loading

```python
sim.save_state("checkpoint.bin")
sim.load_state("checkpoint.bin")
```

Save files are binary (magic `PBGS`, version 1) and store tick count, plant genomes, resources, and positions.
