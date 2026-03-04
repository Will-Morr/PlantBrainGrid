"""
single_reproducer.py — start the simulation with random plants and watch them evolve.

Usage:
    PYTHONPATH=src/python python examples/single_reproducer.py
    PYTHONPATH=src/python python examples/single_reproducer.py --seed 7 --ticks 5000
    PYTHONPATH=src/python python examples/single_reproducer.py --headless
    PYTHONPATH=src/python python examples/single_reproducer.py --backup-every-nth 5000 --ticks-per-frame 100

Controls (visual mode):
    Arrow keys / WASD  — pan
    Mouse wheel / +/-  — zoom
    Space              — pause / unpause
    . / ,              — double / halve simulation speed
    1                  — toggle water overlay
    2                  — toggle nutrient overlay
    M                  — toggle brain memory hex dump (selected plant)
    N                  — advance exactly one tick (works while paused)
    F                  — toggle fullscreen
    Click              — select plant (shows energy/water/nutrients)
    Escape             — deselect
"""

import argparse
import os
import random
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
    """Create the simulation and seed it with 20 random-genome plants at the centre.

    If a backup exists in sim_backup/, the most recent one is loaded instead of
    starting fresh. Returns (sim, n_plants, resumed_from) where resumed_from is
    the backup path (str) or None.
    """
    backup = _find_latest_backup()
    if backup is not None:
        sim = Simulation(width, height, seed)
        sim.load_state(backup)
        print(f"Resumed from backup: {backup}")
        return sim, 0, backup

    rng = random.Random(seed)
    sim = Simulation(width, height, seed)

    cx, cy = width // 2, height // 2
    n_placed = 0
    for i in range(-10, 10):
        genome = [rng.randint(0, 255) for _ in range(1024)]
        plant = sim.add_plant(GridCoord(cx + i * 3, cy), genome)
        if plant is None:
            continue
        plant.resources().energy    = 20000.0
        plant.resources().water     = 1000.0
        plant.resources().nutrients = 1000.0
        n_placed += 1

    return sim, n_placed, None


def run_headless(width, height, seed, ticks, backup_every_nth=10000):
    sim, n_plants, resumed_from = _build_sim(width, height, seed)
    cx, cy = width // 2, height // 2

    print(f"World:  {width}x{height}  seed={seed}")
    if resumed_from:
        print(f"Resumed from: {resumed_from}  tick={sim.tick()}")
    else:
        print(f"Start:  {n_plants} random plants at centre")
    print()

    report_every = max(1, ticks // 20)
    tick_offset = sim.tick()

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

        abs_tick = tick_offset + tick + 1
        if backup_every_nth > 0 and abs_tick % backup_every_nth == 0:
            os.makedirs("sim_backup", exist_ok=True)
            sim.save_state(f"sim_backup/{abs_tick:016d}")

    final = len(sim.plants())
    print(f"Done — {final} plant{'s' if final != 1 else ''} alive after {ticks} ticks.")


def run_visual(width, height, seed, fullscreen=False, backup_every_nth=10000, ticks_per_frame=50):
    try:
        import pyray as rl
        from plantbraingrid.visualization import Visualizer
    except ImportError:
        print("raylib not available (pip install raylib). Falling back to headless.")
        run_headless(width, height, seed, ticks=5000,
                     backup_every_nth=backup_every_nth)
        return

    sim, n_plants, resumed_from = _build_sim(width, height, seed)
    cx, cy = width // 2, height // 2

    print(f"World:  {width}x{height}  seed={seed}")
    if resumed_from:
        print(f"Resumed from: {resumed_from}  tick={sim.tick()}")
    else:
        print(f"Start:  {n_plants} random plants at centre")
    print("Controls: WASD/arrows=pan  scroll=zoom  space=pause  ./,=speed  1=water  2=nutrients  click=select")
    print()

    vis = Visualizer(1280, 720, "PlantBrainGrid — evolution sandbox")
    vis.initialize(fullscreen=fullscreen)

    # Centre view on the starting plants.
    # screen = (world - camera) * zoom * cell_size  →  camera = world - screen/(zoom*cell_size)
    vis.camera.zoom = 1.0
    scale = vis.camera.zoom * vis.camera.cell_size
    vis.camera.x = cx - vis.width  / (2 * scale)
    vis.camera.y = cy - vis.height / (2 * scale)

    vis.paused = True

    report_every = ticks_per_frame

    tick = sim.tick()-1

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

        n_ticks = 0 if vis.paused else ticks_per_frame
        if vis.step_one:
            n_ticks = 1

        for _ in range(n_ticks):
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

            if backup_every_nth > 0 and tick > 0 and tick % backup_every_nth == 0:
                os.makedirs("sim_backup", exist_ok=True)
                sim.save_state(f"sim_backup/{tick:016d}")

        vis.render_world(sim.world(), list(sim.plants()))

    vis.close()
    print(f"Closed — {len(sim.plants())} plants alive at tick {sim.tick()}.")


def main():
    parser = argparse.ArgumentParser(description="Evolution sandbox: random plants competing to survive")
    parser.add_argument('--width',    type=int, default=64)
    parser.add_argument('--height',   type=int, default=64)
    parser.add_argument('--seed',     type=int, default=42)
    parser.add_argument('--ticks',    type=int, default=3000,
                        help='Ticks to run in headless mode (default: 3000)')
    parser.add_argument('--headless', action='store_true',
                        help='Run without visualization even if raylib is available')
    parser.add_argument('--fullscreen', action='store_true',
                        help='Start in fullscreen mode')
    parser.add_argument('--backup-every-nth', type=int, default=10000,
                        dest='backup_every_nth',
                        help='Save a backup every N ticks (0 = disabled, default: 10000)')
    parser.add_argument('--ticks-per-frame', type=int, default=50,
                        dest='ticks_per_frame',
                        help='Simulation ticks advanced per rendered frame (default: 50)')
    args = parser.parse_args()

    if args.headless:
        run_headless(args.width, args.height, args.seed, args.ticks,
                     backup_every_nth=args.backup_every_nth)
    else:
        run_visual(args.width, args.height, args.seed,
                   fullscreen=args.fullscreen,
                   backup_every_nth=args.backup_every_nth,
                   ticks_per_frame=args.ticks_per_frame)


if __name__ == '__main__':
    main()
