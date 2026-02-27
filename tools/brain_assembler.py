#!/usr/bin/env python3
"""Brain Assembler for PlantBrainGrid.

Assembles human-readable brain programs into bytecode genomes.

Assembly format:
    ; This is a comment
    label_name:          ; Define a label at current position

    ; Control flow
    NOP
    HALT
    JUMP label           ; or JUMP 0x0010
    JUMP_REL +5          ; or JUMP_REL -3
    JUMP_IF_ZERO [addr], label
    JUMP_IF_NEQ [addr], value, label
    CALL label
    RET

    ; Memory operations (addresses as [0xNNNN] or decimal)
    LOAD_IMM [dest], value
    COPY [dest], [src]
    ADD [dest], [a], [b]
    SUB [dest], [a], [b]
    MUL [dest], [a], [b]
    DIV [dest], [a], [b]
    MOD [dest], [a], [b]
    AND [dest], [a], [b]
    OR  [dest], [a], [b]
    XOR [dest], [a], [b]
    NOT [dest], [src]
    SHL [dest], [src], amount
    SHR [dest], [src], amount
    CMP_LT [dest], [a], [b]
    CMP_EQ [dest], [a], [b]
    LOAD_IND [dest], [ptr]
    STORE_IND [ptr], [src]
    RANDOMIZE [start], length

    ; World sensing
    SENSE_WATER [dest], dx, dy
    SENSE_NUTRIENTS [dest], dx, dy
    SENSE_LIGHT [dest]
    SENSE_CELL [dest], dx, dy
    SENSE_FIRE [dest], dx, dy
    SENSE_OWNED [dest], dx, dy
    SENSE_SELF_ENERGY [dest]
    SENSE_SELF_WATER [dest]
    SENSE_SELF_NUTRIENTS [dest]
    SENSE_CELL_COUNT [dest]
    SENSE_AGE [dest]

    ; Plant actions
    PLACE_CELL type, dx, dy, direction    ; type=SmallLeaf|BigLeaf|Root|Xylem|FireproofXylem|Thorn|FireStarter
    ROTATE_CELL dx, dy, rotation
    TOGGLE_CELL dx, dy, ON|OFF|1|0
    REMOVE_CELL dx, dy

    ; Reproduction
    START_MATE_SEARCH max_dist
    ADD_MATE_WEIGHT criterion, weight    ; criterion=Size|Age|Energy|Water|Nutrients|Distance|Similarity|Difference
    FINISH_MATE_SELECT
    LAUNCH_SEED recomb, energy, water, nutrients, power, dx, dy, placement
                                         ; recomb=MotherOnly|FatherOnly|Mother75|Father75|HalfHalf|RandomMix|Alternating
                                         ; placement=exact|random

    ; Macros
    .define NAME value               ; Define a named constant
    .org 0x100                       ; Set current address (fills with NOPs)
    .db 0x42, 0xFF, ...             ; Insert raw bytes
    .fill length, value              ; Fill with repeated value
"""

import argparse
import sys
import re
from typing import Dict, List, Optional, Tuple, Union

NUM_OPCODES = 0xA0
BRAIN_SIZE = 1024

OPCODES = {
    "NOP": (0x00, 0),
    "HALT": (0x01, 0),
    "JUMP": (0x02, "addr16"),
    "JUMP_REL": (0x03, "rel8"),
    "JUMP_IF_ZERO": (0x04, "addr16 addr16"),
    "JUMP_IF_NEQ": (0x05, "addr16 imm8 addr16"),
    "CALL": (0x06, "addr16"),
    "RET": (0x07, 0),
    "LOAD_IMM": (0x20, "addr16 imm8"),
    "COPY": (0x21, "addr16 addr16"),
    "ADD": (0x22, "addr16 addr16 addr16"),
    "SUB": (0x23, "addr16 addr16 addr16"),
    "MUL": (0x24, "addr16 addr16 addr16"),
    "DIV": (0x25, "addr16 addr16 addr16"),
    "MOD": (0x26, "addr16 addr16 addr16"),
    "AND": (0x27, "addr16 addr16 addr16"),
    "OR": (0x28, "addr16 addr16 addr16"),
    "XOR": (0x29, "addr16 addr16 addr16"),
    "NOT": (0x2A, "addr16 addr16"),
    "SHL": (0x2B, "addr16 addr16 imm8"),
    "SHR": (0x2C, "addr16 addr16 imm8"),
    "CMP_LT": (0x2D, "addr16 addr16 addr16"),
    "CMP_EQ": (0x2E, "addr16 addr16 addr16"),
    "LOAD_IND": (0x2F, "addr16 addr16"),
    "STORE_IND": (0x30, "addr16 addr16"),
    "RANDOMIZE": (0x31, "addr16 imm8"),
    "SENSE_WATER": (0x40, "addr16 rel8 rel8"),
    "SENSE_NUTRIENTS": (0x41, "addr16 rel8 rel8"),
    "SENSE_LIGHT": (0x42, "addr16"),
    "SENSE_CELL": (0x43, "addr16 rel8 rel8"),
    "SENSE_FIRE": (0x44, "addr16 rel8 rel8"),
    "SENSE_OWNED": (0x45, "addr16 rel8 rel8"),
    "SENSE_SELF_ENERGY": (0x46, "addr16"),
    "SENSE_SELF_WATER": (0x47, "addr16"),
    "SENSE_SELF_NUTRIENTS": (0x48, "addr16"),
    "SENSE_CELL_COUNT": (0x49, "addr16"),
    "SENSE_AGE": (0x4A, "addr16"),
    "PLACE_CELL": (0x60, "ctype rel8 rel8 dir4"),
    "ROTATE_CELL": (0x61, "rel8 rel8 rel8"),
    "TOGGLE_CELL": (0x62, "rel8 rel8 bool"),
    "REMOVE_CELL": (0x63, "rel8 rel8"),
    "START_MATE_SEARCH": (0x80, "imm8"),
    "ADD_MATE_WEIGHT": (0x81, "criterion imm8"),
    "FINISH_MATE_SELECT": (0x82, 0),
    "LAUNCH_SEED": (0x83, "recomb imm8 imm8 imm8 imm8 rel8 rel8 placement"),
}

CELL_TYPES = {
    "Empty": 0, "Primary": 1, "SmallLeaf": 2, "BigLeaf": 3,
    "Root": 4, "Xylem": 5, "FireproofXylem": 6, "Thorn": 7, "FireStarter": 8
}

DIRECTIONS = {
    "North": 0, "East": 1, "South": 2, "West": 3,
    "N": 0, "E": 1, "S": 2, "W": 3,
}

MATE_CRITERIA = {
    "Size": 0, "Age": 1, "Energy": 2, "Water": 3,
    "Nutrients": 4, "Distance": 5, "Similarity": 6, "Difference": 7
}

RECOMB_METHODS = {
    "MotherOnly": 0, "FatherOnly": 1, "Mother75": 2, "Father75": 3,
    "HalfHalf": 4, "RandomMix": 5, "Alternating": 6
}


class AssemblerError(Exception):
    def __init__(self, message: str, line: int = 0):
        self.line = line
        super().__init__(f"Line {line}: {message}" if line else message)


class BrainAssembler:
    def __init__(self, size: int = BRAIN_SIZE):
        self.size = size
        self.defines: Dict[str, int] = {}
        self.labels: Dict[str, int] = {}
        self.output: bytearray = bytearray(size)
        self.ip: int = 0
        self.fixups: List[Tuple[int, str, str]] = []  # (offset, label, type)

    def _parse_int(self, token: str, context: str = "") -> int:
        """Parse an integer token (hex, decimal, or define name)."""
        token = token.strip().strip(",")
        if token in self.defines:
            return self.defines[token]
        try:
            if token.startswith("0x") or token.startswith("0X"):
                return int(token, 16)
            return int(token)
        except ValueError:
            raise AssemblerError(f"Invalid integer '{token}' in {context}")

    def _parse_addr(self, token: str) -> int:
        """Parse an address like [0x0010] or [256]."""
        token = token.strip().strip(",")
        if token.startswith("[") and token.endswith("]"):
            inner = token[1:-1].strip()
            if inner in self.defines:
                return self.defines[inner]
            try:
                return int(inner, 0)
            except ValueError:
                raise AssemblerError(f"Invalid address '{token}'")
        # Also accept bare numbers as addresses
        return self._parse_int(token, "address")

    def _parse_rel(self, token: str) -> int:
        """Parse a signed relative offset, clamped to -128..127."""
        val = self._parse_int(token, "relative offset")
        if val < -128 or val > 127:
            raise AssemblerError(f"Relative offset {val} out of range -128..127")
        return val & 0xFF

    def _emit(self, byte: int):
        """Emit one byte."""
        if self.ip >= self.size:
            raise AssemblerError(f"Program too large (exceeds {self.size} bytes)")
        self.output[self.ip] = byte & 0xFF
        self.ip += 1

    def _emit16(self, value: int):
        """Emit a 16-bit little-endian value."""
        self._emit(value & 0xFF)
        self._emit((value >> 8) & 0xFF)

    def _reserve_label_ref(self, label: str, ref_type: str):
        """Emit placeholder bytes and register a fixup for a label reference."""
        offset = self.ip
        self.fixups.append((offset, label, ref_type))
        self._emit(0)  # placeholder
        if ref_type == "addr16":
            self._emit(0)

    def _resolve_fixups(self):
        """Resolve all label references."""
        for offset, label, ref_type in self.fixups:
            if label not in self.labels:
                raise AssemblerError(f"Undefined label '{label}'")
            addr = self.labels[label]

            if ref_type == "addr16":
                self.output[offset] = addr & 0xFF
                self.output[offset + 1] = (addr >> 8) & 0xFF
            elif ref_type == "rel8":
                # Relative offset from byte AFTER the fixup byte
                rel = addr - (offset + 1)
                if rel < -128 or rel > 127:
                    raise AssemblerError(
                        f"Label '{label}' too far for relative jump "
                        f"(offset {rel} out of -128..127 range)"
                    )
                self.output[offset] = rel & 0xFF

    def _tokenize(self, line: str) -> List[str]:
        """Split a line into tokens, handling brackets."""
        line = line.strip()
        tokens = []
        current = ""
        in_bracket = False

        for ch in line:
            if ch == "[":
                in_bracket = True
                current += ch
            elif ch == "]":
                in_bracket = False
                current += ch
            elif ch in (" ", "\t", ",") and not in_bracket:
                if current:
                    tokens.append(current)
                    current = ""
            else:
                current += ch

        if current:
            tokens.append(current)
        return tokens

    def _assemble_line(self, line: str, lineno: int):
        """Assemble a single line."""
        # Strip comments
        comment_idx = line.find(";")
        if comment_idx >= 0:
            line = line[:comment_idx]

        line = line.strip()
        if not line:
            return

        # Label definition
        if line.endswith(":"):
            label = line[:-1].strip()
            if not label.isidentifier():
                raise AssemblerError(f"Invalid label name '{label}'", lineno)
            self.labels[label] = self.ip
            return

        tokens = self._tokenize(line)
        if not tokens:
            return

        mnemonic = tokens[0].upper()
        args = tokens[1:]

        # Directives
        if mnemonic == ".DEFINE":
            if len(args) < 2:
                raise AssemblerError(".define requires name and value", lineno)
            self.defines[args[0]] = self._parse_int(args[1], ".define")
            return

        if mnemonic == ".ORG":
            if len(args) < 1:
                raise AssemblerError(".org requires an address", lineno)
            target = self._parse_int(args[0], ".org")
            while self.ip < target:
                self._emit(0x00)  # NOP
            if self.ip > target:
                raise AssemblerError(f".org 0x{target:04X} already passed (ip=0x{self.ip:04X})", lineno)
            return

        if mnemonic == ".DB":
            for a in args:
                self._emit(self._parse_int(a, ".db"))
            return

        if mnemonic == ".FILL":
            if len(args) < 2:
                raise AssemblerError(".fill requires length and value", lineno)
            length = self._parse_int(args[0], ".fill length")
            value = self._parse_int(args[1], ".fill value")
            for _ in range(length):
                self._emit(value)
            return

        # Regular instructions
        if mnemonic not in OPCODES:
            raise AssemblerError(f"Unknown mnemonic '{mnemonic}'", lineno)

        opcode, fmt = OPCODES[mnemonic]
        self._emit(opcode)

        if fmt == 0:
            return  # No arguments

        if isinstance(fmt, int):
            return

        fmt_parts = fmt.split()
        if len(fmt_parts) != len(args):
            raise AssemblerError(
                f"{mnemonic} expects {len(fmt_parts)} arguments, got {len(args)}",
                lineno
            )

        for fmt_type, arg in zip(fmt_parts, args):
            arg = arg.strip().strip(",")

            if fmt_type == "addr16":
                # Could be a label or an address
                if arg.startswith("[") or arg.startswith("0x") or arg.lstrip("-+").isdigit():
                    self._emit16(self._parse_addr(arg))
                elif arg in self.labels:
                    self._emit16(self.labels[arg])
                else:
                    # Forward reference - need fixup
                    self._reserve_label_ref(arg, "addr16")

            elif fmt_type == "rel8":
                if arg.lstrip("-+").isdigit() or arg.startswith("0x"):
                    self._emit(self._parse_rel(arg))
                elif arg in self.labels:
                    rel = self.labels[arg] - self.ip - 1
                    self._emit(self._parse_rel(str(rel)))
                else:
                    self._reserve_label_ref(arg, "rel8")

            elif fmt_type == "imm8":
                self._emit(self._parse_int(arg, f"{mnemonic} imm8") & 0xFF)

            elif fmt_type == "ctype":
                ct = arg.strip("[]")
                if ct in CELL_TYPES:
                    self._emit(CELL_TYPES[ct])
                else:
                    self._emit(self._parse_int(arg, "cell type") % 9)

            elif fmt_type == "dir4":
                d = arg.strip()
                if d in DIRECTIONS:
                    self._emit(DIRECTIONS[d])
                else:
                    self._emit(self._parse_int(arg, "direction") % 4)

            elif fmt_type == "bool":
                larg = arg.strip().lower()
                if larg in ("on", "1", "true", "yes"):
                    self._emit(1)
                elif larg in ("off", "0", "false", "no"):
                    self._emit(0)
                else:
                    self._emit(self._parse_int(arg, "bool") & 1)

            elif fmt_type == "criterion":
                c = arg.strip()
                if c in MATE_CRITERIA:
                    self._emit(MATE_CRITERIA[c])
                else:
                    self._emit(self._parse_int(arg, "mate criterion") & 0xFF)

            elif fmt_type == "recomb":
                r = arg.strip()
                if r in RECOMB_METHODS:
                    self._emit(RECOMB_METHODS[r])
                else:
                    self._emit(self._parse_int(arg, "recombination") % 7)

            elif fmt_type == "placement":
                larg = arg.strip().lower()
                if larg == "exact":
                    self._emit(0)   # SeedPlacementMode::Exact = 0
                elif larg == "random":
                    self._emit(1)   # SeedPlacementMode::Random = 1
                else:
                    self._emit(self._parse_int(arg, "placement") & 1)

    def assemble(self, source: str) -> bytes:
        """Assemble source code into bytecode.

        Returns the genome as bytes (padded to self.size with NOPs).
        """
        self.ip = 0
        self.labels = {}
        self.fixups = []
        self.output = bytearray(self.size)

        for lineno, line in enumerate(source.splitlines(), 1):
            try:
                self._assemble_line(line, lineno)
            except AssemblerError:
                raise
            except Exception as e:
                raise AssemblerError(str(e), lineno)

        self._resolve_fixups()
        return bytes(self.output)


def assemble_file(input_path: str, output_path: str, size: int = BRAIN_SIZE) -> int:
    """Assemble an assembly file to binary.

    Returns the number of bytes used (excluding padding).
    """
    with open(input_path, "r") as f:
        source = f.read()

    assembler = BrainAssembler(size=size)
    bytecode = assembler.assemble(source)

    with open(output_path, "wb") as f:
        f.write(bytecode)

    return assembler.ip


def main():
    parser = argparse.ArgumentParser(
        description="Assemble PlantBrainGrid brain programs",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("input", help="Input assembly file (.asm)")
    parser.add_argument("-o", "--output", default=None,
                        help="Output binary file (default: input.bin)")
    parser.add_argument("--size", type=int, default=BRAIN_SIZE,
                        help=f"Brain memory size (default: {BRAIN_SIZE})")
    parser.add_argument("--disassemble", "-d", action="store_true",
                        help="Also print disassembly of output")
    parser.add_argument("--hex", action="store_true",
                        help="Print hex dump of output")

    args = parser.parse_args()

    output = args.output or args.input.replace(".asm", ".bin").replace(".s", ".bin")

    try:
        used = assemble_file(args.input, output, size=args.size)
        print(f"Assembled: {args.input} -> {output}")
        print(f"Used {used} / {args.size} bytes ({100.0 * used / args.size:.1f}%)")
    except AssemblerError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    if args.disassemble or args.hex:
        with open(output, "rb") as f:
            bytecode = f.read()

        if args.hex:
            sys.path.insert(0, str(__file__).rsplit("/", 2)[0])
            try:
                from plantbraingrid.brain_viewer import hex_dump
                print("\nHex dump:")
                print(hex_dump(bytecode, rows=16))
            except ImportError:
                print("\nNote: Install plantbraingrid package for hex dump support")

        if args.disassemble:
            sys.path.insert(0, str(__file__).rsplit("/", 2)[0])
            try:
                from plantbraingrid.brain_viewer import disassemble
                print("\nDisassembly:")
                print(disassemble(bytecode, max_instructions=100))
            except ImportError:
                print("\nNote: Install plantbraingrid package for disassembly support")


if __name__ == "__main__":
    main()
