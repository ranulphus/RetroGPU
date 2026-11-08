# The emulated G400 and G450 (86Box local patch 0008)

`tools/86box/patches/local/0008-mga-g400-g450.patch` adds two AGP devices,
"Matrox Millennium G400 (MGA-Glide emulation)" (`millennium_g400`) and
"Matrox Millennium G450 (MGA-Glide emulation)" (`millennium_g450`), so the
runtime's G400-family path can be exercised before the physical cards
(PRD M6). Like every 86Box change here it is local and never upstreamed
(PRD D4). Select them with `--card g400` / `--card g450` (or
`MGA_CARD=...`) in `tools/loopa/run.py`, `glide/tools/conform/run.py` and
`tools/games/replay.py`.

Both build on the emulated G200 (`docs/emulated-g200.md`): the drawing
engine, blending, alpha test, specular and mipmaps are the G200 model's.
Patch `0009-mga-g400-dual-texture.patch` adds the second texture map and
the combiner (below). Patch `0012-mga-ar-field-widths.patch` gives the
trapezoid walk the G400 specification's 22-bit AR fields (18 before the
G400) and lets crossed spans draw nothing, as a G200eR2 does
(`docs/loop-c-results.md`). Patches 0122 and 0123 model the WARP setup
engine (`docs/g400-warp.md`); 0119-0121, 0124 and 0125 follow what a real
G400 did (below). Neither card models CRTC2 or the MAVEN; the runtimes do
not use them.

## Dual texturing (patch 0009)

From the G400 specification and the drivers that ran on real G400s,
collected in `docs/g400-dual-texture.md`. MGA-Glide never uses it; DOS-GL's
`GL_ARB_multitexture` does.

| Area | Model | Basis |
|---|---|---|
| Map 1's registers | A second copy of TMR0-8, TEXORG and TEXORG1-4, TEXWIDTH, TEXHEIGHT, TEXCTL, TEXCTL2, TEXTRANS(HIGH), TEXFILTER, TEXBORDERCOL, ALPHACTRL | specification (the per-map table, p.3-216) |
| Routing | `tmap0dis` is the OR of bit 31 last written to TEXCTL2, TEXWIDTH and TEXHEIGHT and applies from the next write: writes reach both maps while it is 0, map 1 only while it is 1; a register's start alias (+0x100) starts the draw after the routing | specification; **guessed:** that the write setting the bit is itself broadcast (X.org's EXA code relies on it) |
| Sampling | With `TEXCTL2.dualtex`, map 1's coordinates step like map 0's (per pixel, per line, and with the left edge) and it is sampled with its own registers | specification |
| Combiner | TDUALSTAGE0 always (single texturing too), TDUALSTAGE1 with dualtex, after the legacy module (`TEXCTL.tmodulate`, decal blend, specular); zero words pass the texel through, so everything drawn before is unchanged. Products `(a*b)>>8` as the legacy modulate; the blend mode's two passes as the specification's datapath | specification (fields and datapath); **guessed:** rounding, the legacy module's place before the combiner, add2x/addbias order |
| Alpha | ALPHACTRL alphasel "from texture" takes the combiner's alpha | Mesa programs it this way |
| Blending in dual mode | Map 0's ALPHACTRL | **differs:** the silicon takes map 1's; runtimes write both the same |
| Not modelled | TEXBORDERCOL (stored), bump mapping, table fog, Rev A's zero-TDUALSTAGE rule, TEXCTL2 bit 15 | |


## What differs from the G200 model

| Area | G400 | G450 | Source |
|---|---|---|---|
| PCI identity | `102B:0525`, revision 3 | `102B:0525`, revision `0x82` | Matrox PCI IDs; the G450 is a G400 with revision `0x80` and up, and the G400 BIOS applies extra fixes only to revision 2 (`897-21.asm`) |
| Video BIOS | Matrox G400 BIOS 897-21 | Matrox G450 BIOS 935-20 (the 34 KB image; the flasher module appended to the file is ignored) | Matrox's public `setup257.exe`, unpacked by `tools/86box/build.sh` into the local ROM set, never committed |
| Memory | 16 or 32 MB (`memory = 16 \| 32`) | same | card configurations |
| Framebuffer BAR | 32 MB | 32 MB | G400 specification |
| Bus FIFO | 16 entries (`FIFOSTATUS.fifocount<4:0>`, reset `0x210`) | same | G400 specification, FIFOSTATUS |
| CRTC offset | always 128-bit units, start address 64-bit units (the G200 model gets this only with `OPTION.interleave`, which means something else on the G400) | same | G400 specification §4.6.5 |
| `CRTCEXT6..8` | stored and read back (the G400 BIOS reads `CRTCEXT8`) | same | G400 specification |
| Pixel clock | Fvco = Fref·(N+1)/(M+1), Fo = Fvco/(P+1) with **Fref = 14.31818 MHz** | Fvco = 2·27 MHz·(N+2)/(M+1), Fo = Fvco/2^((P&3)+1) | G400: the formula is the specification's, but 897-21's tables only make sense with a 14.318 MHz reference (640x480: M=1, N=27, P=7 → 25.06 MHz; at the specification's 27 MHz the VCO would be 378 MHz, beyond its 310 MHz limit). G450: as the Linux matroxfb driver models it; 935-20 then programs 25.07 MHz. Both inferred: check on the cards |

Under both BIOSes PROBE reports VBE 3.0, the real LFB address, the
expected VRAM size, working engine and depth fills, and the same DAC and
CRTC state as the G200 after setting 640x480x16. Conformance is 26/26 on
both cards, and the GTA and Screamer Rally trace replays pass on both.

## A real G400 (bench-g400, 2026-10-26)

The card is an AGP G400 16 MB in an IBM PC 300PL (440BX). PROBE (main ec03889) ran under GLOS on MS-DOS 7.1
through `tools/bench/run.py`, next to the same PROBE under `--card g400`:

| | Silicon | 86Box | |
|---|---|---|---|
| Identity | `0525` revision 05, subsystem `0338102B` | revision 05 (0125; 03 before), subsystem 0 | the subsystem comes from the BIOS image's 7FF8h, as on the card, and Matrox's package image has none there |
| AGP capability (F0h-F8h) | `00200002`, status `1f000203` (1x and 2x, sideband, queue 32), command `1f000301` after the BIOS | status `1f000203` (0125; `01000201` before), the same command | the HAL's AGP start picks 2x in Loop A too |
| OPTION | `50040120` | `50040120` | same |
| OPTION2, OPTION3 | `01003000`, `0190A419` (G4EXP E7) | 0, 0 | not modelled |
| STATUS at rest | `80020024` | `00020000` | the G200eR2 reads `80020024` too |
| FIFOSTATUS at rest | `00000210`: 16 free, bempty | `0000FF10` | **the model is wrong:** the upper byte should be `02` (bempty only), not `FF` |
| VBE | 2.0, modes up to 800x600, 16 MB | 3.0 (897-21), modes up to 1280x1024, 32 MB | another BIOS; the HAL uses neither difference |
| 640x480 pixel PLL | M=06 N=33 P=1F: 27 MHz x 52 / 7 / 8 = 25.07 MHz | M=01 N=1B P=17 (14.318 MHz) | **this card's reference is 27 MHz,** as the specification says. 897-21 is another BIOS: its tables only fit 14.318 MHz |
| DAC, CRTC after the mode set | as 86Box | | identical lines |
| 16-bit palette lookup | each component's own value (red and blue entries 0-31, green 0-63), the BIOS's layout | patch 0003 indexed by the top bits | **0003 was wrong:** DOS-GL's and Glide's ramps, written for it, came out at an eighth of the brightness on this card. 0003 is dropped and `dac_set_ramp` writes the BIOS's layout |
| Engine | sync, fill and depth fill pass | the same | |
| TEXWIDTH/TEXHEIGHT `rfw` | used: `8 - log2(size)` gives the right texels with the setup's prescaled s and q; the specification's `8 - log2 - k` displaced prescaled triangles in perspective (DOS-GL E9, 2026-10-27) | ignored; a value that fits neither encoding reported (`rfw`, 0124) | the HAL now writes `8 - log2(size)` |
| Clip after WARP triangles | a new CXBNDRY/YTOP/YBOT reaches triangles the WARPs are still setting up (the second of a pair was cut) | in order; reported (`warp-inflight`, 0124) | the HAL puts the DRM's barrier (two zero-length draws) first; see `docs/g400-warp.md` |
| TLUT load and TW8 | every sample black (DOS-GL G4EXP E5 under `DGL_TLUT=1`) | loads nothing (0125; it loaded as on the G200 before) and reports `tlut` | the HAL expands paletted textures on the G400 family |
| AGP fetches | don't snoop the CPU's caches: without a flush WARPTRI drew nothing | read memory as it is; reported from write-back memory (`agp-cached`, 0124) | the HAL's AGP rings are write-combining |
| WARP microcode loads | the third load since a soft reset leaves the WARPs dead | modelled by 0122 | the HAL resets before each load |

DOS-GL's conformance suite passes 23 of 27 here, against 27 of 27 in 86Box. The four differences are the texture
cache and texel-centre filtering (`docs/g400-dual-texture.md` §7, answers 7 and 8). They are modelling work for
patch 0009, as is a texture cache, which 86Box has none of. FPCHECK passes on the Pentium II (72003 cases).

## Checks (local patch 0124)

Where a real G400 went wrong and the model draws right, and what the chip does is not known well enough to model,
the model reports the hazard instead: an `MGA-HAZARD: KIND: #N: ...` line on stderr and in the 86Box log, each
kind's first four, then its 10th, 100th and so on. `tools/loopa/run.py` collects them into `result.json`'s
`mga_hazards`, and with `--hazards fail` (or `LOOPA_HAZARDS=fail`) a job that would pass gets status `HAZARD`.
Only the G400 and G450 are checked: the findings are a G400's.

| Kind | Reported when | On bench-g400 |
|---|---|---|
| `warp-inflight` | with WARP triangles in flight (set up since the last DWGSYNC write, host-started draw or soft reset), the host changes the clip (CXBNDRY, CXLEFT, CXRIGHT, YTOP, YBOT; rewriting the value it last wrote is no change), or starts a draw whose DWGCTL, DSTORG, PITCH or MACCESS differs from the last WARP triangle's. State written between vertex runs for the next triangles is not reported: Linux's DRM does it all the time. | a clip cut the second WARP's triangle; the clip barrier's untextured zero-length draws and lightmap copies spoiled GLQuake's textured triangles; GLOS's HAL drew onto its own target after a program's WARP triangles |
| `warp-opmode` | OPMODE into blit mode with WARP triangles in flight | GLQuake's lightmaps by ILOAD wrong in rows, then triangles across the screen |
| `rfw` | TEXWIDTH or TEXHEIGHT with an rfw that fits neither the CPU setup's encoding (tw = log2, rfw = 8 - log2) nor Mesa's for the WARP (tw = log2 + 11, rfw = 2 - log2) | the specification's 8 - log2 - k displaced prescaled texels in perspective |
| `agp-cached` | PRIMEND or SECEND starts an AGP fetch (bit 1) from memory the CPU's MTRRs and CR0.CD make write-back (the page tables' PCD and PWT aren't looked at) | without a cache flush, WARPTRI by AGP drew nothing |
| `tlut` | a TLUT load (0125 loads nothing on the G400 family) | every TW8 sample black |

The in-flight rule follows the HAL's (`mga_warp_pending`, cleared by `engine_sync`'s DWGSYNC fence) and the DRM's
habits, and it is a lower bound: on the card, the clip barrier's untextured draws spoiled triangles even after two
DWGSYNC writes (`docs/g400-warp.md`, copies among WARP triangles), which the check doesn't see. `WARPTRI hazards` commits each hazard once beside its
safe form; `make loopa-selftest-ext CHECKS=hazards` checks the counts.

## Still unchecked

- On a real G450: the OPTION values, the pixel PLL, the clip quirk (`g400_clip_quirk` in the HAL caps is not yet
  used), and the PCI card's PLX bridge (the HAL now scans every PCI bus, so a card behind a bridge is found).
- The G400's 22-bit AR fields (`mgarig edgecal` runs on Linux only; a DOS build would cover the bench).
