# PlantBrainGrid Implementation Plan

## Phase 1: Foundation (C++ Core Infrastructure)

### 1.1 Project Setup
- [ ] Create CMakeLists.txt with C++17, pybind11, Catch2
- [ ] Create pyproject.toml and setup.py
- [ ] Set up directory structure
- [ ] Configure test harness

### 1.2 Basic Types and Config
- [ ] `src/core/config.hpp` - Configuration struct with defaults
- [ ] `src/core/types.hpp` - GridCoord, Direction, Vec2, CellType enum
- [ ] `src/core/perlin.hpp/cpp` - Perlin noise implementation
- **Test**: Verify Perlin produces smooth gradients, deterministic with seed

### 1.3 World Grid
- [ ] `src/core/world.hpp/cpp` - Grid allocation, cell access
- [ ] Water/nutrient initialization with Perlin noise
- [ ] Basic getters/setters for grid cells
- **Test**: Grid bounds, Perlin initialization, access patterns

---

## Phase 2: Plant Structure

### 2.1 Plant Cells
- [ ] `src/core/plant_cell.hpp/cpp` - CellType enum, PlantCell struct
- [ ] Cell placement validation (adjacency rules)
- [ ] Thorn blocking logic
- **Test**: Cell creation, placement rules, thorn behavior

### 2.2 Plant Entity
- [ ] `src/core/plant.hpp/cpp` - Plant struct with cell management
- [ ] Add/remove cells
- [ ] Primary cell tracking
- [ ] Death detection (primary cell destroyed)
- **Test**: Plant lifecycle, cell addition/removal, death

### 2.3 World-Plant Integration
- [ ] World tracks cell occupancy
- [ ] Collision detection for cell placement
- [ ] Query cells at position
- **Test**: Placement conflicts, occupancy tracking

---

## Phase 3: Brain VM

### 3.1 Brain Core
- [ ] `src/core/brain.hpp/cpp` - Memory, IP, execution state
- [ ] Basic execution loop (fetch, decode, execute)
- [ ] Instruction limit per tick
- [ ] IP bounds checking
- **Test**: Memory access, IP advancement, halt behavior

### 3.2 Control Flow Instructions
- [ ] NOP, HALT
- [ ] JUMP, JUMP_REL
- [ ] Conditional jumps (JUMP_IF_ZERO, JUMP_IF_NEQ)
- [ ] CALL/RET with stack
- **Test**: All jump types, stack behavior, edge cases

### 3.3 Memory Operations
- [ ] LOAD_IMM, COPY
- [ ] Arithmetic: ADD, SUB, MUL, DIV, MOD
- [ ] Bitwise: AND, OR, XOR, NOT, SHL, SHR
- [ ] Comparison: CMP_LT, CMP_EQ
- [ ] Indirect: LOAD_IND, STORE_IND
- **Test**: Each operation, overflow behavior, division by zero

### 3.4 World Sensing Instructions
- [ ] SENSE_WATER, SENSE_NUTRIENTS, SENSE_LIGHT
- [ ] SENSE_CELL, SENSE_FIRE, SENSE_OWNED
- [ ] SENSE_SELF_* (energy, water, nutrients)
- [ ] SENSE_CELL_COUNT, SENSE_AGE
- [ ] Vision radius enforcement
- **Test**: Sensing accuracy, out-of-range behavior, scaling

### 3.5 Plant Action Instructions
- [ ] PLACE_CELL - with resource cost checking
- [ ] SET_CELL_DIR - for xylem
- [ ] TOGGLE_CELL - enable/disable
- [ ] REMOVE_CELL
- **Test**: Placement validation, resource deduction, toggle state

---

## Phase 4: Resource System

### 4.1 Resource Flow
- [ ] `src/core/resources.hpp/cpp` - Flow calculation
- [ ] Root extraction (water/nutrients from ground)
- [ ] Leaf generation (energy from light)
- [ ] Xylem transfer with direction and cost
- [ ] Resource accumulation at primary cell
- **Test**: Flow correctness, cost calculation, edge cases

### 4.2 Cell Maintenance
- [ ] Per-tick maintenance costs
- [ ] Cell death from resource starvation (optional)
- [ ] BigLeaf high costs
- **Test**: Maintenance deduction, cost scaling

### 4.3 Integration
- [ ] Connect resource system to plant tick
- [ ] Ensure brain can read resource levels
- **Test**: Full resource cycle per tick

---

## Phase 5: Reproduction

### 5.1 Seed Structure
- [ ] `src/core/reproduction.hpp/cpp` - Seed struct
- [ ] Seed creation with resources
- [ ] Launch physics (distance from energy)
- [ ] Landing and germination
- **Test**: Seed creation, trajectory, germination

### 5.2 Mate Selection
- [ ] Mate search initiation
- [ ] Criteria weighting system
- [ ] Best match calculation
- [ ] Distance filtering
- **Test**: Selection correctness, weight application

### 5.3 Genetic Recombination
- [ ] MOTHER_ONLY, FATHER_ONLY
- [ ] RANDOM_MIX
- [ ] ALTERNATING
- [ ] Mutation application
- **Test**: Each recombination method, mutation rates

### 5.4 Reproduction Instructions
- [ ] START_MATE_SEARCH, ADD_MATE_WEIGHT, FINISH_MATE_SELECT
- [ ] SET_RECOMB_METHOD
- [ ] ALLOC_SEED_*, SET_LAUNCH_*
- [ ] LAUNCH_SEED
- **Test**: Full reproduction flow via brain

---

## Phase 6: Fire and Seasons

### 6.1 Fire System
- [ ] `src/core/fire.hpp/cpp` - Fire state and spread
- [ ] FireStarter cell ignition
- [ ] Tick-based spread to adjacent cells
- [ ] Cell destruction
- [ ] FireproofXylem immunity
- [ ] Water threshold blocking spread
- **Test**: Spread timing, immunity, thresholds

### 6.2 Season System
- [ ] `src/core/seasons.hpp/cpp` - Light calculation
- [ ] Sinusoidal variation
- [ ] Integration with leaf energy production
- **Test**: Light values over time, energy impact

---

## Phase 7: Simulation Loop

### 7.1 Main Loop
- [ ] `src/core/simulation.hpp/cpp` - Tick orchestration
- [ ] Order: seasons → resources → brains → fire → seeds
- [ ] Plant death processing
- [ ] New plant creation from seeds
- **Test**: Tick ordering, state consistency

### 7.2 Determinism
- [ ] Seeded RNG for all random operations
- [ ] Consistent iteration order
- [ ] No undefined behavior
- **Test**: Same seed produces identical simulation

### 7.3 Serialization
- [ ] `src/core/serialization.hpp/cpp` - Binary save/load
- [ ] Complete state capture
- [ ] Version field for compatibility
- **Test**: Round-trip save/load, determinism after load

---

## Phase 8: Python Bindings

### 8.1 Core Bindings
- [ ] `src/bindings/module.cpp` - pybind11 setup
- [ ] World class binding
- [ ] Plant class binding
- [ ] Config binding
- **Test**: Python can create and manipulate core objects

### 8.2 Simulation Bindings
- [ ] Tick execution from Python
- [ ] State inspection
- [ ] Save/load from Python
- **Test**: Python controls simulation correctly

### 8.3 Brain Inspection
- [ ] Memory read access
- [ ] Execution history access
- [ ] IP tracking
- **Test**: Python can inspect brain state

---

## Phase 9: Visualization

### 9.1 Basic Rendering
- [ ] `src/python/visualization.py` - raylib setup
- [ ] Grid rendering with zoom/pan
- [ ] Cell type coloring
- [ ] Water/nutrient overlay option
- **Test**: Manual visual verification

### 9.2 UI Controls
- [ ] `src/python/ui.py` - Control panel
- [ ] Play/pause/step
- [ ] Speed control
- [ ] Save/load buttons
- **Test**: UI responsiveness

### 9.3 Plant Selection
- [ ] Click to select plant
- [ ] Highlight selected plant's cells
- [ ] Resource display panel
- **Test**: Selection accuracy

### 9.4 Brain Viewer
- [ ] `src/python/brain_viewer.py` - Brain visualization
- [ ] Memory hex dump
- [ ] Current IP highlight
- [ ] Execution trace
- [ ] Path mapping visualization
- **Test**: Accuracy against known brain states

---

## Phase 10: Polish and Tools

### 10.1 Brain Assembler
- [ ] `tools/brain_assembler.py` - Human-readable → bytecode
- [ ] Label support
- [ ] Macro system
- **Test**: Assemble and execute test programs

### 10.2 Genome Analyzer
- [ ] `tools/genome_analyzer.py` - Analyze evolved plants
- [ ] Common patterns detection
- [ ] Fitness correlation
- **Test**: Analysis correctness

### 10.3 Performance Optimization
- [ ] Profile hot paths
- [ ] Optimize grid access patterns
- [ ] Consider parallel brain execution
- **Test**: Benchmark before/after

---

## Testing Strategy

### Unit Tests (Catch2 for C++, pytest for Python)

Each component has dedicated tests before integration:

```
tests/cpp/
├── test_perlin.cpp      # Noise generation, determinism
├── test_world.cpp       # Grid operations, initialization
├── test_plant_cell.cpp  # Cell types, placement rules
├── test_plant.cpp       # Plant lifecycle
├── test_brain.cpp       # VM execution
├── test_brain_ops.cpp   # Each instruction
├── test_resources.cpp   # Flow calculations
├── test_reproduction.cpp# Genetics, seeds
├── test_fire.cpp        # Spread mechanics
├── test_seasons.cpp     # Light calculation
├── test_simulation.cpp  # Tick behavior
└── test_serialization.cpp# Save/load

tests/python/
├── test_bindings.py     # C++ → Python interface
├── test_integration.py  # Full simulation flows
└── test_determinism.py  # Reproducibility
```

### Integration Tests

1. **Single Plant Lifecycle**: Create plant, run N ticks, verify growth
2. **Two Plant Interaction**: Plants compete for resources
3. **Reproduction Cycle**: Plant creates seed, seed germinates
4. **Fire Spread**: Verify spread timing and destruction
5. **Season Impact**: Verify energy production varies
6. **Save/Load Determinism**: Save, load, compare to continued original

### Test-Driven Development

For each component:
1. Write failing test for expected behavior
2. Implement minimum code to pass
3. Refactor while keeping tests green
4. Add edge case tests

### Continuous Testing

```bash
# Run C++ tests
cd build && ctest --output-on-failure

# Run Python tests
pytest tests/python/ -v

# Run all tests
make test
```

---

## Build Commands

```bash
# Initial setup
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install Python package
pip install -e .

# Run tests
ctest                    # C++ tests
pytest                   # Python tests

# Run simulation
python -m plantbraingrid
```
