#!/usr/bin/env python3
"""
generate_overview.py — Generate a Markdown overview report from simulation logs.

Reads the same Parquet logs produced by run_logged.py and generates:
  - Simulation metadata (dimensions, ticks, seed)
  - Population graph over time
  - Water and nutrient distribution maps
  - Species clusters (DBSCAN on genome Hamming distance)
  - Per-cluster: spatial heatmap, lifespan histogram, body plans, opcode usage

Usage:
    python tools/generate_overview.py logs/
    python tools/generate_overview.py logs/ --eps 200 --min-samples 5
    python tools/generate_overview.py logs/ --min-body-count 10 --output report/

Requirements:
    pip install pyarrow numpy matplotlib scikit-learn
"""

import argparse
import math
import os
import sys
from collections import Counter

import numpy as np

try:
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
    plt.style.use("dark_background")
except ImportError:
    print("matplotlib required:  pip install matplotlib", file=sys.stderr)
    sys.exit(1)

try:
    from sklearn.cluster import DBSCAN
except ImportError:
    print("scikit-learn required:  pip install scikit-learn", file=sys.stderr)
    sys.exit(1)

# Add src/python so we can import brain_viewer
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_SRC_DIR = os.path.join(_SCRIPT_DIR, "..", "src", "python")
if _SRC_DIR not in sys.path:
    sys.path.insert(0, _SRC_DIR)

try:
    from plantbraingrid.brain_viewer import OPCODES, NUM_OPCODES, CELL_TYPE_NAMES
except ImportError:
    NUM_OPCODES = 0xA0
    OPCODES = {}
    CELL_TYPE_NAMES = {}

# ---------------------------------------------------------------------------
# Opcode table for arg counts
# ---------------------------------------------------------------------------

# Map opcode number -> (name, n_args)
OPCODE_TABLE = {}
for raw_op, (name, n_args) in OPCODES.items():
    OPCODE_TABLE[raw_op] = (name, n_args)

# Default arg count: 0 for unknown opcodes
def _op_info(opcode: int):
    return OPCODE_TABLE.get(opcode, (f"OP_0x{opcode:02X}", 0))


# ---------------------------------------------------------------------------
# Cell type costs (mirroring config.hpp defaults)
# ---------------------------------------------------------------------------

# (build_energy, build_water, build_nutrients, maint_energy, maint_water, maint_nutrients)
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

# Income per cell per tick (energy, water, nutrients)
CELL_INCOME = {
    "SmallLeaf":  (1.0, 0,   0),
    "BigLeaf":    (5.0, 0,   0),
    "FiberRoot":  (0,   1.2, 1.0),
    "TapRoot":    (0,   3.5, 0),
}


# ---------------------------------------------------------------------------
# Data loading helpers
# ---------------------------------------------------------------------------

def load_tick_stats(log_dir: str):
    path = os.path.join(log_dir, "tick_stats.parquet")
    if not os.path.exists(path):
        print(f"[ERR] {path} not found", file=sys.stderr)
        sys.exit(1)
    t = pq.read_table(path)
    return {col: t[col].to_pylist() for col in t.column_names}


def load_plant_events(log_dir: str):
    path = os.path.join(log_dir, "plant_events.parquet")
    if not os.path.exists(path):
        print(f"[ERR] {path} not found", file=sys.stderr)
        sys.exit(1)
    t = pq.read_table(path)
    return {col: t[col].to_pylist() for col in t.column_names}


def load_genomes(log_dir: str):
    path = os.path.join(log_dir, "genomes.parquet")
    if not os.path.exists(path):
        print(f"[ERR] {path} not found", file=sys.stderr)
        sys.exit(1)
    t = pq.read_table(path)
    return t


# ---------------------------------------------------------------------------
# Genome analysis: extract body plan and opcodes
# ---------------------------------------------------------------------------

def _extract_placed_cells(genome: bytes) -> list[tuple[str, int, int]]:
    """Walk genome and return adjacency-valid (cell_name, dx, dy) placements.

    Only cells placed adjacent to an already-occupied position are kept.
    The primary cell at (0, 0) is the starting occupied set.
    """
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
                # Only accept if adjacent to an existing cell
                if any((dx + ox, dy + oy) in occupied for ox, oy in _ADJ):
                    if pos not in occupied:
                        occupied.add(pos)
                        cells.append((cell_name, dx, dy))

        i += 1 + n_args
    return cells


def extract_body_plan(genome: bytes) -> dict[str, int]:
    """Walk genome opcodes and count PLACE_CELL types (adjacency-valid only)."""
    counts: dict[str, int] = {}
    for cell_name, _dx, _dy in _extract_placed_cells(genome):
        counts[cell_name] = counts.get(cell_name, 0) + 1
    return counts


def extract_body_layout(genome: bytes) -> list[tuple[str, int, int]]:
    """Walk genome and return adjacency-valid (cell_name, dx, dy) placements."""
    return _extract_placed_cells(genome)


def extract_opcodes(genome: bytes) -> list[str]:
    """Walk genome and return list of opcode names encountered.
    Unknown opcodes (OP_0x...) are grouped as 'UNK'."""
    ops = []
    i = 0
    n = len(genome)
    while i < n:
        raw = genome[i]
        opcode = raw % NUM_OPCODES
        name, n_args = _op_info(opcode)
        if name.startswith("OP_0x"):
            ops.append("UNK")
        else:
            ops.append(name)
        i += 1 + n_args
    return ops


def body_plan_key(plan: dict[str, int]) -> str:
    """Canonical string key for a body plan dict."""
    if not plan:
        return "(no cells)"
    parts = sorted(plan.items())
    return ", ".join(f"{count}x {name}" for name, count in parts)


# ---------------------------------------------------------------------------
# Hamming distance (reused from plot_species_map.py)
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


# ---------------------------------------------------------------------------
# Colour palette (reused from plot_species_map.py)
# ---------------------------------------------------------------------------

def make_palette(n: int) -> list:
    base = (
        [plt.get_cmap("tab20")(i) for i in range(20)]
        + [plt.get_cmap("tab20b")(i) for i in range(20)]
    )
    return [base[i % len(base)] for i in range(max(n, 1))]


# ---------------------------------------------------------------------------
# Plot helpers — each returns relative path to saved image
# ---------------------------------------------------------------------------

def plot_population(tick_stats: dict, img_dir: str) -> str:
    ticks = tick_stats["tick"]
    pop = tick_stats["plant_count"]

    fig, ax = plt.subplots(figsize=(10, 4))
    ax.plot(ticks, pop, linewidth=0.8, color="cyan")
    ax.set_xlabel("Tick")
    ax.set_ylabel("Plant Count")
    ax.set_title("Population Over Time")
    ax.grid(True, alpha=0.3)
    plt.tight_layout()

    path = os.path.join(img_dir, "population.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    return "images/population.png"


def plot_terrain_maps(log_dir: str, img_dir: str) -> tuple[str, str]:
    """Generate water and nutrient distribution maps using Perlin noise params."""
    # We'll reconstruct from the simulation. Try importing pbg bindings.
    try:
        sys.path.insert(0, os.path.join(_SCRIPT_DIR, ".."))
        import _plantbraingrid as pbg

        # Read world dimensions from tick_stats or use defaults
        stats_path = os.path.join(log_dir, "tick_stats.parquet")
        cfg = pbg.get_config()
        w, h = cfg.world_width, cfg.world_height

        world = pbg.World(w, h, 42)

        water = np.zeros((h, w), dtype=np.float32)
        nutrients = np.zeros((h, w), dtype=np.float32)
        for y in range(h):
            for x in range(w):
                cell = world.cell_at(x, y)
                water[y, x] = cell.water_level
                nutrients[y, x] = cell.nutrient_level

        fig, ax = plt.subplots(figsize=(6, 6))
        ax.imshow(water, origin="lower", cmap="Blues", aspect="equal")
        ax.set_title("Water Distribution")
        ax.set_xlabel("X")
        ax.set_ylabel("Y")
        plt.tight_layout()
        water_path = os.path.join(img_dir, "water_map.png")
        fig.savefig(water_path, dpi=150, bbox_inches="tight")
        plt.close(fig)

        fig, ax = plt.subplots(figsize=(6, 6))
        ax.imshow(nutrients, origin="lower", cmap="YlOrBr", aspect="equal")
        ax.set_title("Nutrient Distribution")
        ax.set_xlabel("X")
        ax.set_ylabel("Y")
        plt.tight_layout()
        nut_path = os.path.join(img_dir, "nutrient_map.png")
        fig.savefig(nut_path, dpi=150, bbox_inches="tight")
        plt.close(fig)

        return "images/water_map.png", "images/nutrient_map.png"
    except (ImportError, Exception) as e:
        print(f"[WARN] Could not generate terrain maps: {e}")
        return "", ""


def plot_cluster_heatmap(xs, ys, mask, cluster_id, color, img_dir, world_w, world_h) -> str:
    fig, ax = plt.subplots(figsize=(5, 5))
    cmap = LinearSegmentedColormap.from_list(f"c{cluster_id}", ["black", color])
    H, xedges, yedges = np.histogram2d(
        xs[mask], ys[mask], bins=64,
        range=[[0, world_w], [0, world_h]],
    )
    ax.imshow(H.T, origin="lower", aspect="equal", interpolation="bilinear",
              extent=[0, world_w, 0, world_h], cmap=cmap)
    ax.set_title(f"Cluster {cluster_id} Spatial Distribution (n={int(mask.sum())})")
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    plt.tight_layout()

    fname = f"cluster_{cluster_id}_heatmap.png"
    path = os.path.join(img_dir, fname)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    return f"images/{fname}"


def plot_lifespan_histogram(lifespans, cluster_id, color, img_dir) -> str:
    fig, ax = plt.subplots(figsize=(5, 3))
    ax.hist(lifespans, bins=min(50, max(10, len(lifespans) // 5)),
            color=color, alpha=0.85, edgecolor="white", linewidth=0.3)
    avg = np.mean(lifespans)
    ax.axvline(avg, color="yellow", linestyle="--", linewidth=1, label=f"mean={avg:.0f}")
    ax.set_xlabel("Lifespan (ticks)")
    ax.set_ylabel("Count")
    ax.set_title(f"Cluster {cluster_id} Lifespan Distribution")
    ax.legend(fontsize=8)
    plt.tight_layout()

    fname = f"cluster_{cluster_id}_lifespan.png"
    path = os.path.join(img_dir, fname)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    return f"images/{fname}"


# Cell name -> cell type ID (for colour lookup)
CELL_NAME_TO_ID = {v: k for k, v in CELL_TYPE_NAMES.items()}

# Visualizer-matching RGB colours (0-255)
CELL_COLORS_RGB = {
    0: (50, 50, 50),       1: (139, 69, 19),     2: (34, 139, 34),
    3: (0, 100, 0),        4: (139, 90, 43),      5: (255, 220, 50),
    6: (101, 67, 33),      7: (128, 128, 128),    8: (255, 69, 0),
    9: (110, 55, 90),      10: (255, 200, 50),    11: (50, 150, 255),
    12: (180, 120, 60),    13: (200, 0, 0),
}


def plot_body_plan(layout: list[tuple[str, int, int]], plan: dict[str, int],
                   plan_key: str, cluster_id: int, plan_idx: int,
                   img_dir: str) -> str:
    """Render body plan: visual grid (left), cell count list (middle-right)."""
    import matplotlib.gridspec as gridspec
    from matplotlib.patches import Rectangle

    # Build cell grid: primary at (0,0) plus all PLACE_CELL offsets
    cells: dict[tuple[int, int], str] = {(0, 0): "Primary"}
    for name, dx, dy in layout:
        cells[(dx, dy)] = name  # last write wins for overlapping positions

    if not cells:
        return ""

    all_x = [p[0] for p in cells]
    all_y = [p[1] for p in cells]
    min_x, max_x = min(all_x) - 1, max(all_x) + 1
    min_y, max_y = min(all_y) - 1, max(all_y) + 1
    grid_w = max_x - min_x + 1
    grid_h = max_y - min_y + 1

    fig = plt.figure(figsize=(8, max(3, grid_h * 0.4 + 1)))
    gs = gridspec.GridSpec(1, 2, width_ratios=[3, 2], wspace=0.3)

    # ── Left: visual grid ────────────────────────────────────────────────────
    ax_grid = fig.add_subplot(gs[0])
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

    # ── Right: cell count list ───────────────────────────────────────────────
    ax_text = fig.add_subplot(gs[1])
    ax_text.axis("off")

    # Build sorted cell list with colour swatches
    lines = []
    sorted_cells = sorted(plan.items(), key=lambda x: -x[1])
    # Add Primary (always 1)
    sorted_cells.insert(0, ("Primary", 1))

    y_pos = 0.95
    for name, count in sorted_cells:
        cid_cell = CELL_NAME_TO_ID.get(name, 0)
        r, g, b = CELL_COLORS_RGB.get(cid_cell, (255, 0, 240))
        color = (r / 255, g / 255, b / 255)

        # Colour swatch
        ax_text.add_patch(Rectangle(
            (0.0, y_pos - 0.03), 0.06, 0.04,
            facecolor=color, edgecolor="white", linewidth=0.5,
            transform=ax_text.transAxes, clip_on=False,
        ))
        ax_text.text(0.09, y_pos - 0.01, f"{count}x {name}",
                     fontsize=8, color="white", va="center",
                     transform=ax_text.transAxes)
        y_pos -= 0.07

    fig.suptitle(f"{plan_key}", fontsize=9, y=1.01)
    plt.tight_layout()

    fname = f"cluster_{cluster_id}_body_{plan_idx}.png"
    path = os.path.join(img_dir, fname)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    return f"images/{fname}"


def body_plan_cost_table(plan: dict[str, int]) -> list[str]:
    """Return markdown lines for a 3x3 resource cost table (rows=resource, cols=build/income/maint)."""
    total_build = [0.0, 0.0, 0.0]   # energy, water, nutrients
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

    def fmt(v):
        return f"{v:.2f}" if v != 0 else "—"

    lines = [
        "| | Build | Income/tick | Maint/tick |",
        "|---|---|---|---|",
        f"| Energy | {fmt(total_build[0])} | {fmt(total_income[0])} | {fmt(total_maint[0])} |",
        f"| Water | {fmt(total_build[1])} | {fmt(total_income[1])} | {fmt(total_maint[1])} |",
        f"| Nutrients | {fmt(total_build[2])} | {fmt(total_income[2])} | {fmt(total_maint[2])} |",
    ]
    return lines


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate Markdown overview report from simulation logs",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("log_dir", help="Directory containing Parquet log files")
    parser.add_argument("--eps", type=int, default=200,
                        help="DBSCAN eps: max byte difference for same cluster")
    parser.add_argument("--min-samples", type=int, default=5,
                        help="DBSCAN min_samples: min plants to form a cluster")
    parser.add_argument("--min-body-count", type=int, default=5,
                        help="Min occurrences for a body plan to be displayed")
    parser.add_argument("--max-plants", type=int, default=5000,
                        help="Subsample if more plants than this")
    parser.add_argument("--output", type=str, default=None,
                        help="Output directory (default: <log_dir>/report)")
    parser.add_argument("--min-lifespan", type=int, default=None,
                        help="Drop plants that lived fewer than N ticks")
    parser.add_argument("--seed", type=int, default=0, help="RNG seed for subsampling")
    args = parser.parse_args()

    log_dir = os.path.abspath(args.log_dir)
    out_dir = args.output or os.path.join(log_dir, "report")
    img_dir = os.path.join(out_dir, "images")
    os.makedirs(img_dir, exist_ok=True)

    md_lines: list[str] = []

    def md(line: str = ""):
        md_lines.append(line)

    # ── Load data ────────────────────────────────────────────────────────────
    print("Loading tick stats...")
    tick_stats = load_tick_stats(log_dir)

    print("Loading plant events...")
    events = load_plant_events(log_dir)

    print("Loading genomes...")
    genome_table = load_genomes(log_dir)

    # ── Simulation metadata ──────────────────────────────────────────────────
    ticks = tick_stats["tick"]
    total_ticks = max(ticks) - min(ticks) + 1 if ticks else 0
    max_pop = max(tick_stats["plant_count"]) if tick_stats["plant_count"] else 0

    # Try to get world dimensions from bindings
    world_w, world_h = 128, 128
    try:
        sys.path.insert(0, os.path.join(_SCRIPT_DIR, ".."))
        import _plantbraingrid as pbg
        cfg = pbg.get_config()
        world_w, world_h = cfg.world_width, cfg.world_height
    except ImportError:
        pass

    md("# Simulation Overview Report")
    md()
    md("## Simulation Parameters")
    md()
    md(f"| Parameter | Value |")
    md(f"|-----------|-------|")
    md(f"| World Size | {world_w} x {world_h} |")
    md(f"| Total Ticks | {total_ticks:,} |")
    md(f"| Peak Population | {max_pop:,} |")
    md(f"| Total Plants Born | {sum(1 for e in events['event'] if e == 'birth'):,} |")
    md(f"| Total Deaths | {sum(1 for e in events['event'] if e == 'death'):,} |")
    md()

    # ── Population graph ─────────────────────────────────────────────────────
    print("Plotting population...")
    pop_img = plot_population(tick_stats, img_dir)
    md("## Population Over Time")
    md()
    md(f"![Population]({pop_img})")
    md()

    # ── Terrain maps ─────────────────────────────────────────────────────────
    print("Generating terrain maps...")
    water_img, nutrient_img = plot_terrain_maps(log_dir, img_dir)
    if water_img and nutrient_img:
        md("## Terrain Distribution")
        md()
        md(f"| Water | Nutrients |")
        md(f"|-------|-----------|")
        md(f"| ![Water]({water_img}) | ![Nutrients]({nutrient_img}) |")
        md()

    # ── Genome clustering ────────────────────────────────────────────────────
    print("Preparing genomes for clustering...")

    plant_ids = genome_table["plant_id"].to_pylist()
    ticks_born = genome_table["tick_born"].to_pylist()
    raw_genomes = genome_table["genome"]

    # Build position and lifetime lookups
    birth_pos: dict[int, tuple[int, int]] = {}
    birth_ticks: dict[int, int] = {}
    death_ticks: dict[int, int] = {}

    for i in range(len(events["event"])):
        pid = events["plant_id"][i]
        if events["event"][i] == "birth":
            birth_pos[pid] = (events["x"][i], events["y"][i])
            birth_ticks[pid] = events["tick"][i]
        elif events["event"][i] == "death":
            death_ticks[pid] = events["tick"][i]

    final_tick = max(ticks) if ticks else 0

    # Filter to plants with positions
    keep_indices = [i for i, pid in enumerate(plant_ids) if pid in birth_pos]

    # Filter by minimum lifespan
    if args.min_lifespan is not None:
        filtered = []
        for i in keep_indices:
            pid = plant_ids[i]
            tb = birth_ticks.get(pid, 0)
            lived = death_ticks.get(pid, final_tick) - tb
            if lived >= args.min_lifespan:
                filtered.append(i)
        dropped = len(keep_indices) - len(filtered)
        keep_indices = filtered
        print(f"[INFO] Dropped {dropped} plants with lifespan < {args.min_lifespan} ticks")

    if not keep_indices:
        print("[ERR] No plants with birth positions found.", file=sys.stderr)
        sys.exit(1)

    # Subsample
    if len(keep_indices) > args.max_plants:
        rng = np.random.default_rng(args.seed)
        keep_indices = sorted(
            rng.choice(keep_indices, size=args.max_plants, replace=False).tolist()
        )
        print(f"[INFO] Subsampled to {args.max_plants} plants")

    sel_ids = [plant_ids[i] for i in keep_indices]
    sel_born = [ticks_born[i] for i in keep_indices]
    genomes = np.array(
        [np.frombuffer(raw_genomes[i].as_py(), dtype=np.uint8) for i in keep_indices],
        dtype=np.uint8,
    )
    xs = np.array([birth_pos[pid][0] for pid in sel_ids])
    ys = np.array([birth_pos[pid][1] for pid in sel_ids])

    # Lifespans
    lifespans = np.array([
        death_ticks.get(pid, final_tick) - birth_ticks.get(pid, 0)
        for pid in sel_ids
    ], dtype=np.float64)

    N = len(sel_ids)
    genome_len = genomes.shape[1]
    print(f"Clustering {N} plants (genome={genome_len} bytes, eps={args.eps})...")

    dist = hamming_matrix(genomes)
    labels = DBSCAN(eps=args.eps, min_samples=args.min_samples,
                    metric="precomputed").fit_predict(dist)
    n_clusters = int((labels >= 0).any() and labels.max() + 1) if len(labels) else 0
    n_noise = int((labels == -1).sum())

    print(f"Found {n_clusters} clusters, {n_noise} noise points")

    md("## Species Clusters")
    md()
    md(f"Clustering: DBSCAN with eps={args.eps} bytes, min_samples={args.min_samples}")
    md(f"- **{N:,}** plants analyzed")
    md(f"- **{n_clusters}** clusters found")
    md(f"- **{n_noise:,}** unclustered (noise)")
    md()

    palette = make_palette(n_clusters)

    # ── Per-cluster analysis ─────────────────────────────────────────────────
    for cid in range(n_clusters):
        mask = labels == cid
        cluster_size = int(mask.sum())
        cluster_ids = [sel_ids[i] for i in range(N) if mask[i]]
        cluster_genomes_raw = [raw_genomes[keep_indices[i]].as_py() for i in range(N) if mask[i]]
        cluster_lifespans = lifespans[mask]
        color = palette[cid]

        md(f"### Cluster {cid} ({cluster_size} plants)")
        md()
        md(f"- Average lifespan: **{np.mean(cluster_lifespans):.0f}** ticks")
        md(f"- Median lifespan: **{np.median(cluster_lifespans):.0f}** ticks")
        md(f"- Max lifespan: **{np.max(cluster_lifespans):.0f}** ticks")
        md()

        # Heatmap
        print(f"  Cluster {cid}: heatmap...")
        hm_img = plot_cluster_heatmap(xs, ys, mask, cid, color, img_dir, world_w, world_h)
        md(f"#### Spatial Distribution")
        md(f"![Cluster {cid} heatmap]({hm_img})")
        md()

        # Lifespan histogram
        print(f"  Cluster {cid}: lifespan histogram...")
        ls_img = plot_lifespan_histogram(cluster_lifespans, cid, color, img_dir)
        md(f"#### Lifespan Distribution")
        md(f"![Cluster {cid} lifespan]({ls_img})")
        md()

        # Body plans
        print(f"  Cluster {cid}: body plans...")
        plan_counter: Counter = Counter()
        plan_map: dict[str, dict[str, int]] = {}
        layout_map: dict[str, list[tuple[str, int, int]]] = {}

        for genome_bytes in cluster_genomes_raw:
            plan = extract_body_plan(genome_bytes)
            key = body_plan_key(plan)
            plan_counter[key] += 1
            if key not in plan_map:
                plan_map[key] = plan
                layout_map[key] = extract_body_layout(genome_bytes)

        md(f"#### Body Plans")
        md()

        plan_idx = 0
        displayed_any = False
        for key, count in plan_counter.most_common():
            if count < args.min_body_count:
                continue
            displayed_any = True
            plan = plan_map[key]
            layout = layout_map.get(key, [])
            pct = count / cluster_size * 100
            md(f"**{key}** — {count} plants ({pct:.1f}%)")
            md()

            if plan:
                bp_img = plot_body_plan(layout, plan, key, cid, plan_idx, img_dir)
                if bp_img:
                    md(f"![Body plan]({bp_img})")
                    md()
                for line in body_plan_cost_table(plan):
                    md(line)
                md()
            plan_idx += 1

        if not displayed_any:
            md(f"_No body plan appeared >= {args.min_body_count} times._")
            md()

        # Opcode usage
        print(f"  Cluster {cid}: opcode analysis...")
        op_counter: Counter = Counter()
        for genome_bytes in cluster_genomes_raw:
            ops = extract_opcodes(genome_bytes)
            for op in ops:
                op_counter[op] += 1

        threshold = cluster_size * 0.8
        common_ops = [(op, cnt) for op, cnt in op_counter.most_common() if cnt >= threshold]

        md(f"#### Common Operations (>= 80% of cluster)")
        md()
        if common_ops:
            md(f"| Operation | Count | % of Cluster |")
            md(f"|-----------|-------|--------------|")
            for op, cnt in common_ops:
                pct = cnt / cluster_size * 100
                md(f"| {op} | {cnt:,} | {pct:.0f}% |")
            md()
        else:
            md("_No operation reached the 80% threshold._")
            md()

        md("---")
        md()

    # ── Write markdown ───────────────────────────────────────────────────────
    md_path = os.path.join(out_dir, "overview.md")
    with open(md_path, "w") as f:
        f.write("\n".join(md_lines))
        f.write("\n")

    print(f"\nReport written to {md_path}")
    print(f"Images in {img_dir}/")


if __name__ == "__main__":
    main()
