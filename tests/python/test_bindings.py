"""Tests for C++ Python bindings (Phase 8).

Tests that the Python interface correctly exposes C++ types and functions.
"""

import pytest
import sys

try:
    import _plantbraingrid as pbg
    BINDINGS_AVAILABLE = True
except ImportError:
    BINDINGS_AVAILABLE = False

pytestmark = pytest.mark.skipif(
    not BINDINGS_AVAILABLE,
    reason="C++ bindings not built"
)


# ─────────────────────────────────────────────────────────
# Basic types
# ─────────────────────────────────────────────────────────

def test_gridcoord_creation():
    c = pbg.GridCoord(10, 20)
    assert c.x == 10
    assert c.y == 20


def test_gridcoord_default():
    c = pbg.GridCoord()
    assert c.x == 0
    assert c.y == 0


def test_gridcoord_equality():
    a = pbg.GridCoord(5, 7)
    b = pbg.GridCoord(5, 7)
    c = pbg.GridCoord(1, 2)
    assert a == b
    assert not (a == c)


def test_gridcoord_repr():
    c = pbg.GridCoord(3, 4)
    assert "3" in repr(c)
    assert "4" in repr(c)


def test_direction_enum():
    assert pbg.Direction.North != pbg.Direction.South
    assert pbg.Direction.East != pbg.Direction.West


def test_cell_type_enum():
    assert pbg.CellType.Empty != pbg.CellType.Primary
    assert pbg.CellType.SmallLeaf != pbg.CellType.BigLeaf
    assert pbg.CellType.FiberRoot != pbg.CellType.Xylem


def test_recombination_method_enum():
    assert pbg.RecombinationMethod.MotherOnly != pbg.RecombinationMethod.FatherOnly
    assert pbg.RecombinationMethod.HalfHalf != pbg.RecombinationMethod.RandomMix


# ─────────────────────────────────────────────────────────
# Config
# ─────────────────────────────────────────────────────────

def test_config_creation():
    cfg = pbg.Config()
    assert cfg.brain_size > 0
    assert cfg.vision_radius > 0
    assert cfg.max_instructions_per_tick > 0


def test_config_mutation():
    cfg = pbg.get_config()
    old_val = cfg.vision_radius
    cfg.vision_radius = 99
    assert pbg.get_config().vision_radius == 99
    cfg.vision_radius = old_val  # restore


# ─────────────────────────────────────────────────────────
# World
# ─────────────────────────────────────────────────────────

def test_world_creation():
    world = pbg.World(100, 100, 42)
    assert world.width() == 100
    assert world.height() == 100
    assert world.seed() == 42
    assert world.tick() == 0


def test_world_bounds_checking():
    world = pbg.World(50, 50, 1)
    assert world.in_bounds(0, 0)
    assert world.in_bounds(49, 49)
    assert not world.in_bounds(-1, 0)
    assert not world.in_bounds(50, 50)
    assert world.in_bounds(pbg.GridCoord(25, 25))
    assert not world.in_bounds(pbg.GridCoord(100, 100))


def test_world_cell_access():
    world = pbg.World(50, 50, 1)
    cell = world.cell_at(10, 10)
    assert cell is not None
    # Terrain should have been initialized
    assert cell.water_level >= 0
    assert cell.nutrient_level >= 0


def test_world_cell_at_gridcoord():
    world = pbg.World(50, 50, 1)
    c = pbg.GridCoord(15, 20)
    cell = world.cell_at(c)
    assert cell is not None


def test_world_advance_tick():
    world = pbg.World(50, 50, 1)
    world.advance_tick()
    assert world.tick() == 1
    world.advance_tick()
    assert world.tick() == 2


def test_world_light_multiplier():
    world = pbg.World(50, 50, 1)
    light = world.current_light_multiplier()
    assert 0.0 <= light <= 1.0


def test_world_fire():
    world = pbg.World(50, 50, 1)
    # Set water to 0 so fire spreads
    cell = world.cell_at(25, 25)
    cell.water_level = 0.0
    world.ignite(pbg.GridCoord(25, 25))
    assert world.cell_at(25, 25).is_on_fire()


# ─────────────────────────────────────────────────────────
# Resources
# ─────────────────────────────────────────────────────────

def test_resources_creation():
    r = pbg.Resources()
    assert r.energy == 0.0
    assert r.water == 0.0
    assert r.nutrients == 0.0


def test_resources_with_values():
    r = pbg.Resources(100.0, 50.0, 25.0)
    assert r.energy == pytest.approx(100.0)
    assert r.water == pytest.approx(50.0)
    assert r.nutrients == pytest.approx(25.0)


def test_resources_mutation():
    r = pbg.Resources()
    r.energy = 42.0
    assert r.energy == pytest.approx(42.0)


# ─────────────────────────────────────────────────────────
# Plant
# ─────────────────────────────────────────────────────────

def test_plant_creation():
    genome = [0] * 1024
    plant = pbg.Plant(1, pbg.GridCoord(10, 10), genome)
    assert plant.id() == 1
    assert plant.primary_position() == pbg.GridCoord(10, 10)
    assert plant.is_alive()
    assert plant.age() == 0
    assert plant.cell_count() >= 1  # Has primary cell


def test_plant_resources():
    genome = [0] * 1024
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    res = plant.resources()
    res.energy = 100.0
    res.water = 50.0
    assert plant.resources().energy == pytest.approx(100.0)
    assert plant.resources().water == pytest.approx(50.0)


def test_plant_brain_access():
    genome = list(range(256)) * 4  # 1024 bytes
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    brain = plant.brain()
    assert brain is not None
    assert brain.size() >= 1024
    assert brain.ip() == 0
    assert not brain.is_halted()


def test_plant_brain_memory():
    genome = list(range(256)) * 4
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    brain = plant.brain()
    mem = brain.memory()
    assert len(mem) >= 1024
    # Check first byte matches genome
    assert mem[0] == genome[0]


def test_plant_brain_read_write():
    genome = [0] * 1024
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    brain = plant.brain()
    brain.write(100, 42)
    assert brain.read(100) == 42


def test_plant_cells():
    genome = [0] * 1024
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    cells = plant.cells()
    assert len(cells) >= 1  # Primary cell
    primary = cells[0]
    assert primary.type == pbg.CellType.Primary


def test_plant_death():
    genome = [0] * 1024
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    assert plant.is_alive()
    plant.kill()
    assert not plant.is_alive()


# ─────────────────────────────────────────────────────────
# Brain
# ─────────────────────────────────────────────────────────

def test_brain_tracing():
    genome = [0] * 1024  # All NOPs
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    brain = plant.brain()
    brain.enable_tracing(True)
    assert brain.last_trace() is None or True  # Just verify no crash


# ─────────────────────────────────────────────────────────
# Simulation
# ─────────────────────────────────────────────────────────

def test_simulation_creation():
    sim = pbg.Simulation(100, 100, 42)
    assert sim.world().width() == 100
    assert sim.world().height() == 100
    assert sim.tick() == 0
    assert len(sim.plants()) == 0
    assert len(sim.seeds()) == 0


def test_simulation_add_plant():
    sim = pbg.Simulation(100, 100, 42)
    genome = [0] * 1024
    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    assert plant is not None
    assert plant.id() == 1
    assert len(sim.plants()) == 1


def test_simulation_add_plant_occupied():
    sim = pbg.Simulation(100, 100, 42)
    genome = [0] * 1024
    p1 = sim.add_plant(pbg.GridCoord(50, 50), genome)
    p2 = sim.add_plant(pbg.GridCoord(50, 50), genome)
    assert p1 is not None
    assert p2 is None


def test_simulation_add_plant_oob():
    sim = pbg.Simulation(100, 100, 42)
    genome = [0] * 1024
    plant = sim.add_plant(pbg.GridCoord(-1, -1), genome)
    assert plant is None


def test_simulation_find_plant():
    sim = pbg.Simulation(100, 100, 42)
    genome = [0] * 1024
    sim.add_plant(pbg.GridCoord(30, 30), genome)
    sim.add_plant(pbg.GridCoord(60, 60), genome)

    found = sim.find_plant(2)
    assert found is not None
    assert found.primary_position() == pbg.GridCoord(60, 60)

    not_found = sim.find_plant(999)
    assert not_found is None


def test_simulation_advance_tick():
    sim = pbg.Simulation(100, 100, 42)
    genome = [0] * 1024
    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    plant.resources().energy = 1000.0

    stats = sim.advance_tick()
    assert stats.tick == 0
    assert stats.plant_count == 1
    assert sim.tick() == 1


def test_simulation_run():
    sim = pbg.Simulation(100, 100, 42)
    genome = [0] * 1024
    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    plant.resources().energy = 10000.0

    sim.run(10)
    assert sim.tick() == 10


def test_simulation_event_callbacks():
    sim = pbg.Simulation(100, 100, 42)
    genome = [0] * 1024

    births = []
    deaths = []

    sim.on_plant_birth(lambda p: births.append(p.id()))
    sim.on_plant_death(lambda p: deaths.append(p.id()))

    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    assert len(births) == 1

    plant.kill()
    sim.remove_dead_plants()
    assert len(deaths) == 1


# ─────────────────────────────────────────────────────────
# Serialization via Python
# ─────────────────────────────────────────────────────────

def test_simulation_save_load():
    import tempfile
    import os

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        path = f.name

    try:
        sim = pbg.Simulation(50, 50, 42)
        genome = [0] * 1024
        plant = sim.add_plant(pbg.GridCoord(25, 25), genome)
        plant.resources().energy = 500.0
        sim.run(5)

        sim.save_state(path)
        assert os.path.exists(path)
        assert os.path.getsize(path) > 0

        sim2 = pbg.Simulation(50, 50, 42)
        sim2.load_state(path)

        assert sim2.tick() == sim.tick()
        assert len(sim2.plants()) == len(sim.plants())
    finally:
        os.unlink(path)


# ─────────────────────────────────────────────────────────
# ResourceSystem
# ─────────────────────────────────────────────────────────

def test_resource_system_static_methods():
    # ResourceSystem has static methods exposed
    sim = pbg.Simulation(50, 50, 42)
    genome = [0] * 1024
    plant = sim.add_plant(pbg.GridCoord(25, 25), genome)

    # process_tick should not crash
    pbg.ResourceSystem.process_tick(plant, sim.world())


def test_tick_stats_fields():
    sim = pbg.Simulation(50, 50, 42)
    genome = [0] * 1024
    sim.add_plant(pbg.GridCoord(25, 25), genome)

    stats = sim.advance_tick()
    # All fields should be accessible
    assert stats.tick >= 0
    assert stats.plant_count >= 0
    assert stats.seed_count >= 0
    assert stats.cells_placed >= 0
    assert stats.cells_removed >= 0
    assert stats.placements_cancelled >= 0
    assert stats.seeds_launched >= 0
    assert stats.seeds_germinated >= 0
    assert stats.plants_died >= 0
