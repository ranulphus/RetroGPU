# The G200's WARP setup engine

The G200 has a WARP setup engine, as the G400 does (`docs/g400-warp.md`). It has one WARP processor where the
G400 has two. On the G400 the WARP path made DOS-GL's DOSBench scenes three to five times faster on a
Pentium II 400. The G200 is the minimum card of DOS-GL's games: Fifth Wheel, and Echoes of the Void, whose PRD
(D32) asks for this path before its EV2. The owner decided on 2026-11-01 to bring it forward (PRD D2, amended).

The real card passed on 2026-11-03 (G6 below), and the G200's WARP is on by default since (G7): `warp_g200` is 1,
and DOS-GL's `DGL_WARP=0` and Glide's `warp=0` turn it off as on the G400. On the G200 the DMA goes by PCI only:
its AGP drew nothing on the real card, and AGP gains the G400 about 4%.

## Sources

| Source | What it gives |
|---|---|
| linux-firmware `matrox/g200_warp.fw` (MIT, LICENSES/LICENSE.mga; sha256 `6f97edeb…829c8d`) | the microcode: `hal/src/warp_g200.h`, made by `tools/warpfw.py` |
| Linux's mga DRM driver, v6.2 (`mga_warp.c`, `mga_state.c`, `mga_dma.c`; MIT) | the G200's init and per-pipe sequences, its texture registers, the AGP 2x PLL |
| Mesa 7.11's mga DRI driver (`mga_texstate.c`, `mgavb.c`, `t_dd_vbtmp.h`) | the vertex (as the G400's), tw = log2 + 28 |
| MGA-G200 specification, Nov 1998 (bitsavers) | the registers that exist, the instruction memory, the DMA channels, AGP |

The research, with citations, was done on 2026-11-01. Its conclusions are below.

## The G200 against the G400

| | G200 | G400 |
|---|---|---|
| WARPs | 1 (STATUS.wbusy <18> only) | 2, taking alternate triangles |
| Microcode | 8 pipes, 1,208-1,504 bytes: TGZ ... TGZSAF (F 1, A 2, S 4); **no two-texture pipes** | 16 pipes, 824-1,256 bytes |
| Binary compatibility | none: 2 of 248 distinct 64-bit words in common | |
| Instruction memory | one, 2 KB: WIMEMADDR, WIMEMDATA 2000h-207Fh (WMISC.wmaster 0 to write it) | two: WIMEMDATA and WIMEMDATA1 |
| Start, suspend | WIADDR (1DC0h); WIADDRNB (1E60h) without the FIFO | WIADDR2 (1DD8h); WIADDRNB2 |
| At start (the DRM's values) | WGETMSB 1606h, WVRTXSZ 7, WFLAG 0, WR24 0, WR25 100h, WR34 0, WR42 FFFFh, WR60 FFFFh; three DMAPADs; WIADDR = 0 \| START | WGETMSB 0E00h, WVRTXSZ 1807h, WACCEPTSEQ, WFLAG/WFLAG1 0, WR56 12800.0f, WR49/53/57/61 0 |
| Reserved on the G200 | WACCEPTSEQ (no sequencer: no strips or fans), WIADDR2, WFLAG1, WIMEMDATA1, the second WARP's NB registers | |
| Vertex | 8 dwords, Mesa's: x, y with (-0.5, -0.375), z 0..1, 1/w, BGRA, specular BGR + fog, u0, v0 | the same, or 10 with u1, v1 |
| Texture sizes | TEXWIDTH/TEXHEIGHT with tw = log2 + 28 (mod 64), rfw = 2 - log2; the same values in WR24/WR34 | tw = log2 + 11; WR54/WR62 with \| 40h |
| Texel convention | samples at texel centres itself, as the G400 (the real card, G6; the specification and 86Box's model had texel corners, and the HAL shifted u and v by half a texel) | samples at texel centres itself |
| DMA | as the G400's: a primary list, vertices by the secondary channel in vertex mode, PRIMEND extended with no-op padding | |
| AGP | sideband addressing required (AGP_CMD.sba_enable); 2x needs AGP_PLL (1E4Ch) bit 0; the G200-PCI has none. On the real card nothing drew by AGP, 1x or 2x: the HAL keeps the G200 on PCI | sideband optional; about 4% over PCI |

DOS-GL and Glide select pipe 7 (TGZSAF) on both chips, as Mesa did, so the G200 loads it once, and again only
after a soft reset.

## In the HAL (`hal/src/warp.c`)

The G400's code carries over except where the table differs: `pipe_code` reads the chip's own microcode; the load
writes WIMEMDATA only; `g200_start` writes the G200's values and starts through WIADDR; `started` waits for wbusy;
`warp_tex` and `warp_tex_sizes` write the G200's encoding into WR24/WR34; `warp_reserve_seq` returns NULL (callers
send triangles). `dma_setup` keeps the G200's rings cached and fetched by PCI unless `warp_agp_rate` asks for AGP
(an experiment: `agp_enable` refuses AGP without sideband, writes AGP_PLL after the modes, as the DRM does, and
waits 10 ms when it changes). The G200's `texel_centre` is set (`chip.c`). The soft reset before each load stays:
the G200 starts as well with it as without.

## In 86Box (local patch 0126)

Patch 0122's model, extended: the G200's pipes recognised by length and CRC-32; one WARP started through WIADDR;
the reserved G400 registers ignored; wbusy alone; lists only; the triangles set up with the HAL's G200 capabilities
(18-bit AR fields, one map). The G400's third-load failure is not modelled on the G200. Since G6 the G200 filters
at texel centres in 86Box too (patch 0120), and DOS-GL's conformance passes 32 of 32 both ways in Loop A again.

WARPTRI passes in Loop A on the emulated G200 (2026-11-01), by DMA and by the CPU: pipe 0 loads and reads back,
and Gouraud and depth triangles through the WARP match `setup_triangle` pixel for pixel. The G400's WARPTRI and
`seq` still pass with the patch.

## In emulation (2026-11-02, before the card)

- **DOS-GL's conformance** on the emulated G200 passes 32 of 32 both ways. With `DGL_WARP=1`, 31 tests draw through
  the WARP (the other draws no triangles). Against the CPU's setup, 16 frames are identical. In the other 17, almost
  every difference is within 8-17 levels: dithering and colour rounding, as on the emulated G400 (the same tests, the
  same pixel counts). The largest is 41 levels, on 82 pixels of t25's combine grid. No texel moved, so tw = log2 + 28
  and the half-texel shift agreed with the CPU's path (the shift was wrong for the silicon: below).
- **A DOS-GL bug the WARP brought out (t19):** loading a TW8 texture's palette into the LUT may sync, which ended the
  draw's busy mark on the texture, so a `glTexSubImage2D` after the draw wrote in place under triangles still queued.
  The G400 never loads the LUT. DOS-GL now loads the LUT before it marks the texture. The CPU's path had the same race,
  less often; t19 is now byte-identical both ways.
- **Glide** (`warp=2`) on the emulated G200: GTA's trace drew 39,253 triangles through the WARP and Screamer Rally's
  3,320, with no failures. Against `warp=0`, 2 frames are identical and 4 differ on 10-164 pixels, where the WARP's
  edges meet the CPU's Voodoo-exact edges.
- **The session** (G6) is `tools/bench/g200-session.sh`.

## On the real card (G6, 2026-11-03)

A Matrox G200 AGP (8 MB, revision 3) in bench-g400's PC (IBM 300PL, Pentium II 400, 440BX), under GLOS over SSH.
The results are under `out/g200-session/bench-g400/`.

- **The start:** a pipe loaded by PIO starts (STATUS.wbusy after the start; WIADDRNB reads 40h for pipe 0, 30h for
  pipe 7: the WARP waits for vertices). There is no third-load failure: 30 starts, reloading pipes 0 and 7 in turn,
  all draw, with the soft reset before each load and without it.
- **WARPTRI by PCI** passes everything, by DMA and by the CPU: Gouraud, two triangles in a batch, depth, pipes 0 and
  7. The cycles a triangle (small ones, the engine's wait included): 839 by DMA (pipe 0; 799 over 20,000; 1,006 for
  pipe 7), 6,105 with the vertices written by the CPU, against 3,120 for the CPU's own setup.
- **The texel convention:** DOS-GL's conformance failed 5 tests (t07, t20, t25, t28, t30) with the WARP and with the
  CPU's setup alike, on bilinearly filtered textures moved by half a texel. Without the shift it passes 32 of 32 both
  ways: the G200 samples at texel centres, as the G400 does. The HAL, DOS-GL's WARP path and 86Box are corrected, and
  tw = log2 + 28 holds.
- **Against the CPU's setup** the WARP's frames differ more than in emulation (86Box sets the WARP's triangles up with
  the HAL's own code): edge pixels, and rows where a coordinate falls on a texel's boundary. Both pass the
  conformance; GTA's frames differ on 76,000 pixels, 99.8% of them by less than 16 levels.
- **AGP:** the HAL turned AGP on by itself (`warp_use_agp`), at 2x with AGP_PLL, under the first WARPTRI runs: two
  batches drew, the third hung, and from then on nothing drew by AGP, 1x or 2x, with AGP_PLL written after the modes
  and given 10 ms, while PCI went on working. A cold start was not tried. The G400 gains about 4% by AGP; the G200
  keeps PCI.
- **The bench:** bench-g400's DOS answers `SET X=a=b` with "Syntax error" and sets nothing, so Glide's options
  (`MGAGLIDE=warp=2 agp=0`) never reached the first Glide runs. `tools/bench/run.py` writes `--set MGAGLIDE=...`
  into `MGAGLIDE.CFG` now.

DOSBench under GLOS, frames per second (by PCI; the CPU's setup against the WARP):

| Scene | Triangles | OpenGL, CPU | OpenGL, WARP | | Glide, CPU | Glide, WARP | |
|---|---|---|---|---|---|---|---|
| G1ARNA | 10,865 | 6.2 | 14.6 | 2.3x | 3.9 | 9.2 | 2.4x |
| G2RACE | 6,993 | 7.7 | 15.2 | 2.0x | 4.4 | 10.9 | 2.5x |
| G3SPAC | 12,235 | 5.7 | 16.3 | 2.8x | 2.9 | 8.5 | 2.9x |
| G4CITY | 9,229 | 4.9 | 10.3 | 2.1x | 3.0 | 6.8 | 2.2x |
| M1TP16 | 16,184 | 6.0 | 21.9 | 3.7x | 2.2 | 11.6 | 5.3x |
| M2DRGN | 47,794 | 2.5 | 8.9 | 3.5x | 1.5 | 5.5 | 3.7x |
| M3LANT | 5,394 | 19.7 | 98.4 | 5.0x | 10.9 | 34.5 | 3.2x |
| S4T16 | 8,192 | 5.6 | 22.7 | 4.1x | 3.0 | 8.7 | 2.9x |
| L1LQ2P | 1,097 | 20.6 | 21.8 | 1.1x | 16.4 | 21.5 | 1.3x |

The OpenGL scenes drew 50.9 million triangles through the WARP without a failure. Fifth Wheel's tour drew 1.6
million, and its state hashes agree with the CPU's run. Glide's GTA and Screamer Rally traces drew 39,253 and 3,320,
without a failure.

## The plan

| Step | What | Where |
|---|---|---|
| G0 | The research above | done |
| G1 | `warp_g200.h` from linux-firmware; `tools/warpfw.py` (it reproduces `warp_g400.h`'s bytes too) | done |
| G2 | The HAL's G200 path; WARPTRI on the G200 | done |
| G3 | 86Box: patch 0126 | done |
| G4 | DOS-GL: the G200 under `DGL_WARP=1`; the half-texel shift; conformance in Loop A on the emulated G200, the WARP against the CPU's setup | done (below) |
| G5 | Glide `warp=2` on the emulated G200: the acceptance games' traces | done (below) |
| G6 | The real card: one session (below) | done, 2026-11-03 (above) |
| G7 | `warp_g200` on by default (and DOS-GL's and Glide's); the G200 by PCI only; Echoes of the Void's presets re-based | done |

After G7, MGA-Glide's own conformance (`make conform-ci`) failed t04 on the G200: 237 edge pixels against the
Voodoo's frame. The WARP sets triangles up with its own edge rule, and t04's manifest entry already allowed for it
(`"warp": {"edge": true}`), but `glide/tools/conform/run.py` applied that only on the G400 family. It now applies it
wherever the WARP draws: the G200, G400 and G450, unless `MGA_OPTS` has `warp=0` (2026-11-04: 27 of 27 on the
G200; t04 drew 39 triangles through the WARP).

## G6: the session on the real G200

The owner swaps the G200 into bench-g400's PC (an IBM 300PL, Pentium II 400) once. Everything after that runs over
SSH (GLOS agent mode) as one prepared batch, and fixes go round without another swap. The card goes back when the
batch passes. The CPU is 400 MHz; the cycle counts scale to a Pentium II 266.

The batch, in order (each item answers an open question):

1. **WARPTRI**: the PIO load and start (does the G200 start from its instruction memory? Linux only ever used
   cached microcode); `starts=30 reload` with and without `noreset` (the third-load failure); `pio`; the cycles a
   triangle by DMA, by the CPU into the window, and by the CPU's own setup; `agpinfo`, then `agp=1` and `agp=2`
   (sideband, AGP_PLL).
2. **DOS-GL's conformance** (`make conform-bench`) with `DGL_WARP=1` and with `DGL_WARP=0`: textures (tw = log2 +
   28 and the half-texel shift), fog, specular, alpha, depth against the CPU's setup.
3. **DOSBench**: the GL scenes, GLQuake, Quake 2 and Half-Life's timedemos, both ways.
4. **Fifth Wheel**: FW1 and the FWP tests, both ways.
5. **Glide**: the acceptance games' replays with the WARP.
6. **GLOS windows**: a DOS-GL program windowed (the CPU's setup, as on the G400) and full-screen.

If the PIO load fails, the fallback is Linux's way: the microcode in conventional memory, WMISC = 0Bh, WIADDR =
its address | START through the list.

## Open, for the card (answered by G6)

Answered: a PIO-loaded pipe starts; the soft reset is not needed but harmless (kept); no third-load failure;
tw = log2 + 28 is right and the half-texel shift was wrong (texel centres); the DRM's start values work at 16-bit
and 32-bit depth; DOS-GL's G400 rules around WARP triangles serve the G200 unchanged. Not measured: the WARP's
clock (OPTION2), AGP from a cold start, and batch item 6 (DOS-GL in a GLOS window sets triangles up on the CPU, on
any chip). The questions as they were:

- Does a PIO-loaded pipe start (wbusy after the start), and does a soft reset keep or clear the instruction memory?
- Does the third-load failure exist on the G200?
- Are tw = log2 + 28 and the half-texel shift right on textured triangles?
- What are WR25, WR42 and WR60 (100h, FFFFh, FFFFh)? Do they depend on the depth's width?
- Does the G200 need the G400's clip barrier, DWGSYNC fences, or OPMODE rule around WARP triangles?
- What WARP clock does the BIOS leave (OPTION2's nowclkdiv and wclkdiv), and how fast is the WARP by PCI and by AGP?
