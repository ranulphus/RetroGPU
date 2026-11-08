# The emulated G200 (86Box local patch 0004)

86Box emulates the Matrox G100 but not the G200. Patch
`tools/86box/patches/local/0004-mga-g200.patch` adds a "Matrox Millennium
G200 (MGA-Glide emulation)" AGP device (`millennium_g200`) so the G200 code
paths of the runtime can be developed and tested before the physical G200
and cuda6 checks (PRD WP-3, milestone M5). Like every 86Box change in this
project it is local and never upstreamed (PRD D4).

Select it with `--card g200` (or `MGA_CARD=g200`) in `tools/loopa/run.py`
and `glide/tools/conform/run.py`.

## What it adds to the G100 model

| Area | Behaviour | Source |
|---|---|---|
| Identity | PCI device `0x0521`, family G200 | Matrox PCI IDs (public) |
| Video BIOS | Matrox's G200 AGP BIOS 900-33 (VBE 3.0), unpacked from Matrox's public `setup257.exe` into the local ROM set by `tools/86box/build.sh` (patch `0007`; never committed). Fallback, or with `MGAGLIDE_G200_BIOS=g100`: the Productiva G100 BIOS with its PCI data structure and three device-ID checks rewritten to `0x0521` at load | Matrox BIOS package (pinned by sha256 in `tools/setup/versions.mk`); the G100 fallback's rewrites were measured: without them it never programs the DAC depth |
| Origins | `DSTORG` moves the colour buffer (in bytes); `ZORG` is absolute | Matrox G200 register documentation, as used by the X.org and Mesa drivers |
| Blending | `ALPHACTRL` source and destination factors, in `TRAP` and `TEXTURE_TRAP`; destination alpha is 255 in 16 bpp | same |
| Alpha test | `ALPHACTRL.aten`, `atmode`, `atref` on the selected alpha | same |
| Specular | `TEXCTL2.specen` adds the iterated `SPECR/G/B` colour after texturing | same |
| Decal blend | `TEXCTL2.decalblend` blends texel and diffuse by texel alpha | same |
| Mipmaps | `TEXORG1..4`, `TEXFILTER.mapnb` and the `mm*` minification modes; the level comes from the per-pixel coordinate gradients and the nearest-level modes round it | Rounding measured on the cuda6 G200eR2 (`docs/loop-c-results.md`, E5) |
| `DWGSYNC` | reads back the last value written | same register documentation |

With the genuine BIOS, PROBE reports VBE 3.0 and the real LFB address in
every mode entry (the G100 BIOS reported `fffffff0`), `OPTION=4007dd21`
and subsystem ID 0 (the package image carries no board subsystem bytes at
`7FF8h`). The DAC state after the mode set (`HX-STAT dac`, `HX-STAT crtc`)
is identical under both BIOSes; the conformance suite and both game
replays pass unchanged.

Patch `0010-mga-tlut-rstr.patch` (all cards) loads the texture lookup
table from a BITBLT with atype RSTR as well as RPL: the G200 and G400
specifications' recipe uses RSTR, which upstream drew into VRAM instead.
The HAL's `engine_tlut_load()` follows the specifications; DOS-GL uses the
table for 8-bit paletted textures (TW8) on the G200.

ILOAD (`engine_iload_begin/row/end`, G200 and later; DOS-GL writes
`glTexSubImage2D` rectangles with it) works in the unpatched model, with two
conditions:

- The data goes through the DMA window, and the model accepts it only while
  `OPMODE.dmamod` is BLIT, as the specification requires. The HAL sets that
  once.
- The model ignores `DWGCTL.clipdis`, so the HAL also opens the clip
  registers for the load and restores them after it.

Whether the texture cache sees texels written by ILOAD is silicon
experiment E6 (DOS-GL `docs/silicon-experiments.md`).

Patch `0011-mga-display-zoom-and-flip.patch` (all cards) fixes three display
details the resolution work (`vbe_plan_mode`, `vbe_set_zoom`,
`engine_present`) depends on:

- **Pixel doubling.** In power-graphics mode, XZOOMCTRL hzoom 2x now shows
  each pixel twice. Upstream stored the register and did nothing, and its
  generic "low-res" renderers do not double pixels on the modern render
  path, so the card has its own renderer for 15/16 and 32 bpp. Line
  doubling through CRTC9's maxscan already worked. 4x zoom, and zoom at 8
  or 24 bpp, are logged as unsupported.
- **Page flips at 1024x768 and above.** A new display start that changes
  only CRTCEXT0's high address bits (the second page of 1024x768 and
  1280x1024 lands on a 64 KB boundary) now redraws the whole screen, as a
  CRTC 0Ch/0Dh change always did. Before, the emulator kept showing the
  previous page's lines until they changed.
- **Horizontal blanking.** The Matrox blanking end is a 7-bit field (CRTC3,
  CRTC5 bit 7, CRTCEXT1 bit 6), but 86Box's generic code compares only 6
  bits unless the value has a higher bit set, so the G100 BIOS's 1600x1200
  mode ended blanking 64 clocks early and showed a 512-pixel phantom left
  border. Blanking start bit 8 (CRTCEXT1 bit 1) was also always read as 0.

`tests/hal/scale.c` checks both, against the unit tester's picture of the
monitor. Real cards are unaffected by the second fix; the first is the model
of a documented register, to be confirmed on each card at the bench.

Patch `0117-mga-2d-fidelity.patch` (the G-series) makes the model follow
the specifications where the HAL's 2D primitives (GLOS M7) depend on it: a
BITBLT reads its source at SRCORG + AR3 on the G200 and later (upstream
ignored SRCORG), CRTCEXT0 bit 6 is display start bit 20, XCURADDH has 6
bits on the G200 and G400, and the hardware cursor is 64 lines tall, in
red, green and blue (upstream swapped red and blue and, in XGA mode, passed
the colours through the palette), with the three-colour mode drawn. See
`docs/matrox-2d.md`; TWODTEST (`make loopa-twodtest`) checks it all.

Known gap: with 8 MB configured, the genuine BIOS reports **2 MB** as VBE
`TotalMemory` (DOS-GL's PROBE: `vram=2097152`), so it presumably sizes
memory from something the model does not provide. MGA-Glide never
noticed, because it sizes VRAM with `mga_probe_vram()`; DOS-GL did (a 248 KB
texture heap) and now probes too. Check what the real G200 BIOS reports at
milestone S before changing the model.

Patch `0005-mga-texel-alpha.patch` (both cards) gives TW15 texels their
1-bit alpha and expands TW12 fields by bit replication; patch
`0006-mga-measured-texturing.patch` makes the larger-weight texel decide
colour keys under bilinear filtering and rounds the mip level. All three
behaviours were then confirmed on the G200eR2 (Loop C).

## Checked on the G200eR2 (Loop C)

Texel expansion, TW15 alpha, blend arithmetic, blending in plain `TRAP`,
mip level rounding, colour keys under bilinear filtering, and the
trapezoid walk's 18-bit AR fields with crossed spans drawing nothing
(local patch 0012, for every chip before the G400): see
`docs/loop-c-results.md`. Still unchecked: large `fthres` values, the
two-level (trilinear) modes, `DSTORG` alignment rules, and anything that
might differ between the G200eR2 and a retail G200 (Loop B).
