# PlantBrainGrid Architecture

## Overview

PlantBrainGrid is a plant evolution simulation where plants grow, compete for resources, reproduce with genetic variation, and evolve over time. Each plant has a bytecode-based "brain" that controls its behavior.

**Technology Stack:**
- **Core Simulation**: C++17 (performance-critical logic)
- **Python Bindings**: pybind11
- **Visualization**: Python + raylib
- **Build System**: CMake + pip

---

## Core Components

### 1. World Grid (`src/core/world.hpp`)

A 2D grid of cells. Each grid cell contains:
- `water_level: float` - Water availability (Perlin noise generated)
- `nutrient_level: float` - Nutrient availability (separate Perlin noise)
- `light_level: float` - Current sunlight (varies with seasons)
- `fire_ticks: int` - Ticks remaining if on fire (0 = no fire)
- `occupant: PlantCell*` - Pointer to plant cell occupying this tile (or nullptr)

```
World
├── width, height (configurable)
├── cells[width * height]
├── season_tick (current position in season cycle)
├── rng (seeded random generator for determinism)
└── perlin generators for water/nutrients
```

### 2. Plant (`src/core/plant.hpp`)

Represents an individual plant organism.

```
Plant
├── id: uint64_t
├── primary_cell: GridCoord          // Fixed location, plant dies if destroyed
├── cells: vector<PlantCell>         // All cells belonging to this plant
├── brain: Brain                     // The plant's control program
├── resources:
│   ├── energy: float
│   ├── water: float
│   └── nutrients: float
├── alive: bool
└── age: uint64_t                    // Ticks since germination
```

### 3. Plant Cells (`src/core/plant_cell.hpp`)

Different cell types with unique functions:

| Cell Type | Function | Placement Cost | Maintenance Cost |
|-----------|----------|----------------|------------------|
| **Primary** | Core cell, plant anchor | N/A (initial) | Low |
| **SmallLeaf** | Produces energy from light | Low | Low water |
| **BigLeaf** | Produces more energy | Medium | High water + nutrients |
| **Root** | Extracts water from ground | Low | Low energy |
| **Xylem** | Transmits resources directionally | Medium | Consumes some transmitted |
| **FireproofXylem** | Xylem immune to fire | High nutrients | Same as xylem |
| **Thorn** | Blocks adjacent cell placement | Medium | Low |
| **FireStarter** | Ignites adjacent tiles | High | None (one-time) |

Each cell has:
```
PlantCell
├── type: CellType
├── position: GridCoord
├── enabled: bool           // Brain can toggle on/off
├── direction: Direction    // For xylem: resource flow direction
└── parent_offset: GridCoord // Relative position to connected cell
```

### 4. Brain VM (`src/core/brain.hpp`)

A simple bytecode virtual machine.

```
Brain
├── memory: vector<uint8_t>     // Default 1024 bytes
├── ip: uint16_t                // Instruction pointer
├── halted: bool                // Stop execution this tick
├── execution_history: optional<ExecutionTrace>  // For visualization
```

**Execution Model:**
1. Read byte at `memory[ip]`
2. Compute `opcode = byte % NUM_OPCODES`
3. Execute instruction (may read additional bytes as arguments)
4. Advance `ip` by instruction length
5. Repeat until `HALT` or tick limit reached

**Parallel Execution:**
Brain execution is parallelizable across all plants. Actions (cell placements, toggles, removals) are queued during execution rather than applied immediately. After all brains complete:
1. Collect all placement/update queues
2. Detect conflicts (multiple plants placing at same tile)
3. **Conflicting placements cancel out** - none go through
4. Apply non-conflicting actions atomically

**Error Handling (Non-Fatal):**
- **Out-of-bounds memory access**: Incurs resource penalty (configurable energy cost), returns 0 for reads, ignores writes
- **Instruction limit exceeded**: Incurs resource penalty, halts execution for tick (not infinite loop death)
- Plants only die from primary cell destruction, never from brain errors

### 5. Resource Flow System (`src/core/resources.hpp`)

Resources flow through the plant network each tick:

1. **Roots** extract water/nutrients from ground tiles
2. **Leaves** generate energy based on light level
3. **Xylem** (when enabled) moves resources toward primary cell
4. Resources accumulate at primary cell for use
5. Each xylem transfer costs a small percentage (configurable)

### 6. Reproduction System (`src/core/reproduction.hpp`)

```
Seed
├── genome: vector<uint8_t>     // Brain memory to initialize offspring
├── energy: float               // Starting energy
├── water: float                // Starting water
├── nutrients: float            // Starting nutrients
├── launch_energy: float        // Determines travel distance
├── position: GridCoord         // Current/landing position
├── velocity: Vec2              // For in-flight seeds
└── parent_id: uint64_t
```

**Sexual Reproduction Flow:**
1. Brain enters mate selection mode
2. Iterates through potential mates within max_distance
3. Brain specifies weight criteria (size, age, resources, etc.)
4. Best match selected after brain signals completion
5. Recombination method applied (see table below)
6. Mutations applied (random byte modifications)
7. Seed created with allocated resources

**Recombination Methods:**
| Code | Name | Description |
|------|------|-------------|
| 0x00 | MOTHER_ONLY | 100% mother's genome |
| 0x01 | FATHER_ONLY | 100% father's genome |
| 0x02 | MOTHER_75 | 75% mother, 25% father (each byte) |
| 0x03 | FATHER_75 | 75% father, 25% mother (each byte) |
| 0x04 | HALF_HALF | First half mother, second half father |
| 0x05 | RANDOM_MIX | Each byte 50/50 randomly from either parent |
| 0x06 | ALTERNATING | Even bytes mother, odd bytes father |

### 7. Fire System (`src/core/fire.hpp`)

- FireStarter cells ignite adjacent tiles
- Fire spreads to adjacent cells after `FIRE_SPREAD_TICKS` (configurable)
- Cells destroyed after `FIRE_DESTROY_TICKS`
- FireproofXylem is immune
- Fire does not spread to empty tiles or water-heavy tiles

### 8. Season System (`src/core/seasons.hpp`)

Sinusoidal light variation over time:
```cpp
light_multiplier = BASE_LIGHT + LIGHT_AMPLITUDE * sin(2 * PI * tick / SEASON_LENGTH)
```

---

## Proposed Brain VM Instruction Set

### Control Flow (0x00-0x1F)
| Opcode | Name | Args | Description |
|--------|------|------|-------------|
| 0x00 | NOP | 0 | No operation |
| 0x01 | HALT | 0 | End execution this tick |
| 0x02 | JUMP | 2 | Jump to address (2-byte absolute) |
| 0x03 | JUMP_REL | 1 | Jump relative (signed 1-byte offset) |
| 0x04 | JUMP_IF_ZERO | 3 | Jump to addr if mem[arg1] == 0 |
| 0x05 | JUMP_IF_NEQ | 4 | Jump to addr if mem[arg1] != arg2 |
| 0x06 | CALL | 2 | Push IP+3, jump to address |
| 0x07 | RET | 0 | Pop and jump to address |

### Memory Operations (0x20-0x3F)
| Opcode | Name | Args | Description |
|--------|------|------|-------------|
| 0x20 | LOAD_IMM | 3 | mem[addr] = immediate value |
| 0x21 | COPY | 4 | mem[dest] = mem[src] |
| 0x22 | ADD | 4 | mem[dest] = mem[a] + mem[b] |
| 0x23 | SUB | 4 | mem[dest] = mem[a] - mem[b] |
| 0x24 | MUL | 4 | mem[dest] = mem[a] * mem[b] |
| 0x25 | DIV | 4 | mem[dest] = mem[a] / mem[b] (safe) |
| 0x26 | MOD | 4 | mem[dest] = mem[a] % mem[b] |
| 0x27 | AND | 4 | mem[dest] = mem[a] & mem[b] |
| 0x28 | OR | 4 | mem[dest] = mem[a] \| mem[b] |
| 0x29 | XOR | 4 | mem[dest] = mem[a] ^ mem[b] |
| 0x2A | NOT | 2 | mem[dest] = ~mem[src] |
| 0x2B | SHL | 3 | mem[dest] = mem[src] << arg |
| 0x2C | SHR | 3 | mem[dest] = mem[src] >> arg |
| 0x2D | CMP_LT | 4 | mem[dest] = mem[a] < mem[b] ? 1 : 0 |
| 0x2E | CMP_EQ | 4 | mem[dest] = mem[a] == mem[b] ? 1 : 0 |
| 0x2F | LOAD_IND | 3 | mem[dest] = mem[mem[addr_ptr]] |
| 0x30 | STORE_IND | 3 | mem[mem[addr_ptr]] = mem[src] |
| 0x31 | RANDOMIZE | 3 | Randomize mem[start] through mem[start+length-1] |

### World Sensing (0x40-0x5F)
| Opcode | Name | Args | Description |
|--------|------|------|-------------|
| 0x40 | SENSE_WATER | 3 | mem[dest] = water at relative (dx, dy) |
| 0x41 | SENSE_NUTRIENTS | 3 | mem[dest] = nutrients at (dx, dy) |
| 0x42 | SENSE_LIGHT | 1 | mem[dest] = current light level |
| 0x43 | SENSE_CELL | 3 | mem[dest] = cell type at (dx, dy) |
| 0x44 | SENSE_FIRE | 3 | mem[dest] = fire status at (dx, dy) |
| 0x45 | SENSE_OWNED | 3 | mem[dest] = 1 if own cell at (dx, dy) |
| 0x46 | SENSE_SELF_ENERGY | 1 | mem[dest] = own energy (scaled) |
| 0x47 | SENSE_SELF_WATER | 1 | mem[dest] = own water (scaled) |
| 0x48 | SENSE_SELF_NUTRIENTS | 1 | mem[dest] = own nutrients (scaled) |
| 0x49 | SENSE_CELL_COUNT | 1 | mem[dest] = number of own cells |
| 0x4A | SENSE_AGE | 1 | mem[dest] = plant age (scaled) |

### Plant Actions (0x60-0x7F)
| Opcode | Name | Args | Description |
|--------|------|------|-------------|
| 0x60 | PLACE_CELL | 4 | Place cell (type, dx, dy, direction) - direction used for xylem |
| 0x61 | ROTATE_CELL | 3 | Rotate existing cell direction at (dx, dy) by amount |
| 0x62 | TOGGLE_CELL | 3 | Enable/disable cell at (dx, dy) |
| 0x63 | REMOVE_CELL | 2 | Remove own cell at (dx, dy) |

**Direction values for PLACE_CELL:**
| Value | Direction |
|-------|-----------|
| 0 | North (toward primary) |
| 1 | East |
| 2 | South |
| 3 | West |
| 4+ | (value % 4) |

### Reproduction (0x80-0x9F)

**Mate Selection (variable length):**
| Opcode | Name | Args | Description |
|--------|------|------|-------------|
| 0x80 | START_MATE_SEARCH | 1 | Begin mate selection, max_dist = arg |
| 0x81 | ADD_MATE_WEIGHT | 2 | Add weight for criterion (type, weight) |
| 0x82 | FINISH_MATE_SELECT | 0 | Complete selection, store best match |

**Seed Launch (fixed order, single instruction):**
| Opcode | Name | Args | Description |
|--------|------|------|-------------|
| 0x83 | LAUNCH_SEED | 8 | Launch with fixed-order parameters (see below) |

**LAUNCH_SEED argument order (8 bytes):**
```
Byte 0: recombination_method (0x00-0x06, see recombination table)
Byte 1: seed_energy (scaled, 0-255)
Byte 2: seed_water (scaled, 0-255)
Byte 3: seed_nutrients (scaled, 0-255)
Byte 4: launch_power (determines max distance)
Byte 5: launch_direction_dx (signed, -128 to 127)
Byte 6: launch_direction_dy (signed, -128 to 127)
Byte 7: placement_mode (0 = exact direction, 1 = random within range)
```

**Placement modes:**
- `0 (EXACT)`: Seed lands at calculated position based on direction and power
- `1 (RANDOM)`: Seed lands at random position within launch_power radius

### Mate Selection Criteria (for ADD_MATE_WEIGHT)
| Code | Criterion |
|------|-----------|
| 0x00 | Cell count (size) |
| 0x01 | Age |
| 0x02 | Energy level |
| 0x03 | Water level |
| 0x04 | Nutrient level |
| 0x05 | Distance (closer = higher) |
| 0x06 | Genome similarity |
| 0x07 | Genome difference |

---

## Directory Structure

```
PlantBrainGrid/
├── CMakeLists.txt              # C++ build configuration
├── pyproject.toml              # Python package configuration
├── setup.py                    # pybind11 build setup
├── CLAUDE.md                   # AI assistant guidance
├── ARCHITECTURE.md             # This file
├── README.md                   # User documentation
│
├── src/
│   ├── core/                   # C++ simulation core
│   │   ├── world.hpp/cpp       # Grid and world state
│   │   ├── plant.hpp/cpp       # Plant entity
│   │   ├── plant_cell.hpp/cpp  # Cell types and behavior
│   │   ├── brain.hpp/cpp       # VM implementation
│   │   ├── brain_ops.hpp/cpp   # Instruction implementations
│   │   ├── resources.hpp/cpp   # Resource flow logic
│   │   ├── reproduction.hpp/cpp# Seeds and genetics
│   │   ├── fire.hpp/cpp        # Fire spread mechanics
│   │   ├── seasons.hpp/cpp     # Light variation
│   │   ├── simulation.hpp/cpp  # Main simulation loop
│   │   ├── perlin.hpp/cpp      # Perlin noise generator
│   │   ├── serialization.hpp/cpp # Save/load state
│   │   └── config.hpp          # Configurable parameters
│   │
│   ├── bindings/               # pybind11 bindings
│   │   └── module.cpp          # Python module definition
│   │
│   └── python/                 # Python package
│       ├── __init__.py
│       ├── visualization.py    # raylib rendering
│       ├── ui.py               # UI controls
│       ├── brain_viewer.py     # Brain execution visualizer
│       └── main.py             # Entry point
│
├── tests/
│   ├── cpp/                    # C++ unit tests (Catch2)
│   │   ├── test_world.cpp
│   │   ├── test_plant.cpp
│   │   ├── test_brain.cpp
│   │   ├── test_resources.cpp
│   │   ├── test_reproduction.cpp
│   │   ├── test_fire.cpp
│   │   └── test_perlin.cpp
│   │
│   └── python/                 # Python tests (pytest)
│       ├── test_bindings.py
│       ├── test_integration.py
│       └── test_determinism.py
│
├── tools/
│   ├── brain_assembler.py      # Human-readable -> bytecode
│   └── genome_analyzer.py      # Analyze evolved genomes
│
└── examples/
    ├── simple_plant.brain      # Example brain bytecode
    └── test_scenario.json      # Example initial state
```

---

## State Serialization

For save/load and deterministic replay:

```cpp
struct SimulationState {
    uint64_t tick;
    uint64_t rng_seed;
    uint64_t rng_state;        // Current RNG position
    WorldState world;
    vector<PlantState> plants;
    vector<SeedState> seeds;
    SeasonState season;
};
```

All state must be serializable to binary format. Loading a state with the same RNG seed/state guarantees identical future simulation.

---

## Performance Considerations

1. **Grid Access**: Use flat array with `index = y * width + x`
2. **Plant Cell Lookup**: Each plant maintains cell list; world grid has back-pointers
3. **Brain Execution**: Limit instructions per tick (configurable, default 1000)
4. **Parallel Brain Execution**:
   - All plant brains execute independently in parallel
   - Each brain builds a queue of actions (placements, toggles, removals)
   - After all brains complete, action queues are merged
   - Conflicting placements (same tile) cancel out entirely
   - Non-conflicting actions applied atomically
5. **Memory**: Avoid allocations in hot loop; pre-allocate pools
6. **Visualization**: Only render visible tiles; use spatial culling

---

## Configuration Defaults

```cpp
struct Config {
    // World
    uint32_t world_width = 512;
    uint32_t world_height = 512;
    float water_perlin_scale = 0.02;
    float nutrient_perlin_scale = 0.015;

    // Plants
    uint16_t brain_size = 1024;
    uint8_t vision_radius = 16;
    uint16_t max_instructions_per_tick = 1000;

    // Brain error penalties (non-fatal)
    float oob_memory_penalty = 0.5;        // Energy cost per out-of-bounds access
    float instruction_limit_penalty = 5.0; // Energy cost for hitting instruction limit

    // Resources
    float xylem_transfer_cost = 0.05;  // 5% loss per hop
    float small_leaf_energy_rate = 1.0;
    float big_leaf_energy_rate = 3.0;
    float big_leaf_water_cost = 2.0;
    float big_leaf_nutrient_cost = 1.5;

    // Reproduction
    float mutation_rate = 0.01;        // Per byte
    uint8_t mutation_magnitude = 16;   // Max change per mutation
    float max_mate_distance = 100.0;

    // Fire
    uint16_t fire_spread_ticks = 5;
    uint16_t fire_destroy_ticks = 10;
    float fire_water_threshold = 50.0; // Won't spread to wet tiles

    // Seasons
    uint32_t season_length = 10000;    // Ticks per full cycle
    float base_light = 0.5;
    float light_amplitude = 0.4;
};
```
