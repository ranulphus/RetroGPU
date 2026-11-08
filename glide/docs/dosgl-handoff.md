# DOS-GL handoff (PRD Appendix B)

What MGA-Glide delivers to DOS-GL, item by item against PRD Appendix B,
with the evidence behind each answer. "Emulated" means 86Box with the local
patch series; "silicon" results wait for the bench (PRD milestone S) and
are marked **pending**.

## Delivered

| Item | Status | Evidence |
|---|---|---|
| **B1** G100, G200, G450 in the HAL | G100: complete (emulated). G200: complete against the emulated G200 on Matrox's BIOS 900-33. G400 and G450: complete against emulated cards on Matrox's BIOSes 897-21 and 935-20 | `hal/src/chip.c`; conformance 26/26 on all four emulated cards; `docs/emulated-g400.md` |
| **B2** G100 blending | Stipple only: `ALPHACTRL` must hold `0x54` in its low byte, `astipple` (bit 11) turns the stipple on, `alphasel` picks texture, diffuse or modulated alpha. Additive and destination-reading blends cannot be expressed | `docs/combine-coverage.md`; G100 approximations |
| **B3** 86Box | The G100 needs `-DDEV_BRANCH=ON`. Use `tools/86box/build.sh` (pinned commit plus `tools/86box/series`) | local patches 0001–0011, 0101–0104 (listed in `tools/86box/series`); 0103 fixes a CPU fault that crashed DJGPP programs in DPMI calls, depending on where the stack sat (`docs/loops.md`) |
| **B4** G100 `ALPHACTRL` | Stipple alpha only, confirmed in 86Box. On silicon: **pending** | as B2 |
| **B5** Emulated G200 (and G400/G450) | `--card g200`: native `ALPHACTRL` blending and alpha test (also in plain `TRAP`), `TEXCTL2` specular and decal blend, `TEXORG1..4` mip levels, `DSTORG` with absolute `ZORG`. `--card g400` / `g450` add the G400 family's identity, BIOS, 32 MB BAR, 16-entry FIFO and CRTC units | `docs/emulated-g200.md`, `docs/emulated-g400.md` list what is modelled and what is guessed |
| **B6** HAL and harness | `tools/hal-export.sh` produced a standalone tree (removed 2026-11-02: DOS-GL, GLOS, Fifth Wheel and DOSBench build from this checkout, on main, with no copy) (HAL, reference rasteriser, smoke/probe/romdump, the guest shim with DJGPP builds of HELLO and PROBE, Loop A, Loop B bench tooling, the Loop C rig, 86Box build and patches) with a `MANIFEST` of sha256 sums. The DJGPP HELLO and PROBE pass in Loop A (CWSDPMI r7 from `setup-djgpp`) and as bench jobs on the 86Box virtual bench PC | `tests/shim/`, `tests/hal/` |
| **B9** DPMI and crash handling | `hal/port/djgpp.c` implements the port API for DJGPP. The fault/exit hooks exist only in the DOS/4GW port (`sys_hook_faults` returns -1 elsewhere), so DOS-GL keeps its own top-level handler, as planned | `hal/include/mga/sys.h` |
| **B10** Toolchain neutrality | The HAL builds warning-free with Open Watcom, DJGPP (gcc 12.2, `-Werror`) and the host compiler; CI builds all three | `.github/workflows/ci.yml` |
| **B12** Shared test infrastructure | `tools/imgcmp.py` (565 quantisation, tolerance, edge masks, box filter, ignore rectangles, per-cell gating), `tools/loopa/run.py` (statuses, serial parsing, screenshots, `--card`), `VMODE` for display-state checks | `docs/loops.md` |
| Resolutions (2026-09-29) | `vbe_plan_mode` chooses how any size is shown with the BIOS's modes (native; zoom = the BIOS mode at twice the size with `vbe_set_zoom`'s line and pixel doubling; integer, fill or aspect scaling through `engine_present`); `engine_save/restore`; targets may differ in pitch. Zoom is opt-in until the bench confirms it per card. SCALE.EXE (HAL) and GLRES.EXE (every Glide resolution through the runtime) pass on all four emulated cards (86Box patch 0011) | `hal/include/mga/hal.h`, `tests/hal/scale.c`, `tests/unit/test_plan.c`, `test_present.c` |

## Lessons DOS-GL should inherit

- **Initialise `ALPHACTRL` on blending chips.** Its reset value multiplies
  source and destination by zero, so every 3D trapezoid draws black.
  `engine_init` now writes ONE/ZERO; a program that bypasses it must too.
- **G200 origins.** `DSTORG` is in bytes and moves only the colour buffer;
  `ZORG` is absolute. On the G100, `ZORG` is relative to `YDSTORG` and the
  clip registers include `YDSTORG`.
- **Engine sync.** Wait for `FIFOSTATUS.bempty` and then `!STATUS.dwgengsts`;
  the busy bit alone races with the FIFO.
- **DOS time.** The PIT is in mode 3 (not monotonic when read directly);
  the BIOS tick count is reliable.
- **Emulator gaps affect references.** 86Box's Voodoo ignores the LFB
  Y-origin and lane swizzle, and its texture cache keys palettes by XOR
  (collisions). 86Box's MGA left TW15 texel alpha unset and shifted TW12
  fields (fixed locally: patch 0005). See `docs/reference-gaps.md`.
- **16-bit display goes through the palette.** The DAC indexes the palette
  with each component's own value: red and blue 0-31, green 0-63 in 16-bit
  modes. That is the layout the BIOS loads, and how a real G400 reads it
  (bench-g400, 2026-10-26). `dac_set_ramp` writes that layout for the
  current depth. Local patch 0003 had made 86Box index by the top bits
  instead; on silicon, ramps in that layout drew every frame at an eighth of
  its brightness. It was dropped.

## Pending (needs the bench or cuda6)

| Item | What is missing |
|---|---|
| **B7** Throughput | Only emulator figures exist (`t23`: ~52 K small triangles/s in 86Box, which measures the emulator). Pentium II figures and the WARP decision come from milestone S |
| **B8** BAR layout, revision IDs, VBE lists, FIFO depth | Emulated values: framebuffer, MMIO and ILOAD apertures as in `tests/hal/probe.c` output; FIFO depth 64 on G100/G200, 16 on G400/G450 (from the G400 specification, modelled); the Matrox BIOSes report VBE 3.0 and 555/565 modes to 1280x1024. BAR layout confirmed on the G200eR2. Physical G100/G200/G400/G450: **pending** |
| **B11** cuda6 | All four `mgarig` stages ran on the G200eR2 (register values, `DWGSYNC`, one `TRAP`, one `TEXTURE_TRAP`) plus texturing and blending experiments; the chip kept its 3D engine. `mgarig` identifies any Matrox card from its config space (`RIG_BDF`) | `docs/loop-c-results.md` |
| **B2/B4 on silicon** | Stipple pattern, alpha thresholds and texel-key behaviour under bilinear filtering (see the milestone S list in the plan) |
