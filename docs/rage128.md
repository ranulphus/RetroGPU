# The ATI Rage 128 Pro

The third vendor after the S3 ViRGE/DX (`docs/virge.md`) and the Riva TNT2 (`docs/tnt2.md`), in the owner's order
of 2026-11-12. The card is in bench B (the P3) since 2026-11-20, the TNT2 out.

## The references and their licences

- **X.org's `xf86-video-r128`** (MIT): `r128_reg.h` for the register offsets and bits, `r128_driver.c` for the
  memory type's and the video BIOS's PLL table's readings, `r128_accel.c` for the 2D engine. Facts and the
  register sequences are taken from it; the code isn't copied.
- **Linux's DRM `r128` driver** (MIT, `drivers/gpu/drm/r128`): the command processor (CCE) and its microcode,
  for the 3D steps; read before any of it is used, its licence checked file by file.
- Measurement on the card (the P3), as the TNT2's facts were.

## What R0 found (the P3, 2026-11-20, `RPROBE`)

`RPROBE` (`tests/r128/rprobe.c`) reads only, but for the PLL index (put back).

- **The chip:** `1002:5046`, a Rage 128 Pro PF (AGP 4x), revision 0, subsystem `0008:1002` (ATI's own board),
  IRQ 10, on bus 1 behind the AGP bridge.
- **Its windows:** BAR0 `d8000000` (prefetchable, 64 MB: two 32 MB apertures, APER_0 at `d8000000` and APER_1 at
  `da000000`), BAR1 I/O `cc00`, BAR2 the registers at `dfefc000` (16 KB; REG_APER_SIZE 8 KB, a second copy at
  `dfefe000`), the ROM BAR at `dfec0000` (off).
- **AGP:** capability 2.0 at `50`: 1x, 2x and 4x, sideband addressing, 32 requests; the BIOS leaves AGP off
  (command `00000200`). Power management at `5c`.
- **Memory:** CONFIG_MEMSIZE 16 MB; MEM_CNTL `b0004300`, bits 1-0 zero: 128-bit SDR SGRAM (X.org's reading).
- **Clocks:** the BIOS's PLL table (header `0110`, PLL block `0620`): a 29.50 MHz reference, reference divider 65
  (PPLL_REF_DIV reads `41`, the same), XCLK 120 MHz, the PLL from 125 to 400 MHz. The PLL registers `00`-`1f` are in
  the log for R1.
- **The engine:** idle (GUI_STAT `00000040`: FIFO room, nothing active); the CCE off (PM4_BUFFER_CNTL 0,
  PM4_STAT `c0`); interrupts off.
- **The BIOS:** 32 KB, ATI's signature at `31`, "Rage128 PRO GL P/N 113-60609-100"; VBE 2.0, 16 MB, 56 modes
  (OEM "ATI RAGE128", product "R128"), every one with its mode attributes and linear window in the log.

## Next (a plan for the owner before any of it)

R1 the register layer (the MMIO window, the engine's FIFO and idle, the 2D engine, the display start, the
cursor; a 2D test against `tests/hal/sw2d.c`), R2 the 3D engine (through the CCE, its microcode's licence first),
R3 the device on `rg_device`, R4 DOS-GL's back end, R5 Glide's, R6 the docs: the TNT2's steps, in the same order.
