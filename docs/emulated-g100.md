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

## Still open

- **The BIOS's `TotalMemory`:** the real BIOS reports 4 MB. The emulated BIOS reports 8 MB for a 4 MB card, with or
  without 0130: it probably reads the size from a register 86Box sets for 8 MB.
- **2D on silicon:** TWODTEST passes on the real G100 at 16 and 8 bits (every primitive byte for byte against its
  software copy). Its surfaces lie below 1.2 MB, so they say nothing about the memory size.
- **DOS-GL's conformance:** 12 of 35 tests fail on the emulated G100 (t08 t09 t13 t17 t20 t21 t22 t24 t25 t26 t28
  t30), the same before and after these changes. Comparing on the real card is next.
