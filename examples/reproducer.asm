; Reproducer
;
; Strategy:
;   1. Each tick: sense cell count to decide if growth is needed.
;      If fully grown, skip PLACE_CELL instructions entirely (no wasted cost).
;   2. If not fully grown, attempt to place body cells.
;   3. Once energy exceeds threshold, launch a seed.
;   4. HALT each tick — no instruction-limit penalty.
;
; Body: Primary + 1 FiberRoot (below) + 2 SmallLeafs (above, right)
;   Placement cost: FiberRoot(8) + SmallLeaf(10) + SmallLeaf(10) = 28 energy
;   A child starting with 80/2.55 ≈ 31 energy units can afford the whole body.
;
; Income (fully grown):
;   Energy income:  2 × 1.0 = 2.0/tick  (× light ≈ 0.9 avg → ~1.8/tick)
;   Maintenance:    primary 0.1 + root 0.1 = 0.2/tick
;   Net energy:     ~1.6/tick
;   Water income:   1 × 1.5 = 1.5/tick
;   Water cost:     2 leaves × 0.2 = 0.4/tick
;   Net water:      +1.1/tick
;
; Reproduction (LAUNCH_SEED Alternating, 80, 50, 20, 50, +0, +0, random):
;   Seed energy:    80/2.55 ≈ 31 units — child can place FiberRoot(8)+Leaf(10)+Leaf(10)=28
;   Seed water:     50/2.55 ≈ 20 units
;   Seed nutrients: 20/2.55 ≈  8 units
;   Launch power:   50 energy units; max scatter radius = 50 × 2 = 100 cells
;   Total cost to parent: 31 + 50 = 81 energy units
;
; Threshold: 220 bytes ≈ 86 energy units
;   After launch:  86 − 81 = ~5 units remaining → recovers at ~1.6/tick
;   Re-launch every ~50 ticks at full growth
;
; Memory layout:
;   0x00CD = TMP    (scratch for cell count comparison)
;   0x00CE = GROWN  (0=needs growth, 1=fully grown — recomputed each tick)
;   0x00D0 = ENERGY (sensed energy, 0–255)
;   0x00D1 = THRESH (comparison scratch, 0 or 1)

.define TMP     0x00CD
.define GROWN   0x00CE
.define ENERGY  0x00D0
.define THRESH  0x00D1

main:
    ; --- Growth check ---
    ; Sense current cell count. If == 4 (primary + root + 2 leaves), skip growth.
    ; This prevents paying placement cost for already-occupied positions.
    SENSE_CELL_COUNT [TMP]
    LOAD_IMM  [THRESH], 5
    CMP_EQ    [GROWN], [TMP], [THRESH]
    JUMP_IF_NEQ [GROWN], 0, reproduce

    ; --- Growth phase (only runs when cell count < 4) ---
    PLACE_CELL FiberRoot,      +0, +1, North
    PLACE_CELL SmallLeaf, +0, -1, North
    PLACE_CELL SmallLeaf, +1,  0, North
    PLACE_CELL SmallLeaf, -1,  0, North

reproduce:
    ; --- Reproduction check ---
    ; SENSE_SELF_ENERGY fills ENERGY with energy * 2.55, capped at 255.
    ; 220 bytes ≈ 86 units, safely above the 81-unit total seed+launch cost.
    ; CMP_LT sets THRESH = 1 when 220 < ENERGY (i.e. we are above threshold).
    ; JUMP_IF_ZERO skips LAUNCH_SEED when THRESH = 0 (below threshold).
    SENSE_SELF_ENERGY [ENERGY]
    LOAD_IMM  [THRESH], 220
    CMP_LT    [THRESH], [THRESH], [ENERGY]
    JUMP_IF_ZERO [THRESH], done

    LAUNCH_SEED Alternating, 150, 150, 120, 50, +0, +0, random

done:
    HALT
