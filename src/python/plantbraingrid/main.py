"""Main entry point for PlantBrainGrid simulation."""

import argparse
import random
import sys

def run_headless(width: int, height: int, seed: int, ticks: int):
    """Run simulation without visualization."""
    try:
        from _plantbraingrid import World, Plant, GridCoord
    except ImportError:
        print("Error: C++ bindings not available. Build with:")
        print("  cd build && cmake .. -DBUILD_PYTHON_BINDINGS=ON && make")
        sys.exit(1)

    print(f"Running headless simulation: {width}x{height}, seed={seed}, ticks={ticks}")

    world = World(width, height, seed)

    # Create some initial plants with random genomes
    plants = []
    rng = random.Random(seed)

    for i in range(10):
        x = rng.randint(10, width - 10)
        y = rng.randint(10, height - 10)
        genome = bytes([rng.randint(0, 255) for _ in range(1024)])

        plant = Plant(i + 1, GridCoord(x, y), list(genome))
        plant.resources().energy = 100.0
        plant.resources().water = 50.0
        plant.resources().nutrients = 30.0
        plants.append(plant)
        print(f"Created plant {i+1} at ({x}, {y})")

    print(f"\nRunning {ticks} ticks...")
    for tick in range(ticks):
        world.advance_tick()

        if tick % 100 == 0:
            alive = sum(1 for p in plants if p.is_alive())
            print(f"Tick {tick}: {alive} plants alive")

    print("\nSimulation complete.")
    alive = sum(1 for p in plants if p.is_alive())
    print(f"Final: {alive} plants alive")


def run_visual(width: int, height: int, seed: int):
    """Run simulation with visualization."""
    try:
        from _plantbraingrid import World, Plant, GridCoord, Resources
        from .visualization import Visualizer, RAYLIB_AVAILABLE

        if not RAYLIB_AVAILABLE:
            print("Error: raylib not available. Install with: pip install raylib")
            print("Falling back to headless mode...")
            run_headless(width, height, seed, 1000)
            return
    except ImportError as e:
        print(f"Error: {e}")
        print("C++ bindings or raylib not available.")
        sys.exit(1)

    import pyray as rl

    print(f"Starting visual simulation: {width}x{height}, seed={seed}")

    world = World(width, height, seed)
    vis = Visualizer(1280, 720, "PlantBrainGrid")

    # Create initial plants
    plants = []
    rng = random.Random(seed)

    for i in range(20):
        x = rng.randint(50, width - 50)
        y = rng.randint(50, height - 50)
        genome = bytes([rng.randint(0, 255) for _ in range(1024)])

        plant = Plant(i + 1, GridCoord(x, y), list(genome))
        plant.resources().energy = 200.0
        plant.resources().water = 100.0
        plant.resources().nutrients = 50.0

        # Register with world
        world.cell_at(x, y).occupant = plant.cells()[0]
        plants.append(plant)

    vis.initialize()

    # Center camera on world
    vis.camera.x = width / 2 - vis.width / (2 * vis.camera.zoom)
    vis.camera.y = height / 2 - vis.height / (2 * vis.camera.zoom)

    ticks_per_frame = 1

    while not vis.should_close():
        vis.handle_input()

        # Mouse click to select plant
        if rl.is_mouse_button_pressed(rl.MOUSE_BUTTON_LEFT):
            mx, my = rl.get_mouse_x(), rl.get_mouse_y()
            if my > 25:  # Below status bar
                vis.select_plant_at(mx, my, plants)

        # Speed control
        if rl.is_key_pressed(rl.KEY_PERIOD):
            ticks_per_frame = min(100, ticks_per_frame * 2)
        if rl.is_key_pressed(rl.KEY_COMMA):
            ticks_per_frame = max(1, ticks_per_frame // 2)

        # Run simulation if not paused
        if not vis.paused:
            for _ in range(ticks_per_frame):
                world.advance_tick()

        vis.render_world(world, plants)

    vis.close()
    print("Simulation ended.")


def main():
    parser = argparse.ArgumentParser(description="PlantBrainGrid - Plant Evolution Simulation")
    parser.add_argument("--width", type=int, default=256, help="World width")
    parser.add_argument("--height", type=int, default=256, help="World height")
    parser.add_argument("--seed", type=int, default=None, help="Random seed")
    parser.add_argument("--headless", action="store_true", help="Run without visualization")
    parser.add_argument("--ticks", type=int, default=1000, help="Ticks to run in headless mode")

    args = parser.parse_args()

    seed = args.seed if args.seed is not None else random.randint(0, 2**32 - 1)

    if args.headless:
        run_headless(args.width, args.height, seed, args.ticks)
    else:
        run_visual(args.width, args.height, seed)


if __name__ == "__main__":
    main()
