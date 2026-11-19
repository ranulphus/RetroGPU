# The emulated G100, against a real one

86Box's own Matrox model emulates the Productiva G100 (MGA-1064SG's successor, device 1001h). Until 2026-11-18 it was
only checked against itself and against the G200eR2 (`emulated-g200.md`, `loop-c-results.md`). That day the owner put
a real G100 in bench-g400 (the IBM 300PL, 440BX AGP), in place of the G400 moved to the P3. These are the findings, and
what changed in 86Box and the HAL to match them.

## The card

PROBE on the real card: `MGA-G100 (AGP) id=1001 rev=02`, `subsys=ff00102b`, `OPTION=4007d121`, a 16 MB frame-buffer
aperture, VBE 2.0 with `TotalMemory` 4 MB. Its modes are 8, 15, 16 and 32 bits up to 1600x1200 (16-bit modes at
640x480 and up, 32-bit at 640x480 to 1024x768).

## Memory past the card's 4 MB (local patch 0130)

`VRAMBUS` (`tests/hal/vrambus.c`) writes a value past the card's memory and reads it back:

| | Real G100 (4 MB) | 86Box before 0130 |
|---|---|---|
| A write past 4 MB | lost; nothing mirrors (the words at the same offset within 4 and 8 MB keep their own values) | lost |
| A read straight after it | the value just written | `ffffffff` |
| A read after a write to memory that is there | the value of that write | `ffffffff` |
| A read after a read of memory that is there | still the last value written (reads don't change it) | `ffffffff` |

So a read past the memory gives the data lines' last written value. The HAL's VRAM probe (`mga_probe_vram`) wrote a
marker at each MB and read it back at once, so it found 16 MB on the real card (GLOS's kernel, with the same function,
found 8 MB). DOS-GL and GLOS then put buffers in memory the card doesn't have. GLOS's screensavers drew a white screen,
then black, in a window on the real G100, but right on the emulated one (whose 8 MB were all there).
- **The probe (2026-11-18):** before each read-back, a locked instruction drains the write-combining buffers and a write
  to memory that is there puts another value on the bus. It now finds 4 MB on the real card, and GLOS's saver draws
  right there (GLOS's run, `vram=4096K`).
- **86Box, local patch 0130:** an emulated G100 keeps the frame buffer's last written dword (`g100_bus`, by byte lane
  for byte and word writes). A read past its memory returns it, and a write there is lost. `VRAMBUS` in Loop A with
  `--g100-mb 4` now reads back as the real card did, and the old probe there finds 16 MB, as on the real card.
- **Self-test:** `make loopa-selftest-ext CHECKS=g100vram` runs `VRAMBUS` on a 4 MB emulated G100. It checks the
  read-backs past 4 MB and that the probe finds 4 MB, so it fails without patch 0130.
- **Only the G100:** whether the G200, G400 and G450 behave the same past their memory is unmeasured. Their probes were
  right on their benches, where the aperture is filled or the memory mirrors.

## Bilinear filtering at texel centres (local patch 0131)

The real G100 drew DOS-GL's t07 (a bilinear-filtered texture) half a texel off with the HAL's shift for filtering
from texel corners, which the emulated G100 needed. Without the shift it matches Mesa, as the G200 and G400 do.
- **The HAL:** `mga.texel_centre` is set for the G100 too, so `tex_adjust_coords` no longer shifts its coordinates.
- **86Box, local patch 0131:** the emulated G100 filters around texel centres, as the G200 and G400 models already did.
  DOS-GL's frames on the emulated G100 change by at most one RGB565 step, on filtered textures (t07, t20, t21, t28,
  t30); the same tests pass and fail as before.

## A textured trapezoid with an empty row left of the clip stalls the engine (local patches 0132, 0133)

DOS-GL's conformance t10 and t13 (both with fog) hung the PC with the real G100. Found in two rounds, on the card:

1. **2026-11-18, one shape a run** (`FOGPROBE`, `tests/hal/fogprobe.c`, without fog, where a stall only times out
   the HAL's engine sync and resets the engine):

   | Shape (a perspective-textured `TEXTURE_TRAP` unless said) | Pixels drawn | Real G100 |
   |---|---|---|
   | On the screen | 19,500 | draws |
   | Its last rows wholly left of the screen (t10's third triangle) | 176 | draws |
   | Its first rows wholly left of the screen | 169 | draws |
   | A sliver on the screen between pixel centres, textured or not | 0 | draws (nothing) |
   | Wholly right of the screen | 0 | draws (nothing) |
   | Wholly left of the screen, untextured (`TRAP`) | 0 | draws (nothing) |
   | Wholly left of the screen, its tip at the top, affine or perspective | 0 | **stalls** |
   | Its tip on the screen, every other row wholly left (t10's fifth triangle) | 0 | **stalls** |

   The first fix left out textured pieces that draw nothing. t10 still hung the PC, with the engine's fog off
   (`DGL_NOFOG=1`) too, and t13 sends no such piece.
2. **2026-11-19, DOS-GL's own triangles** (`FOGPROBE replay` of `DGL_TRILOG=1` logs, an engine sync after each):
   exactly 14 of t10's 816 triangles and 2 of t13's 22 stalled, the same whatever ALPHACTRL held. Each of them, and
   none of the rest, has a row whose span is **empty and left of the clip**: a triangle's tip off the screen's left
   edge, below rows that draw (t10's eighth: its top on the screen, its tip 244 pixels left of it) or above them.
   The shapes of round 1 agree: the two that stalled have such rows below their tips; the rest have none (empty rows
   on the screen, or wider rows wholly left, do not stall it). Replaying t10 without a sync after each triangle then
   hung the PC without fog: after a stall the CPU's next writes to the card never complete.

- **The HAL:** with `mga.textrap_empty_stall` (the G100 only), the setup sends a textured piece of a triangle from its
  first row that draws when an empty row left of the clip lies before it, and only up to the first such row after it
  (`trap_rows` in `hal/src/setup/trap.c`: from the edges' own row stepping; usually two divisions, row by row only
  for a piece whose left edge leaves the clip). The edges move one way each, so such a row never lies between rows
  that draw. A piece that draws nothing is not sent; any other piece is sent whole, as before. No pixel changes:
  DOS-GL's frames on the emulated G100 are identical with and without it. `test_trap` checks 5,800 triangles (across
  and beyond the clip's edges, slivers, flat tops, both edge rules) against each row's exact span: no such row sent,
  every row that draws sent once, a piece without such a row sent whole. The G100 setup goldens follow.
- **86Box, local patches 0132 and 0133:** the emulated G100 reports a textured trapezoid with such a row as the
  `textrap-empty` hazard. Replaying t13 without the HAL's test, it reports exactly the 2 triangles the card stalled
  on. `make loopa-selftest-ext CHECKS=g100stall` draws t10's fifth and eighth triangles with the HAL's test (no hazard;
  the eighth's 52 pixels drawn) and without it (`nocull`: the hazard).

## Still open

- **The BIOS's `TotalMemory`:** the real BIOS reports 4 MB. The emulated BIOS reports 8 MB for a 4 MB card, with or
  without 0130: it probably reads the size from a register 86Box sets for 8 MB.
- **2D on silicon:** TWODTEST passes on the real G100 at 16 and 8 bits (every primitive byte for byte against its
  software copy). Its surfaces lie below 1.2 MB, so they say nothing about the memory size.
- **DOS-GL's conformance:** 12 of 35 tests fail on the emulated G100 (t08 t09 t13 t17 t20 t21 t22 t24 t25 t26 t28
  t30), the same before and after these changes. On the real card t01 to t09 run (t07 passes now; t08 and t09 fail
  as in 86Box).
- **t10 and t13 on the real card with `trap_rows`:** to be run (2026-11-19). Until then, the tools used:
  - `FOGPROBE replay` draws DOS-GL's own triangles from a `DGL_TRILOG=1` log (t10's 816, t13's 22, with a depth
    buffer when they use one), without fog, syncing after each (`each`): a stall names its triangle.
  - `DGL_TRISYNC=1` makes DOS-GL sync after each triangle and log one that stalls (`DGL-STALL`); the device's close
    logs `DGL-ENGINE` when the engine timed out at all.
  - `DGL_STATEDUMP=N` (a `REGCRC=1` build) prints every register's last value after the Nth triangle, to compare two
    programs' engine state. It found that t10 drew with ALPHACTRL's alpha from the iterated colour, unlike any test
    run on the card before; the replays then stalled on the same triangles with it and without it (`FOGPROBE ...
    alphactrl HEX`), so it plays no part. (The G100's `engine_init` writes no ALPHACTRL.)
