# DOS-GL's front end: speed — Plan (2026-11-14)

**Status:** proposed, for the owner's approval. Nothing is built yet.

## Why

On bench-g400 (Pentium II 400, G400), GLQuake's demo1 runs at 45 fps at 640x480 on DOS-GL's G400 path, the same
as id's software Quake with write-combining (39-44 fps), and at 52 fps at 320x200 against software's 91-107. The
card is not what holds it back. DOS-GL's own CPU work takes about 70% of a frame, and the engine is waited on for 6%
of a frame at 320x200 and 27% at 640x480 (`PROF=1`, `gl/docs/b2.md`). GLQuake's single-pass multitexture
(`-mtex`) is slower still: 640x480 43.2 to 38.4 fps, 320x200 51.7 to 42.9.

The front end is shared by every GL program, not just Quake: Quake II, Half-Life, ClassiCube, PrBoom-plus, Fifth
Wheel, GLOS's GL windows and DOSBench's GL scenes. They feed it in different ways:

| path | used by |
|---|---|
| immediate mode (`glBegin`/`glVertex`/`glEnd`) | GLQuake, Quake II, DOSBench's game scenes |
| vertex arrays, `glDrawElements` | ClassiCube, Fifth Wheel's world, PrBoom-plus (via SDL) |
| buffer objects | Half-Life (`vbo1`) |
| display lists | Fifth Wheel's props, DOSBench |

**What a GLQuake frame asks of it** (bench-g400, `DGL_STATS=2`): about 700 `glBegin`/`glEnd` pairs, 3,000
triangles in (a quarter culled, 8% clipped), 2-4 sub-image uploads of dynamic lightmaps. DOS-GL's share comes to
roughly 2,000 CPU cycles a triangle on the PII. A lean driver of the period spent several times less.

**What is already done.** The October plan (P0-P7) cut emulated cycles a triangle by about 30%: register shadows,
a vertex cache, projection once a vertex, `mga_irint_nearest`. The G400 has a fast path (`emit.c`, `fast_tri`):
for a batch with nothing to clip, each vertex is fetched, transformed, given its outcode, projected and turned into
its WARP form once, then written to the DMA ring. All of that runs under one profiler stage (`xform`, 43.5% of a
frame), so today's profile can't say which part costs what. That is the first thing to find out.

## Goal and targets

Cut DOS-GL's CPU time a triangle on every path, with frames unchanged. On bench-g400, GLQuake demo1:
- 640x480: 45 to at least 60 fps;
- 320x200: 52 to at least 80 fps;
- `-mtex` at least as fast as two passes.

DOSBench's game scenes (G1-G4) should gain at least 30%. The targets are guesses until F0's numbers are in; F0's
report revises them.

## Steps

### F0. Measure (no behaviour change)
- **Finer stages in `PROF=1` builds.**
  - Split `xform` into fetch, transform, outcodes, projection with the WARP form, cull, and the DMA ring
    writes and kicks.
  - Split `valid` into "nothing changed" and real work.
  - Counters: validations with nothing dirty, texture binds, vertices transformed and projected a triangle, ring
    kicks.
- **Workloads on bench-g400,** each at 320x200 and 640x480 (booked with GLOS):
  - GLQuake demo1 with and without `-mtex`;
  - Quake II's demo;
  - Half-Life (`vbo1` and `vbo0`);
  - ClassiCube;
  - DOSBench's GL set (S1, G1-G4, L1).
- **86Box** for counts and correctness only, never speed. The G200's path (CPU setup, no WARP fast path) is
  counted there too.
- **Output:** a table in this file of cycles a triangle by stage and workload, and the order of F1 onwards.

### F0's results (bench-g400, 2026-11-14 evening)

`PROF=2` builds of fe-plan 6bfbacd, `DGL_STATS=2`, GLOS's kernel with write-combining on. Runs with fixed time
(`-fixedtime`, `fixedtime 14`, `host_framerate 0.02`), so later steps draw the same frames. Numbers are CPU cycles
a triangle drawn, with each stage's timer entries taken out (`profsum.py`; the timers cost another 2-7%). Frame
rates under `PROF=2` are 15-20% below normal builds'.

**Games** (`lib` = DOS-GL without waits; `fifo` = waiting for room in the card's FIFO, i.e. the card is busy):

| run | fps | lib | app | fifo | draw: xform+valid+vidle+batch | vertex: fxf+fprj | tri+ring | clip+proj+setup | tex | swap |
|---|---|---|---|---|---|---|---|---|---|---|
| GLQuake 640x480 | 37.6 | 3,615 | 916 | 956 | 951 | 1,150 | 580 | 564 | 123 | 247 |
| GLQuake 320x200 | 40.6 | 3,972 | 768 | 93 | 940 | 1,159 | 565 | 524 | 119 | 655 |
| GLQuake 640x480 `-mtex` | 32.1 | 6,101 | 1,596 | 1,608 | 2,489 | 1,442 | 715 | 523 | 533 | 397 |
| GLQuake 320x200 `-mtex` | 33.9 | 6,495 | 1,409 | 454 | 2,387 | 1,417 | 688 | 482 | 515 | 992 |
| Quake II 640x480 | 27.6 | 4,233 | 3,912 | 985 | 1,386 | 1,297 | 597 | 284 | 288 | 383 |
| Quake II 320x240 | 29.2 | 4,836 | 2,776 | 327 | 1,434 | 1,349 | 627 | 288 | 316 | 809 |
| Quake II 640x480 multitexture | 27.4 | 4,380 | 3,104 | 1,068 | 1,452 | 1,330 | 627 | 310 | 306 | 354 |
| Half-Life 640x480 | 34.3 | 4,072 | 2,148 | 3,870 | 1,163 | 358 | 171 | 1,938 | 3 | 433 |
| Half-Life 640x480 `gl_vbo 1` | 45.1 | 4,679 | 3,172 | 6,815 | 1,443 | 437 | 226 | 1,933 | 5 | 626 |

A GLQuake frame: 1,750 triangles, 810 draws, 4,400 `glVertex`, 35 binds; 95% of validations find nothing to
write. With `-mtex`: 590 draws, 103 binds, and 39% of validations rewrite registers (about 40 writes each).

**DOSBench's GL set at 640x480** (cycles a triangle; S2 and S4 are synthetic):

| test | lib | what dominates |
|---|---|---|
| S2M16, S2SA16 (shared-vertex mesh; immediate mesh drawn as arrays) | 635 | ring 186, batch 181, vertex 191 |
| S2SL16 (display lists) | 717 | as arrays |
| S2SI16 (immediate mode, unshared vertices) | 2,245 | vertex 1,607 (three a triangle) |
| S4D1 against S4D16 (a draw per triangle against per 16) | 2,984 / 2,087 | a draw: about 1,100-1,500 cycles before its vertices |
| S4T1 (a texture change per triangle) | 6,950 | validation 3,347: a texture change rewrites everything |
| S4B1 (a blend change per triangle) | 8,746 | validation 4,756 |
| G4CITY (fog) | 3,484 | projection 986: the fog factor is computed per vertex with `expf` |
| L1LQ2P (lightmapped level) | 3,429 | waits on the card 4,646 |

**What the numbers say:**
1. **A vertex costs about 500 cycles** on the fast path (about 250 to fetch, transform and outcode, 250 to
   project and form). The code is compiled for the Pentium (`-march=i586`): every x87 comparison is
   `FCOM`/`FNSTSW`/`SAHF`, and a vertex makes about 20 (six outcodes, colour clamps, the depth clamp, the
   texture floors' range checks). The Pentium II has `FCOMI` and `CMOV`.
2. **A draw costs about 1,100-2,000 cycles** before its vertices: validation (about 300 even with nothing to
   write), the fast path's set-up, `dgl_assemble`. GLQuake makes 810 a frame.
3. **A state change costs 3,000-4,800 cycles** and about 40 register writes: any texture or blend change rebuilds
   and rewrites the whole register block. This is all of `-mtex`'s loss, and much of Quake II's validation.
4. **Ring writes and per-triangle work, about 580 cycles a triangle.** Quads (4 vertices) don't go as sequences
   (the minimum is 5), so each of their triangles writes three vertices.
5. **The general path** (triangles past the guard band, or draws the fast path refuses): about 380 cycles a
   GLQuake triangle; Half-Life takes it for most vertices (1.5 general against 0.7-0.9 fast a triangle).
6. **The card is busy at 640x480** in GLQuake (16% waiting), Quake II (about 20%) and Half-Life (40-60%). So
   CPU savings there are capped by the card; `-mtex` halves GLQuake's world fill as well, so fixing 3 helps both.
7. 320x200's swap (the card's upscale) costs 650-1,000 cycles a triangle; fog's `expf` per vertex in G4CITY.

**Revised targets.** GLQuake 320x200 is CPU-bound: 52 to 80 fps stands. At 640x480 the card caps it: 45 to 60
fps if the card's share holds, more with `-mtex` once it pays. Quake II gains less (its own CPU work is as large as
DOS-GL's). Half-Life is mostly the card's.

### F1 onwards, in F0's order
Each is its own commit with its own checks; each step is measured with `PROF=2` against F0's table.
1. **F1, state changes write only what changed** (finding 3). Split the register block by what each GL state
   feeds (texture unit 0, unit 1, blend, alpha test, depth, fog); a texture change rewrites its unit's words, kept
   with the texture object (`sampler()`'s results). Target: S4T1 and S4B1 validation under 800 cycles, GLQuake
   `-mtex` faster than two passes.
2. **F2, a draw with nothing changed costs little** (finding 2). Validation that returns at once when no group is
   dirty and the bound textures are the same (their residency and busy marks once a frame, not once a draw); the
   fast path's per-draw values (colour, texture scales, matrices) only when they change. Target: under 500 cycles a
   draw.
3. **F3, the vertex on the Pentium II** (finding 1). The fast path's vertex and triangle routines built twice,
   `-march=i586` and `-march=pentium2`, chosen by CPUID at start (as the software renderer chooses MMX), with the
   same arithmetic: the results are the same, only the comparisons and selections differ. Then reorder the
   routine's work (outcodes from the values already in registers, fewer `volatile` stores) where REGCRC shows
   nothing moves. Target: 500 cycles a vertex down to about 300.
4. **F4, rings and triangles** (finding 4): quads and four-vertex polygons as sequences; fewer per-triangle tests.
5. **F5, the general path** (finding 5): why Half-Life's draws leave the fast path, and the clipped triangles'
   cost.
6. **F6, the rest**, if the numbers still point at them: fog's factor without `expf` per vertex (exactly the same
   values or not at all), immediate mode's copies, array fetches by layout, lightmap uploads, the 320x200 swap.

### F1-F3's results (bench-g400, 2026-11-14 late)

Commits fbf6798 (F1), 8f4ed2e (F2), 3dbe990 and b2a6f97 (F3). In 86Box, against main: every conformance frame on
the G200, G400 and G450 the same (F2 and F3 also every register write and WARP vertex). On the card, GLQuake's
`-fixedtime` snapshots are identical to main's byte for byte, two-pass and `-mtex`, with either copy of F3.

**Normal builds, frames a second** (GLQuake demo1, write-combining on):

| | main | F1-F3 | |
|---|---|---|---|
| 640x480 | 45.3 | 45.8 | +1.2% |
| 320x200 | 53.0 | 55.3 | +4.4% |
| 640x480 `-mtex` | 39.5 | 43.1 | +8.9% |
| 320x200 `-mtex` | 43.2 | 47.4 | +9.8% |
| 640x480, F3's Pentium copy (`DGL_P6=0`) | | 45.7 | |
| 320x200, F3's Pentium copy | | 54.7 | |

**What each step did** (`PROF=2`, cycles a triangle, F0 against now):
- **F2 worked:** validation with nothing to write, GLQuake 147 to 24; batch 503 to 444; ring 278 to 222.
- **F1 helped where state changes come without texel writes:** DOSBench S4T1 (a texture change a triangle)
  validation 3,347 to 2,217, -19% library time. GLQuake `-mtex` validation 1,594 to 1,205 only: it writes a
  lightmap between surfaces, and after any texel write everything is written again (the texel cache).
  S4B1 (a blend change a triangle) didn't move: `glEnable`/`glDisable` of any capability marked every group,
  the target among them, and a dirty target means everything. Fixed after this run (only the scissor marks the
  target now); not yet measured.
- **F3 did not pay.** The vertex's two stages together are the same (GLQuake 1,150 against 1,155), the Pentium
  copy runs within 1% of the Pentium II's on GLQuake, and DOSBench's meshes are 4-5% slower in library time with
  the Pentium II copy (CMOV in place of well-predicted branches, by the look of the stages). The comparisons
  were not what a vertex costs.

**So the targets are out of reach by trimming.** GLQuake's library time fell 6% (3,615 to 3,399 at 640x480).
What remains a triangle at 320x200: the vertex about 1,150 (projection, colour and texture conversions, the
reciprocal), draws about 750 (assembly, the fast path's set-up and loops), the general path for clipped
triangles about 360, the ring and triangle work about 520, the swap's upscale about 650.

### F-last. Docs and numbers
- Update this file, `gl/docs/porting.md` and DOSBench's records with before and after on bench-g400.

## Checks for every step
- **Host tier.** The software renderer shares the front end, so `soft-conform` at 8, 16 and 32 bits and the tile
  check cover it too.
- **REGCRC on the G200, G400 and G450 in 86Box, every conformance test.**
  - A purely CPU-side change must leave every register-write and WARP-vertex CRC the same.
  - A change that alters the command stream on purpose (fewer redundant writes, different ring kicks) must leave
    every frame byte-identical instead.
- **Games in 86Box.** GLQuake, Quake II and Half-Life snapshots with `-fixedtime`, byte-identical to main's.
- **bench-g400.** Each merge gets the timedemos with `-fixedtime` snapshots (identical to main's) and timings,
  plus DOSBench's GL set. Booked with GLOS each time.
- **Merge tier** on devserver before each merge.

Merges: F0 on its own, then every step or two, each with your OK.

## Not in this plan
- **Glide's front end.** It is separate code (GTA's setup is about 6,100 emulated cycles a triangle) and would be
  a plan of its own.
- **The software renderer's spans** (B2's), and WARP microcode changes.
- **Overlapping frames,** beyond the DMA ring the G400 already uses.

## Risks

| Risk | What answers it |
|---|---|
| x87 rounding moves pixels when arithmetic is reordered | Same expressions in the same order where results reach the card; REGCRC and snapshots catch any change |
| Gains on the PII don't carry to the G200's CPU-setup path | F0 counts both paths; the G200 has no bench, so its gains are argued from counts |
| Silicon-only effects (FIFO stalls, write-combining) | Measured on bench-g400 only; 86Box speed is never used |
| The targets are wrong | F0 revises them before any optimisation starts |
