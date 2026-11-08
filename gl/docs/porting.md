# Using DOS-GL

DOS-GL is a static library for 32-bit DOS programs built with DJGPP (gcc
12.2 cross compiler) and run under CWSDPMI. It implements the OpenGL 1.1
subset in PRD §6 on Matrox G400, G450 and G200 cards.

## Building and linking

```
#include <GL/gl.h>       /* OpenGL 1.1 (includes GL/glext.h)       */
#include <GL/glext.h>    /* extension and GL 1.2 tokens            */
#include <GL/dosgl.h>    /* dglInit, dglSwapBuffers, ...           */

i586-pc-msdosdjgpp-gcc ... -I<dosgl>/include -L<dosgl>/build/lib -lGL -lm
```

Ship `CWSDPMI.EXE` next to the program (or on the `PATH`).

## Context

```
DGLConfig cfg = { 0 };            /* all zero: 640x480x16, double-buffered, 16-bit Z, vsync */
if (dglInit(&cfg) != 0) { puts(dglGetErrorString()); return 1; }
... draw with GL ...
dglSwapBuffers();
dglShutdown();
```

- `dglEnumModes` lists, smallest first, the 16-bit modes the card's BIOS
  offers and (v1.2) 320x200, 320x240, 400x300, 512x384 and 640x512,
  which the BIOSes lack. See "Resolutions" below.
- A crash (#GP, #PF, #UD, divide error), `exit()` or Ctrl-Break resets the
  engine and restores text mode.
- DOS-GL maps the card with near pointers (`__djgpp_nearptr_enable`). A
  program that turns them off must not call GL until `dglSwapBuffers`
  (which turns them back on) or turn them back on itself.
- Games may leave the x87 FPU at 24-bit precision or change its rounding
  (Quake 2 does): drawing calls switch to their own control word and
  restore the caller's.
- Under GLOS (`DGL_API_VERSION` 0x0104), on a desktop that can show GL
  in a window, `dglInit` opens a window of the context's size instead of
  setting a mode (any size the engine can draw; `dglEnumModes` lists the
  usual ones that fit), and the program becomes a GLOS app: its buffers and
  texture heap are VRAM GLOS gives it (`DGL_TEXHEAP_KB`, default up to
  4 MB), and DOS-GL takes GLOS's GPU lock before each piece of work with the
  engine. `DGL_FULLSCREEN=1` takes the screen instead (a GLOS session);
  `dglGetGlosWindow()` gives the window's GLOS id; `DGL_TITLE` its title.
- `dglSetWaitHook(fn, arg)` (0.3, `DGL_API_VERSION` 0x0103) runs `fn(arg)`
  about once a millisecond while `dglSwapBuffers` waits for the drawing
  engine to finish the frame, and once before it waits for the retrace
  (never during it: a late return would cost a frame). Programs with
  cooperative threads yield there; SDL's DOS-GL bridge does
  (`docs/sdl.md`). The hook must not call GL. `DGL-STAT` counts it
  (`hooks=`).
  It also runs during long work outside the swap, at most every 4 ms: a texture's texels written into VRAM or
  converted, a frame read back with `glReadPixels` (a program's sound thread kept up through first frames and
  level loads: GLOS M8).
- `dglGetProcAddress(name)` returns any GL 1.1 function and the extension
  functions DOS-GL implements (for programs that bind GL at run time).
- Test hooks, over COM1:
  - `DGL_EXIT_AFTER=n` ends the program after n swaps (0: no limit). With
    it set, `dglInit` logs `DGL-START` and every way out logs
    `DGL-EXIT frames=n`, which is how Loop A knows a program has finished.
  - `DGL_STATS=1` logs frame and triangle rates once a second (`DGL-STAT`),
    with the milliseconds the swaps spent waiting for the engine to finish
    the frame (`drain_ms`) and for the retrace (`retrace_ms`);
    `DGL_STATS=2` adds where primitives went (`DGL-PRIMS`: skipped, clipped
    away, zero area, culled) and texture traffic (`DGL-TEX`: uploads,
    sub-image writes in place by the CPU, in place after waiting for the
    engine, by the CPU into a new block (`sub_move`, counted in `sub_fast`
    too), through the engine (ILOAD), or as
    whole re-uploads, renames, evictions, syncs forced by texture memory,
    palettes loaded into the lookup table).
  - `DGL_FRAMES=C:\OUT\FRAMES.TXT` writes a line per frame when the program
    leaves (not after a fault): the frame's time, the part spent in the
    swap and its wait for the engine, the time in texture uploads and
    sub-images, and the frame's uploads, sub-images (moved, after a wait,
    whole), evictions, waits forced by texture memory, triangles, engine
    resets (the WARP's pipe switches among them), engine timeouts and stuck
    fences, for finding long frames that averages hide. Times are in
    microseconds; the first frame counts from the context's start.
    `DGL_FRAMES_MAX` caps the frames kept (65536).
  - Profiling: a library built with `make PROF=1` (stage timers; needs a
    Pentium) adds, with `DGL_STATS=2`, a `DGL-PROF` line a second: the
    cycles each stage took, in units of 1024 (the program itself `app`,
    `xform`, `valid`, `clip`, `proj`, `setup`, the HAL's setup stages
    `splane`/`sinc`/`strap`, `fifo` waits, `tex` uploads, `clear`, and the
    swap's `drain`, `vsync` and the rest), then triangles set up, register
    writes, FIFOSTATUS reads, stage switches and what one switch costs.
    `DGL_STATS=3` also logs `DGL-MICRO` at start (single operations'
    cycles). The HAL's `tools/perf/profsum.py` sums a run's lines into each
    stage's share and cycles per triangle. In 86Box the cycles are the
    emulated CPU's: compare builds with each other, never with hardware,
    and read FIFOSTATUS counts there as waits (its queue is 64K entries
    deep, so the CPU polls only once the engine is far behind). Building
    again without `PROF=1` returns to the library without timers.
  - `DGL_VSYNC=0` or `1` overrides the program's choice of swap on retrace.
  - `DGL_WARP=0` sets every triangle up on the CPU on a G200, G400 or G450, which otherwise use the chip's WARP
    setup engine with vertices by bus-master DMA (MGA-Glide `docs/g400-warp.md`, `docs/g200-warp.md`). At exit a
    `DGL-WARP tris= failures= kicks= ring= resets= timeouts= seqs=` line counts its triangles, failures (each one
    sends the rest to the CPU) and DMA transfers, says what the rings were (1 cached, PCI; 2 write-combining, PCI; 3
    write-combining fetched by AGP; and their size), counts the engine's resets and timed-out waits, and the
    strips and fans sent whole (all / with two textures). `DGL_WARPDMA=0` sends the WARP's vertices by the CPU through
    the DMA window instead of the DMA command stream. In a GLOS window the CPU sets every triangle up, as with
    `DGL_WARP=0`: work left in the stream's list when GLOS takes the engine back would run after GLOS's own
    drawing, and the WARP fed by the CPU was slower than the CPU's setup on bench-g400 (and stalled DOSBench's
    scenes until GLOS's GPU watchdog ended them).
  - `DGL_AGP=0` keeps the DMA rings cached and fetched by PCI under GLOS too, which otherwise makes them
    write-combining (API 1.4) and turns AGP on, at the fastest rate the card and the bridge both have;
    `DGL_AGP=1` or `2` caps the rate at 1x or 2x. The G200 keeps cached rings and PCI unless `DGL_AGP=1` or `2`
    asks (its AGP drew nothing on the real card: MGA-Glide `docs/g200-warp.md`). `DGL_RING=bytes` (256 KB) and `DGL_KICK=bytes` (6 KB of
    vertices between kicks) tune the rings.
  - `DGL_FAST=0` turns off the fast path: triangles from vertex arrays, immediate mode and display lists straight
    to the WARP (no texture matrix, polygon offset or remapped unit; two textures on the G400 and G450 when unit 1
    has its own), each vertex transformed and projected once.
    On it, a fan, polygon or strip of 5 to 64 vertices goes as one sequence of the WARPs' (each vertex once in the
    DMA stream) when all of its triangles would be drawn; `DGL_SEQ=0` sends triangles instead (1 fans and polygons,
    2 strips, 4 flat polygons; 7 by default). `DGL-PRIMS ... seqs=` counts them.
  - `DGL_TEXHEAP_KB=n` caps the texture heap (to test eviction, or to
    behave like a card with less memory).
  - `DGL_SNAP=100,250` writes the frames shown by those swaps to
    `DGL_SNAPDIR` (default `C:\OUT`) as `F00100.PPM`...; a timedemo draws
    every frame, so these are the same pictures on every card and run.
    `dglSnapshot(path)` does the same on demand.
  - For the silicon experiments (`docs/silicon-experiments.md`), on the
    G400/G450 only: `DGL_COMBINER=1` draws single textures through the
    texture-stage combiner (as Mesa does) instead of the legacy modulate,
    and `DGL_TC2_EXTRA=8000` (hex) ORs those bits into every TEXCTL2.
  - `DGL_ZOOM=1` shows the half-size modes zoomed instead of scaled;
    `DGL_PRESENT=force` takes the scaled path even for the BIOS's own sizes
    (a test switch); `DGL_SCALE_FILTER=bilinear` smooths scaled modes.
  - `DGL_GUARD_PX=n` (at most 2000, the default) clips triangles whose
    screen coordinates reach more than n pixels from the viewport's centre.
    A smaller band clips more, but keeps the edges and texture gradients the
    setup computes shorter. At 800 no conformance frame changed in 86Box, so
    this is kept for silicon, where precision may differ.
  - The first 16 GL errors are logged as `DGL-GLERR <code> at <address>`
    (the address is inside the GL function that raised it; look it up in
    the program's link map).

## Resolutions

The Matrox BIOSes offer 16-bit 640x480, 800x600, 1024x768 and 1280x1024
(1600x1200 only where the BIOS lists it). DOS-GL shows the other sizes in a
larger BIOS mode, as the HAL's mode planner decides:

| Size | Shown in | How (`DGLDeviceInfo.fit`) |
|---|---|---|
| 320x240, 400x300, 512x384, 640x512 | 640x480, 800x600, 1024x768, 1280x1024 | `integer`: exactly 2x; with `DGL_ZOOM=1`, `zoom`: the chip doubles lines and pixels |
| 320x200 | 640x480 | `fill`: stretched to 4:3, as a CRT showed it |
| the BIOS's own sizes | the same | `native` |

A scaled mode draws into a render buffer; each `dglSwapBuffers` scales it
into the hidden display buffer with the drawing engine (nearest filtering,
or `DGLConfig.scale_filter = 2` / `DGL_SCALE_FILTER=bilinear`) and flips.
The scaling is part of every frame's time (`DGLStats.present_us`, DGL-STAT
`present_ms`). `glReadPixels` and snapshots read the render buffer.
Single-buffered programs, and drawing to `GL_FRONT`, are shown at
`glFlush`/`glFinish`. A zoomed mode draws into the display buffers and costs
nothing extra; it stays opt-in until the bench confirms it on each card.
`DGLMode` says which modes are `scaled` or `zoomed` and their
`display_width`/`display_height`; after `dglInit`, `DGLDeviceInfo` gives
the display mode and where the picture lands in it.

`dglInit` sets the mode, probes VRAM and only then checks the buffers fit,
so a BIOS that under-reports memory (Matrox's G200 BIOS says 2 MB of 8) no
longer blocks the larger modes; `DGL_VRAM_KB` states the size for
`dglEnumModes`' advice before the first `dglInit`.

## What differs from full OpenGL 1.1

| Area | Behaviour |
|---|---|
| Colour | RGB565 only, dithered; `GL_ALPHA_BITS` is 0 (destination alpha reads as 1) |
| API | Every OpenGL 1.1 function links. Those DOS-GL does not implement (evaluators, feedback, selection, pixel maps, lighting state...) are stubs that log `DGL-STUB glName` once, count the call in `DGLStats.stub_calls` and return zero; the list is generated at build time (`build/gen/stubs.c`, from `src/gl/gl11.api`) |
| Textures | Power-of-two sizes; stored as RGB565, ARGB1555 or ARGB4444 chosen from the texels' alpha after the internal format has been applied (`GL_RGB` drops alpha, luminance and intensity come from red, `GL_ALPHA` keeps only alpha, `GL_RGB5_A1` never gets ARGB4444; sized formats are otherwise hints). Luminance textures keep exact greys in RGB565. Textures smaller than 8 texels are widened to 8 |
| Texture memory | Whole uploads never wait for the engine: a texture drawn since the last swap is rewritten into a new block (the old one is freed at the next swap). `glTexSubImage2D` writes just the rectangle when it can. It goes through the drawing engine (ILOAD), in order with the draws, when the texture level is at least 32 texels wide (64 for 8-bit textures), except on a G400 or G450 drawing through the WARPs. There an ILOAD must first wait for the WARPs to finish, and CPU writes were faster on bench-g400: GLQuake's demo1 ran at 39.3 fps against 35.9 (33.0 against 31.6 with `-mtex`). Otherwise the CPU writes it. If queued draws may still read the texture, the CPU writes into a new block and the engine copies the rest of the texture there from the old block, queued behind those draws; the old block is freed at the next swap. That needs the rectangle's rows whole, or its level's rows a multiple of 64 bytes (32 texels, 64 for 8-bit textures). Failing that, the CPU waits for the engine first when the rectangle is at most a quarter of the texture, and re-uploads the whole texture into a new block when it is larger. `DGL_ILOAD=0` makes the CPU write every rectangle; `DGL_ILOAD=1` uses ILOAD on a G400 or G450 too; `DGL_TEXMOVE=0` waits for the engine instead of moving. When VRAM runs out, the least recently drawn textures are evicted and re-uploaded from DOS-GL's copies when next used; `GL_OUT_OF_MEMORY` only when one texture cannot fit at all |
| Texture environment | `GL_MODULATE` and `GL_REPLACE` everywhere. `GL_DECAL`: on RGB textures as `GL_REPLACE`; on textures with alpha a blend by texel alpha (the G400/G450's combiner, the G200's decal blend). `GL_BLEND` with a black environment colour on the G400/G450 (the combiner); with any other colour, or on the G200, it is drawn as `GL_MODULATE` (logged once). On the G400/G450 also `GL_ADD` (GL 1.3) and `GL_COMBINE_ARB` |
| Combine | `GL_ARB_texture_env_combine` (and the EXT name) on the G400/G450, as far as the combiner goes: each stage takes its own texture and one other source (`GL_PRIMARY_COLOR_ARB`, `GL_PREVIOUS_ARB`), as colour or alpha, inverted or not, into `GL_REPLACE`, `GL_MODULATE` (`GL_RGB_SCALE_ARB`/`GL_ALPHA_SCALE` 2 and 4), `GL_ADD`, `GL_ADD_SIGNED_ARB` and `GL_SUBTRACT_ARB` (scale 1 or 2). `GL_INTERPOLATE_ARB`, `GL_CONSTANT_ARB`, two sources other than the texture, and scale 4 on the adder are drawn as `GL_MODULATE` (logged once). The combiner's multiplies lose a little precision: unit 1's x2 and x4 come out a few levels darker than Mesa (conformance t25). Not on the G200 (one unit, no combiner): `GL_COMBINE_ARB` is `GL_INVALID_ENUM` there |
| Buffer objects | `GL_ARB_vertex_buffer_object` everywhere: buffers live in system memory (DOS-GL transforms on the CPU), so with a buffer bound the `gl*Pointer` pointers and `glDrawElements` indices are offsets into it; `glMapBufferARB` returns the memory itself. The GL 1.5 names (`glBindBuffer`...) reach the same functions through `dglGetProcAddress`, as `glDrawRangeElements` reaches `GL_EXT_draw_range_elements`'s. Their use is the renderers' faster paths: Xash3D's `gl_vbo 1` draws world lightmaps in the G400/G450's second texture unit (one pass instead of two) |
| Multitexture | `GL_ARB_multitexture` (and `GL_SGIS_multitexture`'s names) with two units on the G400 and G450, drawn in one pass with the chip's two texture maps and its combiner; each unit has its own binding, environment, texture matrix, current coordinates and client array. The G200 has one unit (`GL_MAX_TEXTURE_UNITS_ARB` is 1) and does not advertise the extensions: programs draw two passes, as GLQuake and Quake 2 do |
| `GL_CLAMP` | Clamps to the edge texel (as the hardware does), the same as `GL_CLAMP_TO_EDGE`; GL 1.1 would blend edge texels with the border colour under linear filtering. Border colours are stored, never drawn |
| Mipmaps | Up to 5 levels in a window starting at level 0 (levels at least 8x8); the chip picks the level per pixel and rounds it |
| Fog | Per-vertex factor, interpolated linearly across the screen; triangles whose fog varies a lot are subdivided |
| Lighting, accumulation | Not implemented (normals accepted and ignored) |
| Stencil | No stencil buffer (`GL_STENCIL_BITS` is 0): the stencil state is kept and the test always passes, as GL specifies for a zero-bit buffer |
| Colour buffers | `glDrawBuffer` and `glReadBuffer` choose the front or back buffer; `GL_FRONT_AND_BACK` draws into the back buffer only |
| Polygon offset | `GL_POLYGON_OFFSET_FILL` only (point and line offset are accepted and ignored) |
| Display lists | `GL_COMPILE` and `GL_COMPILE_AND_EXECUTE` capture draw calls and immediate-mode blocks only; other calls made while compiling take effect immediately; no nesting |
| Polygon mode | Fill only |
| Lines and points | Screen-space quads of the line width and point size |
| Paletted textures | `GL_EXT_paletted_texture` and `GL_EXT_shared_texture_palette`: `GL_COLOR_INDEX*_EXT` textures keep their 8-bit indices. On the G200, textures using the shared palette while it is opaque are stored as 8-bit indices and read through the chip's lookup table (half the VRAM; a palette change reloads the table). Otherwise they are expanded through the palette (shared or their own) into the 16-bit formats when they go to VRAM, and a palette change re-uploads the textures that use it when next drawn. `DGL_TLUT=1` uses the lookup table on the G400/G450 too (the G400 specification says to expand 8-bit textures; untested on silicon), `DGL_TLUT=0` never. Indices go only into colour-index textures (no pixel maps) |
| Extensions | `GL_EXT_bgra`, `GL_EXT_texture_edge_clamp`, `GL_SGIS_texture_edge_clamp`, `GL_EXT_paletted_texture`, `GL_EXT_shared_texture_palette`, `GL_ARB_vertex_buffer_object`, `GL_EXT_draw_range_elements`; on the G400/G450 also `GL_ARB_multitexture`, `GL_SGIS_multitexture`, `GL_ARB_texture_env_combine` and `GL_EXT_texture_env_combine`. `dglGetProcAddress` also answers to the core names of the multitexture, buffer and draw-range functions. Prototypes in `GL/glext.h` with `GL_GLEXT_PROTOTYPES` |
