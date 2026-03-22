; Reproducer
;
; Strategy:
;   The brain advances one byte/tick after each HALT, cycling through
;   the program in ~989 ticks. LAUNCH_SEED fires at tick 501 of each cycle.
;
; Body: Primary + Anther (+0,-1) + SmallLeaf (+1,0) + SmallLeaf (-1,0)
;       + FiberRoot (+0,+1)   [all cells directly adjacent to primary]
;   Build costs: Anther(10E) + 2×SmallLeaf(20E) + FiberRoot(8E) = 38 energy
;
; Net income (fully grown, avg light=1.0):
;   Energy:  2 SmallLeaf × 1.0 × light − primary(0.1) − anther(0.2) − fiberroot(0.2)
;            = 2.0 − 0.5 = +1.5 E/tick on average
;   Winter:  2 × 0.25 − 0.5 = 0 E/tick  (breakeven, no starvation)
;   Water:   fiberroot(1.5) + primary(0.2) − 2×leaf(0.2) = +1.3 W/tick
;
; Seed child resources: energy≈47E, water≈31W, nutrients≈20N
;   Child builds body (38E) → 9E remaining; earns 1.5E/tick on avg
;   At LAUNCH_SEED (500 ticks later): ~758E → always launches viable seeds
;
; The 499-HALT accumulation window spans half a season, averaging out
; light variation so winter-born children still reach the energy threshold.
;
; Reproduction mode:
;   MATE_BY_SIZE 128, 1 prefers large mates up to 128 cells away.
;   The always-active distance bias selects the nearest Anther-bearing
;   mate for mutated offspring that have lost MATE_BY_* instructions.
;   Asexual fallback fires only when no other Anther-bearing plant exists.
;
; Memory layout:
;   byte 0-2:  JUMP main  (wraps ip safely back to program start)
;   byte 3-6:  scratch variables (unused by this program)
;   byte 7+:   main program

    JUMP main   ; bytes 0-2
    .db 0x00    ; byte 3: scratch
    .db 0x00    ; byte 4: scratch
    .db 0x00    ; byte 5: unused
    .db 0x00    ; byte 6: unused

main:
    PLACE_CELL Anther,    +0, -1
    ; PLACE_CELL SmallLeaf, +1,  0
    PLACE_CELL SmallLeaf, -1,  0
    PLACE_CELL FiberRoot, +0, +1

reproduce:
    .fill 100, 0x01

    MATE_BY_SIZE 128, 1
    LAUNCH_SEED RandomMix, 80, 10, 50, 15, +0, +0, random

done:
    HALT
