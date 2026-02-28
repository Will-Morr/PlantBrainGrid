"""
single_reproducer.py — start the simulation with one reproducer plant and watch it spread.

Usage:
    PYTHONPATH=src/python python examples/single_reproducer.py
    PYTHONPATH=src/python python examples/single_reproducer.py --seed 7 --ticks 5000
    PYTHONPATH=src/python python examples/single_reproducer.py --headless

Controls (visual mode):
    Arrow keys / WASD  — pan
    Mouse wheel / +/-  — zoom
    Space              — pause / unpause
    . / ,              — double / halve simulation speed
    1                  — toggle water overlay
    2                  — toggle nutrient overlay
    Click              — select plant (shows energy/water/nutrients)
    Escape             — deselect
"""

import argparse
import os
import sys

# Allow running from project root without pip-installing the package
_root = os.path.join(os.path.dirname(__file__), '..')
sys.path.insert(0, os.path.join(_root, 'src', 'python'))
sys.path.insert(0, _root)  # _plantbraingrid.so lives here

from _plantbraingrid import Simulation, GridCoord


def _find_latest_backup(backup_dir="sim_backup"):
    """Return the path to the most recent backup file, or None if none exist."""
    if not os.path.isdir(backup_dir):
        return None
    entries = sorted(
        (e for e in os.listdir(backup_dir) if not e.startswith('.')),
        reverse=True,
    )
    return os.path.join(backup_dir, entries[0]) if entries else None


def _build_sim(width, height, seed):
    """Create the simulation and seed it with one reproducer plant at the centre.

    If a backup exists in sim_backup/, the most recent one is loaded instead of
    starting fresh. Returns (sim, genome_path, genome_len, resumed_from) where
    resumed_from is the backup path (str) or None.
    """
    genome_path = os.path.join(os.path.dirname(__file__), 'reproducer.bin')
    genome = list(open(genome_path, 'rb').read())

    backup = _find_latest_backup()
    if backup is not None:
        sim = Simulation(width, height, seed)
        sim.load_state(backup)
        print(f"Resumed from backup: {backup}")
        return sim, genome_path, len(genome), backup

    sim = Simulation(width, height, seed)

    cx, cy = width // 2, height // 2
    plant = sim.add_plant(GridCoord(cx, cy), genome)
    if plant is None:
        print("Failed to place initial plant — position occupied?")
        sys.exit(1)

    plant.resources().energy    = 200.0
    plant.resources().water     = 100.0
    plant.resources().nutrients = 100.0

    return sim, genome_path, len(genome), None


def run_headless(width, height, seed, ticks):
    sim, genome_path, genome_len, resumed_from = _build_sim(width, height, seed)
    cx, cy = width // 2, height // 2

    print(f"World:  {width}x{height}  seed={seed}")
    print(f"Genome: {genome_path}  ({genome_len} bytes)")
    if resumed_from:
        print(f"Resumed from: {resumed_from}  tick={sim.tick()}")
    else:
        print(f"Start:  1 plant at ({cx}, {cy})")
    print()

    report_every = max(1, ticks // 20)

    for tick in range(ticks):
        stats = sim.advance_tick()

        if tick % report_every == 0 or tick == ticks - 1:
            print(
                f"  tick {stats.tick:>6}  "
                f"plants={stats.plant_count:>4}  "
                f"seeds={stats.seed_count:>3}  "
                f"placed={stats.cells_placed:>3}  "
                f"died={stats.plants_died:>2}"
            )

    final = len(sim.plants())
    print(f"Done — {final} plant{'s' if final != 1 else ''} alive after {ticks} ticks.")


def run_visual(width, height, seed):
    try:
        import pyray as rl
        from plantbraingrid.visualization import Visualizer
    except ImportError:
        print("raylib not available (pip install raylib). Falling back to headless.")
        run_headless(width, height, seed, ticks=5000)
        return

    sim, genome_path, genome_len, resumed_from = _build_sim(width, height, seed)
    cx, cy = width // 2, height // 2

    print(f"World:  {width}x{height}  seed={seed}")
    print(f"Genome: {genome_path}  ({genome_len} bytes)")
    if resumed_from:
        print(f"Resumed from: {resumed_from}  tick={sim.tick()}")
    else:
        print(f"Start:  1 plant at ({cx}, {cy})")
    print("Controls: WASD/arrows=pan  scroll=zoom  space=pause  ./,=speed  1=water  2=nutrients  click=select")
    print()

    vis = Visualizer(1280, 720, "PlantBrainGrid — single reproducer")
    vis.initialize()

    # Centre view on the starting plant.
    # screen = (world - camera) * zoom * cell_size  →  camera = world - screen/(zoom*cell_size)
    vis.camera.zoom = 1.0
    scale = vis.camera.zoom * vis.camera.cell_size
    vis.camera.x = cx - vis.width  / (2 * scale)
    vis.camera.y = cy - vis.height / (2 * scale)

    ticks_per_frame = 1
    report_every = 1
    tick = -1

    backup_every_nth = 1000
    os.makedirs("sim_backup", exist_ok=True)

    while not vis.should_close():
        vis.handle_input()

        if rl.is_mouse_button_pressed(rl.MOUSE_BUTTON_LEFT):
            mx, my = rl.get_mouse_x(), rl.get_mouse_y()
            if my > 25:  # below status bar
                vis.select_plant_at(mx, my, list(sim.plants()))

        if rl.is_key_pressed(rl.KEY_PERIOD):
            ticks_per_frame = min(100, ticks_per_frame * 2)
        if rl.is_key_pressed(rl.KEY_COMMA):
            ticks_per_frame = max(1, ticks_per_frame // 2)

        if not vis.paused:
            for _ in range(ticks_per_frame):
                stats = sim.advance_tick()

                tick += 1
                if tick % report_every == 0:
                    print(
                        f"  tick {stats.tick:>6}  "
                        f"plants={stats.plant_count:>4}  "
                        f"seeds={stats.seed_count:>3}  "
                        f"placed={stats.cells_placed:>3}  "
                        f"died={stats.plants_died:>2}  "
                    )
                
                if tick > 0 and tick%backup_every_nth == 0:
                    sim.save_state(f"sim_backup/{tick:016d}")

        vis.render_world(sim.world(), list(sim.plants()))

    vis.close()
    print(f"Closed — {len(sim.plants())} plants alive at tick {sim.tick()}.")


def main():
    parser = argparse.ArgumentParser(description="Single-reproducer bootstrap example")
    parser.add_argument('--width',    type=int, default=64)
    parser.add_argument('--height',   type=int, default=64)
    parser.add_argument('--seed',     type=int, default=42)
    parser.add_argument('--ticks',    type=int, default=3000,
                        help='Ticks to run in headless mode (default: 3000)')
    parser.add_argument('--headless', action='store_true',
                        help='Run without visualization even if raylib is available')
    args = parser.parse_args()

    if args.headless:
        run_headless(args.width, args.height, args.seed, args.ticks)
    else:
        run_visual(args.width, args.height, args.seed)


if __name__ == '__main__':
    main()
