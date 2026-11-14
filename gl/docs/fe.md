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

### F1 onwards: candidates, to be ordered by F0
Each is its own commit with its own checks.
1. **Validation only when something changed.** `emit_begin` rebuilds parts of the state at every `glBegin` (about
   700 a frame): texture lookups and residency, the skip test and the guard band's divisions. The idea is to cache
   them and redo them only on dirty bits.
2. **Cheaper texture binds.** Keep each texture object's register words (`sampler()`'s results) with it, so a bind
   writes saved words. This is the `-mtex` case: GLQuake binds a lightmap per polygon there.
3. **The fast path's inner loop.** Act on F0's split: specialised transforms (affine matrices for 2D and
   orthographic; the common perspective shape), the texture-matrix and fog tests hoisted out of the vertex loop,
   fewer stores between stages, and ring writes in larger runs.
4. **Immediate mode's copies.** Each `glVertex` copies the current colour and both texture coordinates (48 bytes)
   whether they changed or not. The idea is to copy only what changed since the last vertex.
5. **Arrays and buffers.** Fetch routines for the common layouts (float positions, byte colours, float texture
   coordinates; shorts), with no type switch per component. Today `glDrawElements` copies every index into an
   `int` buffer first.
6. **Dynamic lightmap uploads.** `tex` was about 10% of a 320x200 frame; convert and write only the changed
   rectangle.
7. **The 320x200 swap** (12.8% of a frame): find out whether the CPU waits for the card's upscaling blit, and
   overlap it with the next frame if so.

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
