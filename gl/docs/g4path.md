# The G400 path rework: plan (2026-11-16)

## Why

The owner ran the same PC (bench-g400: PII-400, G400) under Windows 98 with Matrox's own OpenGL driver
(ICD dated 2000-02-21, GLQuake 0.97, `timedemo demo1`, 16-bit colour, vsync off):

| | Matrox (Win98) | DOS-GL (main 0f59088, GLOS) | Gap |
|---|---|---|---|
| GLQuake 640x480, two-pass | 93.6 | 45.6 | 2.1x |
| GLQuake 640x480, multitexture | 97.9 | 43.3 (`-mtex`) | 2.3x |
| GLQuake 320x200, two-pass | 116.7 | 55.1 (60.5 with `DGL_PRESENT_LAG`) | 1.9-2.1x |
| GLQuake 320x200, multitexture | 123 | 47.7 | 2.6x |
| GLQuake 640x480x32 / 800x600 / 1024x768 | 94.6 / 83.9 / 61.5 | - | |
| Quake II 3.20 demo1 640x480 / 320x240 | 65.3 / 67.2 | 31.9 / 36.1 | 2.0 / 1.9x |
| Quake II 800x600 / 1024x768 | 64.4 / 56.1 | - | |

Matrox's numbers barely move from 320x200 to 800x600, so neither the card's fill rate nor this CPU stops
it near 100 fps. DOS-GL gets half that.

## What the bench showed (2026-11-16, branch g4path)

Waits for the card by cause (`DGL-PWAIT`, a PROF=2 build), then three switches:

1. **Nearly all waiting is the swap's drain.** At 640x480, the swap waits about 5 ms a frame for the card
   to finish. Waits for ring room, kick slots or FIFO slots are negligible. The kick size and ring size don't
   help either: 1.5 KB kicks change nothing, 24 KB and 96 KB kicks are slower (42.5 and 33.1 fps), and a 128 KB
   ring is slower (43.7).
2. **Not draining doesn't help.** With `DGL_NODRAIN=1` (an experiment on branch g4path, since removed: the swap kicks and goes on, so frames
   may show unfinished), 640x480 gains only 2% (46.4 against 45.3-45.8), Quake II gains 1.5%, and 320x200 with
   the present lag loses 3%. The wait moves to the next frame's first lightmap update. Its total stays at about
   a third of the frame.
3. **The card itself is the limit.** With kicks held back until the swap (`DGL_KICK=250000`), the swap's drain
   is the card working alone. That takes about 17.7 ms for a 640x480 frame and about 9.8 ms for a 320x200 frame.
   Matrox's whole 640x480 frame, CPU included, takes 10.2 ms; its 320x200 frame takes 8.1 ms. With a fifth of
   the pixels, our 320x200 frame still costs the card about 10 ms. So **about 8 ms a frame is card cost per draw
   or per batch**, not per pixel: around 4 µs per triangle for about 2,200 triangles in 730 draws.
4. **Multitexture adds about 5 full waits a frame.** A lightmap changed while queued draws still read it is
   moved to a new block (`sub_move`, 30-60 a frame). The move's engine copies are written to the card directly,
   not through the DMA list, so each one first sends the list and waits for the card to empty it
   (`fifo_reserve` → `mga_dma_flush`). This is why `-mtex` is slower for us and 5% faster for Matrox.
5. **The CPU side.** At 320x200, our CPU time is about 15 ms a frame (setup, clipping, projection, the WARP
   vertex writes, state). Matrox's whole frame there is 8.1 ms. Once the card stops being the limit, the CPU
   becomes it: the F-series results in fe.md say where the cycles go.

**The suspect for point 3.** Every register write between draws ends the open vertex run (`close_run`):
- each run is a separate secondary DMA (`SECADDRESS`/`SECEND`);
- the card fetches each run from system memory by PCI or AGP and restarts the WARP;
- GLQuake two-pass makes about 730 draws a frame, and nearly every texture or state change ends a run.

If each run costs the card a few microseconds of fetch latency and pipeline refill, that is the 8 ms. This is
not yet measured: M0 measures it.

## Targets

- **GLQuake 640x480 demo1:** 70 fps two-pass and 75 multitexture (now 45.6 and 43.3); Matrox gets 93.6 and
  97.9.
- **320x200:** 80 fps (now 55); Matrox gets 117-123.
- **Quake II 640x480:** 45 fps (now 32); its own game code costs more under DJGPP (fe.md), so its gap is
  partly outside DOS-GL.
- **No change in output:** frames identical on the bench (snapshots) and in 86Box (REGCRC streams may change,
  pixels may not), and conformance unchanged on g200, g400 and g450.

The targets are about three quarters of Matrox's speed. Closing the rest would need the CPU side rebuilt,
which is not in this plan.

## Steps

### M0. Measure the card's costs (bench, about one hour; no product change)
- **Counters (PROF builds):**
  - vertex runs per frame;
  - register writes per frame;
  - dwords per run.
- **A card-cost probe** (`G4COST.EXE`, built from the HAL and DOS-GL's emitter):
  - Streams fixed in advance, each played with the CPU waiting until it has queued the whole stream, then
    timed from kick to idle.
  - The streams sweep:
    - triangles per run: 1, 2, 8, 64;
    - register writes between runs: 0, 2, 8, 20;
    - which registers they are: texture, ALPHACTRL, DWGCTL, fog;
    - the fetch path: AGP 1x/2x, PCI, cached rings and write-combining rings;
    - triangle size: 4, 64 and 1,024 pixels.
  - The 86Box G400 is only a functional check.
- **Output:** a cost model,
  card_us = a·runs + b·reg_writes + c(register kind) + d·triangles + e·pixels,
  checked by predicting GLQuake's frame (from M0's counters) within 15%.
- **The decision rule:** M1 and M2 are done in the order of the time the model says they free. If the model
  doesn't point at runs or register writes, I stop and come back to you before M1.

### M1. Fewer, longer vertex runs (the main lever, if M0 confirms)
Which of these are done depends on what M0 finds expensive:
- **Merge runs across state that doesn't need a new run.**
  - Some register writes can go into the vertex DMA stream itself: the WARP's per-run header, or the DRM's
    "state then vertices" grouping.
  - Others can be grouped: where state flips back and forth, as GLQuake's surfaces do, the change is written
    once per group instead of once per draw.
- **Sort within a frame where GL allows it (none for now).** GL's draw order is fixed. Batching consecutive
  draws with the same state into one run is already done; this widens what counts as "same" to state the card
  doesn't read per run, such as a texture register unchanged in value.
- **Texture state as one block.** Rewriting the texture registers costs a pipeline flush on the card (G4EXP E6:
  the texel cache). If M0 shows that this, not the run, is the cost, then:
  - texture binds are batched;
  - DOS-GL keeps lightmap pages and their surface textures in fewer, larger textures (atlas-style, in VRAM
    only, invisible to GL) where that fits the program's texture coordinates. Only if needed: this is the
    biggest change in the plan.

### M2. Lightmap updates without waiting (multitexture's 5 waits a frame)
- `sub_move`'s engine copies (`engine_vram_move`/`engine_vram_copy`) go into the DMA list as register writes,
  as the drawing does, so they never send the list and wait.
- If the list route needs the ILOAD fix's care (an ILOAD from the list once broke the WARP's DMA on silicon:
  `afe0629`), these are BITBLTs, not ILOADs. The conformance tests t18, t31 and t32 (stale texels) and G4EXP's
  texture-cache cases guard it.
- **Expected:** `-mtex` at least as fast as two-pass, as with Matrox.

### M3. Overlap the CPU and the card (once the card is faster than the CPU)
Only worth doing after M1 (point 2 above).
- The swap queues the present behind a fence and goes on, as the opt-in present lag does today, but for every
  mode and by default.
- **640x480:** three display buffers, so the next frame never draws into the one on screen. VRAM is ample:
  3 x 600 KB at 16 bits.
- The flip comes at the first GL call after the fence passes (`flip_poll`), or at the next swap at the latest.
- **Latency:** never more than one frame queued, which bounds the added latency. Vsync keeps the current
  synced swap. Your concern about latency at low frame rates holds: the gain is throughput.

### M4. CPU work (only where M0-M3 leave it the limit)
Use the fe.md profile to pick from:
- short-integer positions on the fast path;
- cheaper clipping for triangles inside the guard band;
- the per-vertex WARP format work.
Each is measured on its own, as in F1-F3.

## How each step is tested
- **Host:** `make tests-host`, `conform-host`.
- **86Box:**
  - conformance on g200, g400 and g450;
  - REGCRC frame comparisons against main: pixels identical, streams may differ;
  - `loopa-quake` on g400.
- **bench-g400:**
  - GLQuake 640x480 two-pass and `-mtex`, 320x200, and Quake II 640x480;
  - two runs each against main's build;
  - snapshots compared byte for byte (`-fixedtime`);
  - the conformance suite on silicon (`conform-bench`).
- **The merge tier** on devserver (`tools/regress-remote.sh`), then your OK to merge.
- **G200 path:** unchanged unless a step says otherwise. The G200 has one WARP with its own setup
  (docs/g200-warp.md); M1's changes apply there only if M0's probe, run on the real G200eR2 in an agreed devserver
  window, says they help.

## Risks
| Risk | Mitigation |
|---|---|
| The per-run suspect is wrong | M0 measures before anything changes; its model must predict GLQuake's frame |
| Silicon quirks in the DMA list (DWGSYNC, ILOAD) | M2 uses BITBLTs; every step is tested on the bench with snapshots and conformance |
| Stale texels from reordered texture writes | t18, t31 and t32 and G4EXP's cache cases at every step |
| The rare G400 corruption (~1 in 105 runs) gets mixed up with new faults | Snapshot checks on every bench run; any corrupt frame is compared against a main build on the same day |
| Bench time shared with Echoes and GLOS | Sessions of about an hour, arranged with them first |

## M0 results (2026-11-16, bench-g400): the plan's suspect was wrong

- **Vertex runs cost the card nothing.** G4COST (tests/hal/g4cost.c, kicks held, kick-to-idle timed) gives the
  same time per triangle whether each run is ended by a DMAPAD, an ALPHACTRL write, a texture switch
  (`tex_emit_change`) or the whole texture state, or the triangles go in one run. For example: 1.50 µs against
  1.53 µs at 8 pixels, 4.07 µs against 4.06 µs at 64 pixels. Fans by the sequencer cost the same as the same
  triangles as lists at 64 pixels (2.81 µs in both cases), and 0.4 µs more per fan at 8 pixels. M1 as planned
  would gain nothing.
- **GLQuake's stream** (Loop A counts, `DGL-PRUN`): about 230 runs a frame of about 7 triangles each, nearly
  every run after a WACCEPTSEQ write (a GL_POLYGON as a fan).
- **The card's time for GLQuake's frame hardly moves** with the stream's content: 17.7/18.0 normal, 17.2 with
  GL_NEAREST, 17.1 with GL_NEAREST_MIPMAP_NEAREST, 18.0 trilinear, 16.8 with `gl_picmip 2`, 17.6 without
  entities, 16.4 with `DGL_SEQ=0` (ms per frame, kicks held).
- **The clocks** (CLKINFO, tests/hal/clkinfo.c):
  - The card runs on the first record of its BIOS's PInS v4 block: OPTION3 `0190A419`, which is also Linux's
    `default_pins4`. Records 2 and 3 hold `019B8419`, a different memory-clock field.
  - System PLL: M=09, N=3C, P=10, so 164.7 MHz from 27 MHz. The PInS's `pins[65]` is 0x7E.
  - Whole-screen fills reach 641 Mpix/s (1.28 GB/s), which is 16 bytes a clock at about 82 MHz.
  - Matrox's frames cost the card about a quarter as much per pixel as ours (from the 640-to-320 difference),
    which fits slower clocks under DOS.
- **Next:** the owner runs CLKINFO in a Windows 98 DOS box with Matrox's driver loaded. That reads the clocks
  Matrox's driver sets, read-only. A clock step for DOS-GL or GLOS (the PInS's values, set once at start and put
  back at exit) gets planned from what it shows.

## What Matrox's driver sets (the owner, Windows 98, CLKINFO in a DOS box, 2026-11-16)

| | DOS (the BIOS) | Windows 98 (Matrox's driver) |
|---|---|---|
| System PLL | M=09 N=3C P=10: 164.7 MHz | M=02 N=1B P=18: 252.0 MHz |
| OPTION3 | `0190A419` | `019B8419` |
| OPTION | `50040120` (rfhcnt 8) | `50538120` (rfhcnt 27h, bit 22 set) |

The PInS v4 block holds three 10-byte clock records at bytes 55, 65 and 75. Each is
{system PLL in 2 MHz units, the same in 4 MHz units, OPTION3, MCTLWTST}:
- record 1: {52h, 29h, `0190A419`, `24045491`}, that is 164 MHz: the BIOS's;
- records 2 and 3: {7Eh, 3Fh, `019B8419`, `24045491`}, that is 252 MHz: Matrox's driver's.

Linux's `parse_pins4` reads `pins[65] * 4000` (504 MHz) and the OPTION3 at 67, so it would take record 2's
OPTION3 and twice its clock. It only programs the clocks of a card the BIOS didn't start, so it never does on
a primary card.

## C: the clock step (replaces M1; for approval)

- **C0. Measure (bench-g400, one run, the owner nearby).**
  - `CLKSET` (a HAL test program) sets record 2, the way Linux's `g450_mclk_init` does:
    1. switch the clocks to the PCI clock (OPTION3 sources cleared, under `sysclkdis`);
    2. program the system PLL and wait for its lock bit;
    3. write record 2's OPTION3;
    4. write OPTION's refresh count and bit 22 as Windows has them.
  - Then it repeats CLKINFO's fill test and G4COST's main cases, puts record 1 back, and checks it reads back.
  - It also checks whether a VBE mode set puts the BIOS's clocks back. If it does, the step must come after every
    mode set.
- **C1. The HAL does it.**
  - At engine start, when the card's PInS has a faster record than the clocks running, the HAL takes Matrox's
    record 2 (`engine_clocks(1)`).
  - It puts the BIOS's clocks back at exit, from the same exit paths as the engine reset and the text mode.
  - `DGL_CLOCKS=0` / `MGAGLIDE` settings turn it off.
  - G400 only at first. The G450 has PInS v5 and is checked on bench P4 the same way (Windows values first). The
    G200 stays as it is.
  - Every DOS-GL and Glide program and GLOS's desktop gets it. GLOS is told first, since under GLOS the program
    writes PCI configuration.
- **C2. Re-measure.**
  - GLQuake and Quake II at 640x480 and 320x200 against main, with snapshots and conformance on silicon, as before.
  - The card-alone time (kicks held) shows what is left.
  - Then M2 (lightmap moves), M3 (overlap) and M4 (CPU) are re-ranked with the new numbers. M1 is dropped.
- **Expected:** the card's frame time falls from about 17.7 ms toward 9-11 ms at 640x480. The CPU (about 17 ms a
  frame) is then the limit, so 640x480 rises to about 55 fps at once and toward 60 with M3's overlap. The 70 fps
  target needs M4.
- **Risk:** a wrong sequence can stop the memory clock and hang the PC until it's power-cycled (Linux warns about
  this). So C0 runs once with the owner at hand, uses only the card's own BIOS values that Windows uses, and
  follows Linux's order step by step.

## C0 results (2026-11-16, bench-g400, the owner at the PC)

CLKSET (tests/hal/clkset.c):

| Step | Clocks | Fill | Read-back check |
|---|---|---|---|
| A | BIOS: 164.7 MHz, `0190A419`, OPTION `50040120` | 646 Mpix/s | 0 bad |
| B | record 2: 252.0 MHz, `019B8419` | 979 Mpix/s | 0 bad |
| C | plus OPTION `50538120` | 984 Mpix/s | 0 bad |
| D | after a VBE mode set | still B/C: the BIOS doesn't put its clocks back | |
| E | BIOS values back | 646 Mpix/s | 0 bad; registers read back exactly |

GLQuake demo1 and Quake II demo1 on main's build (0f59088), with the clocks held at C (`CLKSET hold`, then
`CLKSET restore`):

| | BIOS clocks | Matrox's clocks |
|---|---|---|
| GLQuake 640x480 two-pass | 45.6 (43.8 just after) | 56.3 / 56.5 |
| GLQuake 640x480 `-mtex` | 43.3 | 47.9 |
| GLQuake 320x200 | 55.1 | 61.0 |
| GLQuake 320x200 with `DGL_PRESENT_LAG` | 60.5 | 65.6 |
| Quake II 640x480 / 320x240 | 31.9 / 36.1 | 36.1 / 38.2 |
| Card alone, GLQuake 640x480 frame (kicks held) | 17.7 ms | 12.95 ms |

Snapshots (7 frames, `-fixedtime`) are byte-identical at both clocks.

**Where that leaves the gap.**
- At 640x480, the card's 13 ms and the CPU's ~17 ms per frame are now close. So M3's overlap pays: the frame
  could fall toward max(CPU, card) ≈ 17 ms, about 58 fps.
- The rest is the CPU (M4). The card is also still slower than Matrox's whole 10.2 ms frame, so some card
  inefficiency remains (M2's lightmap waits for `-mtex`, and the per-pixel cost left).

## C1 (2026-11-16): the HAL does it

- **The code.** `hal/src/clocks.c` holds `mga_clocks_fast(mga_clocks *saved)` and `mga_clocks_restore(saved)`:
  - no engine context;
  - only bench-g400's PInS records and running clocks are acted on;
  - `mga_clocks_why` gives the reason.
- **Under GLOS** (the owner's choice, 2026-11-16) GLOS calls them for its whole run, with a `[display]` setting to
  keep the BIOS's clocks. That is GLOS's change.
- **The Matrox device** (`rg/mga.c`) calls them at the full-screen open and at close only without GLOS.
  `DGL_CLOCKS=0`: never. `DGL_CLOCKS=2`: under GLOS too (the test hook).
- **bench-g400, the device's path (`DGL_CLOCKS=2`):**

  | | fps |
  |---|---|
  | GLQuake 640x480 two-pass | 56.7 (twice) |
  | GLQuake 640x480 `-mtex` | 47.8 |
  | GLQuake 320x200 | 61.5 |
  | Quake II 640x480 | 36.2 |
  | GLQuake 640x480 under GLOS by default (left alone) | 43.9 |

  - CLKINFO after each program read the BIOS's clocks back.
  - Snapshots matched the BIOS-clock set byte for byte.
  - `conform-bench` with `DGL_CLOCKS=2` passed 35 of 35 (t33 and t34 skipped, as always on the bench).
- **Not yet covered:**
  - the G450 (PInS v5; Windows's values to be read on bench P4 the same way);
  - the Glide runtime (it opens the screen through its own path, not the device);
  - whether a warm reboot's POST puts record 1 back.

## The CPU side (2026-11-16): where the remaining gap is

At the fast clocks the gap to Matrox's driver is a fixed amount of time per frame, about the same at both sizes,
so it is not pixel work:

| | DOS-GL | Matrox | DOS-GL's extra time |
|---|---|---|---|
| 640x480 | 17.6 ms | 10.7 ms | 6.9 ms |
| 320x200 | 16.3 ms | 8.6 ms | 7.7 ms |

- **Not GLOS.** The owner ran the same build under plain MS-DOS (`C:\GAMES\DGL\PLAIN\PLAIN.BAT`):

  | | Plain DOS | GLOS, write-combining and AGP off | GLOS, normal |
  |---|---|---|---|
  | 640x480 | 55.5 | 54.7 | 56.7 |
  | 320x200 | 60.9 | 58.7 | 60.9 |
  | `-mtex` | 47.1 | 45.8 | 47.8 |
  | BIOS clocks | 42.5 | 42.3 | |

  GLOS costs 1.5–3.5% against plain DOS under the same conditions. The HAL's clock switch also worked under
  plain DOS, with the clocks put back after each run.
- **Not the game build.** Our software Quake port beats id's `QUAKE.EXE` on this PC (107 against 91 fps at
  320x200).
- **It is DOS-GL's CPU work.** From the PROF=2 profile (profsum.py), per triangle:

  | | Cycles |
  |---|---|
  | GLQuake | 879 |
  | Waits for the card | 1,047 at the BIOS clocks (far less at the fast ones) |
  | DOS-GL | 3,366 |
  | … transform | 654, about 260 a vertex |
  | … projection | 535, about 250 a vertex |
  | … per draw | 415, about 900 per `glBegin`/`glEnd` |
  | … clipping | 365 |

  Matrox's whole frame works out at about 2,400 cycles a triangle, so its driver has about 1,500.
- **The half-second pause** at frame 200 of demo1 is DOSBench's results writer: `-dosbench` saves `L<test>.PPM`
  there (`dbwrite.c`). Its time is left out of the results' frame times and fps.

### E1: GL_POLYGON clipped whole (branch g4cpu)

About 20% of GLQuake's triangles need clipping: floors and walls crossing the near plane. The guard band is
already 2,000 pixels. Before E1 each of them was clipped as its own triangle, after a second full transform.

- **What E1 does.** A GL_POLYGON with a vertex outside goes whole to the new polygon sink (`dgl_poly_general`,
  then `emit_polygon`). It is clipped once, culled on its whole signed area, and drawn as a fan, the way Mesa
  clips polygons.
- **Fans and strips stay triangle by triangle.** Clipping a fan whole moved GLQuake's weapon's texture visibly,
  because its texture coordinates are not planar across the fan.
- `DGL_POLYCLIP=0` turns E1 off.

bench-g400, fast clocks:

| | Before | E1 | Gain |
|---|---|---|---|
| GLQuake 640x480 | 56.5 / 56.7 | 59.5 / 59.5 | +5.0% |
| GLQuake 320x200 | 61.5 | 64.5 | +4.8% |
| GLQuake `-mtex` | 47.9 | 48.8 | +2.0% |
| Quake II 640x480 (fans included, in the first version) | | | +0.9% |

- **Frames.** 2 of 7 snapshots are identical and 1 differs in one pixel. The other four differ in 193 to 1,584
  pixels, by at most 17 per channel, all on floors right in front of the camera. That is rounding of the new
  triangulation and can't be seen.
- **Conformance** passed 35 of 35 on the emulated G400.

### Ablation: a GLQuake frame by layer (`DGL_ABLATE`, diagnosis only; bench-g400, fast clocks, E1)

| Removed | 640x480 frame | 320x200 frame |
|---|---|---|
| nothing | 16.8 ms | 15.5 ms |
| 1: nothing sent to the card | 12.7 ms | 14.0 ms |
| 2: the fast path returns at once | 4.4 ms | 5.8 ms |
| 4: `glEnd` returns at once | 3.1 ms | 4.4 ms |
| 8: `glVertex` and `glEnd` do nothing | 2.8 ms | 4.1 ms |

So per 640x480 frame:

| | ms |
|---|---|
| GLQuake itself, with the swap and the GL calls' overhead | 2.8 |
| Storing the vertices | 0.3 |
| Validation and planning (826 draws) | 1.3 |
| **The fast path: transform, project, clip, assemble** | **8.3** (the same at 320x200: pure CPU) |
| The ring writes and the card | 4.1 |

The fast path works out at about 740 cycles per vertex (4,493 a frame). Matrox's driver does everything in about
8 ms a frame.

### E2a: one colour per draw converted once

Immediate mode's colour plan is marked uniform when every vertex of the draw has the first one's colour (a
16-byte compare each), and the fast path then converts it once per draw. Frames are byte-identical to E1's (7 of
7 snapshots).

| | E1 | E2a |
|---|---|---|
| GLQuake 640x480 | 59.4 / 59.5 | 59.8 / 59.9 (+0.7%) |
| GLQuake 320x200 | 64.5 | 65.5 (+1.7%) |
| `-mtex` | 48.8 | 48.8 |

### E2b (dropped): sequences from 4 vertices

With E1 and E2a, GLQuake on bench-g400 at the fast clocks:

| Sequences (WACCEPTSEQ fans) | 640x480 | 320x200 |
|---|---|---|
| From 5 vertices, as now | 59.7 / 59.6 | 65.0 |
| From 4 | 58.8 / 58.8 | 63.9 |
| From 3 | 58.4 / 58.4 | |
| None (`DGL_SEQ=0`) | 59.8 | |

Sequences for small polygons cost more than they save, and on GLQuake they don't pay at all now. Dropped.

### Where E1-E2b leave it

PROF=2 profile with E1 and E2a (640x480, per triangle): GLQuake 920 cycles, card waits 372, per draw (`batch`)
606, transform 554, projection 554, ring 234, clipping 225, swap 195, validation 158, the general path's
transform 145, triangle 144, texture state 124.

The cost is spread thin over a per-vertex, per-triangle and per-draw structure: function calls, a vertex cache
that immediate mode never hits, volatile stores, and x87 comparisons and branches for each vertex. Single trims
gain 1-5% each.

## V: the vertex path per draw (plan, for approval)

**Why.** After E1 and E2a, the fast path costs about 8 ms of a 16.7 ms GLQuake frame at 640x480 (ablation). That
is about 700 cycles per vertex all told. The cost is in the structure, not in any one instruction (F3 gained
nothing from Pentium II instructions):
- every vertex goes through a function call and a cache lookup, though immediate mode never shares a vertex;
- values are forced through memory (volatile) and tested by x87 comparisons and branches;
- every triangle reserves ring room separately;
- every clipped polygon is transformed a second time.

Matrox's driver does the whole frame, card waits included, in about 8 ms.

**The idea.** A draw whose vertices come in order (immediate mode, `glDrawArrays`), which is most of GLQuake and
all of Echoes' sky and HUD, is handled in loops over the whole draw, not vertex by vertex. Indexed draws
(`glDrawElements`, Echoes' terrain) keep the cached path, with V3's cheaper clipping. Every step keeps the
arithmetic as it is, so the frames are byte-identical.

**Steps:**
- **V0. The floor** (bench, about 15 minutes). A tight loop that transforms and projects 4,500 vertices the way
  the fast path does, timed on the Pentium II. That gives the cycles per vertex this CPU can reach, and sets V1's
  target before any rework.
- **V1. The batch vertex stage.** One loop transforms the draw's vertices into a local array of clip
  coordinates and clip codes; the clip codes come from the coordinates' sign bits, stored once. If no vertex is
  outside (the common case), a second loop projects them all straight into WARP form, with the texture
  coordinates' whole periods taken once per draw. Otherwise the draw goes to the polygon or triangle path with
  the coordinates already computed (V3).
- **V2. Straight into the ring.** For a draw that is all inside, each triangle is culled by its integer area and
  written into one ring reservation for the whole draw, instead of one per triangle.
- **V3. Clipping without the second transform.** Clipped polygons and triangles are built from the batch's or
  the cache's clip coordinates, plus the arrays' colours and texture coordinates, instead of `dgl_transform`.
  These are the same values: the fast path already stores them as `dgl_transform` does. This is Echoes' biggest
  cost (31% of its triangles are clipped) and about a fifth of GLQuake's.
- **V4. Per draw.** Validation's exit when nothing changed (96% of draws), `plan_imm`, and the dispatch from
  `glEnd` to the fast path.
- **V5. Only if worth it after V0-V4: cheaper arithmetic.** Viewport constants once per viewport, and a
  single-precision reciprocal. Frames would then differ by rounding, so the fast and general paths change
  together, so that vertices they share stay identical.

**Targets** (GLQuake demo1 on bench-g400, fast clocks; now 59.9 / 65.5): 640x480 70 fps, 320x200 80 fps.

**Tests at each step:**
- snapshots byte-identical to E2a's (V1-V4);
- conformance on the three emulated cards and on bench-g400;
- REGCRC streams may change; pixels may not;
- `loopa-quake`, Quake II, Half-Life and Echoes (its own suite, run by its session);
- the merge tier, then your OK.

**Risks:**
- A bug in the batch path draws wrong triangles. The snapshots and conformance catch that, and `DGL_FAST=0` stays
  as a way back.
- The gain could be smaller than hoped. V0 gives the floor before any rework, and if the floor doesn't leave room
  for the targets, I stop and come back to you.

### V0's result (bench-g400): the arithmetic, not the structure

VFLOOR (`gl/tools/perf/vfloor.c`) puts 4,500 GLQuake-like vertices through the fast path's arithmetic in tight
loops on the Pentium II:

| Variant | Cycles per vertex |
|---|---|
| Transform and clip codes only | 141 |
| Transform, clip codes, projection and WARP form, batched | 407 |
| The same one vertex per call through the cache, as `fast_get` does now | 416 |
| The viewport and depth range as doubles computed once | 367-387 |
| Plus no volatile (the transform in registers, stored once) | 360 |
| Plus clip codes from the stored sums' sign bits | 346 |
| Plus the reciprocal under single precision (the control word set once around the loop) | 334 |
| Plus texture coordinates' range tests by exponent bits | **304** |

- **The structure costs about 9 cycles a vertex.** V1-V2's whole-draw loops would gain little, so they are
  dropped.
- **The arithmetic as compiled is the cost.**

**Plan V, revised (the owner, 2026-11-16):**
- **VC.** The 304-cycle formulation goes into both the fast path (`fast_get`) and the general path (`project`),
  so that vertices the two share stay identical. Frames then differ from E2a's by rounding: checked by
  conformance (all cards and the bench) and by snapshot differences.
- **V3.** Clipping without the second transform, as planned.
- **VA.** A hand-scheduled x87 version is tried in VFLOOR first, and ported only if it clearly beats VC.
- **V4** as planned. V1, V2 and V5 are folded into these.

## W: the WARP pipes from memory (2026-11-17, branch ucache)

GLQuake `-mtex` was 16% slower than without it (44.0 against 52.7 fps, bench-g400 at the fast clocks), while
Matrox's ICD is faster with it. Its profile showed why: about four pipe switches a frame, each a full wait, a soft
reset, a load of both WARPs' instruction memories and a start, with every drawing register written again after
the reset (validation 1,262 cycles a triangle against 172 without `-mtex`; 3,687 resets in demo1). The two pipes
(T0 and T2, 1,000 and 1,256 bytes) don't fit in the 2 KB instruction memories together, and the G400's third load
since a reset fails (`docs/g400-warp.md`), so each switch had to be a load after a reset.

Linux's DRM never loads the instruction memories. It turns microcode caching on (WMISC 0Bh, which reads back
03h), puts every pipe's code in memory, 256-byte aligned, and switches by writing the pipe's registers into the
DMA stream, ending in WIADDR2 = the code's address | start (`mga_g400_emit_pipe`). The HAL now does the same
behind `warp_ucode_cache`, DOS-GL's default (`DGL_WARPCACHE=0` loads as before; WARPTRI `ucache`; Glide, with
one pipe, still loads):
- the pipes' code in conventional memory, as the DMA rings are;
- the first start direct, as before, but with WMISC 0Bh and WIADDR2 at the code; WMISC reading back anything but
  03h falls back to loads;
- every later switch in the stream: suspend, the DRM's flush when leaving a T2 pipe, the WARP registers, three pads
  and the start. No load, no soft reset, no wait. DOS-GL's validation writes its registers again after a switch
  (`warp_uc_switches`), as after a reset.

Results on bench-g400 (fast clocks, GLOS):

| | Loaded (now) | From memory |
|---|---|---|
| WARPTRI `switch=200`: cycles a switch | 43,415 (200 resets) | 12,509 (no resets) |
| WARPTRI `starts=30 reload` | (needs the resets) | 30 of 30, no resets |
| GLQuake 640 `-mtex`, two runs each | 43.98, 43.98 fps | 48.53, 46.26 fps |
| GLQuake 640 `-mtex`, snapshots at 7 frames | | identical to loaded |
| GLQuake 640 without `-mtex` | 52.66 | 52.67 |

- **The gain is +5-10%, not the 1% the cost of a switch predicted** (2026-10-28): each switch also emptied the
  card's queue, so the card and the CPU stopped overlapping four times a frame, and every register went again.
- **Conformance** with `DGL_WARPCACHE=1` passes on the emulated G400 and G450 (86Box already runs pipes from
  memory: `warp_start` reads the code by bus mastering when WMISC bit 0 is set).
- The first run after the fix differed in every snapshot (the textures garbled): the switch writes TEXCTL2, DWGCTL
  and the WARPs' registers behind DOS-GL's register cache. Validation now forgets its cache after a switch.

Bench F (G450, Pentium 4 3.06 GHz, PCI), the same builds:

| | Loaded | From memory |
|---|---|---|
| WARPTRI `switch=200` and `switch=2000`: P4 cycles a switch | 1,797,882 and 1,802,547 (a reset and the G450's memory set-up each) | 208,221 and 208,653 (no resets) |
| A triangle on one pipe (WARPTRI `same`, P4 cycles) | 14,407 and 13,945 | 15,501 and 14,052 |
| GLQuake 640 `-mtex` | 59.95 fps (3,687 resets) | 67.04 fps (none) |
| GLQuake 640 `-mtex` with snapshots | 55.74 | 61.83, 4 frames identical |

On bench-g400, WARPTRI's `same` was 490 cycles loaded and 1,908 from memory in a 200-triangle run. Probably a
one-off of about 0.7 ms after the first start rather than a slower triangle (not yet checked with `switch=2000`
there): the speed test's 4,000 triangles took 491 cycles each from memory, the G450's `same` matches both ways,
and GLQuake without `-mtex` is unchanged.
