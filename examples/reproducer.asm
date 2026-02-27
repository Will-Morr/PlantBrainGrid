; Reproducer
;
; Strategy: grow quickly, then reproduce asexually once energy is high.
; Uses LAUNCH_SEED with MotherOnly recombination to clone itself.
;
; Memory layout:
;   0x00D0 = energy
;   0x00D1 = cell count
;   0x00D2 = threshold
;   0x00D3 = launch direction x
;   0x00D4 = launch direction y

.define ENERGY  0x00D0
.define COUNT   0x00D1
.define THRESH  0x00D2

main:
    SENSE_SELF_ENERGY [ENERGY]
    SENSE_CELL_COUNT  [COUNT]

    ; Growth phase: build up cells while energy < 200
    LOAD_IMM [THRESH], 200
    CMP_LT [THRESH], [ENERGY], [THRESH]    ; THRESH = (energy < 200)
    JUMP_IF_NEQ [THRESH], 1, grow_phase

    ; Reproduction phase: energy >= 200 — launch a seed
    JUMP reproduce

grow_phase:
    ; Only grow if we have at least some energy
    LOAD_IMM [THRESH], 30
    CMP_LT [THRESH], [THRESH], [ENERGY]
    JUMP_IF_ZERO [THRESH], main

    ; Build a compact body: leaves + xylem for resource flow
    PLACE_CELL SmallLeaf, +0, -1, North
    PLACE_CELL SmallLeaf, +1, +0, East
    PLACE_CELL BigLeaf,   -1, +0, West
    PLACE_CELL Root,      +0, +1, North
    PLACE_CELL Xylem,     +0, -2, South

    JUMP main

reproduce:
    ; Launch seed: clone (MotherOnly), give it 60 energy, 30 water, 15 nutrients
    ; Power=80, aim slightly northeast, exact placement
    LAUNCH_SEED MotherOnly, 60, 30, 15, 80, +5, -5, random

    ; Cool down — wait for energy to recover
    JUMP main
