; Fire Starter
;
; Strategy: grow fireproof xylem for self-protection, then place a
; FireStarter cell to burn neighbouring plants, then reproduce into
; the cleared territory.
;
; Memory layout:
;   0x00C0 = energy
;   0x00C1 = cell count
;   0x00C2 = threshold
;   0x00C3 = neighbour cell type
;   0x00C4 = is fire nearby

.define ENERGY      0x00C0
.define COUNT       0x00C1
.define THRESH      0x00C2
.define NEIGHBOR    0x00C3
.define FIRE_NEAR   0x00C4

main:
    SENSE_SELF_ENERGY [ENERGY]
    SENSE_CELL_COUNT  [COUNT]

    ; Don't act if energy is low
    LOAD_IMM [THRESH], 50
    CMP_LT [THRESH], [THRESH], [ENERGY]
    JUMP_IF_ZERO [THRESH], main

    ; Build fireproof xylem before doing anything aggressive
    LOAD_IMM [THRESH], 4
    CMP_LT [THRESH], [COUNT], [THRESH]
    JUMP_IF_NEQ [THRESH], 1, build_body

    JUMP offense

build_body:
    ; Fireproof xylem in all 4 directions for protection
    PLACE_CELL FireproofXylem, +0, -1, South
    PLACE_CELL FireproofXylem, +0, +1, North
    PLACE_CELL FireproofXylem, +1, +0, West
    PLACE_CELL FireproofXylem, -1, +0, East
    ; Plus a leaf for energy
    PLACE_CELL SmallLeaf, +0, -2, North
    JUMP main

offense:
    ; Check if a competitor is nearby (2 tiles away)
    SENSE_CELL [NEIGHBOR], +2, +0
    LOAD_IMM [THRESH], 0            ; Empty = 0
    CMP_EQ [THRESH], [NEIGHBOR], [THRESH]
    JUMP_IF_NEQ [THRESH], 1, place_fire_east

    SENSE_CELL [NEIGHBOR], -2, +0
    LOAD_IMM [THRESH], 0
    CMP_EQ [THRESH], [NEIGHBOR], [THRESH]
    JUMP_IF_NEQ [THRESH], 1, place_fire_west

    ; No nearby enemies — try reproducing instead
    JUMP try_reproduce

place_fire_east:
    PLACE_CELL FireStarter, +1, +0, East
    JUMP main

place_fire_west:
    PLACE_CELL FireStarter, -1, +0, West
    JUMP main

try_reproduce:
    ; Only reproduce if very energy-rich
    LOAD_IMM [THRESH], 180
    CMP_LT [THRESH], [THRESH], [ENERGY]
    JUMP_IF_ZERO [THRESH], main

    LAUNCH_SEED MotherOnly, 70, 35, 20, 60, +8, +0, random
    JUMP main
