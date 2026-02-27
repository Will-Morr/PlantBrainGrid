#!/usr/bin/env python3
"""Genome Analyzer for PlantBrainGrid.

Analyzes evolved plant genomes to detect common patterns,
measure complexity, and correlate genome structure with fitness.

Usage:
    python genome_analyzer.py genome.bin
    python genome_analyzer.py --dir /path/to/genomes/
    python genome_analyzer.py --simulation save.bin --ticks 1000
"""

import argparse
import os
import sys
import struct
from typing import Dict, List, Optional, Tuple
from collections import Counter
import math

# Add parent dir to import brain_viewer
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_SRC_DIR = os.path.join(_SCRIPT_DIR, "..", "src", "python")
if _SRC_DIR not in sys.path:
    sys.path.insert(0, _SRC_DIR)

try:
    from plantbraingrid.brain_viewer import (
        decode_instruction, format_instruction, disassemble,
        OPCODES, NUM_OPCODES, CELL_TYPE_NAMES
    )
    BRAIN_VIEWER_AVAILABLE = True
except ImportError:
    BRAIN_VIEWER_AVAILABLE = False
    NUM_OPCODES = 0xA0
    OPCODES = {}
    CELL_TYPE_NAMES = {}

BRAIN_SIZE = 1024


# ─────────────────────────────────────────────────────────
# Metrics
# ─────────────────────────────────────────────────────────

def entropy(data: bytes) -> float:
    """Shannon entropy of byte data (bits per byte, 0-8)."""
    if not data:
        return 0.0
    counts = Counter(data)
    n = len(data)
    h = 0.0
    for c in counts.values():
        p = c / n
        if p > 0:
            h -= p * math.log2(p)
    return h


def opcode_frequency(genome: bytes) -> Dict[str, int]:
    """Count opcode occurrences via static disassembly."""
    if not BRAIN_VIEWER_AVAILABLE:
        # Fallback: raw byte frequency
        return {f"0x{b:02X}": 0 for b in range(NUM_OPCODES)}

    counts: Dict[str, int] = {}
    ip = 0
    steps = 0
    while ip < len(genome) and steps < 2000:
        result = decode_instruction(genome, ip)
        if result is None:
            break
        name, args, next_ip = result
        counts[name] = counts.get(name, 0) + 1
        ip = next_ip
        steps += 1
    return counts


def category_breakdown(freq: Dict[str, int]) -> Dict[str, int]:
    """Group opcodes by category."""
    if not BRAIN_VIEWER_AVAILABLE:
        return {}

    categories = {
        "control_flow": ["NOP", "HALT", "JUMP", "JUMP_REL", "JUMP_IF_ZERO",
                         "JUMP_IF_NEQ", "CALL", "RET"],
        "memory_ops": ["LOAD_IMM", "COPY", "ADD", "SUB", "MUL", "DIV", "MOD",
                       "AND", "OR", "XOR", "NOT", "SHL", "SHR", "CMP_LT",
                       "CMP_EQ", "LOAD_IND", "STORE_IND", "RANDOMIZE"],
        "sensing": ["SENSE_WATER", "SENSE_NUTRIENTS", "SENSE_LIGHT",
                    "SENSE_CELL", "SENSE_FIRE", "SENSE_OWNED",
                    "SENSE_SELF_ENERGY", "SENSE_SELF_WATER",
                    "SENSE_SELF_NUTRIENTS", "SENSE_CELL_COUNT", "SENSE_AGE"],
        "actions": ["PLACE_CELL", "ROTATE_CELL", "TOGGLE_CELL", "REMOVE_CELL"],
        "reproduction": ["START_MATE_SEARCH", "ADD_MATE_WEIGHT",
                         "FINISH_MATE_SELECT", "LAUNCH_SEED"],
    }

    result = {cat: 0 for cat in categories}
    for op, count in freq.items():
        for cat, ops in categories.items():
            if op in ops:
                result[cat] += count
                break

    return result


def detect_loops(genome: bytes) -> List[Tuple[int, int, int]]:
    """Detect backward jump targets (potential loops).

    Returns list of (jump_ip, target_ip, loop_size).
    """
    if not BRAIN_VIEWER_AVAILABLE:
        return []

    loops = []
    ip = 0
    while ip < len(genome):
        result = decode_instruction(genome, ip)
        if result is None:
            break
        name, args, next_ip = result

        target = None
        if name == "JUMP" and len(args) >= 2:
            target = args[0] | (args[1] << 8)
        elif name == "JUMP_REL" and args:
            offset = args[0] if args[0] < 128 else args[0] - 256
            target = next_ip + offset - 1

        if target is not None and target < ip:
            loops.append((ip, target, ip - target))

        ip = next_ip

    return loops


def detect_cell_placement_patterns(genome: bytes) -> Dict[str, int]:
    """Count what cell types are most commonly placed."""
    if not BRAIN_VIEWER_AVAILABLE:
        return {}

    counts: Dict[str, int] = {}
    ip = 0
    while ip < len(genome):
        result = decode_instruction(genome, ip)
        if result is None:
            break
        name, args, next_ip = result

        if name == "PLACE_CELL" and args:
            ctype = args[0] % 9
            cname = CELL_TYPE_NAMES.get(ctype, f"Type{ctype}")
            counts[cname] = counts.get(cname, 0) + 1

        ip = next_ip

    return counts


def complexity_score(genome: bytes) -> float:
    """Compute a heuristic complexity score (0-100).

    Based on:
    - Entropy (information density)
    - Instruction variety
    - Presence of loops
    - Sensing instructions (adaptive behavior)
    - Reproduction instructions
    """
    score = 0.0

    # Entropy component (0-30)
    ent = entropy(genome)
    score += (ent / 8.0) * 30.0

    if BRAIN_VIEWER_AVAILABLE:
        freq = opcode_frequency(genome)
        total = sum(freq.values())
        if total > 0:
            # Instruction variety (0-25)
            unique_ops = len([k for k, v in freq.items() if v > 0])
            score += min(25.0, unique_ops * 1.0)

            # Sensing usage (0-20)
            cat = category_breakdown(freq)
            sensing_ratio = cat.get("sensing", 0) / total
            score += min(20.0, sensing_ratio * 200.0)

            # Reproduction (0-15)
            repro_ops = freq.get("LAUNCH_SEED", 0) + freq.get("START_MATE_SEARCH", 0)
            if repro_ops > 0:
                score += 15.0

            # Loop presence (0-10)
            loops = detect_loops(genome)
            score += min(10.0, len(loops) * 2.0)

    return min(100.0, score)


# ─────────────────────────────────────────────────────────
# Analysis report
# ─────────────────────────────────────────────────────────

def analyze_genome(genome: bytes, name: str = "genome") -> str:
    """Generate a full analysis report for a genome."""
    lines = [f"═══ Genome Analysis: {name} ═══",
             f"Size: {len(genome)} bytes"]

    # Entropy
    ent = entropy(genome)
    lines.append(f"Entropy: {ent:.3f} bits/byte  (max=8.0, random≈8.0)")

    # Complexity
    cx = complexity_score(genome)
    lines.append(f"Complexity score: {cx:.1f} / 100")

    # Zero bytes (unused genome)
    zero_pct = 100.0 * genome.count(0) / len(genome) if genome else 0
    lines.append(f"Zero bytes: {genome.count(0)} ({zero_pct:.1f}%)")

    if BRAIN_VIEWER_AVAILABLE:
        # Opcode frequency
        freq = opcode_frequency(genome)
        total = sum(freq.values())
        if total > 0:
            lines.append(f"\nInstruction count: {total}")

            # Category breakdown
            cat = category_breakdown(freq)
            lines.append("\nCategory breakdown:")
            for cname, count in sorted(cat.items(), key=lambda x: -x[1]):
                pct = 100.0 * count / total
                bar = "█" * int(pct / 3)
                lines.append(f"  {cname:<20} {count:4d}  ({pct:5.1f}%) {bar}")

            # Top instructions
            lines.append("\nTop 10 instructions:")
            for name_op, count in sorted(freq.items(), key=lambda x: -x[1])[:10]:
                pct = 100.0 * count / total
                lines.append(f"  {name_op:<25} {count:4d}  ({pct:5.1f}%)")

        # Loop detection
        loops = detect_loops(genome)
        lines.append(f"\nLoops detected: {len(loops)}")
        for i, (jip, tgt, size) in enumerate(loops[:5]):
            lines.append(f"  Loop {i+1}: 0x{jip:04X} -> 0x{tgt:04X} (size {size})")
        if len(loops) > 5:
            lines.append(f"  ... ({len(loops) - 5} more)")

        # Cell placement patterns
        placements = detect_cell_placement_patterns(genome)
        if placements:
            lines.append("\nCell placement targets:")
            for ctype, count in sorted(placements.items(), key=lambda x: -x[1]):
                lines.append(f"  {ctype:<20} {count:4d}")

        # Sensing behavior
        lines.append("\nSensing instructions:")
        sensing = ["SENSE_WATER", "SENSE_NUTRIENTS", "SENSE_LIGHT",
                   "SENSE_CELL", "SENSE_FIRE", "SENSE_OWNED",
                   "SENSE_SELF_ENERGY", "SENSE_SELF_WATER",
                   "SENSE_SELF_NUTRIENTS", "SENSE_CELL_COUNT", "SENSE_AGE"]
        has_sensing = False
        for op in sensing:
            if freq.get(op, 0) > 0:
                lines.append(f"  {op}: {freq[op]}x")
                has_sensing = True
        if not has_sensing:
            lines.append("  (none)")

        # First 10 instructions
        lines.append("\nFirst 10 instructions:")
        lines.append(disassemble(genome, start=0, max_instructions=10))

    else:
        # Fallback without brain_viewer
        byte_freq = Counter(genome)
        lines.append("\nTop 10 byte values:")
        for byte, count in byte_freq.most_common(10):
            pct = 100.0 * count / len(genome)
            lines.append(f"  0x{byte:02X} ({byte:3d}): {count:4d}  ({pct:.1f}%)")

    return "\n".join(lines)


def compare_genomes(g1: bytes, g2: bytes) -> str:
    """Compare two genomes and report similarity."""
    if len(g1) != len(g2):
        return f"Genomes differ in size: {len(g1)} vs {len(g2)}"

    identical = sum(b1 == b2 for b1, b2 in zip(g1, g2))
    pct = 100.0 * identical / len(g1)

    lines = [f"Genome comparison:",
             f"  Identical bytes: {identical}/{len(g1)} ({pct:.1f}%)"]

    # Hamming distance
    hamming = len(g1) - identical
    lines.append(f"  Hamming distance: {hamming}")

    # Entropy comparison
    e1 = entropy(g1)
    e2 = entropy(g2)
    lines.append(f"  Entropy: {e1:.3f} vs {e2:.3f}")

    return "\n".join(lines)


def load_genome_from_binary(path: str) -> Optional[bytes]:
    """Load a raw genome binary file."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        if len(data) == 0:
            return None
        # Pad or truncate to BRAIN_SIZE
        if len(data) < BRAIN_SIZE:
            data = data + bytes(BRAIN_SIZE - len(data))
        return data[:BRAIN_SIZE]
    except IOError as e:
        print(f"Error reading {path}: {e}", file=sys.stderr)
        return None


def load_genomes_from_directory(dirpath: str) -> List[Tuple[str, bytes]]:
    """Load all .bin files from a directory."""
    genomes = []
    try:
        for fname in sorted(os.listdir(dirpath)):
            if fname.endswith(".bin"):
                fpath = os.path.join(dirpath, fname)
                genome = load_genome_from_binary(fpath)
                if genome:
                    genomes.append((fname, genome))
    except OSError as e:
        print(f"Error reading directory {dirpath}: {e}", file=sys.stderr)
    return genomes


def main():
    parser = argparse.ArgumentParser(
        description="Analyze PlantBrainGrid genomes",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("input", nargs="?", help="Genome binary file (.bin)")
    parser.add_argument("--dir", help="Directory containing genome .bin files")
    parser.add_argument("--compare", nargs=2, metavar=("GENOME1", "GENOME2"),
                        help="Compare two genome files")
    parser.add_argument("--disassemble", "-d", action="store_true",
                        help="Show full disassembly of the genome")
    parser.add_argument("--summary", action="store_true",
                        help="Show summary table for directory analysis")

    args = parser.parse_args()

    if args.compare:
        g1 = load_genome_from_binary(args.compare[0])
        g2 = load_genome_from_binary(args.compare[1])
        if g1 and g2:
            print(compare_genomes(g1, g2))
        sys.exit(0)

    if args.dir:
        genomes = load_genomes_from_directory(args.dir)
        if not genomes:
            print("No genome files found.", file=sys.stderr)
            sys.exit(1)

        if args.summary:
            # Summary table
            print(f"{'Genome':<30} {'Entropy':>8} {'Complexity':>12} {'Zeros%':>8}")
            print("-" * 62)
            for name, genome in genomes:
                ent = entropy(genome)
                cx = complexity_score(genome)
                zero_pct = 100.0 * genome.count(0) / len(genome)
                print(f"{name:<30} {ent:>8.3f} {cx:>12.1f} {zero_pct:>7.1f}%")
        else:
            for name, genome in genomes:
                print()
                print(analyze_genome(genome, name))

        sys.exit(0)

    if args.input:
        genome = load_genome_from_binary(args.input)
        if not genome:
            sys.exit(1)

        print(analyze_genome(genome, os.path.basename(args.input)))

        if args.disassemble and BRAIN_VIEWER_AVAILABLE:
            print("\nFull disassembly:")
            print(disassemble(genome, max_instructions=200))

        sys.exit(0)

    parser.print_help()
    sys.exit(1)


if __name__ == "__main__":
    main()
