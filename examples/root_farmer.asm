; Root Farmer
;
; Strategy: grow a network of roots to maximise water and nutrient
; extraction. Use xylem to bring resources to the primary cell.
; Only grow when energy is abundant.
;
; Memory layout:
;   0x00E0 = energy level
;   0x00E1 = water level
;   0x00E2 = cell count
;   0x00E3 = threshold register
;   0x00E4 = loop counter

.define ENERGY  0x00E0
.define WATER   0x00E1
.define COUNT   0x00E2
.define THRESH  0x00E3
.define LOOP    0x00E4

main:
    ; Sense current state
    SENSE_SELF_ENERGY [ENERGY]
    SENSE_SELF_WATER  [WATER]
    SENSE_CELL_COUNT  [COUNT]

    ; Don't grow if energy is low
    LOAD_IMM [THRESH], 40
    CMP_LT [THRESH], [THRESH], [ENERGY]
    JUMP_IF_ZERO [THRESH], main

    ; Don't grow too large (cap at 12 cells)
    LOAD_IMM [THRESH], 12
    CMP_LT [THRESH], [COUNT], [THRESH]
    JUMP_IF_ZERO [THRESH], main

    ; Place xylem connectors to route resources
    PLACE_CELL Xylem, +0, -1, South
    PLACE_CELL Xylem, +0, +1, North

    ; Spread roots outward (multiple directions)
    PLACE_CELL Root, +0, -2, South
    PLACE_CELL Root, +1, -1, South
    PLACE_CELL Root, -1, -1, South
    PLACE_CELL Root, +0, +2, North
    PLACE_CELL Root, +1, +1, North
    PLACE_CELL Root, -1, +1, North

    ; A couple of leaves to maintain energy income
    PLACE_CELL SmallLeaf, +1, +0, North
    PLACE_CELL SmallLeaf, -1, +0, North

    JUMP main
