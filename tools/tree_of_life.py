#!/usr/bin/env python3
"""
tree_of_life.py — Run simulations from save-file snapshots, cluster genomes into
species, link species across adjacent snapshots, and plot a tree of life.

Pipeline stages (each cached in --output dir):
  1. simulate  — run each save file for N ticks, log to Parquet (parallel)
  2. cluster   — DBSCAN cluster genomes within each snapshot into species
  3. link      — connect species across adjacent snapshots by genome similarity
  4. plot      — render the tree of life with body plan thumbnails

Run individual stages:
    python tools/tree_of_life.py <snapshot_dir> --simulate
    python tools/tree_of_life.py <snapshot_dir> --cluster
    python tools/tree_of_life.py <snapshot_dir> --link
    python tools/tree_of_life.py <snapshot_dir> --plot

Or all at once:
    python tools/tree_of_life.py <snapshot_dir> --all

Requirements:
    pip install pyarrow numpy matplotlib scikit-learn scipy
"""

import argparse
import hashlib
import json
import math
import os
import struct
import sys
import time
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np

_root = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(_root, "src", "python"))
sys.path.insert(0, _root)

try:
    import pyarrow as pa
    import pyarrow.parquet as pq
except ImportError:
    print("pyarrow required:  pip install pyarrow", file=sys.stderr)
    sys.exit(1)

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    from matplotlib.colors import LinearSegmentedColormap
    import matplotlib.gridspec as gridspec
    from matplotlib.patches import Rectangle, FancyBboxPatch
    from matplotlib.lines import Line2D
    plt.style.use("dark_background")
except ImportError:
    print("matplotlib required:  pip install matplotlib", file=sys.stderr)
    sys.exit(1)

try:
    from sklearn.cluster import DBSCAN
except ImportError:
    print("scikit-learn required:  pip install scikit-learn", file=sys.stderr)
    sys.exit(1)

try:
    from plantbraingrid.brain_viewer import OPCODES, NUM_OPCODES, CELL_TYPE_NAMES
except ImportError:
    NUM_OPCODES = 0xA0
    OPCODES = {}
    CELL_TYPE_NAMES = {
        0: "Empty", 1: "Primary", 2: "SmallLeaf", 3: "BigLeaf",
        4: "FiberRoot", 5: "Anther", 6: "Bark", 7: "Thorn",
        8: "FireStarter", 9: "TapRoot", 10: "StoreEnergy",
        11: "StoreWater", 12: "StoreNutrients", 13: "Haustorium",
    }

OPCODE_TABLE = {}
for _raw_op, (_name, _n_args) in OPCODES.items():
    OPCODE_TABLE[_raw_op] = (_name, _n_args)


def _op_info(opcode: int):
    return OPCODE_TABLE.get(opcode, (f"OP_0x{opcode:02X}", 0))


# ---------------------------------------------------------------------------
# Cell costs / colours (from generate_overview.py)
# ---------------------------------------------------------------------------

CELL_COSTS = {
    "Primary":        (10,  0,  0,  0.1,  0,     0),
    "SmallLeaf":      (10,  0,  0,  0,    0.2,   0),
    "BigLeaf":        (25,  0,  10, 0,    0.4,   0.3),
    "FiberRoot":      (8,   0,  0,  0.2,  0,     0),
    "TapRoot":        (12,  0,  0,  0.1,  0,     0),
    "Anther":         (10,  0,  0,  0.2,  0,     0),
    "Bark":           (0,   1,  1,  0,    0.01,  0.01),
    "Thorn":          (5,   0,  0,  0,    0.01,  0),
    "FireStarter":    (30,  0,  0,  0,    0,     0),
    "StoreEnergy":    (10,  0,  0,  0,    0.02,  0),
    "StoreWater":     (10,  0,  0,  0,    0.02,  0),
    "StoreNutrients": (10,  0,  0,  0,    0.02,  0),
    "Haustorium":     (10,  0,  0,  0.1,  0,     0),
}

CELL_INCOME = {
    "SmallLeaf":  (1.0, 0,   0),
    "BigLeaf":    (5.0, 0,   0),
    "FiberRoot":  (0,   1.2, 1.0),
    "TapRoot":    (0,   3.5, 0),
}

CELL_NAME_TO_ID = {v: k for k, v in CELL_TYPE_NAMES.items()}

CELL_COLORS_RGB = {
    0: (50, 50, 50),       1: (139, 69, 19),     2: (34, 139, 34),
    3: (0, 100, 0),        4: (139, 90, 43),      5: (255, 220, 50),
    6: (101, 67, 33),      7: (128, 128, 128),    8: (255, 69, 0),
    9: (110, 55, 90),      10: (255, 200, 50),    11: (50, 150, 255),
    12: (180, 120, 60),    13: (200, 0, 0),
}


# ---------------------------------------------------------------------------
# Genome analysis (from generate_overview.py)
# ---------------------------------------------------------------------------

def _extract_placed_cells(genome: bytes) -> list[tuple[str, int, int]]:
    _ADJ = [(0, 1), (0, -1), (1, 0), (-1, 0)]
    occupied: set[tuple[int, int]] = {(0, 0)}
    cells: list[tuple[str, int, int]] = []
    i = 0
    n = len(genome)
    while i < n:
        raw = genome[i]
        opcode = raw % NUM_OPCODES
        name, n_args = _op_info(opcode)
        if name == "PLACE_CELL" and i + 3 <= n:
            type_byte = genome[i + 1]
            dx_raw = genome[i + 2]
            dy_raw = genome[i + 3] if i + 3 < n else 0
            cell_id = type_byte % 14
            cell_name = CELL_TYPE_NAMES.get(cell_id, f"Type{cell_id}")
            dx = dx_raw if dx_raw < 128 else dx_raw - 256
            dy = dy_raw if dy_raw < 128 else dy_raw - 256
            if cell_name not in ("Empty", "Primary"):
                pos = (dx, dy)
                if any((dx + ox, dy + oy) in occupied for ox, oy in _ADJ):
                    if pos not in occupied:
                        occupied.add(pos)
                        cells.append((cell_name, dx, dy))
        i += 1 + n_args
    return cells


def extract_body_plan(genome: bytes) -> dict[str, int]:
    counts: dict[str, int] = {}
    for cell_name, _dx, _dy in _extract_placed_cells(genome):
        counts[cell_name] = counts.get(cell_name, 0) + 1
    return counts


def extract_body_layout(genome: bytes) -> list[tuple[str, int, int]]:
    return _extract_placed_cells(genome)


def body_plan_key(plan: dict[str, int]) -> str:
    if not plan:
        return "(no cells)"
    parts = sorted(plan.items())
    return ", ".join(f"{count}x {name}" for name, count in parts)


def _compute_body_costs(plan: dict[str, int]):
    total_build = [0.0, 0.0, 0.0]
    total_income = [0.0, 0.0, 0.0]
    total_maint = [0.0, 0.0, 0.0]
    for name, count in plan.items():
        costs = CELL_COSTS.get(name, (0, 0, 0, 0, 0, 0))
        income = CELL_INCOME.get(name, (0, 0, 0))
        total_build[0] += costs[0] * count
        total_build[1] += costs[1] * count
        total_build[2] += costs[2] * count
        total_maint[0] += costs[3] * count
        total_maint[1] += costs[4] * count
        total_maint[2] += costs[5] * count
        total_income[0] += income[0] * count
        total_income[1] += income[1] * count
        total_income[2] += income[2] * count
    return total_build, total_income, total_maint


# ---------------------------------------------------------------------------
# Distance / clustering helpers
# ---------------------------------------------------------------------------

def hamming_matrix(genomes: np.ndarray) -> np.ndarray:
    try:
        from scipy.spatial.distance import cdist
        return (cdist(genomes.astype(np.float64),
                      genomes.astype(np.float64),
                      metric="hamming") * genomes.shape[1]).astype(np.int32)
    except ImportError:
        N = len(genomes)
        dist = np.empty((N, N), dtype=np.int32)
        for i in range(N):
            dist[i] = (genomes[i:i + 1] != genomes).sum(axis=1)
        return dist


def cluster_order(dist: np.ndarray) -> np.ndarray:
    """Return a permutation of [0, N) that groups related genomes together."""
    try:
        from scipy.cluster.hierarchy import linkage, leaves_list
        from scipy.spatial.distance import squareform
        Z = linkage(squareform(dist.astype(np.float64)), method="ward")
        return leaves_list(Z)
    except ImportError:
        print("[INFO] scipy not available — skipping clustering reorder",
              file=sys.stderr)
        return np.arange(len(dist))


def make_palette(n: int) -> list:
    base = (
        [plt.get_cmap("tab20")(i) for i in range(20)]
        + [plt.get_cmap("tab20b")(i) for i in range(20)]
    )
    return [base[i % len(base)] for i in range(max(n, 1))]


# ---------------------------------------------------------------------------
# Save-file header reader
# ---------------------------------------------------------------------------

GENOME_BYTES = 1024


def read_save_header(path: str) -> dict:
    with open(path, "rb") as f:
        data = f.read(4 + 4 + 8 + 8 + 4 + 4 + 8)
    if len(data) < 40:
        raise ValueError(f"Save file too short: {path}")
    magic, version = struct.unpack_from("<II", data, 0)
    if magic != 0x50424753 or version != 1:
        raise ValueError(f"Invalid save file (magic=0x{magic:08X}, version={version}): {path}")
    w, h = struct.unpack_from("<II", data, 24)
    seed, = struct.unpack_from("<Q", data, 32)
    tick, = struct.unpack_from("<Q", data, 8)
    return {"width": w, "height": h, "seed": seed, "tick": tick}


# ---------------------------------------------------------------------------
# Stage 1: Simulate — run each save file and log to Parquet
# ---------------------------------------------------------------------------

def _run_one_sim(save_path: str, out_dir: str, ticks: int, log_every: int) -> str:
    """Run a single simulation from a save file, logging to out_dir.

    This function runs in a subprocess via ProcessPoolExecutor, so it must
    import _plantbraingrid fresh (not shared across processes).
    """
    import _plantbraingrid as pbg

    os.makedirs(out_dir, exist_ok=True)

    # Schemas
    tick_stats_schema = pa.schema([
        ("tick",                 pa.int64()),
        ("plant_count",          pa.int32()),
        ("seed_count",           pa.int32()),
        ("cells_placed",         pa.int32()),
        ("cells_removed",        pa.int32()),
        ("placements_cancelled", pa.int32()),
        ("seeds_launched",       pa.int32()),
        ("seeds_germinated",     pa.int32()),
        ("plants_died",          pa.int32()),
        ("season",               pa.utf8()),
        ("tick_ms",              pa.float32()),
    ])
    plant_events_schema = pa.schema([
        ("tick",       pa.int64()),
        ("event",      pa.string()),
        ("plant_id",   pa.int64()),
        ("x",          pa.int32()),
        ("y",          pa.int32()),
        ("energy",     pa.float32()),
        ("water",      pa.float32()),
        ("nutrients",  pa.float32()),
        ("age",        pa.int64()),
        ("cell_count", pa.int32()),
    ])
    genomes_schema = pa.schema([
        ("plant_id",  pa.int64()),
        ("tick_born", pa.int64()),
        ("genome",    pa.binary()),
    ])
    reproduction_schema = pa.schema([
        ("tick",          pa.int64()),
        ("mother_id",     pa.int64()),
        ("father_id",     pa.int64()),
        ("seed_x",        pa.int32()),
        ("seed_y",        pa.int32()),
        ("seed_energy",   pa.float32()),
        ("seed_water",    pa.float32()),
        ("seed_nutrients", pa.float32()),
        ("genome_hash",   pa.string()),
    ])

    FLUSH_ROWS = 100_000

    class Sink:
        def __init__(self, path, schema):
            self.path = path
            self.schema = schema
            self.writer = pq.ParquetWriter(path, schema, compression="snappy")
            self._buf = {name: [] for name in schema.names}
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

    sinks = {
        "tick_stats": Sink(os.path.join(out_dir, "tick_stats.parquet"), tick_stats_schema),
        "plant_events": Sink(os.path.join(out_dir, "plant_events.parquet"), plant_events_schema),
        "genomes": Sink(os.path.join(out_dir, "genomes.parquet"), genomes_schema),
        "reproduction_events": Sink(os.path.join(out_dir, "reproduction_events.parquet"), reproduction_schema),
    }

    header = read_save_header(save_path)
    width, height, seed = header["width"], header["height"], header["seed"]
    sim = pbg.Simulation(width, height, seed)
    sim.load_state(save_path)

    cfg = pbg.get_config()
    season_list = [
        {"name": s.name, "start_tick": s.start_tick,
         "light_mult": s.light_mult, "water_mult": s.water_mult,
         "nutrient_mult": s.nutrient_mult}
        for s in cfg.seasons
    ]
    with open(os.path.join(out_dir, "sim_metadata.json"), "w") as f:
        json.dump({
            "width": width, "height": height, "seed": int(seed),
            "start_tick": int(header["tick"]),
            "season_cycle_length": cfg.season_cycle_length,
            "seasons": season_list,
        }, f)

    current_tick = [sim.tick()]

    def on_birth(p):
        pos = p.primary_position()
        res = p.resources()
        sinks["plant_events"].append(
            tick=current_tick[0], event="birth", plant_id=p.id(),
            x=pos.x, y=pos.y,
            energy=float(res.energy), water=float(res.water), nutrients=float(res.nutrients),
            age=p.age(), cell_count=p.cell_count(),
        )
        genome_bytes = bytes(p.brain().memory()[:GENOME_BYTES])
        sinks["genomes"].append(
            plant_id=p.id(), tick_born=current_tick[0], genome=genome_bytes,
        )

    def on_death(p):
        pos = p.primary_position()
        res = p.resources()
        sinks["plant_events"].append(
            tick=current_tick[0], event="death", plant_id=p.id(),
            x=pos.x, y=pos.y,
            energy=float(res.energy), water=float(res.water), nutrients=float(res.nutrients),
            age=p.age(), cell_count=p.cell_count(),
        )

    def on_seed(s):
        genome_bytes = bytes(bytearray(s.genome))
        gh = hashlib.sha256(genome_bytes).hexdigest()[:16]
        sinks["reproduction_events"].append(
            tick=current_tick[0],
            mother_id=s.mother_id, father_id=s.father_id,
            seed_x=s.position.x, seed_y=s.position.y,
            seed_energy=float(s.energy), seed_water=float(s.water),
            seed_nutrients=float(s.nutrients), genome_hash=gh,
        )

    sim.on_plant_birth(on_birth)
    sim.on_plant_death(on_death)
    sim.on_seed_launch(on_seed)

    # Capture existing plants as birth events
    for p in sim.plants():
        on_birth(p)

    for i in range(ticks):
        current_tick[0] = sim.tick()
        t0 = time.perf_counter()
        stats = sim.advance_tick()
        tick_ms = (time.perf_counter() - t0) * 1000.0

        world = sim.world()
        season_idx = world.current_season_index()
        season_name = pbg.get_config().seasons[season_idx].name

        sinks["tick_stats"].append(
            tick=stats.tick, plant_count=stats.plant_count,
            seed_count=stats.seed_count, cells_placed=stats.cells_placed,
            cells_removed=stats.cells_removed,
            placements_cancelled=stats.placements_cancelled,
            seeds_launched=stats.seeds_launched,
            seeds_germinated=stats.seeds_germinated,
            plants_died=stats.plants_died,
            season=season_name, tick_ms=float(tick_ms),
        )

    for sink in sinks.values():
        sink.close()

    return out_dir


def stage_simulate(snapshot_dir: str, output_dir: str, ticks: int,
                   log_every: int, max_workers: int) -> list[str]:
    """Run simulations from all save files in snapshot_dir, in parallel."""
    save_files = sorted(
        f for f in os.listdir(snapshot_dir)
        if not f.startswith('.') and os.path.isfile(os.path.join(snapshot_dir, f))
    )
    if not save_files:
        print(f"[ERR] No save files found in {snapshot_dir}", file=sys.stderr)
        sys.exit(1)

    sim_dir = os.path.join(output_dir, "sims")
    os.makedirs(sim_dir, exist_ok=True)

    # Check which are already done (cached)
    jobs = []
    done = []
    for sf in save_files:
        out = os.path.join(sim_dir, sf)
        marker = os.path.join(out, "tick_stats.parquet")
        if os.path.exists(marker):
            done.append(out)
        else:
            jobs.append((os.path.join(snapshot_dir, sf), out))

    if done:
        print(f"  {len(done)} sims already cached, {len(jobs)} remaining")

    if jobs:
        print(f"  Running {len(jobs)} simulations with {max_workers} workers...")
        t0 = time.monotonic()
        results = []
        with ProcessPoolExecutor(max_workers=max_workers) as pool:
            futures = {
                pool.submit(_run_one_sim, save_path, sf_out, ticks, log_every): sf_out
                for save_path, sf_out in jobs
            }
            for i, future in enumerate(as_completed(futures)):
                out = futures[future]
                try:
                    future.result()
                    results.append(out)
                    elapsed = time.monotonic() - t0
                    print(f"    [{i+1}/{len(jobs)}] {os.path.basename(out)} done  ({elapsed:.1f}s)")
                except Exception as e:
                    print(f"    [{i+1}/{len(jobs)}] {os.path.basename(out)} FAILED: {e}")
        done.extend(results)

    done.sort()
    # Write manifest
    manifest = [os.path.basename(d) for d in done]
    with open(os.path.join(output_dir, "sim_manifest.json"), "w") as f:
        json.dump({"sims": manifest}, f, indent=2)

    print(f"  {len(done)} total simulations logged")
    return done


# ---------------------------------------------------------------------------
# Stage 2: Cluster — DBSCAN on genomes within each snapshot
# ---------------------------------------------------------------------------

def _cluster_one_sim(sim_dir: str, eps: int, min_samples: int,
                     max_plants: int, seed: int) -> dict:
    """Cluster genomes in one simulation directory. Returns cluster data."""
    genomes_path = os.path.join(sim_dir, "genomes.parquet")
    events_path = os.path.join(sim_dir, "plant_events.parquet")
    if not os.path.exists(genomes_path):
        return {"error": f"No genomes.parquet in {sim_dir}"}

    try:
        gt = pq.read_table(genomes_path)
        events = pq.read_table(events_path)
    except Exception as e:
        print(f"    [WARN] Corrupt parquet in {sim_dir}: {e}")
        return {"n_clusters": 0, "clusters": [], "n_plants": 0,
                "sim_dir": os.path.basename(sim_dir), "start_tick": 0,
                "n_noise": 0}

    plant_ids = gt["plant_id"].to_pylist()
    ticks_born = gt["tick_born"].to_pylist()
    raw_genomes = gt["genome"]

    # Load birth positions
    birth_pos = {}
    birth_ticks = {}
    death_ticks = {}
    for ev, pid, tick, x, y in zip(
        events["event"].to_pylist(), events["plant_id"].to_pylist(),
        events["tick"].to_pylist(), events["x"].to_pylist(), events["y"].to_pylist(),
    ):
        if ev == "birth":
            birth_pos[pid] = (x, y)
            birth_ticks[pid] = tick
        elif ev == "death":
            death_ticks[pid] = tick

    # Filter to plants with positions
    keep = [i for i, pid in enumerate(plant_ids) if pid in birth_pos]
    if not keep:
        return {"n_clusters": 0, "clusters": [], "n_plants": 0}

    # Subsample if needed
    if len(keep) > max_plants:
        rng = np.random.default_rng(seed)
        keep = sorted(rng.choice(keep, size=max_plants, replace=False).tolist())

    sel_ids = [plant_ids[i] for i in keep]
    genomes = np.array(
        [np.frombuffer(raw_genomes[i].as_py(), dtype=np.uint8) for i in keep],
        dtype=np.uint8,
    )

    if len(genomes) < 2:
        return {"n_clusters": 0, "clusters": [], "n_plants": len(genomes)}

    dist = hamming_matrix(genomes)
    labels = DBSCAN(eps=eps, min_samples=min_samples,
                    metric="precomputed").fit_predict(dist)
    n_clusters = int((labels >= 0).any() and labels.max() + 1) if len(labels) else 0

    # Read metadata for tick info
    meta_path = os.path.join(sim_dir, "sim_metadata.json")
    start_tick = 0
    if os.path.exists(meta_path):
        with open(meta_path) as f:
            meta = json.load(f)
        start_tick = meta.get("start_tick", 0)

    # Build cluster summaries
    clusters = []
    for cid in range(n_clusters):
        mask = labels == cid
        cluster_ids = [sel_ids[i] for i in range(len(sel_ids)) if mask[i]]
        cluster_genomes = genomes[mask]

        # Compute centroid genome (median byte at each position)
        centroid = np.median(cluster_genomes, axis=0).astype(np.uint8)

        # Representative genome = closest to centroid
        dists_to_centroid = np.sum(cluster_genomes != centroid[None, :], axis=1)
        rep_idx = int(np.argmin(dists_to_centroid))
        rep_genome = cluster_genomes[rep_idx].tobytes()

        # Body plan from representative
        plan = extract_body_plan(rep_genome)
        layout = extract_body_layout(rep_genome)

        # Lifespans
        stats_path = os.path.join(sim_dir, "tick_stats.parquet")
        final_tick = start_tick
        if os.path.exists(stats_path):
            st = pq.read_table(stats_path, columns=["tick"])
            tks = st["tick"].to_pylist()
            if tks:
                final_tick = max(tks)

        lifespans = [
            death_ticks.get(pid, final_tick) - birth_ticks.get(pid, start_tick)
            for pid in cluster_ids
        ]

        clusters.append({
            "cluster_id": cid,
            "size": int(mask.sum()),
            "centroid": centroid.tolist(),
            "representative_genome": list(rep_genome),
            "body_plan": plan,
            "body_layout": [(n, dx, dy) for n, dx, dy in layout],
            "body_plan_key": body_plan_key(plan),
            "mean_lifespan": float(np.mean(lifespans)) if lifespans else 0,
            "median_lifespan": float(np.median(lifespans)) if lifespans else 0,
        })

    return {
        "sim_dir": os.path.basename(sim_dir),
        "start_tick": start_tick,
        "n_plants": len(sel_ids),
        "n_clusters": n_clusters,
        "n_noise": int((labels == -1).sum()),
        "clusters": clusters,
    }


def stage_cluster(output_dir: str, eps: int, min_samples: int,
                  max_plants: int, seed: int) -> list[dict]:
    """Cluster genomes in each simulation directory."""
    manifest_path = os.path.join(output_dir, "sim_manifest.json")
    if not os.path.exists(manifest_path):
        print("[ERR] sim_manifest.json not found. Run --simulate first.", file=sys.stderr)
        sys.exit(1)

    with open(manifest_path) as f:
        manifest = json.load(f)

    cache_path = os.path.join(output_dir, "clusters.json")
    if os.path.exists(cache_path):
        print(f"  Cluster cache found: {cache_path}")
        with open(cache_path) as f:
            return json.load(f)

    sim_dir = os.path.join(output_dir, "sims")
    all_clusters = []
    for i, sim_name in enumerate(manifest["sims"]):
        sd = os.path.join(sim_dir, sim_name)
        print(f"  [{i+1}/{len(manifest['sims'])}] Clustering {sim_name}...")
        result = _cluster_one_sim(sd, eps, min_samples, max_plants, seed)
        all_clusters.append(result)

    with open(cache_path, "w") as f:
        json.dump(all_clusters, f)

    return all_clusters


# ---------------------------------------------------------------------------
# Stage 3: Link — connect species across adjacent snapshots
# ---------------------------------------------------------------------------

def stage_link(output_dir: str, link_eps: int) -> list[dict]:
    """Link species across adjacent snapshots by genome similarity."""
    cache_path = os.path.join(output_dir, "links.json")
    if os.path.exists(cache_path):
        print(f"  Link cache found: {cache_path}")
        with open(cache_path) as f:
            return json.load(f)

    clusters_path = os.path.join(output_dir, "clusters.json")
    if not os.path.exists(clusters_path):
        print("[ERR] clusters.json not found. Run --cluster first.", file=sys.stderr)
        sys.exit(1)

    with open(clusters_path) as f:
        all_clusters = json.load(f)

    # Assign global IDs to each species
    global_id = 0
    for snap in all_clusters:
        for cluster in snap.get("clusters", []):
            cluster["global_id"] = global_id
            global_id += 1

    # Link adjacent snapshots
    links = []
    for i in range(len(all_clusters) - 1):
        snap_a = all_clusters[i]
        snap_b = all_clusters[i + 1]
        clusters_a = snap_a.get("clusters", [])
        clusters_b = snap_b.get("clusters", [])

        if not clusters_a or not clusters_b:
            continue

        # Compute pairwise distances between centroids
        centroids_a = np.array([c["centroid"] for c in clusters_a], dtype=np.uint8)
        centroids_b = np.array([c["centroid"] for c in clusters_b], dtype=np.uint8)

        for ia, ca in enumerate(clusters_a):
            ga = np.array(ca["centroid"], dtype=np.uint8)
            best_dist = float('inf')
            best_ib = -1
            for ib, cb in enumerate(clusters_b):
                gb = np.array(cb["centroid"], dtype=np.uint8)
                d = int(np.sum(ga != gb))
                if d < best_dist:
                    best_dist = d
                    best_ib = ib

            if best_dist <= link_eps and best_ib >= 0:
                links.append({
                    "from_global_id": ca["global_id"],
                    "to_global_id": clusters_b[best_ib]["global_id"],
                    "distance": best_dist,
                    "from_snap": i,
                    "to_snap": i + 1,
                    "from_cluster": ia,
                    "to_cluster": best_ib,
                })

        # Also check reverse: species in B that aren't already linked
        linked_b = {l["to_global_id"] for l in links if l["to_snap"] == i + 1}
        for ib, cb in enumerate(clusters_b):
            if cb["global_id"] in linked_b:
                continue
            gb = np.array(cb["centroid"], dtype=np.uint8)
            best_dist = float('inf')
            best_ia = -1
            for ia, ca in enumerate(clusters_a):
                ga = np.array(ca["centroid"], dtype=np.uint8)
                d = int(np.sum(ga != gb))
                if d < best_dist:
                    best_dist = d
                    best_ia = ia

            if best_dist <= link_eps and best_ia >= 0:
                links.append({
                    "from_global_id": clusters_a[best_ia]["global_id"],
                    "to_global_id": cb["global_id"],
                    "distance": best_dist,
                    "from_snap": i,
                    "to_snap": i + 1,
                    "from_cluster": best_ia,
                    "to_cluster": ib,
                })

    # Deduplicate links
    seen = set()
    deduped = []
    for l in links:
        key = (l["from_global_id"], l["to_global_id"])
        if key not in seen:
            seen.add(key)
            deduped.append(l)
    links = deduped

    result = {"all_clusters": all_clusters, "links": links}
    with open(cache_path, "w") as f:
        json.dump(result, f)

    print(f"  {len(links)} links found across {len(all_clusters)} snapshots")
    return result


# ---------------------------------------------------------------------------
# Stage 4: Plot — tree of life visualization
# ---------------------------------------------------------------------------

def _draw_body_plan_thumbnail(ax, layout, plan):
    """Draw a small body plan grid on a matplotlib axes."""
    cells = {(0, 0): "Primary"}
    for name, dx, dy in layout:
        cells[(dx, dy)] = name

    if not cells:
        ax.set_visible(False)
        return

    all_x = [p[0] for p in cells]
    all_y = [p[1] for p in cells]
    min_x, max_x = min(all_x) - 0.5, max(all_x) + 0.5
    min_y, max_y = min(all_y) - 0.5, max(all_y) + 0.5

    ax.set_xlim(min_x, max_x + 0.1)
    ax.set_ylim(min_y, max_y + 0.1)
    ax.set_aspect("equal")
    ax.set_facecolor("black")
    ax.axis("off")

    for (cx, cy), name in cells.items():
        cid_cell = CELL_NAME_TO_ID.get(name, 0)
        r, g, b = CELL_COLORS_RGB.get(cid_cell, (255, 0, 240))
        ax.add_patch(Rectangle(
            (cx - 0.4, cy - 0.4), 0.8, 0.8,
            facecolor=(r / 255, g / 255, b / 255),
            edgecolor=(0.3, 0.3, 0.3), linewidth=0.3,
        ))

    ax.invert_yaxis()


def _generate_genome_heatmaps(output_dir: str, max_plants: int = 400,
                              seed: int = 0, dpi: int = 150):
    """Generate per-snapshot genome-difference heatmaps (same method as
    plot_genome_heatmap.py).  Saves raw heatmap PNGs into images/heatmaps/.
    """
    import matplotlib.ticker as ticker

    manifest_path = os.path.join(output_dir, "sim_manifest.json")
    if not os.path.exists(manifest_path):
        print("[WARN] sim_manifest.json not found — skipping heatmaps.",
              file=sys.stderr)
        return

    with open(manifest_path) as f:
        manifest = json.load(f)

    sim_dir = os.path.join(output_dir, "sims")
    heatmap_dir = os.path.join(output_dir, "images", "heatmaps")
    os.makedirs(heatmap_dir, exist_ok=True)

    for sim_name in manifest["sims"]:
        sd = os.path.join(sim_dir, sim_name)
        genomes_path = os.path.join(sd, "genomes.parquet")
        if not os.path.exists(genomes_path):
            continue

        table = pq.read_table(genomes_path)
        plant_ids = table["plant_id"].to_pylist()
        ticks_born = table["tick_born"].to_pylist()
        raw_genomes = table["genome"]

        if len(plant_ids) < 2:
            continue

        # Load start tick from metadata
        meta_path = os.path.join(sd, "sim_metadata.json")
        start_tick = 0
        if os.path.exists(meta_path):
            with open(meta_path) as f:
                meta = json.load(f)
            start_tick = meta.get("start_tick", 0)

        # Subsample if needed
        indices = list(range(len(plant_ids)))
        if len(indices) > max_plants:
            rng = np.random.default_rng(seed)
            indices = sorted(
                rng.choice(indices, size=max_plants, replace=False).tolist())

        sel_ids = [plant_ids[i] for i in indices]
        sel_ticks = [ticks_born[i] for i in indices]
        genomes = np.array(
            [np.frombuffer(raw_genomes[i].as_py(), dtype=np.uint8)
             for i in indices],
            dtype=np.uint8,
        )

        N = len(sel_ids)
        genome_len = genomes.shape[1]

        # Pairwise Hamming distance
        dist = hamming_matrix(genomes)

        # Hierarchical clustering reorder
        if N > 2:
            order = cluster_order(dist)
            dist = dist[np.ix_(order, order)]
            sel_ids = [sel_ids[i] for i in order]
            sel_ticks = [sel_ticks[i] for i in order]

        # Plot
        fig_size = max(8, min(20, N / 12))
        fig, ax = plt.subplots(figsize=(fig_size, fig_size))

        im = ax.imshow(dist, cmap="inferno", interpolation="nearest",
                        vmin=0, vmax=genome_len)

        if N <= 60:
            ax.set_xticks(range(N))
            ax.set_yticks(range(N))
            ax.set_xticklabels([str(p) for p in sel_ids],
                               rotation=90, fontsize=max(4, 8 - N // 20))
            ax.set_yticklabels([str(p) for p in sel_ids],
                               fontsize=max(4, 8 - N // 20))
        else:
            k = max(1, N // 30)
            ax.xaxis.set_major_locator(ticker.MultipleLocator(k))
            ax.yaxis.set_major_locator(ticker.MultipleLocator(k))
            ax.xaxis.set_major_formatter(
                ticker.FuncFormatter(lambda x, _: str(sel_ids[int(x)])
                                     if 0 <= int(x) < N else ""))
            ax.yaxis.set_major_formatter(
                ticker.FuncFormatter(lambda y, _: str(sel_ids[int(y)])
                                     if 0 <= int(y) < N else ""))
            plt.setp(ax.get_xticklabels(), rotation=90, fontsize=6)
            plt.setp(ax.get_yticklabels(), fontsize=6)

        cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        cbar.set_label("Bytes different", fontsize=10)

        ax.set_title(
            f"Pairwise genome difference — {N} plants (clustered)\n"
            f"Snapshot t={start_tick:,}  |  Bright = more different  (max {genome_len})",
            fontsize=11,
        )
        ax.set_xlabel("Plant ID", fontsize=9)
        ax.set_ylabel("Plant ID", fontsize=9)

        plt.tight_layout()

        out_path = os.path.join(heatmap_dir, f"heatmap_{sim_name}.png")
        fig.savefig(out_path, dpi=dpi, bbox_inches="tight")
        plt.close(fig)

    n_saved = len([f for f in os.listdir(heatmap_dir) if f.endswith(".png")])
    print(f"  {n_saved} genome heatmaps saved to {heatmap_dir}/")


def stage_plot(output_dir: str, dpi: int = 150):
    """Render the tree of life diagram."""
    links_path = os.path.join(output_dir, "links.json")
    if not os.path.exists(links_path):
        print("[ERR] links.json not found. Run --link first.", file=sys.stderr)
        sys.exit(1)

    with open(links_path) as f:
        data = json.load(f)

    all_clusters = data["all_clusters"]
    links = data["links"]

    # Collect all species nodes
    nodes = []  # (global_id, snap_idx, cluster_data, snap_data)
    for snap_idx, snap in enumerate(all_clusters):
        for cluster in snap.get("clusters", []):
            nodes.append((cluster["global_id"], snap_idx, cluster, snap))

    if not nodes:
        print("[WARN] No species clusters found. Nothing to plot.")
        return

    n_snaps = len(all_clusters)

    # Compute layout: X = position within snapshot, Y = snapshot index (time)
    # Group nodes by snapshot
    snap_groups = {}
    for gid, snap_idx, cluster, snap in nodes:
        snap_groups.setdefault(snap_idx, []).append((gid, cluster, snap))

    # Assign x positions: spread species evenly within each snapshot row
    node_positions = {}  # global_id -> (x, y)
    max_species_in_row = max(len(v) for v in snap_groups.values()) if snap_groups else 1

    for snap_idx in sorted(snap_groups.keys()):
        species = snap_groups[snap_idx]
        n_species = len(species)
        for i, (gid, cluster, snap) in enumerate(species):
            x = (i + 0.5) / max(n_species, 1) * max_species_in_row
            y = snap_idx  # bottom = 0, higher = later time
            node_positions[gid] = (x, y)

    # Figure dimensions
    fig_width = max(12, max_species_in_row * 2.5)
    fig_height = max(8, n_snaps * 2.0)

    fig, ax_main = plt.subplots(figsize=(fig_width, fig_height))
    ax_main.set_facecolor("#0a0a0a")

    # Draw links first (behind nodes)
    for link in links:
        from_gid = link["from_global_id"]
        to_gid = link["to_global_id"]
        if from_gid in node_positions and to_gid in node_positions:
            x0, y0 = node_positions[from_gid]
            x1, y1 = node_positions[to_gid]
            dist = link["distance"]
            # Thicker = closer relationship
            lw = max(0.5, 3.0 - dist / 100.0)
            alpha = max(0.2, 1.0 - dist / 500.0)
            ax_main.plot([x0, x1], [y0, y1], color="white", linewidth=lw,
                         alpha=alpha, zorder=1)

    # Draw species nodes with inset body plan thumbnails
    node_size_inches = 1.2  # size of each body plan thumbnail
    # We'll use fig.transFigure to position inset axes

    # Get axis transform to figure coords
    palette = make_palette(len(nodes))

    for idx, (gid, snap_idx, cluster, snap) in enumerate(nodes):
        x, y = node_positions[gid]

        # Draw node background circle/box
        color = palette[idx % len(palette)]

        # Species info text
        size = cluster.get("size", 0)
        bp_key = cluster.get("body_plan_key", "?")
        mean_ls = cluster.get("mean_lifespan", 0)

        # Draw a colored marker for the node
        ax_main.scatter([x], [y], s=300, c=[color], zorder=3,
                        edgecolors="white", linewidths=0.5)

        # Add text annotation
        label = f"n={size}"
        ax_main.annotate(label, (x, y), textcoords="offset points",
                         xytext=(0, -18), ha="center", fontsize=6,
                         color="white", zorder=4)

    # Create inset axes for body plans
    # We need to convert data coords to figure coords for inset placement
    for idx, (gid, snap_idx, cluster, snap) in enumerate(nodes):
        x_data, y_data = node_positions[gid]
        layout = cluster.get("body_layout", [])
        plan = cluster.get("body_plan", {})

        if not layout and not plan:
            continue

        # Convert data coordinates to display coordinates, then to figure fraction
        display_coords = ax_main.transData.transform((x_data, y_data))
        fig_coords = fig.transFigure.inverted().transform(display_coords)

        # Inset size in figure fraction
        inset_w = node_size_inches / fig_width
        inset_h = node_size_inches / fig_height

        inset_ax = fig.add_axes([
            fig_coords[0] - inset_w / 2,
            fig_coords[1] - inset_h / 2,
            inset_w, inset_h,
        ], zorder=5)

        _draw_body_plan_thumbnail(inset_ax, layout, plan)

    # Y-axis: time labels (tick numbers from snapshots)
    y_ticks = []
    y_labels = []
    for snap_idx in sorted(snap_groups.keys()):
        snap_data = snap_groups[snap_idx][0][2]  # first cluster's snap
        # Get start_tick from snap metadata
        start_tick = all_clusters[snap_idx].get("start_tick", 0)
        y_ticks.append(snap_idx)
        y_labels.append(f"t={start_tick:,}")

    ax_main.set_yticks(y_ticks)
    ax_main.set_yticklabels(y_labels, fontsize=7)
    ax_main.set_ylabel("Time (ticks)", fontsize=10)

    ax_main.set_xlim(-0.5, max_species_in_row + 0.5)
    ax_main.set_ylim(-0.5, n_snaps - 0.5)
    ax_main.set_xticks([])
    ax_main.set_title("Tree of Life — Species Across Snapshots", fontsize=14, pad=15)

    # Add stats legend
    total_species = len(nodes)
    total_links = len(links)
    legend_text = f"{total_species} species  |  {total_links} links  |  {n_snaps} snapshots"
    ax_main.text(0.5, -0.02, legend_text, transform=ax_main.transAxes,
                 ha="center", fontsize=9, color="gray")

    plt.tight_layout()

    img_dir = os.path.join(output_dir, "images")
    os.makedirs(img_dir, exist_ok=True)
    out_path = os.path.join(img_dir, "tree_of_life.png")
    fig.savefig(out_path, dpi=dpi, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f"  Tree of life saved to {out_path}")

    # Also generate per-species detail cards
    _generate_species_cards(nodes, img_dir, dpi)

    # Generate per-snapshot genome heatmaps
    _generate_genome_heatmaps(output_dir, dpi=dpi)

    return out_path


def _generate_species_cards(nodes, img_dir, dpi):
    """Generate a detail card for each species with body plan and stats."""
    cards_dir = os.path.join(img_dir, "species_cards")
    os.makedirs(cards_dir, exist_ok=True)

    for gid, snap_idx, cluster, snap in nodes:
        layout = cluster.get("body_layout", [])
        plan = cluster.get("body_plan", {})

        if not plan:
            continue

        fig = plt.figure(figsize=(8, 3.5))
        gs = gridspec.GridSpec(1, 3, width_ratios=[2.5, 2, 2.5], wspace=0.3)

        # Left: body plan grid
        ax_grid = fig.add_subplot(gs[0])
        cells = {(0, 0): "Primary"}
        for name, dx, dy in layout:
            cells[(dx, dy)] = name

        all_x = [p[0] for p in cells]
        all_y = [p[1] for p in cells]
        min_x, max_x = min(all_x) - 1, max(all_x) + 1
        min_y, max_y = min(all_y) - 1, max(all_y) + 1

        ax_grid.set_xlim(min_x - 0.5, max_x + 0.5)
        ax_grid.set_ylim(min_y - 0.5, max_y + 0.5)
        ax_grid.set_aspect("equal")
        ax_grid.set_facecolor("black")
        ax_grid.set_title("Body Layout", fontsize=9)

        for (cx, cy), name in cells.items():
            cid_cell = CELL_NAME_TO_ID.get(name, 0)
            r, g, b = CELL_COLORS_RGB.get(cid_cell, (255, 0, 240))
            ax_grid.add_patch(Rectangle(
                (cx - 0.45, cy - 0.45), 0.9, 0.9,
                facecolor=(r / 255, g / 255, b / 255),
                edgecolor=(0.3, 0.3, 0.3), linewidth=0.5,
            ))
        ax_grid.tick_params(labelsize=6)
        ax_grid.invert_yaxis()

        # Middle: cell legend + stats
        ax_legend = fig.add_subplot(gs[1])
        ax_legend.axis("off")

        size = cluster.get("size", 0)
        mean_ls = cluster.get("mean_lifespan", 0)
        median_ls = cluster.get("median_lifespan", 0)
        start_tick = snap.get("start_tick", 0)

        ax_legend.text(0.0, 1.0, f"Species #{gid}", fontsize=10,
                       color="white", fontweight="bold", va="top",
                       transform=ax_legend.transAxes)
        ax_legend.text(0.0, 0.90, f"Population: {size}", fontsize=8,
                       color="white", va="top", transform=ax_legend.transAxes)
        ax_legend.text(0.0, 0.82, f"Snapshot tick: {start_tick:,}", fontsize=8,
                       color="white", va="top", transform=ax_legend.transAxes)
        ax_legend.text(0.0, 0.74, f"Mean lifespan: {mean_ls:.0f}", fontsize=8,
                       color="white", va="top", transform=ax_legend.transAxes)
        ax_legend.text(0.0, 0.66, f"Median lifespan: {median_ls:.0f}", fontsize=8,
                       color="white", va="top", transform=ax_legend.transAxes)

        sorted_cells = sorted(plan.items(), key=lambda x: -x[1])
        sorted_cells.insert(0, ("Primary", 1))
        y_pos = 0.54
        for name, cnt in sorted_cells:
            cid_cell = CELL_NAME_TO_ID.get(name, 0)
            r, g, b = CELL_COLORS_RGB.get(cid_cell, (255, 0, 240))
            color = (r / 255, g / 255, b / 255)
            ax_legend.add_patch(Rectangle(
                (0.0, y_pos - 0.03), 0.06, 0.04,
                facecolor=color, edgecolor="white", linewidth=0.5,
                transform=ax_legend.transAxes, clip_on=False,
            ))
            ax_legend.text(0.09, y_pos - 0.01, f"{cnt}x {name}",
                           fontsize=7, color="white", va="center",
                           transform=ax_legend.transAxes)
            y_pos -= 0.065

        # Right: cost table
        ax_table = fig.add_subplot(gs[2])
        ax_table.axis("off")

        total_build, total_income, total_maint = _compute_body_costs(plan)

        def fmt(v):
            return f"{v:.2f}" if v != 0 else "—"

        table_data = [
            ["Energy",    fmt(total_build[0]), fmt(total_income[0]), fmt(total_maint[0])],
            ["Water",     fmt(total_build[1]), fmt(total_income[1]), fmt(total_maint[1])],
            ["Nutrients", fmt(total_build[2]), fmt(total_income[2]), fmt(total_maint[2])],
        ]
        col_labels = ["", "Build", "Income/t", "Maint/t"]

        tbl = ax_table.table(
            cellText=table_data, colLabels=col_labels,
            loc="upper center", cellLoc="center",
        )
        tbl.auto_set_font_size(False)
        tbl.set_fontsize(7)
        tbl.scale(1.0, 1.3)
        for (row, col), cell in tbl.get_celld().items():
            cell.set_edgecolor("gray")
            cell.set_text_props(color="white")
            if row == 0:
                cell.set_facecolor("#333333")
            else:
                cell.set_facecolor("#1a1a1a")
        ax_table.set_title("Resource Costs", fontsize=9, pad=8)

        bp_key = cluster.get("body_plan_key", "")
        fig.suptitle(bp_key, fontsize=8, y=1.01)
        plt.tight_layout()

        card_path = os.path.join(cards_dir, f"species_{gid:04d}.png")
        fig.savefig(card_path, dpi=dpi, bbox_inches="tight")
        plt.close(fig)

    print(f"  {len(nodes)} species cards saved to {cards_dir}/")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Tree of Life: run sims, cluster species, link & plot",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("snapshot_dir",
                        help="Directory containing save-file snapshots")
    parser.add_argument("--output", type=str, default=None,
                        help="Output directory (default: <snapshot_dir>_tree)")
    parser.add_argument("--all", action="store_true",
                        help="Run all stages: simulate → cluster → link → plot")
    parser.add_argument("--simulate", action="store_true",
                        help="Stage 1: run simulations from save files")
    parser.add_argument("--cluster", action="store_true",
                        help="Stage 2: DBSCAN cluster genomes within each snapshot")
    parser.add_argument("--link", action="store_true",
                        help="Stage 3: link species across adjacent snapshots")
    parser.add_argument("--plot", action="store_true",
                        help="Stage 4: render the tree of life diagram")

    # Simulation params
    parser.add_argument("--ticks", type=int, default=1000,
                        help="Ticks to simulate per snapshot")
    parser.add_argument("--log-every", type=int, default=1,
                        help="Log plant state every N ticks")
    parser.add_argument("--workers", type=int, default=None,
                        help="Max parallel simulation workers (default: CPU count / 2)")

    # Clustering params
    parser.add_argument("--eps", type=int, default=200,
                        help="DBSCAN eps: max byte difference for same species")
    parser.add_argument("--min-samples", type=int, default=5,
                        help="DBSCAN min_samples")
    parser.add_argument("--max-plants", type=int, default=5000,
                        help="Subsample if more plants than this per snapshot")
    parser.add_argument("--seed", type=int, default=0,
                        help="RNG seed for subsampling")

    # Linking params
    parser.add_argument("--link-eps", type=int, default=300,
                        help="Max centroid distance to link species across snapshots")

    # Plot params
    parser.add_argument("--dpi", type=int, default=150)

    args = parser.parse_args()

    snapshot_dir = os.path.abspath(args.snapshot_dir)
    output_dir = args.output or (snapshot_dir.rstrip("/") + "_tree")
    os.makedirs(output_dir, exist_ok=True)

    run_all = args.all
    if not any([args.simulate, args.cluster, args.link, args.plot, run_all]):
        print("No stage selected. Use --all or --simulate/--cluster/--link/--plot.")
        parser.print_help()
        sys.exit(1)

    max_workers = args.workers or max(1, (os.cpu_count() or 4) // 2)

    if run_all or args.simulate:
        print(f"\n=== Stage 1: Simulate ({args.ticks} ticks per snapshot) ===")
        stage_simulate(snapshot_dir, output_dir, args.ticks, args.log_every, max_workers)

    if run_all or args.cluster:
        print(f"\n=== Stage 2: Cluster (eps={args.eps}, min_samples={args.min_samples}) ===")
        stage_cluster(output_dir, args.eps, args.min_samples, args.max_plants, args.seed)

    if run_all or args.link:
        print(f"\n=== Stage 3: Link species across snapshots (link_eps={args.link_eps}) ===")
        stage_link(output_dir, args.link_eps)

    if run_all or args.plot:
        print(f"\n=== Stage 4: Plot tree of life ===")
        stage_plot(output_dir, args.dpi)

    print("\nDone.")


if __name__ == "__main__":
    main()
