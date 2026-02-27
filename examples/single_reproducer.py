"""
single_reproducer.py — start the simulation with one reproducer plant and watch it spread.

Usage:
    PYTHONPATH=src/python python examples/single_reproducer.py
    PYTHONPATH=src/python python examples/single_reproducer.py --seed 7 --ticks 5000
"""

import argparse
import os
import sys

# Allow running from project root without pip-installing the package
_root = os.path.join(os.path.dirname(__file__), '..')
sys.path.insert(0, os.path.join(_root, 'src', 'python'))
sys.path.insert(0, _root)  # _plantbraingrid.so lives here

from _plantbraingrid import Simulation, GridCoord


def main():
    parser = argparse.ArgumentParser(description="Single-reproducer bootstrap example")
    parser.add_argument('--width',  type=int, default=256)
    parser.add_argument('--height', type=int, default=256)
    parser.add_argument('--seed',   type=int, default=42)
    parser.add_argument('--ticks',  type=int, default=3000)
    args = parser.parse_args()

    # Load the pre-assembled reproducer genome
    genome_path = os.path.join(os.path.dirname(__file__), 'reproducer.bin')
    genome = list(open(genome_path, 'rb').read())

    sim = Simulation(args.width, args.height, args.seed)

    # Place one plant at the centre with enough resources to get started
    cx, cy = args.width // 2, args.height // 2
    plant = sim.add_plant(GridCoord(cx, cy), genome)
    if plant is None:
        print("Failed to place initial plant — position occupied?")
        sys.exit(1)

    plant.resources().energy    = 200.0
    plant.resources().water     = 100.0
    plant.resources().nutrients = 50.0

    print(f"World:  {args.width}x{args.height}  seed={args.seed}")
    print(f"Genome: {genome_path}  ({len(genome)} bytes)")
    print(f"Start:  1 plant at ({cx}, {cy})")
    print()

    report_every = max(1, args.ticks // 20)

    for tick in range(args.ticks):
        stats = sim.advance_tick()

        if tick % report_every == 0 or tick == args.ticks - 1:
            print(
                f"  tick {stats.tick:>6}  "
                f"plants={stats.plant_count:>4}  "
                f"seeds={stats.seed_count:>3}  "
                f"placed={stats.cells_placed:>3}  "
                f"died={stats.plants_died:>2}"
            )

    print()
    final = len(sim.plants())
    print(f"Done — {final} plant{'s' if final != 1 else ''} alive after {args.ticks} ticks.")


if __name__ == '__main__':
    main()
