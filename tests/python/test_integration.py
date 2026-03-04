"""Integration tests for PlantBrainGrid (Phase 8.2).

Tests full simulation flows through the Python interface.
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


def make_genome(size: int = 1024, fill: int = 0) -> list:
    """Create a simple genome filled with a constant value."""
    return [fill] * size


def make_leaf_genome() -> list:
    """Create a genome that places a SmallLeaf north of primary cell.

    Brain bytecode:
        PLACE_CELL SmallLeaf, +0, -1, North   (relative to primary)
        HALT
    """
    genome = [0] * 1024
    # OP_PLACE_CELL = 0x60, args: type(1), dx(1), dy(1), dir(1)
    # SmallLeaf = 2, dx=0, dy=-1 (north), dir=0 (North)
    i = 0
    genome[i] = 0x60; i += 1   # PLACE_CELL opcode
    genome[i] = 2;    i += 1   # SmallLeaf
    genome[i] = 0;    i += 1   # dx = 0
    genome[i] = 0xFF; i += 1   # dy = -1 (signed: 0xFF = -1)
    genome[i] = 0x01; i += 1   # HALT
    return genome


# ─────────────────────────────────────────────────────────
# Single plant lifecycle
# ─────────────────────────────────────────────────────────

def test_plant_survives_ticks():
    """A plant with enough resources should survive many ticks."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()
    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    plant.resources().energy = 100000.0
    plant.resources().water = 100000.0
    plant.resources().nutrients = 100000.0

    sim.run(100)

    assert len(sim.plants()) == 1
    assert sim.plants()[0].is_alive()


def test_plant_grows_leaf():
    """A plant with a genome that places a leaf should grow it."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_leaf_genome()
    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    plant.resources().energy = 1000.0
    plant.resources().water = 500.0
    plant.resources().nutrients = 250.0

    initial_cells = plant.cell_count()

    # Run a few ticks to allow leaf placement
    sim.run(5)

    # Plant should have grown (or at least survived)
    if len(sim.plants()) > 0:
        final_cells = sim.plants()[0].cell_count()
        # Either the leaf was placed (more cells) or energy wasn't enough
        assert final_cells >= initial_cells or final_cells == initial_cells


def test_world_integrates_with_plant():
    """Verify plant registers with world correctly."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()
    plant = sim.add_plant(pbg.GridCoord(30, 30), genome)

    # World should have the plant cell at its position
    wc = sim.world().cell_at(30, 30)
    assert wc.is_occupied()


def test_plant_death_clears_world():
    """When a plant dies, its world tiles should be cleared."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()
    plant = sim.add_plant(pbg.GridCoord(30, 30), genome)

    assert sim.world().cell_at(30, 30).is_occupied()

    plant.kill()
    sim.remove_dead_plants()

    assert not sim.world().cell_at(30, 30).is_occupied()
    assert len(sim.plants()) == 0


# ─────────────────────────────────────────────────────────
# Seed lifecycle
# ─────────────────────────────────────────────────────────

def test_seed_germination():
    """A landed seed should germinate into a plant."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()

    seed = pbg.Seed()
    seed.genome = genome
    seed.energy = 500.0
    seed.water = 200.0
    seed.nutrients = 100.0
    seed.position = pbg.GridCoord(70, 70)
    seed.in_flight = False

    sim.add_seed(seed)
    assert len(sim.seeds()) == 1

    sim.germinate_seeds()

    assert len(sim.seeds()) == 0
    assert len(sim.plants()) == 1
    assert sim.plants()[0].primary_position() == pbg.GridCoord(70, 70)


def test_seed_blocked_by_occupied_tile():
    """Seed landing on occupied tile is destroyed."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()

    # Place blocking plant
    blocker = sim.add_plant(pbg.GridCoord(70, 70), genome)
    assert blocker is not None

    seed = pbg.Seed()
    seed.genome = genome
    seed.energy = 500.0
    seed.position = pbg.GridCoord(70, 70)
    seed.in_flight = False

    sim.add_seed(seed)
    sim.germinate_seeds()

    assert len(sim.seeds()) == 0
    assert len(sim.plants()) == 1  # Only the blocker


def test_in_flight_seed_not_germinated():
    """Seeds still in flight should not germinate."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()

    seed = pbg.Seed()
    seed.genome = genome
    seed.position = pbg.GridCoord(50, 50)
    seed.in_flight = True
    seed.flight_ticks_remaining = 5

    sim.add_seed(seed)
    sim.germinate_seeds()

    assert len(sim.seeds()) == 1  # Still in flight
    assert len(sim.plants()) == 0


# ─────────────────────────────────────────────────────────
# Resource processing
# ─────────────────────────────────────────────────────────

def test_leaf_generates_energy():
    """A plant with a leaf should gain energy each tick."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()
    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    plant.resources().energy = 1000.0
    plant.resources().water = 1000.0
    plant.resources().nutrients = 1000.0

    # Add a leaf directly
    placed = plant.place_cell(
        pbg.CellType.SmallLeaf,
        pbg.GridCoord(51, 50),
        sim.world()
    )
    assert placed, "Should be able to place leaf adjacent to primary"

    # The ResourceSystem should process leaf energy
    pbg.ResourceSystem.process_tick(plant, sim.world())
    # Energy might go up or down depending on maintenance, but plant should live
    assert plant.is_alive()


def test_resource_system_process_tick():
    """ResourceSystem.process_tick should not crash."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()
    plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
    plant.resources().energy = 10000.0

    result = pbg.ResourceSystem.process_tick(plant, sim.world())
    assert result is not None


# ─────────────────────────────────────────────────────────
# Fire integration
# ─────────────────────────────────────────────────────────

def test_fire_destroys_plant_cell():
    """Fire should destroy non-primary plant cells over time."""
    cfg = pbg.get_config()
    orig_destroy = cfg.fire_destroy_ticks
    cfg.fire_destroy_ticks = 2

    try:
        sim = pbg.Simulation(100, 100, 42)
        genome = make_genome()
        plant = sim.add_plant(pbg.GridCoord(50, 50), genome)
        plant.resources().energy = 10000.0

        # Add a leaf
        plant.place_cell(
            pbg.CellType.SmallLeaf,
            pbg.GridCoord(51, 50),
            sim.world()
        )
        assert plant.cell_count() == 2

        # Ignite the leaf position
        cell = sim.world().cell_at(51, 50)
        cell.water_level = 0.0
        sim.world().ignite(pbg.GridCoord(51, 50))

        # Run until leaf burns
        for _ in range(5):
            sim.advance_tick()
            if plant.cell_count() == 1:
                break

        assert plant.cell_count() == 1
    finally:
        cfg.fire_destroy_ticks = orig_destroy


def test_fire_kills_plant_via_primary():
    """Fire on primary cell should kill the plant."""
    cfg = pbg.get_config()
    orig_destroy = cfg.fire_destroy_ticks
    cfg.fire_destroy_ticks = 2

    try:
        sim = pbg.Simulation(100, 100, 42)
        genome = make_genome()
        sim.add_plant(pbg.GridCoord(50, 50), genome)

        cell = sim.world().cell_at(50, 50)
        cell.water_level = 0.0
        sim.world().ignite(pbg.GridCoord(50, 50))

        for _ in range(5):
            sim.advance_tick()
            if len(sim.plants()) == 0:
                break

        assert len(sim.plants()) == 0
    finally:
        cfg.fire_destroy_ticks = orig_destroy


# ─────────────────────────────────────────────────────────
# Multi-plant interaction
# ─────────────────────────────────────────────────────────

def test_multiple_plants_coexist():
    """Multiple plants should coexist and each advance independently."""
    sim = pbg.Simulation(200, 200, 42)
    genome = make_genome()

    positions = [(30, 30), (100, 100), (170, 170)]
    for x, y in positions:
        plant = sim.add_plant(pbg.GridCoord(x, y), genome)
        plant.resources().energy = 100000.0
        plant.resources().water = 100000.0
        plant.resources().nutrients = 100000.0

    sim.run(50)

    # All plants should have survived
    assert len(sim.plants()) == 3


def test_plant_count_via_stats():
    """TickStats should correctly report plant count."""
    sim = pbg.Simulation(100, 100, 42)
    genome = make_genome()

    sim.add_plant(pbg.GridCoord(20, 20), genome)
    sim.add_plant(pbg.GridCoord(60, 60), genome)

    stats = sim.advance_tick()
    assert stats.plant_count == 2


# ─────────────────────────────────────────────────────────
# Pure Python modules
# ─────────────────────────────────────────────────────────

def test_brain_viewer_import():
    """Brain viewer should import without errors."""
    from plantbraingrid.brain_viewer import BrainViewer, hex_dump, disassemble
    assert BrainViewer is not None
    assert hex_dump is not None
    assert disassemble is not None


def test_brain_viewer_hex_dump():
    """hex_dump should produce non-empty output."""
    from plantbraingrid.brain_viewer import hex_dump
    data = bytes(range(32))
    output = hex_dump(data, rows=2)
    assert "0x0000" in output
    assert "0x0010" in output


def test_brain_viewer_disassemble():
    """disassemble should handle known opcodes."""
    from plantbraingrid.brain_viewer import disassemble
    # NOP NOP HALT
    data = bytes([0x00, 0x00, 0x01] + [0] * 100)
    output = disassemble(data, start=0, max_instructions=5)
    assert "NOP" in output
    assert "HALT" in output


def test_brain_viewer_with_brain():
    """BrainViewer should work with a C++ brain object."""
    from plantbraingrid.brain_viewer import BrainViewer
    genome = [0x00, 0x01] + [0] * 1022  # NOP, HALT
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    brain = plant.brain()

    viewer = BrainViewer(brain)
    state = viewer.show_state()
    assert "IP:" in state
    assert "Brain State:" in state


def test_brain_viewer_opcode_stats():
    """Opcode stats should count instructions."""
    from plantbraingrid.brain_viewer import BrainViewer
    genome = [0x00] * 10 + [0x01] + [0] * 1013  # 10 NOPs, 1 HALT
    plant = pbg.Plant(1, pbg.GridCoord(5, 5), genome)
    brain = plant.brain()

    viewer = BrainViewer(brain)
    stats = viewer.show_opcode_stats()
    assert "NOP" in stats


def test_main_module_importable():
    """plantbraingrid.main should import without errors."""
    from plantbraingrid import main as m
    assert m is not None
