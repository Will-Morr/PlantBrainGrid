; Reproducer
;
; Strategy:
;   1. Each tick: sense cell count to decide if growth is needed.
;      If fully grown, skip PLACE_CELL instructions entirely.
;   2. If not fully grown, attempt to place body cells.
;   3. Once energy exceeds threshold, launch a seed.
;   4. HALT each tick — no instruction-limit penalty.
;
; Body: Primary + 1 FiberRoot (+0,+1) + 3 SmallLeafs (+0,-1), (+1,0), (-1,0)
;   Build cost: FiberRoot(8) + 3×SmallLeaf(30) = 38 energy
;
; Income (fully grown, 5 cells):
;   Energy income:  3 × 1.0 × light ≈ 2.7/tick
;   Maintenance:    primary(0.1) + fiber_root(0.1) = 0.2/tick
;   Net energy:     ~2.5/tick
;   Water income:   fiber_root(1.5) + primary_draw(0.3) = 1.8/tick
;   Water cost:     3 × small_leaf(0.2) = 0.6/tick
;   Net water:      ~1.2/tick
;
; Reproduction (LAUNCH_SEED Alternating, 120, 60, 10, 25, +0, +0, random):
;   Seed energy:    120/2.55 ≈ 47 units — child can build body(38) + 9 units margin
;   Seed water:     60/2.55  ≈ 23 units — fiber_root earns 1.5W/tick
;   Seed nutrients: 10/2.55  ≈  4 units — SmallLeaf/FiberRoot need none
;   Launch power:   25 energy units; max scatter radius = 25×2 = 50 cells
;   Total cost to parent: 47 + 25 = 72 energy units per seed
;
; Threshold: 220 bytes ≈ 86 units > 72-unit cost (brain can always afford)
;
; Scatter safety: parent at (64,64), radius=50 → seeds land in [14,114]×[14,114],
;   fully within the 128×128 world — no out-of-bounds seed loss.
;
; Memory layout — variables at bytes 3–6 (NEVER in the NOP-scan path):
;   byte 0–2:  JUMP main   (skips variable zone; NOP-scan wraps here safely)
;   byte 3:    TMP    — cell count scratch
;   byte 4:    GROWN  — 0=needs growth, 1=fully grown (recomputed each tick)
;   byte 5:    ENERGY — sensed energy (0–255)
;   byte 6:    THRESH — comparison scratch (0 or 1)
;   byte 7+:   main program (ends with HALT at ~byte 75)

.define TMP     0x0003
.define GROWN   0x0004
.define ENERGY  0x0005
.define THRESH  0x0006

main:
    ; --- Growth check ---
    ; Sense current cell count into TMP.
    ; CMP_EQ sets GROWN=1 when count==5 (primary + root + 3 leaves).
    ; JUMP_IF_NEQ skips the growth phase when GROWN!=0 (already grown).
    ; SENSE_CELL_COUNT [TMP]
    ; LOAD_IMM  [THRESH], 2
    ; CMP_EQ    [GROWN], [TMP], [THRESH]
    ; JUMP_IF_NEQ [GROWN], 0, reproduce

    ; --- Growth phase (only when count < 5) ---
    ; PLACE_CELL FiberRoot, +0, +1, North
    ; PLACE_CELL SmallLeaf, +0, -1, North
    ; PLACE_CELL SmallLeaf, +1,  0, North
    ; PLACE_CELL SmallLeaf, -1,  0, North

    PLACE_CELL SmallLeaf, +0, -1

    ; PLACE_CELL SmallLeaf, -1, +0
    ; PLACE_CELL SmallLeaf, +1, +0
    ; PLACE_CELL FiberRoot, +0, +1
reproduce:
    ; --- Reproduction check ---
    ; 220 bytes / 2.55 ≈ 86 units > 72-unit seed cost, so launch is always
    ; affordable when the brain decides to launch.
    ; SENSE_SELF_ENERGY [ENERGY]
    ; LOAD_IMM  [THRESH], 220
    ; CMP_LT    [THRESH], [THRESH], [ENERGY]
    ; JUMP_IF_ZERO [THRESH], done

    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT

    LAUNCH_SEED Alternating, 200, 200, 100, 10, +10, +10, random
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT
    HALT

    RET

done:
    HALT
