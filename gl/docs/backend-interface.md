# DOS-GL's back-end interface (step B0)

**Status (2026-11-06): B0 v0, for the owner's review with GLOS's phase A.**

Step B0 of the software renderer's plan (`docs/backends.md`):
- every call DOS-GL's GL layer makes into Matrox code today, listed;
- the back-end call each one becomes.

B1 then moves the Matrox code behind this interface, pixel for pixel the same. B2 writes the software renderer
against it.

The census is of DOS-GL main 7a277d1: `src/gl` and `src/dgl`, against MGA-Glide main's HAL headers
(`hal/include/mga/`).

## What the census found

1. **The drawing seam is already there.**
   - `vertex.c` assembles primitives and hands clip-space vertices (`dgl_cvtx`) to a table of functions,
     `dgl_sink` (`gl_draw.h`): begin, triangle, line, point, end, and an optional whole-draw `fast`.
   - `emit.c` installs them (`dgl_emit_install`). Everything Matrox in drawing happens behind that table:
     - validating GL state into registers;
     - clipping with the Matrox guard band, projecting to the setup's fixed point, culling;
     - polygon offset, fog's subdivision;
     - the WARP or the CPU setup.

   So the interface keeps today's seam, in clip space (question 1).
2. **Platform services aren't the back end's.** Some of the HAL's headers are DOS and DJGPP services that every
   back end and the GL layer use alike. They stay shared:
   - `sys.h`: time in µs, fault and exit hooks;
   - `fp.h`: the x87 control word, rounding;
   - `prof.h`: profiling stages;
   - `serial.h`: the log on COM1.

   In the merged repository they are the HAL's platform part, not Matrox code.
3. **Sixteen-bit colour is assumed outside the back end in three places:**
   - `glClear` packs RGB565 (`emit.c`);
   - `glReadPixels` decodes it from the framebuffer (`readpix.c`; `glCopyTex*Image2D` and snapshots read through
     it);
   - `dglInit` refuses other depths (`context.c`).

   The interface moves all three into the back end: the GL layer gives colours as floats and reads back RGBA8.
4. **Texture objects mix GL's state and the card's copy.**
   - `dgl_texture` (`gl_tex.h`) holds GL's part: levels as RGBA8 shadow copies reduced to the internal format's
     class, filters, wraps, the palette.
   - It also holds the card's: residency, the TEXCTL format, VRAM offsets per level, the stored size, the last
     drawn epoch, the palette expanded with.

   The card's fields move into a back-end part the texture points to.
5. **Stencil has no user that needs it.** DOS-GL reports 0 stencil bits and accepts the state (PRD §2.2: the test
   always passes). Two ports use stencil only when the GL reports some:
   - Quake 2: `gl_mesh.c`'s shadows, `have_stencil`;
   - PrBoom-plus: `gl_missingtexture.c` and `gl_detail.c`, `gl_use_stencil`.

   Both run without it, as they do today (question 2).

## The interface

One table of functions, `dgl_backend` (in `src/gl/backend.h`, beside `gl_draw.h`), chosen at `dglInit`. Each back
end lives in its own folder:
- `src/backend/mga/` (B1);
- `src/backend/soft/` (B2);
- later `src/backend/tnt2/`.

In the merged repository, the same under its GL tree.

```c
typedef struct dgl_backend {
    const char *name;                         /* "mga", "soft": DGL_DEVICE's names */

    /* Capabilities */
    int  (*probe)(void);                      /* 0: it can run on this PC */
    void (*caps)(dgl_caps *c);                /* after open: what glGet and the extensions report */

    /* Modes and surfaces */
    int  (*modes)(DGLMode *out, int max);     /* full-screen modes (dglEnumModes) */
    int  (*open)(const DGLConfig *cfg);       /* full screen: mode, buffers, cleared; 0 or -1 with dgl_set_error */
    int  (*open_glos)(const DGLConfig *cfg);  /* in a GLOS window; 1: not possible here, try full screen */
    void (*close)(void);                      /* dglShutdown, and the fault path's teardown: safe twice */
    void (*swap)(void);                       /* show the back buffer (vsync as dgl_ctx says) */
    void (*present_front)(void);              /* single-buffered or GL_FRONT drawing in a scaled mode */
    void (*draw_buffer)(int front);           /* glDrawBuffer: where drawing goes */

    /* Drawing: today's sink, in clip space */
    int  (*begin)(void);                      /* validate dgl_gl (its dirty groups); 0: draw nothing */
    void (*triangle)(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *provoking);
    void (*line)(const dgl_cvtx *a, const dgl_cvtx *b);
    void (*point)(const dgl_cvtx *a);
    void (*end)(void);
    int  (*fast)(GLenum mode, GLsizei count, const GLint *idx, GLint first);   /* optional (NULL) */
    void (*clear)(GLbitfield mask);           /* colour, depth (and stencil): dgl_gl's values, scissor, masks */
    int  (*finish)(void);                     /* everything drawn: 0, or -1 timed out (glFinish, before reads) */

    /* Textures */
    int  (*tex_ready)(dgl_texture *t);        /* made drawable from its levels: 0 drawable */
    void (*tex_sub)(dgl_texture *t, int level, int x, int y, int w, int h);    /* a rectangle changed */
    void (*tex_free)(dgl_texture *t);         /* its back-end part gone */
    void (*tex_reset)(void);                  /* every texture's back-end part gone (context made again) */

    /* Reading */
    int  (*read)(int front, int x, int y, int w, int h, unsigned char *rgba, int stride);  /* RGBA8, GL rows */

    /* Diagnostics */
    void (*stats)(dgl_be_stats *s);           /* DGL_STATS, DGL_FRAMES, dglGetStats */
    void (*crash)(void);                      /* the fault report's back-end lines */
    void (*device_info)(DGLDeviceInfo *d);    /* dglGetDeviceInfo */
} dgl_backend;
```

- **`dgl_caps`:**
  - the renderer's name (`GL_RENDERER`);
  - texture units (1, or 2), the largest texture, mipmap levels, paletted textures;
  - colour bits per channel, depth bits, stencil bits;
  - what is exact and what is approximated (fog per pixel or per vertex), for the conformance tolerances.
- **The GL layer reads `caps` and never the HAL's `mga` record.**
- **GLOS's GPU lock** (`DGL_GPU_ENTER`, `DGL_GPU_QUIET`) becomes the Matrox back end's own business inside its
  calls. The GL layer's two uses (`texture.c`'s conversions, `readpix.c`'s read) are inside `tex_ready`, `tex_sub`
  and `read`.

### Shared helpers (the GL layer's, for any back end)

- `dgl_clip_polygon` and `dgl_outcode` (`clip.c`, as today, with the back end's guard band as arguments).
- Fog's factor from the eye distance (`emit.c`'s `fog_factor`, which is GL's arithmetic, moved to the GL layer).
- Culling's winding test and polygon offset's depth slope, as functions on projected positions, for back ends that
  want them. The Matrox back end keeps its own, in its fixed point, so B1 changes no pixel.
- The array plans (`dgl_pl_v` and the others) that the optional `fast` reads, as today.

### What stays the GL layer's

- State and its dirty groups (`dgl_gl`), matrices, lighting, vertex arrays and buffers.
- Display lists: `dgl_sink.record` and `record_imm` stay lists.c's and are not the back end's.
- Texture objects and their GL formats: levels as RGBA8, palettes (`palette.c`), environments and combiners' GL
  state (`dgl_texenv`).
- Errors, `glGet`, extensions from `caps`.

## The census, file by file

Every HAL name the GL layer uses today, and where it goes. "Platform" means it stays shared.

### `src/gl/emit.c` (1,350 lines): the drawing back end today

- **Moves whole to `src/backend/mga/`.** Its functions become the back end's begin, triangle, line, point, end,
  fast and clear.
- **Its HAL names:**
  - the WARP: `warp_now`, `warp_since_cpu`, `warp_on`, `warp_form`, `warp_vertices`, `warp_tex`, `warp_tex_sizes`,
    `warp_reserve`, `warp_reserve_seq`, `warp_commit`, `warp_commit_seq`, `warp_select`, `warp_available`,
    `warp_stop`, `warp_use_dma`, `warp_use_agp`, `warp_agp_rate`, `warp_ring_bytes`, `warp_kick_bytes`,
    `warp_tris`, `warp_failures`;
  - the setup: `setup_triangle`, `setup_dwgctl`, `setup_invalidate`, `mga_svtx`, `mga_wvtx`, `mga_tri_ctx`,
    `MGA_S_*`;
  - texture state: `tex_emit`, `tex_emit_dual`, `tex_emit_combiner`, `tex_adjust_coords`, `tex_adjust_coords1`,
    `mga_texstate`, `TEXCTL2_*`;
  - registers: `MGA_WR32`, `fifo_reserve`, `ALPHACTRL_*`, `engine_set_clip`, `engine_set_maccess_flags`,
    `engine_fill`, `engine_fill_depth`, `engine_resets`;
  - the chip's record: `mga.has_dual_tex`, `has_alpha_blend`, `has_alpha_test`, `has_decalblend`;
  - rounding: `mga_ifloor`, `mga_irint_nearest` (fp.h: platform, used by the back end).
- **`glClear`, `glFlush`, `glFinish`:** stay GL entry points in the GL layer, calling `clear`, `present_front` and
  `finish`.
- **`dgl_emit_no_warp`** (the CPU setup in a GLOS window) becomes the Matrox back end's own choice in `open_glos`.

### `src/gl/texture.c` (1,010 lines) and `gl_tex.h`

| Today | Becomes |
|---|---|
| `dgl_texture`'s `resident`, `hwfmt`, `hw_levels`, `vram_off`, `level_off[]`, `vram_size`, `hw_w_log2`, `hw_h_log2`, `drawn`, `pal_used`, `pal_gen` | The Matrox back end's part (`struct mga_tex`), through a `void *be` in `dgl_texture`. `dirty` stays GL's: the levels changed |
| `dgl_texture_ready`, `write_level`, `alloc_vram`, `release_vram` | `tex_ready`, `tex_free` |
| `sub_iload` (`engine_iload_begin`, `_row`, `_end`), `sub_move` (`engine_vram_copy`, `engine_vram_move`, `engine_vram_copy_end`), `sub_in_place` (writes through `mga_fb`) | `tex_sub` |
| `dgl_texture_lut` (`engine_tlut_load`, `mga.has_tlut`) | Inside the Matrox back end's `begin` (a TW8 texture's palette before drawing) |
| `dgl_sync`, `dgl_sync_epoch`, `dgl_texture_busy` (`engine_sync`) | `finish`; the epochs are the Matrox back end's |
| `dgl_textures_reset(heap_start, heap_end)`, `dgl_white_texture` | `tex_reset`; the white texture is the Matrox back end's (fog's) |
| `mga_cmd_open` | The Matrox back end's |
| `mga.max_tex_size`, `mga.max_mip_levels`, `mga.has_dual_tex`, `mga.has_dstorg` | `caps` |
| `sys_time_us` | Platform |

- **Stays GL's in `texture.c`:** names, binding, parameters, environments, `glTexImage2D`'s checks and unpacking
  into the RGBA8 levels, `glGetTex*`.
- **`texconv.c`'s parts:**
  - the hardware formats (`DGL_TW8` and the rest, `dgl_hwfmt_for_class`, `dgl_pack_texel`, `dgl_pack_row`,
    `dgl_pack_grey565`) move to the Matrox back end;
  - the classes (`dgl_alpha_class`, `dgl_ifmt_class`, `dgl_apply_ifmt`, `dgl_to_rgba`) stay GL's.
- **`vram.c`** (the VRAM heap) moves to the Matrox back end.
- **`combine.c`:** `dgl_stage_word` and `dgl_env_needs_combiner` build the G400 combiner's words from GL's
  environment, so they move to the Matrox back end. The environment's GL state stays.

### `src/dgl/context.c` (673 lines)

| Today | Becomes |
|---|---|
| `dglInit`'s card part: `mga_map`, `mga_probe_vram`, `mga.vram_bytes`, `mga.fb_size`, `vbe_set_mode`, `vbe_set_zoom`, the VRAM layout (`front_off`, `back_off`, `z_off`, `heap_off`), `engine_init`, `engine_set_target`, `engine_fill`, `engine_fill_depth`, `engine_sync`, `dac_set_ramp`, `mga_fit_name`, `mga_mode_plan` | `open` |
| `init_glos` | `open_glos` |
| `swap_buffers`' card part: `vbe_set_display_start` (`show_start`), `present` (`engine_present`, `mga_surface`: scaled modes), `engine_vsync_wait`; `drain_with_hook` (`engine_idle`, `mga_mmio`) | `swap`; `present_front`; `finish` |
| `set_target`, `dgl_retarget`, `dgl_color_off`, `mga_target` | `draw_buffer`, and the back end's own |
| `setup_stats`, `MGA_PROF`'s card stages; `prof_micro`'s timed FIFO read (`MGA_RD8`) | `stats`, and the Matrox back end's profiling |
| `dglShutdown` | `close` |
| `prof_micro`'s `mga_ifloor`, `sys_time_us`, the wait hook, `dgl_yield_maybe` | Platform and GL layer: stays |

`dgl_context` keeps what is GL's: size, double buffering, depth bits, vsync, the draw and read buffer. Its VRAM
offsets and the scaled and zoomed display's fields move to the Matrox back end.

### `src/dgl/device.c` (251 lines)

| Today | Becomes |
|---|---|
| `dgl_discover`: `mga_find`, `mga_detect_emulator`, `MGA_FAMILY_G100` | `probe` |
| The mode list: `vbe_enumerate`, `vbe_all`, `vbe_mode`, `vbe_total_memory`, `mga_vbe_mode`, `mga_plan_from_list`, `MGA_PLAN_*`, `MGA_FIT_*`, `mga_pow2_pitch`, `dgl_pitch_for`, `dgl_known_vram`, `dgl_note_vram`, `vram_bytes` | `modes` (the Matrox rules: pitches, VRAM fits, scaled and zoomed modes) |
| `dglGetDeviceInfo`: `mga.name`, `device_id`, `revision`, `fb_phys`, `mmio_phys`, `iload_phys`, `fifo_depth`, `vram_bytes`, `engine_present` | `device_info` |

### `src/dgl/glos.c` (305 lines)

- **Today's API 1.x calls** (`glos_window`, `glos_gpu`, `glos_gpu_share`, `mga_mmio`, `mga_fb`, `vram_bytes`,
  `mga_chip_caps`, `MGA_FAMILY_*`, `engine_resync`, GLOS's VRAM blocks) are the Matrox back end's GLOS path, in
  `open_glos`, `swap` and the lock.
- **What is GLOS's and not the card's stays in the GL layer:**
  - asking whether GLOS wants the program in a window (`dgl_glos_want`, `DGL_FULLSCREEN`);
  - the window's modes (`dgl_glos_modes`).
- **The software back end's GLOS path is new:** API 2.0's DRAW SOFT (GLOS `docs/v1/api-2.md`), a window's pixels.
- **When the moves happen:**
  - GLOS's phase B moves this file's calls to API 2.0 at the same time;
  - doing B1 first gives that move one place to change (the plan's order).

### `src/gl/readpix.c` and `src/dgl/snap.c`

- **`glReadPixels`:** reads RGB565 through `mga_fb` after `dgl_sync`. It becomes `read`, which gives RGBA8; the GL
  layer then packs the program's format and alignment.
- **`glDrawBuffer` and `glReadBuffer`:** stay GL's, calling `draw_buffer`.
- **`snap.c`'s `dglSnapshot`:** reads through `read` too. Its PPM writing stays.
- **`copytex.c`:** reads through `glReadPixels` already, so it needs no change.

### `src/dgl/crash.c` and `src/dgl/ftrace.c`

- **The card's lines move to `crash`:**
  - the WARP's state (`warp_tris`, `warp_failures`, `warp_seqs`, `warp_seqs_t2`, `warp_ring_mode`, `warp_ring_kb`,
    `warp_dma_kicks`, `warp_wc_status`);
  - the engine's (`engine_timeouts`, `engine_resets`, `engine_fence_stuck`);
  - the MMIO trace (`mga_trace_dump`, `MGA_TRACE_MMIO`).
- **The teardown** (`engine_reset`, `warp_stop`, `vbe_set_text_mode`) moves to `close`.
- **The fault and exit hooks** (`sys_hook_faults`, `sys_hook_exit`, `sys_time_backs`) are platform.
- **`ftrace.c`'s per-frame counters** (`engine_*`, `warp_tris`, `setup_stats`) come from `stats`.

### The rest

| File | HAL names | Becomes |
|---|---|---|
| `src/gl/get.c` | `mga.name`, `mga.max_tex_size`, `mga.has_dual_tex`; colour bits 5/6/5, depth bits from `z_off` | `caps` |
| `src/gl/ext.c` | `mga.has_dual_tex` (`dgl_texture_units`) | `caps` |
| `src/gl/gl_state.h` | `mga/prof.h`, `MGA_DJGPP`, `FPU_ENTER` | Platform |
| `src/dgl/dgl.h` | `mga/hal.h`, `mga_mode_plan` in `dgl_context` | The plan moves to the Matrox back end; `dgl.h` includes the platform headers only |
| `src/dgl/log.c` | `serial_init`, `serial_enabled`, `serial_puts` | Platform |
| `src/gl/combine.c` | (TEXCTL in a comment) | With `dgl_stage_word`, to the Matrox back end |

After B1, a grep of `src/gl` and `src/dgl` for `mga/` finds only platform headers, which is B1's check beside the
pixels.

## Choosing a back end

- **`DGL_DEVICE`:**
  - `mga`, `soft`, later `tnt2`;
  - unset: the first whose `probe` succeeds, in the order mga, (tnt2, rage128), soft.
- **Under GLOS:**
  - the Matrox back end when GLOS gives the program the GPU (API 2.0's GPU);
  - otherwise the software back end in its window.
- **`dglInit`:**
  - logs the choice (`DGL-DEVICE soft`);
  - refuses only when no back end opens (the software one always can, given memory and a VBE mode or a GLOS window).
- **The public API stays source- and binary-compatible:**
  - `DGLConfig.color_bits` takes 8, 16 or 32 where the back end has them (the Matrox back end stays 16);
  - `DGLDeviceInfo` keeps its fields (the software back end gives `chip_name` "Software" and zeros for the card's
    addresses), and gains `backend` at its end;
  - `DGL_API_VERSION` becomes 0x0105.

## B1's checks

- **Pixels:** pixel for pixel the same: DOS-GL's conformance on the G200, G400 and G450 in Loop A; the games' frames
  (ClassiCube, GLQuake, Quake 2, Half-Life); GLOS's windowed suites.
- **Speed:** DOSBench's DOS-GL timings within noise.
  - The hot path gains no indirection: `vertex.c` already calls the sink through pointers, and `emit.c`'s inner
    functions stay static in the back end.
  - `begin`, `clear` and `tex_ready` become calls through the table, a few per draw.
- **The bench:** bench-g400 by the usual checks; the P4 (the silicon tier) once it is set up.
- **The grep above:** no Matrox header outside the back end.

## Questions, for the owner

1. **The seam for drawing: clip space (today's), or screen space (the plan's table)?** `docs/backends.md` said
   "triangles after clipping and projection", meaning the GL layer would clip and project for every back end.

   The census suggests clip space, as `dgl_sink` is today, for three reasons:
   - B1 changes no pixel by construction, because the Matrox clipping, projection and culling move as they are;
   - each back end projects to its own precision (the Matrox setup's fixed point; the software rasteriser's
     sub-pixel steps), and clips to its own guard band;
   - the Matrox fast path keeps its own transform.

   The shared clipper and helpers keep the duplication small. **Recommended: clip space**, with the plan's table
   amended.
2. **Stencil: in B2's first version, or later?** No program needs it (finding 5). In the software renderer it would
   cost a byte a pixel and a test in the inner loop. Its gains: PrBoom-plus's fix for missing textures, and Quake
   2's shadows without overlaps.

   **Recommended: later**, as a step after B2. The interface carries the state and `caps` says 0 bits until then.
   Stencil on the Matrox cards is a separate question: the HAL has none today.
