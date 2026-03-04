; Simple Leaf Grower
;
; Strategy: sense energy level, and if high enough place leaves
; in all 4 cardinal directions. Loop forever.
;
; Memory layout:
;   0x00F0 = current energy level (0-255)
;   0x00F1 = cell count (0-255)
;   0x00F2 = scratch/threshold

.define ENERGY_REG  0x00F0
.define COUNT_REG   0x00F1
.define THRESH_REG  0x00F2

; Entry point
main:
    ; Read current energy
    SENSE_SELF_ENERGY [ENERGY_REG]

    ; Only grow if energy > 50 (threshold)
    LOAD_IMM [THRESH_REG], 50
    CMP_LT [THRESH_REG], [THRESH_REG], [ENERGY_REG]   ; THRESH = (50 < energy)
    JUMP_IF_ZERO [THRESH_REG], main                    ; skip if energy <= 50

    ; Try to place leaves in all 4 directions
    PLACE_CELL SmallLeaf, +0, -1    ; north
    PLACE_CELL SmallLeaf, +1, +0     ; east
    PLACE_CELL SmallLeaf, +0, +1    ; south
    PLACE_CELL SmallLeaf, -1, +0     ; west

    ; Also try to place a root below us to gather water
    PLACE_CELL FiberRoot, +0, +2

    JUMP main
