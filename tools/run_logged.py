#!/usr/bin/env python3
"""
run_logged.py — Run N simulation ticks and log brain execution and reproduction
to Parquet files for data science analysis.

Produces:
  <output>/tick_stats.parquet          one row per tick (population stats + wall time)
  <output>/plant_ticks.parquet         one row per (tick × plant) every --log-every ticks
  <output>/plant_events.parquet        birth and death snapshots
  <output>/reproduction_events.parquet every seed launch (mother, father, genome hash)
  <output>/genomes.parquet             genome bytes captured at birth per plant

Brain detail columns (instructions_executed, hit_instruction_limit, hit_oob_memory,
oob_count) are added to plant_ticks only when --trace is passed.

Usage:
    python tools/run_logged.py --ticks 5000
    python tools/run_logged.py --ticks 2000 --genome examples/reproducer.bin --output runs/exp1
    python tools/run_logged.py --ticks 1000 --trace --log-every 10 --width 128 --height 128
    python tools/run_logged.py --ticks 500  --n-plants 5 --seed 7 --trace

Requirements:
    pip install pyarrow
"""

import argparse
import hashlib
import os
import random as pyrandom
import sys
import time

_root = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(_root, "src", "python"))
sys.path.insert(0, _root)

try:
    import pyarrow as pa
    import pyarrow.parquet as pq
except ImportError:
    print("pyarrow is required:  pip install pyarrow", file=sys.stderr)
    sys.exit(1)

try:
    import _plantbraingrid as pbg
except ImportError:
    print("_plantbraingrid not found — build the C++ bindings first.", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Parquet sink: accumulates rows and flushes to disk in batches.
# ---------------------------------------------------------------------------

FLUSH_ROWS = 100_000


class Sink:
    def __init__(self, path: str, schema: pa.Schema):
        self.path = path
        self.schema = schema
        self.writer = pq.ParquetWriter(path, schema, compression="snappy")
        self._buf: dict[str, list] = {name: [] for name in schema.names}
        self.total = 0

    def append(self, **row):
        for k, v in row.items():
            self._buf[k].append(v)
        self.total += 1
        if self.total % FLUSH_ROWS == 0:
            self._flush()

    def _flush(self):
        if not self._buf[self.schema.names[0]]:
            return
        arrays = [
            pa.array(self._buf[name], type=self.schema.field(name).type)
            for name in self.schema.names
        ]
        self.writer.write_batch(pa.RecordBatch.from_arrays(arrays, schema=self.schema))
        for lst in self._buf.values():
            lst.clear()

    def close(self):
        self._flush()
        self.writer.close()


# ---------------------------------------------------------------------------
# Schemas
# ---------------------------------------------------------------------------

GENOME_BYTES = 1024  # brain memory / genome size


def make_schemas(trace: bool) -> dict[str, pa.Schema]:
    tick_stats = pa.schema([
        ("tick",                 pa.int64()),
        ("plant_count",          pa.int32()),
        ("seed_count",           pa.int32()),
        ("cells_placed",         pa.int32()),
        ("cells_removed",        pa.int32()),
        ("placements_cancelled", pa.int32()),
        ("seeds_launched",       pa.int32()),
        ("seeds_germinated",     pa.int32()),
        ("plants_died",          pa.int32()),
        ("tick_ms",              pa.float32()),
    ])

    plant_ticks_fields = [
        ("tick",         pa.int64()),
        ("plant_id",     pa.int64()),
        ("x",            pa.int32()),
        ("y",            pa.int32()),
        ("energy",       pa.float32()),
        ("water",        pa.float32()),
        ("nutrients",    pa.float32()),
        ("age",          pa.int64()),
        ("cell_count",   pa.int32()),
        ("brain_ip",     pa.int32()),
        ("brain_halted", pa.bool_()),
    ]
    if trace:
        plant_ticks_fields += [
            ("instructions_executed",  pa.int32()),
            ("hit_instruction_limit",  pa.bool_()),
            ("hit_oob_memory",         pa.bool_()),
            ("oob_count",              pa.int32()),
        ]
    plant_ticks = pa.schema(plant_ticks_fields)

    plant_events = pa.schema([
        ("tick",       pa.int64()),
        ("event",      pa.string()),   # 'birth' | 'death'
        ("plant_id",   pa.int64()),
        ("x",          pa.int32()),
        ("y",          pa.int32()),
        ("energy",     pa.float32()),
        ("water",      pa.float32()),
        ("nutrients",  pa.float32()),
        ("age",        pa.int64()),
        ("cell_count", pa.int32()),
    ])

    reproduction_events = pa.schema([
        ("tick",          pa.int64()),
        ("mother_id",     pa.int64()),
        ("father_id",     pa.int64()),  # 0 if asexual / no mate
        ("seed_x",        pa.int32()),
        ("seed_y",        pa.int32()),
        ("seed_energy",   pa.float32()),
        ("seed_water",    pa.float32()),
        ("seed_nutrients",pa.float32()),
        ("genome_hash",   pa.string()),  # first 16 hex chars of SHA-256
    ])

    genomes = pa.schema([
        ("plant_id",  pa.int64()),
        ("tick_born", pa.int64()),
        ("genome",    pa.binary()),  # GENOME_BYTES bytes captured at birth
    ])

    return {
        "tick_stats":           tick_stats,
        "plant_ticks":          plant_ticks,
        "plant_events":         plant_events,
        "reproduction_events":  reproduction_events,
        "genomes":              genomes,
    }


# ---------------------------------------------------------------------------
# Plant snapshot helpers
# ---------------------------------------------------------------------------

def _plant_event_row(tick: int, event: str, p) -> dict:
    pos = p.primary_position()
    res = p.resources()
    return dict(
        tick=tick, event=event,
        plant_id=p.id(), x=pos.x, y=pos.y,
        energy=float(res.energy), water=float(res.water), nutrients=float(res.nutrients),
        age=p.age(), cell_count=p.cell_count(),
    )


def _plant_tick_row(tick: int, p, trace_map: dict | None) -> dict:
    pos = p.primary_position()
    res = p.resources()
    br  = p.brain()
    row = dict(
        tick=tick, plant_id=p.id(),
        x=pos.x, y=pos.y,
        energy=float(res.energy), water=float(res.water), nutrients=float(res.nutrients),
        age=p.age(), cell_count=p.cell_count(),
        brain_ip=br.ip(), brain_halted=br.is_halted(),
    )
    if trace_map is not None:
        tr = trace_map.get(p.id())
        if tr:
            row["instructions_executed"]  = len(tr.get("steps", []))
            row["hit_instruction_limit"]  = bool(tr.get("hit_instruction_limit", False))
            row["hit_oob_memory"]         = bool(tr.get("hit_oob_memory", False))
            row["oob_count"]              = int(tr.get("oob_count", 0))
        else:
            row["instructions_executed"]  = 0
            row["hit_instruction_limit"]  = False
            row["hit_oob_memory"]         = False
            row["oob_count"]              = 0
    return row


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Run N ticks and log brain/reproduction data to Parquet",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--ticks",      type=int, required=True,  help="Ticks to simulate")
    parser.add_argument("--width",      type=int, default=256,    help="World width")
    parser.add_argument("--height",     type=int, default=256,    help="World height")
    parser.add_argument("--seed",       type=int, default=42,     help="RNG seed")
    parser.add_argument("--genome",     type=str, default=None,
                        help="Path to .bin genome (default: random genomes)")
    parser.add_argument("--n-plants",   type=int, default=20,     help="Initial plant count")
    parser.add_argument("--output",     type=str, default="logs", help="Output directory")
    parser.add_argument("--log-every",  type=int, default=1,
                        help="Snapshot plant state every N ticks")
    parser.add_argument("--trace",      action="store_true",
                        help="Enable brain instruction tracing (adds detail columns)")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    schemas = make_schemas(args.trace)
    sinks = {name: Sink(os.path.join(args.output, f"{name}.parquet"), schema)
             for name, schema in schemas.items()}

    # ── Build simulation ────────────────────────────────────────────────────
    rng = pyrandom.Random(args.seed)
    sim = pbg.Simulation(args.width, args.height, args.seed)

    genome_template: list | None = None
    if args.genome:
        genome_template = list(open(args.genome, "rb").read())

    # ── Event callbacks (registered BEFORE adding plants so initial plants are
    #    captured by on_birth just like any other plant) ─────────────────────
    # current_tick is updated before each advance_tick() so callbacks see the
    # correct tick number while executing inside advance_tick().
    current_tick = [sim.tick()]

    def on_birth(p):
        if args.trace:
            p.brain().enable_tracing(True)
        sinks["plant_events"].append(**_plant_event_row(current_tick[0], "birth", p))
        # Capture genome from brain memory before any execution has occurred.
        genome_bytes = bytes(p.brain().memory()[:GENOME_BYTES])
        sinks["genomes"].append(
            plant_id=p.id(),
            tick_born=current_tick[0],
            genome=genome_bytes,
        )

    def on_death(p):
        sinks["plant_events"].append(**_plant_event_row(current_tick[0], "death", p))

    def on_seed(s):
        genome_bytes = bytes(bytearray(s.genome))
        gh = hashlib.sha256(genome_bytes).hexdigest()[:16]
        sinks["reproduction_events"].append(
            tick=current_tick[0],
            mother_id=s.mother_id,
            father_id=s.father_id,
            seed_x=s.position.x, seed_y=s.position.y,
            seed_energy=float(s.energy),
            seed_water=float(s.water),
            seed_nutrients=float(s.nutrients),
            genome_hash=gh,
        )

    sim.on_plant_birth(on_birth)
    sim.on_plant_death(on_death)
    sim.on_seed_launch(on_seed)

    cx, cy = args.width // 2, args.height // 2
    spacing = max(3, args.width // (args.n_plants + 2))
    start_x = cx - (args.n_plants // 2) * spacing

    placed = 0
    for i in range(args.n_plants):
        g = genome_template if genome_template else [rng.randint(0, 255) for _ in range(1024)]
        plant = sim.add_plant(pbg.GridCoord(start_x + i * spacing, cy), g)
        if plant:
            plant.resources().energy    = 20000.0
            plant.resources().water     = 1000.0
            plant.resources().nutrients = 1000.0
            placed += 1

    # ── Run loop ────────────────────────────────────────────────────────────
    genome_desc = args.genome or "random"
    print(f"World:    {args.width}×{args.height}  seed={args.seed}")
    print(f"Plants:   {placed} initial  genome={genome_desc}")
    print(f"Ticks:    {args.ticks}  log_every={args.log_every}  trace={args.trace}")
    print(f"Output:   {args.output}/")
    print()

    report_every = max(1, args.ticks // 20)
    wall_start   = time.monotonic()

    for i in range(args.ticks):
        current_tick[0] = sim.tick()   # set before advance so callbacks see correct tick

        t0    = time.perf_counter()
        stats = sim.advance_tick()
        tick_ms = (time.perf_counter() - t0) * 1000.0

        sinks["tick_stats"].append(
            tick=stats.tick,
            plant_count=stats.plant_count,
            seed_count=stats.seed_count,
            cells_placed=stats.cells_placed,
            cells_removed=stats.cells_removed,
            placements_cancelled=stats.placements_cancelled,
            seeds_launched=stats.seeds_launched,
            seeds_germinated=stats.seeds_germinated,
            plants_died=stats.plants_died,
            tick_ms=float(tick_ms),
        )

        if i % args.log_every == 0 and stats.plant_count > 0:
            # Collect traces before iterating (each plant's last_trace is set
            # by the parallel brain execution that just finished).
            trace_map: dict | None = None
            if args.trace:
                trace_map = {}
                for p in sim.plants():
                    tr = p.brain().last_trace()
                    if tr is not None:
                        trace_map[p.id()] = tr

            for p in sim.plants():
                sinks["plant_ticks"].append(**_plant_tick_row(stats.tick, p, trace_map))

        if (i + 1) % report_every == 0 or i == args.ticks - 1:
            wall = time.monotonic() - wall_start
            print(
                f"  tick {stats.tick:>6}  "
                f"plants={stats.plant_count:>5}  "
                f"seeds={stats.seed_count:>4}  "
                f"repro_rows={sinks['reproduction_events'].total:>7}  "
                f"wall={wall:.1f}s"
            )

    # ── Write + report ──────────────────────────────────────────────────────
    print()
    print("Finalising Parquet files...")
    for name, sink in sinks.items():
        sink.close()
        size_kb = os.path.getsize(os.path.join(args.output, f"{name}.parquet")) // 1024
        print(f"  {name}.parquet  {sink.total:>8,} rows  {size_kb:>6} KB")

    total_wall = time.monotonic() - wall_start
    print(f"\nDone in {total_wall:.1f}s")


if __name__ == "__main__":
    main()
