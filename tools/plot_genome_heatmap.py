#!/usr/bin/env python3
"""
plot_genome_heatmap.py — Pairwise genome-difference heatmap.

Loads genomes.parquet produced by run_logged.py and plots a square heatmap
where cell brightness = number of bytes that differ between two plants.
Diagonal is always 0 (same plant).  Brighter = more different.

Optionally reorders plants by hierarchical clustering so related lineages
cluster together (requires scipy).

Usage:
    python tools/plot_genome_heatmap.py logs/
    python tools/plot_genome_heatmap.py logs/ --max-plants 300
    python tools/plot_genome_heatmap.py logs/ --no-cluster --output fig.png
    python tools/plot_genome_heatmap.py logs/ --filter-tick-born-max 500
    python tools/plot_genome_heatmap.py logs/ --min-ticks-lived 200

Requirements:
    pip install pyarrow numpy matplotlib
    pip install scipy          # optional — enables clustering reorder
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
    import matplotlib
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
    plt.style.use("dark_background")
except ImportError:
    print("matplotlib required:  pip install matplotlib", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Distance computation
# ---------------------------------------------------------------------------

def hamming_matrix(genomes: np.ndarray) -> np.ndarray:
    """Return (N, N) int32 array of pairwise byte-difference counts."""
    N = len(genomes)
    try:
        from scipy.spatial.distance import cdist
        dist = cdist(genomes.astype(np.float64),
                     genomes.astype(np.float64),
                     metric="hamming")
        return (dist * genomes.shape[1]).astype(np.int32)
    except ImportError:
        pass

    # Numpy fallback — row-by-row to keep memory bounded.
    dist = np.empty((N, N), dtype=np.int32)
    for i in range(N):
        dist[i] = (genomes[i:i+1] != genomes).sum(axis=1)
    return dist


def cluster_order(dist: np.ndarray) -> np.ndarray:
    """Return a permutation of [0, N) that groups related genomes together."""
    try:
        from scipy.cluster.hierarchy import linkage, leaves_list
        from scipy.spatial.distance import squareform
        Z = linkage(squareform(dist.astype(np.float64)), method="ward")
        return leaves_list(Z)
    except ImportError:
        print("[INFO] scipy not available — skipping clustering reorder", file=sys.stderr)
        return np.arange(len(dist))


# ---------------------------------------------------------------------------
# Lifetime helpers
# ---------------------------------------------------------------------------

def _load_lifetimes(log_dir: str) -> tuple[dict[int, int], int] | None:
    """Load death ticks and final tick from log_dir.

    Returns ({plant_id: death_tick}, final_tick) or None if the required
    files are not found.  Plants with no death entry are still alive;
    their lifetime is computed as final_tick - tick_born.
    """
    events_path = os.path.join(log_dir, "plant_events.parquet")
    stats_path  = os.path.join(log_dir, "tick_stats.parquet")

    if not os.path.exists(events_path):
        print(f"[WARN] {events_path} not found — cannot filter by lifetime.",
              file=sys.stderr)
        return None

    events = pq.read_table(events_path, columns=["tick", "event", "plant_id"])
    death_mask = [e == "death" for e in events["event"].to_pylist()]
    death_ticks: dict[int, int] = {}
    for pid, tick, is_death in zip(
        events["plant_id"].to_pylist(),
        events["tick"].to_pylist(),
        death_mask,
    ):
        if is_death:
            death_ticks[pid] = tick

    # Determine final simulation tick.
    final_tick = 0
    if os.path.exists(stats_path):
        stats = pq.read_table(stats_path, columns=["tick"])
        ticks = stats["tick"].to_pylist()
        final_tick = max(ticks) if ticks else 0
    elif death_ticks:
        final_tick = max(death_ticks.values())

    return death_ticks, final_tick


def compute_lifetime(plant_id: int, tick_born: int,
                     death_ticks: dict[int, int], final_tick: int) -> int:
    """Ticks a plant lived: from birth to death, or to end of run."""
    end = death_ticks.get(plant_id, final_tick)
    return max(0, end - tick_born)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Pairwise genome-difference heatmap from genomes.parquet",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("log_dir",              help="Path to the log directory containing Parquet files")
    parser.add_argument("--max-plants", type=int, default=400,
                        help="Cap number of plants plotted (random subsample if exceeded)")
    parser.add_argument("--filter-tick-born-min", type=int, default=None,
                        help="Only include plants born at or after this tick")
    parser.add_argument("--filter-tick-born-max", type=int, default=None,
                        help="Only include plants born at or before this tick")
    parser.add_argument("--min-ticks-lived", type=int, default=None,
                        help="Only include plants that lived for at least N ticks "
                             "(uses plant_events.parquet + tick_stats.parquet from same dir)")
    parser.add_argument("--no-cluster", action="store_true",
                        help="Skip hierarchical clustering reorder")
    parser.add_argument("--cmap", default="inferno",
                        help="Matplotlib colormap name")
    parser.add_argument("--output", default=None,
                        help="Save figure to file instead of showing interactively")
    parser.add_argument("--dpi",  type=int, default=150)
    parser.add_argument("--seed", type=int, default=0,
                        help="RNG seed for subsample reproducibility")
    args = parser.parse_args()

    # ── Load data ────────────────────────────────────────────────────────────
    log_dir = os.path.abspath(args.log_dir)
    genomes_path = os.path.join(log_dir, "genomes.parquet")
    if not os.path.exists(genomes_path):
        print(f"[ERR] {genomes_path} not found", file=sys.stderr)
        sys.exit(1)

    print(f"Loading {genomes_path} ...")
    table = pq.read_table(genomes_path)
    plant_ids  = table["plant_id"].to_pylist()
    ticks_born = table["tick_born"].to_pylist()
    raw_genomes = table["genome"]  # pyarrow BinaryArray

    # ── Lifetime data (loaded lazily only when needed) ────────────────────────
    lifetime_data = None
    if args.min_ticks_lived is not None:
        lifetime_data = _load_lifetimes(log_dir)

    # ── Filter ───────────────────────────────────────────────────────────────
    mask = [True] * len(plant_ids)
    if args.filter_tick_born_min is not None:
        mask = [m and t >= args.filter_tick_born_min for m, t in zip(mask, ticks_born)]
    if args.filter_tick_born_max is not None:
        mask = [m and t <= args.filter_tick_born_max for m, t in zip(mask, ticks_born)]
    if args.min_ticks_lived is not None and lifetime_data is not None:
        death_ticks, final_tick = lifetime_data
        print(f"Filtering: min_ticks_lived={args.min_ticks_lived}  "
              f"final_tick={final_tick}")
        mask = [
            m and compute_lifetime(pid, tb, death_ticks, final_tick) >= args.min_ticks_lived
            for m, pid, tb in zip(mask, plant_ids, ticks_born)
        ]

    indices = [i for i, m in enumerate(mask) if m]
    if not indices:
        print("No plants match the filter criteria.", file=sys.stderr)
        sys.exit(1)

    # ── Subsample if too many ─────────────────────────────────────────────────
    if len(indices) > args.max_plants:
        rng = np.random.default_rng(args.seed)
        indices = sorted(rng.choice(indices, size=args.max_plants, replace=False).tolist())
        print(f"[INFO] Subsampled to {args.max_plants} plants "
              f"(use --max-plants to increase)")

    plant_ids  = [plant_ids[i]  for i in indices]
    ticks_born = [ticks_born[i] for i in indices]
    genomes    = np.array(
        [np.frombuffer(raw_genomes[i].as_py(), dtype=np.uint8) for i in indices],
        dtype=np.uint8,
    )

    N = len(plant_ids)
    genome_len = genomes.shape[1]
    print(f"Plants: {N}  genome length: {genome_len} bytes")

    # ── Pairwise Hamming distance ─────────────────────────────────────────────
    print("Computing pairwise byte-difference matrix ...")
    dist = hamming_matrix(genomes)

    # ── Optional clustering reorder ───────────────────────────────────────────
    if not args.no_cluster and N > 1:
        print("Clustering for display order ...")
        order = cluster_order(dist)
        dist       = dist[np.ix_(order, order)]
        plant_ids  = [plant_ids[i]  for i in order]
        ticks_born = [ticks_born[i] for i in order]

    # ── Plot ─────────────────────────────────────────────────────────────────
    fig_size = max(8, min(20, N / 12))
    fig, ax = plt.subplots(figsize=(fig_size, fig_size))

    im = ax.imshow(dist, cmap=args.cmap, interpolation="nearest",
                   vmin=0, vmax=genome_len)

    # Axis labels — only show tick labels when there are few enough plants.
    if N <= 60:
        ax.set_xticks(range(N))
        ax.set_yticks(range(N))
        ax.set_xticklabels([str(p) for p in plant_ids],
                           rotation=90, fontsize=max(4, 8 - N // 20))
        ax.set_yticklabels([str(p) for p in plant_ids],
                           fontsize=max(4, 8 - N // 20))
    else:
        # Show only every k-th label to avoid crowding.
        k = max(1, N // 30)
        ax.xaxis.set_major_locator(ticker.MultipleLocator(k))
        ax.yaxis.set_major_locator(ticker.MultipleLocator(k))
        ax.xaxis.set_major_formatter(
            ticker.FuncFormatter(lambda x, _: str(plant_ids[int(x)])
                                 if 0 <= int(x) < N else ""))
        ax.yaxis.set_major_formatter(
            ticker.FuncFormatter(lambda y, _: str(plant_ids[int(y)])
                                 if 0 <= int(y) < N else ""))
        plt.setp(ax.get_xticklabels(), rotation=90, fontsize=6)
        plt.setp(ax.get_yticklabels(), fontsize=6)

    cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("Bytes different", fontsize=10)

    cluster_note = "" if args.no_cluster else " (clustered)"
    ax.set_title(
        f"Pairwise genome difference — {N} plants{cluster_note}\n"
        f"Bright = more different  (max {genome_len})",
        fontsize=11,
    )
    ax.set_xlabel("Plant ID", fontsize=9)
    ax.set_ylabel("Plant ID", fontsize=9)

    plt.tight_layout()

    if args.output:
        plt.savefig(args.output, dpi=args.dpi, bbox_inches="tight")
        print(f"Saved → {args.output}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
