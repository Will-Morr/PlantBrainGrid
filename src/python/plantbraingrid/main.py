"""Main entry point for PlantBrainGrid simulation."""

import argparse
import random
import sys


def _seed_simulation(sim, width, height, seed, count=10):
    """Add `count` plants with randomized genomes to a Simulation."""
    from _plantbraingrid import GridCoord
    rng = random.Random(seed)
    placed = 0
    attempts = 0
    while placed < count and attempts < count * 10:
        x = rng.randint(5, width - 5)
        y = rng.randint(5, height - 5)
        genome = [rng.randint(0, 255) for _ in range(1024)]
        plant = sim.add_plant(GridCoord(x, y), genome)
        if plant:
            plant.resources().energy = 100.0
            plant.resources().water = 50.0
            plant.resources().nutrients = 30.0
            placed += 1
        attempts += 1


def run_headless(width: int, height: int, seed: int, ticks: int,
                 auto_spawn: bool = False):
    """Run simulation without visualization."""
    try:
        from _plantbraingrid import Simulation
    except ImportError:
        print("Error: C++ bindings not available. Build with:")
        print("  cd build && cmake .. -DBUILD_PYTHON_BINDINGS=ON && make")
        sys.exit(1)

    print(f"Running headless simulation: {width}x{height}, seed={seed}, ticks={ticks}")

    sim = Simulation(width, height, seed)

    if auto_spawn:
        sim.enable_auto_spawn(True, 10, 100.0, 50.0, 30.0)
        print("Auto-spawn enabled (min population: 10)")
    else:
        _seed_simulation(sim, width, height, seed)
        print(f"Seeded with {len(sim.plants())} plants")

    print(f"\nRunning {ticks} ticks...")
    for tick in range(ticks):
        stats = sim.advance_tick()
        if tick % 100 == 0:
            print(f"  Tick {tick:5d}: {stats.plant_count} plants, "
                  f"{stats.seed_count} seeds, "
                  f"{stats.cells_placed} cells placed")

    print("\nSimulation complete.")
    print(f"Final: {len(sim.plants())} plants, tick {sim.tick()}")


def run_visual(width: int, height: int, seed: int, auto_spawn: bool = False):
    """Run simulation with visualization."""
    try:
        from _plantbraingrid import Simulation, GridCoord
        from .visualization import Visualizer, RAYLIB_AVAILABLE

        if not RAYLIB_AVAILABLE:
            print("Error: raylib not available. Install with: pip install raylib")
            print("Falling back to headless mode...")
            run_headless(width, height, seed, 1000, auto_spawn)
            return
    except ImportError as e:
        print(f"Error: {e}")
        print("C++ bindings or raylib not available.")
        sys.exit(1)

    import pyray as rl

    print(f"Starting visual simulation: {width}x{height}, seed={seed}")

    sim = Simulation(width, height, seed)

    if auto_spawn:
        sim.enable_auto_spawn(True, 10, 200.0, 100.0, 50.0)
    else:
        _seed_simulation(sim, width, height, seed, count=20)

    vis = Visualizer(1280, 720, "PlantBrainGrid")
    vis.initialize()

    vis.camera.x = width / 2 - vis.width / (2 * vis.camera.zoom)
    vis.camera.y = height / 2 - vis.height / (2 * vis.camera.zoom)

    ticks_per_frame = 1

    while not vis.should_close():
        vis.handle_input()

        # Mouse click to select plant
        if rl.is_mouse_button_pressed(rl.MOUSE_BUTTON_LEFT):
            mx, my = rl.get_mouse_x(), rl.get_mouse_y()
            if my > 25:  # Below status bar
                vis.select_plant_at(mx, my, list(sim.plants()))

        # Speed control
        if rl.is_key_pressed(rl.KEY_PERIOD):
            ticks_per_frame = min(100, ticks_per_frame * 2)
        if rl.is_key_pressed(rl.KEY_COMMA):
            ticks_per_frame = max(1, ticks_per_frame // 2)

        # Run simulation if not paused
        if not vis.paused:
            for _ in range(ticks_per_frame):
                sim.advance_tick()

        vis.render_world(sim.world(), list(sim.plants()))

    vis.close()
    print("Simulation ended.")


def main():
    parser = argparse.ArgumentParser(
        description="PlantBrainGrid - Plant Evolution Simulation"
    )
    parser.add_argument("--width", type=int, default=256, help="World width (default: 256)")
    parser.add_argument("--height", type=int, default=256, help="World height (default: 256)")
    parser.add_argument("--seed", type=int, default=None,
                        help="Random seed (default: random)")
    parser.add_argument("--headless", action="store_true",
                        help="Run without visualization")
    parser.add_argument("--ticks", type=int, default=1000,
                        help="Number of ticks in headless mode (default: 1000)")
    parser.add_argument("--auto-spawn", action="store_true",
                        help="Automatically spawn plants with random brains "
                             "whenever the living population drops below 10")

    args = parser.parse_args()

    seed = args.seed if args.seed is not None else random.randint(0, 2**32 - 1)

    if args.headless:
        run_headless(args.width, args.height, seed, args.ticks,
                     auto_spawn=args.auto_spawn)
    else:
        run_visual(args.width, args.height, seed, auto_spawn=args.auto_spawn)


if __name__ == "__main__":
    main()
