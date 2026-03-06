#!/usr/bin/env python3
"""
plot_species_map.py — Cluster plants by genome similarity and plot by world position.

Loads Parquet logs produced by run_logged.py, clusters plants by pairwise
byte-difference (Hamming distance) using DBSCAN, then draws a scatter plot
where each point is a plant at its birth position coloured by cluster ("species").

Data sources (from the log directory):
  genomes.parquet       genome bytes per plant
  plant_events.parquet  birth positions + death ticks (for --min-ticks-lived)
  tick_stats.parquet    final tick (for crediting still-alive plants)

Usage:
    python tools/plot_species_map.py logs/
    python tools/plot_species_map.py logs/ --eps 150
    python tools/plot_species_map.py logs/ --min-ticks-lived 100 --output species.png
    python tools/plot_species_map.py logs/ --eps 80 --min-samples 3 --point-size 12
    python tools/plot_species_map.py logs/ --ball-radius 20
    python tools/plot_species_map.py logs/ --ball-radius 0  # auto radius

--eps is the maximum number of bytes two genomes may differ and still be
considered the same species.  Tune it relative to genome length (1024 bytes):
  tight clusters   →  small eps  (e.g. 50–100)
  loose / species  →  large eps  (e.g. 200–400)

--ball-radius enables perimeter drawing (2D ball pivot / alpha shape).  The
ball is a circle rolled over the Delaunay triangulation of each cluster's
plant positions; boundary edges are those shared by only one triangle whose
circumradius ≤ ball-radius.  Larger radius = looser / convex-hull-like
boundary; smaller radius = tighter, can split into multiple rings.
Pass 0 to use auto radius (3× average nearest-neighbour distance per cluster).

Requirements:
    pip install pyarrow numpy matplotlib scikit-learn scipy
"""

import argparse
import os
import sys

import numpy as np

try:
    import pyarrow.parquet as pq
except ImportError:
    print("pyarrow required:  pip install pyarrow", file=sys.stderr)
    sys.exit(1)

try:
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    plt.style.use("dark_background")
except ImportError:
    print("matplotlib required:  pip install matplotlib", file=sys.stderr)
    sys.exit(1)

try:
    from sklearn.cluster import DBSCAN
except ImportError:
    print("scikit-learn required:  pip install scikit-learn", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Distance computation
# ---------------------------------------------------------------------------

def hamming_matrix(genomes: np.ndarray) -> np.ndarray:
    """Return (N, N) int32 pairwise byte-difference counts."""
    try:
        from scipy.spatial.distance import cdist
        return (cdist(genomes.astype(np.float64),
                      genomes.astype(np.float64),
                      metric="hamming") * genomes.shape[1]).astype(np.int32)
    except ImportError:
        N = len(genomes)
        dist = np.empty((N, N), dtype=np.int32)
        for i in range(N):
            dist[i] = (genomes[i:i+1] != genomes).sum(axis=1)
        return dist


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

def load_positions(log_dir: str) -> dict[int, tuple[int, int]]:
    """Return {plant_id: (x, y)} from birth events."""
    path = os.path.join(log_dir, "plant_events.parquet")
    if not os.path.exists(path):
        print(f"[ERR] {path} not found", file=sys.stderr)
        sys.exit(1)
    t = pq.read_table(path, columns=["event", "plant_id", "x", "y"])
    return {
        pid: (x, y)
        for event, pid, x, y in zip(
            t["event"].to_pylist(),
            t["plant_id"].to_pylist(),
            t["x"].to_pylist(),
            t["y"].to_pylist(),
        )
        if event == "birth"
    }


def load_lifetimes(log_dir: str) -> tuple[dict[int, int], int]:
    """Return ({plant_id: death_tick}, final_tick)."""
    events_path = os.path.join(log_dir, "plant_events.parquet")
    stats_path  = os.path.join(log_dir, "tick_stats.parquet")

    t = pq.read_table(events_path, columns=["event", "plant_id", "tick"])
    death_ticks: dict[int, int] = {}
    for event, pid, tick in zip(
        t["event"].to_pylist(),
        t["plant_id"].to_pylist(),
        t["tick"].to_pylist(),
    ):
        if event == "death":
            death_ticks[pid] = tick

    final_tick = max(death_ticks.values()) if death_ticks else 0
    if os.path.exists(stats_path):
        st = pq.read_table(stats_path, columns=["tick"])
        tks = st["tick"].to_pylist()
        if tks:
            final_tick = max(tks)

    return death_ticks, final_tick


# ---------------------------------------------------------------------------
# 2D Ball Pivot / Alpha Shape perimeter
# ---------------------------------------------------------------------------

def _circumradius(p0: np.ndarray, p1: np.ndarray, p2: np.ndarray) -> float:
    """Circumradius of a triangle given three 2-D points."""
    a = np.linalg.norm(p1 - p0)
    b = np.linalg.norm(p2 - p1)
    c = np.linalg.norm(p0 - p2)
    # |cross product| = 2 * area
    area2 = abs((p1[0] - p0[0]) * (p2[1] - p0[1])
                - (p2[0] - p0[0]) * (p1[1] - p0[1]))
    return (a * b * c) / area2 if area2 > 1e-12 else np.inf


def _auto_radius(points: np.ndarray) -> float:
    """3× average nearest-neighbour distance — a sensible default ball radius."""
    from scipy.spatial import cKDTree
    dists, _ = cKDTree(points).query(points, k=2)   # k=2: self + nearest
    return float(dists[:, 1].mean()) * 3.0


def ball_pivot_perimeter(points: np.ndarray, radius: float) -> list[np.ndarray]:
    """Return boundary polygon(s) of a point cloud using a 2-D ball pivot.

    Rolls a circle of *radius* over the Delaunay triangulation of *points*.
    Edges that belong to exactly one triangle whose circumradius ≤ radius are
    boundary edges (the alpha-shape criterion).  Those edges are then stitched
    into closed rings and returned as a list of (M, 2) vertex arrays.

    points : (N, 2) float array
    radius : ball radius in the same units as points
    """
    from collections import defaultdict
    from scipy.spatial import Delaunay

    if len(points) < 3:
        return []

    try:
        tri = Delaunay(points)
    except Exception:
        return []

    # Count how many alpha-valid triangles each edge belongs to.
    # Edges with count == 1 are on the boundary.
    edge_count: dict[tuple[int, int], int] = defaultdict(int)
    for s in tri.simplices:
        i, j, k = int(s[0]), int(s[1]), int(s[2])
        if _circumradius(points[i], points[j], points[k]) <= radius:
            for a, b in ((i, j), (j, k), (i, k)):
                edge_count[(min(a, b), max(a, b))] += 1

    boundary = [(a, b) for (a, b), cnt in edge_count.items() if cnt == 1]
    if not boundary:
        return []

    # Build adjacency list for boundary vertices.
    adj: dict[int, list[int]] = defaultdict(list)
    for a, b in boundary:
        adj[a].append(b)
        adj[b].append(a)

    # Trace closed rings using edge-based visited tracking.
    visited_edges: set[tuple[int, int]] = set()
    rings: list[np.ndarray] = []

    for start_a, start_b in boundary:
        edge = (min(start_a, start_b), max(start_a, start_b))
        if edge in visited_edges:
            continue

        visited_edges.add(edge)
        ring = [start_a, start_b]
        prev, cur = start_a, start_b

        while True:
            # Prefer unvisited neighbours; stop when we close the loop.
            moved = False
            for nxt in adj[cur]:
                if nxt == prev:
                    continue
                e = (min(cur, nxt), max(cur, nxt))
                if e not in visited_edges:
                    visited_edges.add(e)
                    prev, cur = cur, nxt
                    ring.append(cur)
                    moved = True
                    break

            if not moved or cur == ring[0]:
                break

        if len(ring) >= 3:
            rings.append(points[np.array(ring)])

    return rings


# ---------------------------------------------------------------------------
# Colour palette — 40 visually distinct colours, cycling for large K
# ---------------------------------------------------------------------------

def make_palette(n: int) -> list:
    base = (
        [plt.get_cmap("tab20")(i) for i in range(20)]
        + [plt.get_cmap("tab20b")(i) for i in range(20)]
    )
    return [base[i % len(base)] for i in range(max(n, 1))]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Genome-cluster scatter plot on world coordinates",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("log_dir",
                        help="Directory containing the Parquet log files")
    parser.add_argument("--eps", type=int, default=200,
                        help="Max bytes different to be in the same cluster")
    parser.add_argument("--min-samples", type=int, default=2,
                        help="Min plants required to form a cluster (DBSCAN)")
    parser.add_argument("--min-ticks-lived", type=int, default=None,
                        help="Exclude plants that lived fewer than N ticks")
    parser.add_argument("--max-plants", type=int, default=5000,
                        help="Subsample randomly if more plants than this")
    parser.add_argument("--no-noise", action="store_true",
                        help="Hide unclustered noise points (DBSCAN label -1)")
    parser.add_argument("--ball-radius", type=float, default=None,
                        help="Enable perimeter outlines; ball radius in world units "
                             "(0 = auto: 3× avg nearest-neighbour distance per cluster)")
    parser.add_argument("--point-size", type=float, default=8.0,
                        help="Marker area in points²")
    parser.add_argument("--output", default=None,
                        help="Save figure to file instead of displaying")
    parser.add_argument("--dpi", type=int, default=150)
    parser.add_argument("--seed", type=int, default=0,
                        help="RNG seed for subsampling")
    args = parser.parse_args()

    log_dir = os.path.abspath(args.log_dir)

    # ── Load genomes ──────────────────────────────────────────────────────────
    genomes_path = os.path.join(log_dir, "genomes.parquet")
    if not os.path.exists(genomes_path):
        print(f"[ERR] {genomes_path} not found", file=sys.stderr)
        sys.exit(1)

    print(f"Loading {genomes_path} ...")
    gt         = pq.read_table(genomes_path)
    plant_ids  = gt["plant_id"].to_pylist()
    ticks_born = gt["tick_born"].to_pylist()
    raw_genomes = gt["genome"]

    # ── Load birth positions ──────────────────────────────────────────────────
    print("Loading plant positions ...")
    positions = load_positions(log_dir)

    # ── Lifetime filter ───────────────────────────────────────────────────────
    keep = [pid in positions for pid in plant_ids]   # drop plants with no position

    if args.min_ticks_lived is not None:
        print(f"Loading lifetimes (min_ticks_lived={args.min_ticks_lived}) ...")
        death_ticks, final_tick = load_lifetimes(log_dir)
        print(f"  final_tick={final_tick}")
        for i, (pid, tb) in enumerate(zip(plant_ids, ticks_born)):
            if keep[i]:
                lived = death_ticks.get(pid, final_tick) - tb
                keep[i] = lived >= args.min_ticks_lived

    indices = [i for i, k in enumerate(keep) if k]
    if not indices:
        print("No plants pass the current filters.", file=sys.stderr)
        sys.exit(1)

    # ── Subsample ─────────────────────────────────────────────────────────────
    if len(indices) > args.max_plants:
        rng = np.random.default_rng(args.seed)
        indices = sorted(
            rng.choice(indices, size=args.max_plants, replace=False).tolist()
        )
        print(f"[INFO] Subsampled to {args.max_plants} plants "
              f"(use --max-plants to increase)")

    plant_ids = [plant_ids[i] for i in indices]
    genomes   = np.array(
        [np.frombuffer(raw_genomes[i].as_py(), dtype=np.uint8) for i in indices],
        dtype=np.uint8,
    )
    xs = np.array([positions[pid][0] for pid in plant_ids])
    ys = np.array([positions[pid][1] for pid in plant_ids])

    N          = len(plant_ids)
    genome_len = genomes.shape[1]
    eps_pct    = round(args.eps / genome_len * 100)
    print(f"Plants: {N}  genome: {genome_len} bytes  eps: {args.eps} ({eps_pct}%)")

    # ── Pairwise Hamming distance ─────────────────────────────────────────────
    print("Computing pairwise Hamming distances ...")
    dist = hamming_matrix(genomes)

    # ── DBSCAN clustering ─────────────────────────────────────────────────────
    print("Clustering ...")
    labels     = DBSCAN(eps=args.eps, min_samples=args.min_samples,
                        metric="precomputed").fit_predict(dist)
    n_clusters = int((labels >= 0).any() and labels.max() + 1) if len(labels) else 0
    n_noise    = int((labels == -1).sum())
    print(f"Clusters: {n_clusters}  noise: {n_noise}")

    # ── Plot ──────────────────────────────────────────────────────────────────
    palette = make_palette(n_clusters)
    fig, ax = plt.subplots(figsize=(10, 8))

    # Noise first (bottom layer)
    if not args.no_noise and n_noise > 0:
        m = labels == -1
        ax.scatter(xs[m], ys[m], color="0.65", marker="x",
                   s=args.point_size * 0.6, linewidths=0.5,
                   alpha=0.5, zorder=1, label=f"unclustered  (n={n_noise})")

    # One scatter call per cluster, with optional ball-pivot perimeter
    for cid in range(n_clusters):
        m = labels == cid
        color = palette[cid]
        ax.scatter(xs[m], ys[m], color=color, marker="o",
                   s=args.point_size, linewidths=0, alpha=0.85,
                   zorder=2, label=f"cluster {cid}  (n={m.sum()})")

        if args.ball_radius is not None and m.sum() >= 3:
            pts = np.column_stack([xs[m], ys[m]])
            radius = _auto_radius(pts) if args.ball_radius == 0 else args.ball_radius
            for ring in ball_pivot_perimeter(pts, radius):
                closed = np.vstack([ring, ring[0]])   # close the polygon
                ax.plot(closed[:, 0], closed[:, 1],
                        color=color, linewidth=1.5, alpha=0.75, zorder=3)

    ax.set_xlabel("World X", fontsize=11)
    ax.set_ylabel("World Y", fontsize=11)
    ax.set_aspect("equal", adjustable="datalim")

    title_parts = [
        f"{N} plants",
        f"{n_clusters} clusters",
        f"eps={args.eps} bytes ({eps_pct}% of genome)",
    ]
    if args.min_ticks_lived:
        title_parts.append(f"min_lived={args.min_ticks_lived} ticks")
    if args.ball_radius is not None:
        r_label = "auto" if args.ball_radius == 0 else f"{args.ball_radius:.1f}"
        title_parts.append(f"ball-radius={r_label}")
    ax.set_title("Species map — " + "  |  ".join(title_parts), fontsize=10)

    # Legend: collapse to ncols if many clusters
    n_entries = n_clusters + (0 if args.no_noise or n_noise == 0 else 1)
    if n_entries <= 40:
        ax.legend(fontsize=7, loc="upper right", framealpha=0.7,
                  markerscale=2, ncol=max(1, n_entries // 15))
    else:
        ax.legend(fontsize=6, loc="upper right", framealpha=0.7,
                  markerscale=1.5, ncol=max(1, n_entries // 20))

    plt.tight_layout()

    if args.output:
        plt.savefig(args.output, dpi=args.dpi, bbox_inches="tight")
        print(f"Saved → {args.output}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
