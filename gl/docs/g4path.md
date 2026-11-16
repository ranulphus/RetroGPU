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
2. **Not draining doesn't help.** With `DGL_NODRAIN=1` (an experiment: the swap kicks and goes on, so frames
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
