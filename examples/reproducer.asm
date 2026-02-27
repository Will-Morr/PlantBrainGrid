; Reproducer
;
; Corrected strategy:
;   1. Attempt to grow a compact body each tick: 2 roots + 3 small leaves.
;      Roots are placed first so the plant builds water income before energy income.
;      Placements that are already occupied or unaffordable fail silently.
;   2. Clone once energy exceeds the threshold (Alternating = get random mix of genes from another plant).
;   3. HALT at the end of each tick — brain runs exactly once per tick,
;      avoiding the instruction-limit penalty entirely.
;
; Resource budget (fully grown, 3 SmallLeafs + 2 Roots):
;   Energy income:  3 × 1.0 = 3.0 / tick  (× light multiplier ≈ 0.9 avg)
;   Energy outgo:   primary 0.1 + 2 roots × 0.1 = 0.3 / tick
;   Net energy:     +2.7 / tick
;
;   Water  income:  2 roots × 1.5 = 3.0 / tick (while local world cells hold water)
;   Water  outgo:   3 leaves × 0.2 = 0.6 / tick
;   Net water:      +2.4 / tick
;
; Reproduction (LAUNCH_SEED Alternating, 80, 50, 20, 60, +0, +0, random):
;   Seed energy:    80 / 2.55 ≈ 31 units  — child can place Root(8)+Root(8)+Leaf(10)
;   Seed water:     50 / 2.55 ≈ 20 units
;   Seed nutrients: 20 / 2.55 ≈  8 units
;   Launch power:   60 units (not scaled); max scatter radius = 60 × 2 = 120 cells
;                   (large radius colonises fresh world cells well away from the parent)
;   Total cost to mother: 31 + 60 = 91 energy, 20 water
;
; Threshold: 240 bytes ≈ 94 energy units
;   After launch:  94 − 91 = ~3 units remaining → earns back at +2.7/tick
;   At full growth: reproduces every ~33 ticks
;
; Memory layout:
;   0x00D0 = ENERGY   (sensed energy, 0–255)
;   0x00D1 = THRESH   (comparison scratch, 0 or 1)

.define ENERGY  0x00D0
.define THRESH  0x00D1

main:
    ; --- Growth phase ---
    ; Try to place all body cells.  Already-placed cells are skipped silently.
    ; Cells the plant cannot yet afford are also skipped silently.
    ; Roots first: even a newly germinated child (31 energy) can place
    ;   Root(8) + Root(8) + Leaf(10) = 26, leaving 5 energy in reserve.
    PLACE_CELL Root,      +0, +1, North   ; root below primary
    PLACE_CELL Root,      +1, +1, North   ; root diagonal — taps a different world cell
    PLACE_CELL SmallLeaf, +0, -1, North   ; leaf above
    PLACE_CELL SmallLeaf, +1,  0, East    ; leaf right
    PLACE_CELL SmallLeaf, -1,  0, West    ; leaf left

    ; --- Reproduction check ---
    ; SENSE_SELF_ENERGY fills ENERGY with energy * 2.55, capped at 255.
    ; 240 bytes ≈ 94 units, safely above the 91-unit total seed+launch cost.
    ; CMP_LT sets THRESH = 1 when 240 < ENERGY (i.e. we are above threshold).
    ; JUMP_IF_ZERO skips LAUNCH_SEED when THRESH = 0 (below threshold).
    SENSE_SELF_ENERGY [ENERGY]
    LOAD_IMM  [THRESH], 240
    CMP_LT    [THRESH], [THRESH], [ENERGY]   ; THRESH = (240 < energy_byte)
    JUMP_IF_ZERO [THRESH], done              ; skip if energy ≤ 240 bytes

    LAUNCH_SEED Alternating, 80, 50, 20, 60, +0, +0, random

done:
    HALT
