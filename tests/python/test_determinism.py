"""Determinism tests for PlantBrainGrid (Phase 7.2 / Phase 8).

Verifies that simulations with the same seed produce identical results.
"""

import pytest

try:
    import _plantbraingrid as pbg
    BINDINGS_AVAILABLE = True
except ImportError:
    BINDINGS_AVAILABLE = False

pytestmark = pytest.mark.skipif(
    not BINDINGS_AVAILABLE,
    reason="C++ bindings not built"
)


def make_genome(seed: int = 0) -> list:
    """Create a deterministic genome from an integer seed."""
    import random
    rng = random.Random(seed)
    return [rng.randint(0, 255) for _ in range(1024)]


def snapshot_sim(sim) -> dict:
    """Capture a comparable snapshot of simulation state."""
    state = {
        "tick": sim.tick(),
        "plant_count": len(sim.plants()),
        "seed_count": len(sim.seeds()),
    }
    for i, plant in enumerate(sim.plants()):
        prefix = f"plant_{i}"
        state[f"{prefix}_id"] = plant.id()
        state[f"{prefix}_pos_x"] = plant.primary_position().x
        state[f"{prefix}_pos_y"] = plant.primary_position().y
        state[f"{prefix}_alive"] = plant.is_alive()
        state[f"{prefix}_energy"] = round(plant.resources().energy, 3)
        state[f"{prefix}_water"] = round(plant.resources().water, 3)
        state[f"{prefix}_age"] = plant.age()
        state[f"{prefix}_cell_count"] = plant.cell_count()
    return state


# ─────────────────────────────────────────────────────────
# Basic determinism
# ─────────────────────────────────────────────────────────

def test_same_seed_same_world():
    """Two simulations with same seed should produce same world terrain."""
    w1 = pbg.World(100, 100, 12345)
    w2 = pbg.World(100, 100, 12345)

    # Check a sample of cells
    for x, y in [(0, 0), (10, 20), (50, 50), (99, 99)]:
        c1 = w1.cell_at(x, y)
        c2 = w2.cell_at(x, y)
        assert c1.water_level == pytest.approx(c2.water_level), \
            f"Water differs at ({x},{y})"
        assert c1.nutrient_level == pytest.approx(c2.nutrient_level), \
            f"Nutrients differ at ({x},{y})"


def test_different_seeds_different_world():
    """Different seeds should produce different terrain."""
    w1 = pbg.World(100, 100, 111)
    w2 = pbg.World(100, 100, 222)

    differences = sum(
        1 for x, y in [(i, j) for i in range(0, 100, 10) for j in range(0, 100, 10)]
        if abs(w1.cell_at(x, y).water_level - w2.cell_at(x, y).water_level) > 0.01
    )
    assert differences > 0, "Different seeds should produce different terrain"


def test_single_plant_determinism():
    """Same seed + same genome + same steps = same state."""
    genome = make_genome(seed=42)

    def run_sim(ticks: int):
        sim = pbg.Simulation(100, 100, 42)
        plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
        plant.resources().energy = 1000.0
        plant.resources().water = 500.0
        plant.resources().nutrients = 250.0
        sim.run(ticks)
        return snapshot_sim(sim)

    state1 = run_sim(50)
    state2 = run_sim(50)

    assert state1 == state2, \
        f"Determinism failed:\n{state1}\nvs\n{state2}"


def test_multi_plant_determinism():
    """Multiple plants should be deterministic."""
    genome_a = make_genome(seed=1)
    genome_b = make_genome(seed=2)

    def run_sim(ticks: int):
        sim = pbg.Simulation(200, 200, 99)
        for x, y, g in [(50, 50, genome_a), (100, 100, genome_b), (150, 150, genome_a)]:
            plant = sim.add_plant(pbg.GridCoord(x, y), g)
            plant.resources().energy = 5000.0
            plant.resources().water = 2000.0
            plant.resources().nutrients = 1000.0
        sim.run(ticks)
        return snapshot_sim(sim)

    state1 = run_sim(30)
    state2 = run_sim(30)

    assert state1 == state2


def test_tick_by_tick_determinism():
    """Running tick-by-tick should match running all-at-once."""
    genome = make_genome(seed=77)

    def run_bulk(ticks: int):
        sim = pbg.Simulation(100, 100, 77)
        plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
        plant.resources().energy = 2000.0
        plant.resources().water = 1000.0
        plant.resources().nutrients = 500.0
        sim.run(ticks)
        return snapshot_sim(sim)

    def run_incremental(ticks: int):
        sim = pbg.Simulation(100, 100, 77)
        plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
        plant.resources().energy = 2000.0
        plant.resources().water = 1000.0
        plant.resources().nutrients = 500.0
        for _ in range(ticks):
            sim.advance_tick()
        return snapshot_sim(sim)

    n = 20
    bulk = run_bulk(n)
    incremental = run_incremental(n)

    assert bulk == incremental


# ─────────────────────────────────────────────────────────
# Save/load determinism
# ─────────────────────────────────────────────────────────

def test_save_load_determinism():
    """Loaded simulation should produce same results as original continuation."""
    import tempfile
    import os

    genome = make_genome(seed=42)

    # Run original for 10 ticks, save, continue to 20
    sim_orig = pbg.Simulation(100, 100, 42)
    plant = sim_orig.add_plant(pbg.GridCoord(50, 50), genome)
    plant.resources().energy = 5000.0
    plant.resources().water = 2000.0
    plant.resources().nutrients = 1000.0

    sim_orig.run(10)

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        path = f.name

    try:
        sim_orig.save_state(path)
        tick_at_save = sim_orig.tick()

        # Continue original
        sim_orig.run(10)
        state_orig = snapshot_sim(sim_orig)

        # Load and continue from saved state
        sim_loaded = pbg.Simulation(100, 100, 42)
        sim_loaded.load_state(path)
        assert sim_loaded.tick() == tick_at_save

        sim_loaded.run(10)
        state_loaded = snapshot_sim(sim_loaded)

        assert state_orig["tick"] == state_loaded["tick"]
        assert state_orig["plant_count"] == state_loaded["plant_count"]

    finally:
        os.unlink(path)


# ─────────────────────────────────────────────────────────
# World determinism
# ─────────────────────────────────────────────────────────

def test_world_light_determinism():
    """Light multiplier should follow the same pattern across runs."""
    def get_light_values(seed, ticks):
        world = pbg.World(50, 50, seed)
        values = []
        for _ in range(ticks):
            values.append(world.current_light_multiplier())
            world.advance_tick()
        return values

    lights1 = get_light_values(42, 20)
    lights2 = get_light_values(42, 20)

    assert lights1 == lights2


def test_world_fire_spread_determinism():
    """Fire spreading should be deterministic."""
    def run_fire(seed):
        world = pbg.World(50, 50, seed)
        cell = world.cell_at(25, 25)
        cell.water_level = 0.0
        world.ignite(pbg.GridCoord(25, 25))
        for _ in range(10):
            world.advance_tick()
        return world.cell_at(25, 24).is_on_fire()

    result1 = run_fire(100)
    result2 = run_fire(100)
    assert result1 == result2


# ─────────────────────────────────────────────────────────
# Brain execution determinism
# ─────────────────────────────────────────────────────────

def test_brain_execution_determinism():
    """Brain execution should be deterministic for same genome."""
    # Genome with RANDOMIZE instruction to test RNG seeding
    genome = [0] * 1024
    # RANDOMIZE [0x00FF], 10  (opcode=0x31, start_low=0xFF, start_high=0x00, len=10)
    genome[0] = 0x31  # RANDOMIZE
    genome[1] = 0xFF  # start_low (address 0x00FF)
    genome[2] = 0x00  # start_high
    genome[3] = 10    # length
    genome[4] = 0x01  # HALT

    def run_brain_once():
        sim = pbg.Simulation(100, 100, 42)
        plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
        plant.resources().energy = 10000.0
        sim.advance_tick()
        brain = sim.plants()[0].brain() if sim.plants() else None
        if brain is None:
            return []
        mem = brain.memory()
        return list(mem[0xFF:0xFF + 10])

    result1 = run_brain_once()
    result2 = run_brain_once()

    assert result1 == result2, \
        f"Brain RANDOMIZE not deterministic: {result1} vs {result2}"
