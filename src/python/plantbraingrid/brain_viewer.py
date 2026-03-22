"""Brain visualization module for PlantBrainGrid.

Shows memory hex dump, current IP, execution trace, and opcode analysis.
Works standalone (no raylib required) for text output, or integrates with
the Visualizer for graphical display.
"""

from typing import Optional, List, Tuple, Dict

# Opcode definitions matching brain_ops.hpp
OPCODES: Dict[int, Tuple[str, int]] = {
    # Control Flow
    0x00: ("NOP", 0),
    0x01: ("HALT", 0),
    0x02: ("JUMP", 2),           # addr16
    0x03: ("JUMP_REL", 1),       # signed offset
    0x04: ("JUMP_IF_ZERO", 4),   # test_addr16, jump_addr16
    0x05: ("JUMP_IF_NEQ", 5),    # test_addr16, val, jump_addr16
    0x06: ("CALL", 2),           # addr16
    0x07: ("RET", 0),
    # Memory Operations
    0x20: ("LOAD_IMM", 3),       # dest16, val
    0x21: ("COPY", 4),           # dest16, src16
    0x22: ("ADD", 6),            # dest16, a16, b16
    0x23: ("SUB", 6),
    0x24: ("MUL", 6),
    0x25: ("DIV", 6),
    0x26: ("MOD", 6),
    0x27: ("AND", 6),
    0x28: ("OR", 6),
    0x29: ("XOR", 6),
    0x2A: ("NOT", 4),            # dest16, src16
    0x2B: ("SHL", 5),            # dest16, src16, amount
    0x2C: ("SHR", 5),
    0x2D: ("CMP_LT", 6),
    0x2E: ("CMP_EQ", 6),
    0x2F: ("LOAD_IND", 4),
    0x30: ("STORE_IND", 4),
    0x31: ("RANDOMIZE", 3),      # start16, length
    # World Sensing
    0x40: ("SENSE_WATER", 4),    # dest16, dx, dy
    0x41: ("SENSE_NUTRIENTS", 4),
    0x42: ("SENSE_LIGHT", 2),    # dest16
    0x43: ("SENSE_CELL", 4),
    0x44: ("SENSE_FIRE", 4),
    0x45: ("SENSE_OWNED", 4),
    0x46: ("SENSE_SELF_ENERGY", 2),
    0x47: ("SENSE_SELF_WATER", 2),
    0x48: ("SENSE_SELF_NUTRIENTS", 2),
    0x49: ("SENSE_CELL_COUNT", 2),
    0x4A: ("SENSE_AGE", 2),
    0x4B: ("SENSE_SEASON", 2),
    # Plant Actions
    0x60: ("PLACE_CELL", 3),     # type, dx, dy
    0x61: ("ROTATE_CELL", 3),    # dx, dy, rotation (NOP)
    0x62: ("TOGGLE_CELL", 3),    # dx, dy, enabled
    0x63: ("REMOVE_CELL", 2),    # dx, dy
    # Reproduction — each MATE_BY_* adds one criterion; cleared each brain tick
    0x80: ("MATE_BY_SIZE", 2),        # max_dist, magnitude
    0x81: ("MATE_BY_AGE", 2),
    0x82: ("MATE_BY_ENERGY", 2),
    0x83: ("MATE_BY_WATER", 2),
    0x84: ("MATE_BY_NUTRIENTS", 2),
    0x85: ("MATE_BY_DISTANCE", 2),
    0x86: ("MATE_BY_SIMILARITY", 2),
    0x87: ("MATE_BY_DIFFERENCE", 2),
    0x88: ("MATE_BY_CELL_COUNT", 4),  # max_dist, cell_type, target_count, magnitude
    0x89: ("LAUNCH_SEED", 8),         # recomb, energy, water, nutrients, power, dx, dy, placement
}

NUM_OPCODES = 0xA0

CELL_TYPE_NAMES = {
    0: "Empty", 1: "Primary", 2: "SmallLeaf", 3: "BigLeaf",
    4: "FiberRoot", 5: "Anther", 6: "Bark", 7: "Thorn",
    8: "FireStarter", 9: "TapRoot", 10: "StoreEnergy",
    11: "StoreWater", 12: "StoreNutrients", 13: "Haustorium",
}

RECOMB_NAMES = {
    0: "RandomMix", 1: "Alternating", 2: "Mother75", 3: "Father75",
}


def decode_instruction(mem: bytes, ip: int) -> Optional[Tuple[str, List[int], int]]:
    """Decode one instruction at ip.

    Returns (mnemonic, args, next_ip) or None if ip is out of bounds.
    """
    size = len(mem)
    if ip >= size:
        return None

    raw = mem[ip]
    opcode = raw % NUM_OPCODES
    ip += 1

    info = OPCODES.get(opcode)
    if info is None:
        return (f"UNK(0x{opcode:02X})", [], ip)

    name, arg_bytes = info
    args = []
    for _ in range(arg_bytes):
        if ip >= size:
            args.append(0)
        else:
            args.append(mem[ip])
            ip += 1

    return (name, args, ip)


NUM_REGISTERS = 8


def _fmt_addr(addr: int) -> str:
    """Format a 16-bit address with register notation.

    MSB clear          → normal address  [0x1234]
    MSB set, bit14 clear → register      [r0]-[r7]
    MSB set, bit14 set   → last register [LAST]
    """
    if not (addr & 0x8000):
        return f"[0x{addr:04X}]"
    if not (addr & 0x4000):
        return f"[r{addr % NUM_REGISTERS}]"
    return "[LAST]"


def format_instruction(name: str, args: List[int]) -> str:
    """Format an instruction into a human-readable string."""
    if name in ("NOP", "HALT", "RET"):
        return name

    if name == "JUMP":
        addr = args[0] | (args[1] << 8)
        return f"JUMP 0x{addr:04X}"
    if name == "JUMP_REL":
        offset = args[0] if args[0] < 128 else args[0] - 256
        return f"JUMP_REL {offset:+d}"
    if name == "JUMP_IF_ZERO":
        test = args[0] | (args[1] << 8)
        dest = args[2] | (args[3] << 8)
        return f"JUMP_IF_ZERO {_fmt_addr(test)}, 0x{dest:04X}"
    if name == "JUMP_IF_NEQ":
        test = args[0] | (args[1] << 8)
        val = args[2]
        dest = args[3] | (args[4] << 8)
        return f"JUMP_IF_NEQ {_fmt_addr(test)}, {val}, 0x{dest:04X}"
    if name == "CALL":
        addr = args[0] | (args[1] << 8)
        return f"CALL 0x{addr:04X}"
    if name == "LOAD_IMM":
        dest = args[0] | (args[1] << 8)
        return f"LOAD_IMM {_fmt_addr(dest)}, {args[2]}"
    if name in ("COPY", "NOT", "LOAD_IND", "STORE_IND"):
        a = args[0] | (args[1] << 8)
        b = args[2] | (args[3] << 8)
        return f"{name} {_fmt_addr(a)}, {_fmt_addr(b)}"
    if name in ("ADD", "SUB", "MUL", "DIV", "MOD", "AND", "OR", "XOR",
                "CMP_LT", "CMP_EQ"):
        dest = args[0] | (args[1] << 8)
        a = args[2] | (args[3] << 8)
        b = args[4] | (args[5] << 8)
        return f"{name} {_fmt_addr(dest)}, {_fmt_addr(a)}, {_fmt_addr(b)}"
    if name in ("SHL", "SHR"):
        dest = args[0] | (args[1] << 8)
        src = args[2] | (args[3] << 8)
        return f"{name} {_fmt_addr(dest)}, {_fmt_addr(src)}, {args[4]}"
    if name == "RANDOMIZE":
        start = args[0] | (args[1] << 8)
        return f"RANDOMIZE {_fmt_addr(start)}, {args[2]}"
    if name in ("SENSE_WATER", "SENSE_NUTRIENTS", "SENSE_CELL", "SENSE_FIRE", "SENSE_OWNED"):
        dest = args[0] | (args[1] << 8)
        dx = args[2] if args[2] < 128 else args[2] - 256
        dy = args[3] if args[3] < 128 else args[3] - 256
        return f"{name} {_fmt_addr(dest)}, ({dx:+d},{dy:+d})"
    if name in ("SENSE_LIGHT", "SENSE_SELF_ENERGY", "SENSE_SELF_WATER",
                "SENSE_SELF_NUTRIENTS", "SENSE_CELL_COUNT", "SENSE_AGE",
                "SENSE_SEASON"):
        dest = args[0] | (args[1] << 8)
        return f"{name} {_fmt_addr(dest)}"
    if name == "PLACE_CELL":
        ctype = CELL_TYPE_NAMES.get(args[0] % 14, f"Type{args[0]}")
        dx = args[1] if args[1] < 128 else args[1] - 256
        dy = args[2] if args[2] < 128 else args[2] - 256
        return f"PLACE_CELL {ctype}, ({dx:+d},{dy:+d})"
    if name == "ROTATE_CELL":
        dx = args[0] if args[0] < 128 else args[0] - 256
        dy = args[1] if args[1] < 128 else args[1] - 256
        rot = args[2] if args[2] < 128 else args[2] - 256
        return f"ROTATE_CELL ({dx:+d},{dy:+d}), {rot}"
    if name == "TOGGLE_CELL":
        dx = args[0] if args[0] < 128 else args[0] - 256
        dy = args[1] if args[1] < 128 else args[1] - 256
        return f"TOGGLE_CELL ({dx:+d},{dy:+d}), {'ON' if args[2] else 'OFF'}"
    if name == "REMOVE_CELL":
        dx = args[0] if args[0] < 128 else args[0] - 256
        dy = args[1] if args[1] < 128 else args[1] - 256
        return f"REMOVE_CELL ({dx:+d},{dy:+d})"
    if name in ("MATE_BY_SIZE", "MATE_BY_AGE", "MATE_BY_ENERGY", "MATE_BY_WATER",
                "MATE_BY_NUTRIENTS", "MATE_BY_DISTANCE", "MATE_BY_SIMILARITY",
                "MATE_BY_DIFFERENCE"):
        return f"{name} max_dist={args[0]}, magnitude={args[1]}"
    if name == "MATE_BY_CELL_COUNT":
        ctype = CELL_TYPE_NAMES.get(args[1] % 14, f"Type{args[1]}")
        return f"MATE_BY_CELL_COUNT max_dist={args[0]}, type={ctype}, target={args[2]}, magnitude={args[3]}"
    if name == "LAUNCH_SEED":
        recomb = RECOMB_NAMES.get(args[0] % 4, f"Recomb{args[0]}")
        dx = args[5] if args[5] < 128 else args[5] - 256
        dy = args[6] if args[6] < 128 else args[6] - 256
        return (f"LAUNCH_SEED {recomb}, energy={args[1]}, water={args[2]}, "
                f"nutrients={args[3]}, power={args[4]}, ({dx:+d},{dy:+d}), "
                f"placement={'random' if args[7] & 1 else 'exact'}")

    return f"{name} {' '.join(str(a) for a in args)}"


def hex_dump(mem: bytes, highlight_ip: Optional[int] = None,
             start: int = 0, rows: int = 16) -> str:
    """Generate a hex dump of memory.

    Args:
        mem: The memory bytes.
        highlight_ip: Offset to highlight (current IP).
        start: Starting byte offset.
        rows: Number of 16-byte rows to show.

    Returns:
        Multi-line string with hex dump.
    """
    lines = []
    for row in range(rows):
        offset = start + row * 16
        if offset >= len(mem):
            break

        hex_parts = []
        ascii_parts = []

        for col in range(16):
            idx = offset + col
            if idx < len(mem):
                byte = mem[idx]
                if highlight_ip is not None and idx == highlight_ip:
                    hex_parts.append(f"[{byte:02X}]")
                else:
                    hex_parts.append(f" {byte:02X} ")
                ascii_parts.append(chr(byte) if 32 <= byte < 127 else ".")
            else:
                hex_parts.append("    ")
                ascii_parts.append(" ")

        hex_str = "".join(hex_parts)
        ascii_str = "".join(ascii_parts)
        lines.append(f"0x{offset:04X}: {hex_str}  |{ascii_str}|")

    return "\n".join(lines)


def disassemble(mem: bytes, start: int = 0, max_instructions: int = 50) -> str:
    """Disassemble instructions from memory.

    Args:
        mem: The memory bytes.
        start: Starting offset.
        max_instructions: Maximum number of instructions to disassemble.

    Returns:
        Multi-line disassembly string.
    """
    lines = []
    ip = start
    count = 0

    while ip < len(mem) and count < max_instructions:
        result = decode_instruction(mem, ip)
        if result is None:
            break

        name, args, next_ip = result
        instr_bytes = mem[ip:next_ip]
        bytes_str = " ".join(f"{b:02X}" for b in instr_bytes)
        formatted = format_instruction(name, args)

        lines.append(f"  0x{ip:04X}:  {bytes_str:<20}  {formatted}")
        ip = next_ip
        count += 1

    return "\n".join(lines)


class BrainViewer:
    """Text-based brain state viewer."""

    def __init__(self, brain=None):
        """Initialize with an optional brain object from C++ bindings."""
        self._brain = brain

    def attach(self, brain):
        """Attach a brain object."""
        self._brain = brain

    def _get_memory(self) -> bytes:
        """Get brain memory as bytes."""
        if self._brain is None:
            return b""
        return bytes(self._brain.memory())

    def show_hex_dump(self, start: int = 0, rows: int = 16) -> str:
        """Show hex dump around current IP."""
        mem = self._get_memory()
        ip = self._brain.ip() if self._brain else None
        return hex_dump(mem, highlight_ip=ip, start=start, rows=rows)

    def show_disassembly(self, start: Optional[int] = None,
                         count: int = 20) -> str:
        """Show disassembly starting at given address (default: current IP)."""
        mem = self._get_memory()
        if start is None and self._brain:
            start = self._brain.ip()
        start = start or 0
        return disassemble(mem, start=start, max_instructions=count)

    def show_state(self) -> str:
        """Show full brain state summary."""
        if not self._brain:
            return "No brain attached."

        mem = self._get_memory()
        ip = self._brain.ip()
        halted = self._brain.is_halted()

        lines = [
            f"Brain State:",
            f"  IP:     0x{ip:04X} ({ip})",
            f"  Halted: {halted}",
            f"  Size:   {len(mem)} bytes",
            "",
            f"Current instruction:",
            self.show_disassembly(start=ip, count=5),
            "",
            f"Memory around IP (0x{max(0, ip-16):04X} - 0x{min(len(mem), ip+32):04X}):",
            hex_dump(mem, highlight_ip=ip,
                     start=max(0, ip - (ip % 16)),
                     rows=4),
        ]
        return "\n".join(lines)

    def show_trace(self, trace) -> str:
        """Show execution trace from Brain.last_trace()."""
        if trace is None:
            return "No trace available. Enable tracing with brain.enable_tracing(True)."

        lines = ["Execution Trace:"]
        if trace.hit_instruction_limit:
            lines.append("  [!] Hit instruction limit")
        if trace.hit_oob_memory:
            lines.append(f"  [!] OOB memory accesses: {trace.oob_count}")

        for i, step in enumerate(trace.steps[:50]):
            opcode = step.opcode % NUM_OPCODES
            info = OPCODES.get(opcode, (f"UNK(0x{opcode:02X})", 0))
            lines.append(f"  {i:3d}  0x{step.ip:04X}: {info[0]}")

        if len(trace.steps) > 50:
            lines.append(f"  ... ({len(trace.steps) - 50} more steps)")

        return "\n".join(lines)

    def opcode_frequency(self, max_instructions: int = 1000) -> Dict[str, int]:
        """Count opcode frequency in brain memory (static analysis)."""
        mem = self._get_memory()
        counts: Dict[str, int] = {}
        ip = 0
        count = 0

        while ip < len(mem) and count < max_instructions:
            result = decode_instruction(mem, ip)
            if result is None:
                break
            name, args, next_ip = result
            counts[name] = counts.get(name, 0) + 1
            ip = next_ip
            count += 1

        return counts

    def show_opcode_stats(self) -> str:
        """Show opcode frequency statistics."""
        freq = self.opcode_frequency()
        if not freq:
            return "No instructions found."

        total = sum(freq.values())
        lines = [f"Opcode Frequencies (total: {total} instructions):"]

        for name, count in sorted(freq.items(), key=lambda x: -x[1])[:20]:
            pct = 100.0 * count / total
            bar = "#" * int(pct / 2)
            lines.append(f"  {name:<25} {count:4d}  ({pct:5.1f}%) {bar}")

        return "\n".join(lines)


def print_brain_info(brain, show_disasm: bool = True, show_trace: bool = False):
    """Convenience function to print brain info to stdout."""
    viewer = BrainViewer(brain)
    print(viewer.show_state())

    if show_disasm:
        print("\nFull disassembly (first 30 instructions):")
        mem = bytes(brain.memory())
        print(disassemble(mem, start=0, max_instructions=30))

    if show_trace:
        trace = brain.last_trace()
        if trace:
            print("\n" + viewer.show_trace(trace))
