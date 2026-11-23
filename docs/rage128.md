# The ATI Rage 128 Pro

The third vendor after the S3 ViRGE/DX (`docs/virge.md`) and the Riva TNT2 (`docs/tnt2.md`), in the owner's order
of 2026-11-12. The card is in bench B (the P3) since 2026-11-20, the TNT2 out.

## The references and their licences

- **X.org's `xf86-video-r128`** (MIT): `r128_reg.h` for the register offsets and bits, `r128_driver.c` for the
  memory type's and the video BIOS's PLL table's readings, `r128_accel.c` for the 2D engine. Facts and the
  register sequences are taken from it; the code isn't copied.
- **Linux's DRM `r128` driver** (MIT, `drivers/gpu/drm/r128`): the command processor (CCE), its PIO mode and the
  order the 3D state is sent in; the microcode from linux-firmware (`r128/r128_cce.bin`, MIT per its `WHENCE`).
- **Mesa 7.x's r128 DRI driver** (MIT): the 3D context's values, the texture registers, the level order.
- Measurement on the card (the P3), as the TNT2's facts were.

## What R0 found (the P3, 2026-11-20, `RPROBE`)

`RPROBE` (`tests/r128/rprobe.c`) reads only, but for the PLL index (put back).

- **The chip:** `1002:5046`, a Rage 128 Pro PF (AGP 4x), revision 0, subsystem `0008:1002` (ATI's own board),
  IRQ 10, on bus 1 behind the AGP bridge.
- **Its windows:** BAR0 `d8000000` (prefetchable, 64 MB: two 32 MB apertures, APER_0 at `d8000000` and APER_1 at
  `da000000`), BAR1 I/O `cc00`, BAR2 the registers at `dfefc000` (16 KB; REG_APER_SIZE 8 KB, a second copy at
  `dfefe000`), the ROM BAR at `dfec0000` (off).
- **AGP:** capability 2.0 at `50`: 1x, 2x and 4x, sideband addressing, 32 requests; the BIOS leaves AGP off
  (command `00000200`). Power management at `5c`.
- **Memory:** CONFIG_MEMSIZE 16 MB; MEM_CNTL `b0004300`, bits 1-0 zero: 128-bit SDR SGRAM (X.org's reading).
- **Clocks:** the BIOS's PLL table (header `0110`, PLL block `0620`): a 29.50 MHz reference, reference divider 65
  (PPLL_REF_DIV reads `41`, the same), XCLK 120 MHz, the PLL from 125 to 400 MHz. The PLL registers `00`-`1f` are in
  the log for R1.
- **The engine:** idle (GUI_STAT `00000040`: FIFO room, nothing active); the CCE off (PM4_BUFFER_CNTL 0,
  PM4_STAT `c0`); interrupts off.
- **The BIOS:** 32 KB, ATI's signature at `31`, "Rage128 PRO GL P/N 113-60609-100"; VBE 2.0, 16 MB, 56 modes
  (OEM "ATI RAGE128", product "R128"), every one with its mode attributes and linear window in the log.

## R1: the register layer and the 2D engine (2026-11-21)

`hal/include/r128/`, `hal/src/r128/{chip,engine2d,cursor}.c` (`libr128.a`), after X.org's `xf86-video-r128`.

- **Modes:** the BIOS's linear modes (VBE `0x4000 | mode`) at 8, 15, 16 and 32 bits; then the 2D engine set up as
  `R128EngineInit` does. The display start is CRTC_OFFSET in bytes (a multiple of 8), the pitch CRTC_PITCH in
  pixels / 8.
- **2D by MMIO** (the CCE off): fills, copies in either direction, copies between surfaces by pitch-and-offset
  words (`((pitch_bytes / bytes / 8) << 21) | (offset >> 5)`), colour expansion from host data (rows padded with
  clear bits, drawn leave-alone at the padded width; opaque fills the background first), images by the CPU
  through the window.
- **Found on the P3:** surface pitches must be multiples of 16 bytes (8-bit surfaces of 328 and 520 bytes drew
  wrongly).
- **Cursor:** 64x64, AND and XOR rows interleaved 16 bytes each, as `r128_cursor.c`.
- **RTWOD** (from VTWOD): fills, copies, surfaces, expansion, images and 300 mixed operations byte-exact against
  `tests/hal/sw2d.c` at 8, 16 and 32 bits (22/22). **RFLIP** (from VFLIP) is written; the flip order is still to
  be watched.

## R2: the CCE and the 3D engine (2026-11-21)

- **The CCE in PIO mode** (`cce.c`, `cce_ucode.c`): ATI's microcode (linux-firmware's `r128/r128_cce.bin`, MIT,
  Copyright 2000 AMD, its notice kept) loaded through `PM4_MICROCODE_ADDR`/`DATAH`/`DATAL`; `PM4_BUFFER_CNTL` =
  192PIO | NOUPDATE, `PM4_MICRO_CNTL` = free run. The CPU writes packet dwords to `PM4_FIFO_DATA_EVEN`/`ODD`
  (0x1000/0x1004) in pairs, a type-2 packet padding an odd count: no ring, no GART. It started first time.
  `r128_cce_init` takes other microcode from its caller (the device reads an `r128_cce.bin` beside the program,
  R3).
- **2D with the CCE on:** the chip then ignores MMIO writes to 0x1400-0x1FFF, so the layer sends the 2D engine's
  registers as type-0 packets. RTWOD `cce`: byte-exact at 8, 16 and 32 bits (25/25).
- **The 3D context** (`tri.c`): Mesa 7.x's starting values (`r128DDInitState`), a linear depth buffer (no
  `Z_TILE`: the CPU reads it as Glide's LFB does), sent in the DRM's order (`r128_emit_state`): the core
  (`SCALE_3D_CNTL`) first, then the context's 12 registers from `DST_PITCH_OFFSET_C`, `SETUP_CNTL` and
  `PM4_VC_FPU_SETUP`, the masks, the window, the constant colour, the first texture unit's block (control,
  combine, size and pitch, eleven level offsets).
- **Triangles:** type-3 `3D_RNDR_GEN_PRIM` with the vertices inline (x, y, z, 1/w, diffuse ARGB, specular with
  the fog factor in its top byte, s and t), as X.org's `R128CCEComposite` sends them.
- **Found on the P3:**
  - Pixel centres are at half pixels (OpenGL's); coverage exact with OpenGL's top-left rule (RTRI0, RTRI32).
  - `SCALE_3D_CNTL` after the context undoes `MISC_3D_STATE_CNTL_REG`'s blend; left out, the texel's alpha never
    reaches the blend (it carries `TEX_MAP_ALPHA_IN_TEXTURE`). Sent first, as the DRM does, both work.
  - The 2D engine shares the 3D context's registers: a triangle right after a 2D fill drew nothing. Any 2D write
    marks the 3D context stale, and the next triangle sends it again.
  - `PRIM_TEX_n_OFFSET_C` slot n holds the level of 2^n texels a side, the **smallest level in slot 0** (Mesa's
    `r128UploadTexImages` order); with mipmapping off every slot holds the base (RMIP).
  - The level is chosen by the largest single texel derivative (RTRI's `lodmode 3`: 957 of 47,007 pixels a
    step or more off in `tex-mip`, against 2,304 for OpenGL's footprint and 34,953 for the TNT2's) at Mesa's
    bias byte 63.
  - Z24S8: the depth in bits 23-0, the stencil in 31-24 (RTRI32).
- **Tests:**
  - **RTRI0:** three Gouraud triangles at three depths: coverage exact, colours within one 565 step.
  - **RTRI** (from NTRI, VTRI's scenes) at 640x480 RGB565 with Z16: random, mesh, zbuf, blend, tex-nearest,
    tex-linear, tex-persp, tex-persp-linear, tex-mip, tex-modulate, tex-8888, tex-alpha and tex-fog, all within
    NTRI's tolerances against OpenGL's rules.
  - **RTRI32** (from NTRI32) at ARGB8888 with Z24S8: coverage exact, the corners' colours, LEQUAL with Z writes
    exact, alpha 128 over blue within one step.
  - **RMIP:** which level the chip samples, at a sweep of bias bytes.

## R3: the device (2026-11-21)

`hal/src/rg/r128.c` (`rg_r128_device`, "r128"; its open context for the drawing back ends in `rg/r128_dev.h`), on
the TNT2 device's pattern. The device list is now mga, virge, tnt2, r128, then vbe and mem by name.

- **Modes:** the BIOS's linear modes at 16 (R5G6B5) and 32 bits, the ones the 3D engine draws.
- **Pitch:** a requested pitch (Glide's 1024 pixels) is written to the CRTC, and every surface and the engine's
  default pitch use it; rows must be multiples of 8 pixels and 16 bytes.
- **Memory:** colour A, colour B, then a linear depth buffer (Z16 with 16-bit colour, Z24S8 with 32, at the colour's
  pitch in pixels), each 4 KB aligned. Textures get the rest, up to the top of the 16 MB.
- **The CCE** starts at open, with the layer's embedded microcode, or with `R128_CCE.BIN` (linux-firmware's
  `r128/r128_cce.bin`, big-endian, 2048 bytes) when it is in the program's folder, else in the current one. Using
  the file always prints a `DGL-R128` line. The Open Watcom build (Glide's) has no C library, so it always uses
  the embedded copy. Close returns to text mode with the CCE stopped.
- **Swap:** it waits for the engine, then writes the display start before the retrace wait (the TNT2's order;
  RFLIP's watch is owed). With PIO feeding, the chip is never more than its 192-entry FIFO behind the CPU, so the
  wait is short.
- **RGDEV** on the P3 passes: `dev=r128`, chosen by probe, `pitch=1024`, and with `R128_CCE.BIN` beside it.
- **DOS-GL** still draws through the VBE software renderer on this card until R4 gives it a back end;
  `DGL_DEVICE=r128` waits for R4 too.

## R4: DOS-GL's back end (2026-11-21)

`gl/src/backend/r128/backend.c` (`dgl_r128_backend`), on the TNT2 back end's pattern for the geometry: clip-space
vertices clipped (a guard band of 2), projected, culled and offset on the CPU, then sent to the chip's own set-up as
window-space vertices; a fog factor a vertex (split where it changes by more than a tenth); lines and points as
quads; GL_POLYGON whole as one triangle fan (`r128_poly`).

- **State:** the texture environments as Mesa's r128 driver maps them, by the texture's family (RGBA, RGB, alpha,
  intensity), GL_BLEND on the Pro's constant-colour blend; GL_COMBINE_ARB onto the chip's one function a unit
  (others drawn as MODULATE, said once); both texture units (ARB_multitexture: the second with its own
  coordinates, the texture cache split); blending, the alpha test, depth, stencil with Z24S8, the colour mask
  (PLANE_3D_MASK_C), the scissor (the context's), dithering.
- **Textures:** converted to ARGB8888, RGB565, ARGB1555 or ARGB4444 and written by the CPU into the device's heap
  (vram.c's allocator), each level after the last, 32-byte aligned; made again into a new block, the old one
  retired until a sync; a rectangle of a single-level texture (lightmaps) written in place after a sync.
- **Found on the P3:** the CPU writes VRAM the chip will read only with the engine idle. Written while a clear's fill
  was running, a texture's last 32-byte line was still old when the triangles after it read it (conformance t14:
  the right half of texel row 15 black in every quad), though the CPU read it back right, and neither draining the
  CPU's write-combining buffers nor a read-back of the last dword helped; a sync before the writes (or a 500 µs wait
  after them) did. `r128.h`'s rule, as RTRI followed from the start.
- **Conformance on the P3** (`make conform-bench PC=bench-p3`): all 33 tests that apply pass (t33 and t34 are the
  software renderer's only), the worst t08 with 0.05% of its pixels off; the multitexture (t20-t22, t30) and
  combine (t25) tests among them.
- **GLQuake on the P3** (QDOSDGL, timedemo demo1, `-fixedtime`, vsync off; the 451 MHz Pentium III), the first back
  end, untuned: 34.97 fps at 640x480 and 48.50 at 320x200 in two passes; 36.88 and 47.36 with `-mtex`. (The TNT2's
  first back end: 21.8 at 640x480; tuned, 77.4 and 104.5.) Windows 98's numbers on this card are still to be taken.
  On GLOS, a RUN.BAT line longer than COMMAND.COM's 127 characters doesn't run ("GLOS can't run it here."): the
  timedemo's `-mtex` lines needed the program renamed short.

### R4's speed (2026-11-21)

The owner's aim: about the TNT2 M64's speed on the same P3 (GLQuake 77.4 and 104.5 fps at 640x480 and 320x200).

- **Where the time goes** (`PROF=1`, k cycles a frame, GLQuake at 320x200 with `-mtex`): the game 1,615, the
  transform 954, the projection 906, validation 408, textures 660, the swap 294, all at or under the TNT2's; but
  **sending to the chip 2,580 against the TNT2's 290**. In PIO each packet dword is an uncached write over the bus.
- **RPIO** (`tests/r128/rpio.c`): on the P3 such a write costs 138 CPU cycles (0.3 µs; the ALi chipset), 183 through
  the layer at first (a PM4_STAT read and a clock read every 32 dwords), 144 now. The CCE never made the CPU
  wait: the bus is the limit.
- **The cuts** (2026-11-21): the FIFO's room read once a batch, the clock only while waiting; the context sent
  by the groups that changed (a texture change was 75 dwords, now 28: the context block goes with any texture
  block, as Mesa sends them); no fog dword without fog, no s and t untextured. GLQuake: 38.72 and 44.12 fps at
  640x480 (two passes, `-mtex`), 57.60 and 60.58 at 320x200 (from 34.97, 36.88, 48.50, 47.36). With GLOS's `MTRR
  LFB ON` (safe here: the registers have a BAR of their own) texture writes take a third less time.
- **Found on the P3:** after the CPU writes a texture, the pixel cache's flush (PC_GUI_CTLSTAT) must come in the
  stream as well as TEX_CNTL_C's TEX_CACHE_FLUSH; without it every texture written after the first drew nothing
  (conformance t06).
- **The bus-master ring doesn't fetch** (`tests/r128/rringp.c`): with the ring at a VRAM offset; or in
  conventional memory through the chip's PCI GART (PCI_GART_PAGE at a 32 KB table, 32 KB aligned; BM_CHUNK_0_VAL's
  force-to-PCI bits as X.org sets them for AGP boards; bus mastering on in BUS_CNTL, the card and the AGP bridge),
  the CCE goes busy at the first write pointer and its read pointer never moves. PM4_BUFFER_OFFSET keeps 25 bits:
  offsets into the GART's space only. ATI's register guide (RAGE 128 PRO RRG, bitsavers) doesn't describe the
  CCE's own registers.
- **The next step: bursts.** The FIFO takes dwords anywhere in 0x1000-0x13FF (RPIO), in address order within a
  write (RTRI `burst`: all cases pass through it), and the chip's second register aperture
  (CONFIG_REG_1_BASE, 0xDFEFE000 on the P3) is there to be given another cache type. Written through a
  write-combining mapping of its FIFO page, a 32-byte line goes as one burst. `r128_cce_burst_window` is the path;
  it waits for a way to make that one 4 KB page write-combining under GLOS (asked of GLOS, 2026-11-21).

### R4's speed, part 2: bursts and the ring (2026-11-21, evening)

- **Bursts through a write-combining FIFO page** (GLOS 2.11's DRAW CACHE for one page of a display card's BAR,
  `MTRR PAGE`): the second register aperture's FIFO page (0xDFEFF000 on the P3) write-combining, packets as whole
  32-byte lines in ascending addresses, a locked OR after each. RPIO: 39 cycles a dword (144 by PIO), RTRI's scenes
  all pass. **But GLQuake froze the P3 hard** in a normal GLOS session: an interrupt in the middle of a line flushes
  the part written as pieces in no set order, and the FIFO takes dwords as they come. In a normal session CLI only
  sets GLOS's virtual flag (~1,400 cycles a trap) and the kernel takes every interrupt anyway (its clock, the RTC
  at 1024 Hz, the NIC). So bursts are used **only at IOPL 3** (GLOS's direct-mode sessions, `glos run --direct`;
  `tools/bench/run.py --direct`), with a real CLI before each line and STI after its locked OR: 43.5 cycles a dword,
  GLQuake stable. Elsewhere PIO, as before (`r128_cce_burst_wc` refuses, -2).
- **GLQuake with bursts** (direct sessions): 44.60 and 52.43 fps at 640x480 (two passes, `-mtex`), 77.10 and 80.50
  at 320x200 (PIO in the same sessions: 60.47 at 320 `-mtex`, as in normal ones). Against the TNT2 M64's 73.7,
  77.4, 104.5 and 96.9.
- **What's left is overlap.** The timers at 640x480: sending 3,960k cycles a frame against 1,110k at 320x200, for
  the same geometry: the CPU waits on the chip's drawing, which can never be more than the 192-entry FIFO behind. A
  frame costs the CPU's time plus the chip's, not the larger. Tried: TEX_CACHE_FLUSH only after the CPU wrote
  textures (it was in every context block: no change in speed, kept); a tiled depth buffer as Mesa's (Z_TILE, the
  2D clears with DST_TILE): +2 to 4%, conformance all 33 with it (2026-11-22), so DOS-GL's default
  (`DGL_R128_ZTILE=0`: linear). Glide keeps a linear Z16: games read it through the LFB.
- **The bus-master ring fetches** (RRINGP on the P3 after a power cycle): the ring in conventional memory, the chip's
  own PCI GART (PCI_GART_PAGE: a 32 KB-aligned table of page addresses, bit 0 clear), BM_CHUNK_0_VAL's force-to-PCI
  bits (21-23; X.org's R128DRIPciInit for AGP boards: without them no fetch), bus mastering on (BUS_CNTL bit 6
  clear). PM4_BUFFER_OFFSET keeps 25 bits: an offset in the GART's space. **A ring at a VRAM offset never fetches
  and leaves the chip's bus mastering wedged until a power cycle**, an engine reset doesn't clear it: the first
  failures were that. The CCE fetches qwords: the write index must be even (padded with a type-2).
  `r128_cce_init_ring` builds that ring (a first-fetch check, PIO on failure).
- **The ring, working (2026-11-23):** the CCE fetches the ring in blocks and leaves a partial one at the write
  index unfetched until more comes; a flush must mark the download done (DL_DONE, bit 31 of the write pointer, as
  the DRM's r128_do_cce_flush), or an idle wait never ends: RTRI ring's tex-linear stopped 4 dwords short of its
  last triangle. A full ring is waited for while its read index moves (a stall is only no progress for half a
  second). With that: RTRI's 17 scenes at 64 and 256 KB, RTWOD's 25, RGDEV, both conformance suites (33 and 26 of
  27) pass through it, and **the device uses it by default** (`DGL_R128_RING=0`: PIO), in any GLOS session.
- **GLQuake with the ring:** 56.00 and **65.63** fps at 640x480 (two passes, `-mtex`), **92.88** and **92.26** at
  320x200, against the TNT2 M64's 73.7, 77.4, 104.5 and 96.9: 85-95% of it with `-mtex`. (PIO: 38.72, 44.12, 57.60,
  60.58; bursts in direct sessions: 44.60, 52.43, 77.10, 80.50.)
- **Lightmaps through the engine** (`r128_upload`, the DRM's r128_cce_dispatch_blit): a rectangle of texels
  goes as a CNTL_HOSTDATA_BLT packet in the stream (the pixel cache flushed and made read-invalid before, flushed
  after, the texture cache at the next draw), so no sync: the one before each rectangle drained the ring, 93M cycles
  a second at 640x480 `-mtex` (a fifth of the CPU). GLQuake: **61.43 and 72.49** fps at 640x480 (two passes,
  `-mtex`), **102.43 and 98.94** at 320x200: 83%, 94%, 98% and 102% of the TNT2 M64's 73.7, 77.4, 104.5 and 96.9.
  Its frames 200, 400 and 600 at 640x480 `-mtex` identical to the PIO path's with synced writes; conformance all 33
  and Glide 26/27 with it.
- **Flips in the command stream** (2026-11-23; below, under R5): the swap no longer drains the engine, with or
  without the retrace. GLQuake (`DGL_VSYNC=0`): **62.81 and 74.44** fps at 640x480 (two passes, `-mtex`), **105.71
  and 102.24** at 320x200: 85%, 96%, 101% and 106% of the TNT2 M64's.
- **Still owed:** textures made again whole (`prepare`) still sync first.

## R5: Glide's back end (2026-11-21)

`glide/src/backend/r128/r128.c` (`mg_r128_backend`), on the seam beside the Matrox, TNT2 and ViRGE back ends (the
branch is based on the ViRGE's, `virge5`, for `glide/src/backend/share.c`); probed after them.

- **The window:** the BIOS's 16-bit mode at a 1024-pixel pitch (the Voodoo's 2048-byte stride), double buffered,
  Z16 linear at the same pitch; clears by the 2D engine; the start written before the retrace wait; the clip
  window as the context's scissor.
- **Triangles:** the Voodoo's level and table fog per piece (`mg_split`); the combines on the chip's combiner (one
  function a unit over the vertex colour and the texel): a combine without the texture worked out at the vertices;
  with it the texture, the texture modulated by a factor at the vertices, texture + local, the lerp by the
  texel's alpha (BLEND_TEXTURE) or by an iterated factor (BLEND_VERTEX), (1 - texture) * local; and texture *
  factor + local with the local added by the **specular colour after the combine** (SPEC_LIGHT_ENABLE: t13's cells
  8, 10, 14, 15 exact with it). Depth as the Matrox cards keep it (W: a linear 1/w in Z16), the alpha test,
  blending, dithering, fog by the specular's alpha, chroma key by the core's keyed variants and the alpha test.
- **Textures** in the chip's linear layout, one level a triangle, written with the engine idle.
- **Gamma** by the DAC's palette: in 16-bit modes each component is looked up there (X.org's R128LoadPalette),
  8-bit entries (DAC_8BIT_EN); put back at close.
- **On the P3:** Glide conformance 26 of 27 (`glide/tools/conform/run.py check --bench bench-p3`), with the
  manifest's r128 class as the TNT2's: t07 (W-buffering in Z16) and t13's cells 12 (a subtraction) and 13 (the
  constant blended by the texel's alpha) approximated; t22 (gamma) captures the screen, which the P3 can't: the
  owner's eyes. Replays against the emulated Voodoo at 40 levels and 5%: GTA frames 60, 150, 240, 295 with no
  pixel past it; Screamer Rally 60, 150, 250, 300, 340, 380 with at most 0.22%.
- **The live games watched** (2026-11-23): t22's gamma as described, GTA right and smooth, Screamer Rally right
  but slow (about 20 fps, as on the TNT2 the owner guessed).

### R5's speed (2026-11-23)

Screamer Rally's call trace (`sr-389.bin`) replayed on the P3 with the profiling OVL (`make runtime-prof`), with
the Matrox-only stages borrowed for the back end's parts while measuring. A race frame: ~905 triangles, ~194
texture sources, ~139 palette loads, 6 LFB locks.

- **A palette compared at every triangle** (shared core, `texmgr.c`): a palettised texture is converted again only
  when the palette differs from its levels'; after each palette load (a new generation) the bind compared the 1 KB
  palette, found it equal, and kept the old generation, so the next triangle compared it again: ~4,100 cycles a
  triangle with Open Watcom's byte-at-a-time memcmp, half of the drawing. The same palette under a new generation is
  now remembered as it, compared a dword at a time. The TNT2 paid it too (its SR profile: ~6,800 cycles a triangle).
- **Texels by the CPU into uncached VRAM:** ~140 cycles a 16-bit store, ~20 ms a 256x256 level, after a full sync.
  Glide's `tex_write` now goes through `r128_upload` in the stream (`mg_backend.tex_ordered`: the core's sync
  skipped), rows under 8 texels by the CPU after a sync: ~10x less.
- **Conversion** (shared core, `texconv.c`): a call and a switch a texel; 8-bit texels now through a table of their
  256 values, the format switch and the padding's index outside the inner loops, bit for bit the same (a host check
  of old against new over 463,632 cases: every format, sizes 1-256, padding, clamps, variants, forced formats).
- **The state sent at every triangle:** `validate` ran before each, marking the 3D state dirty, and the emit
  compared every group (Open Watcom's memcmp again) to send nothing ~670 times a frame. Words are now marked only
  when they change (`SETW`), groups compared a dword at a time.
- **Flips in the command stream** (`r128_flip_queue`, the DRM's r128_cce_dispatch_flip): CRTC_OFFSET written in
  the stream, then WAIT_UNTIL with EVENT_CRTC_OFFSET (the register guide: the CCE stalls until the last start
  written is being shown; CRTC_OFFSET_FLIP_CNTL 0 takes it at the vertical blank), then a sequence number into
  GUI_SCRATCH_REG0. The CPU doesn't wait for the retrace: the drawing queued after the flip reaches the buffer that
  was on screen only once it no longer is. One flip in flight (the one before shown first). RFLIP's third phase
  (`RFLIP ring`): 480 flips in 8 s, each seen ~8.7 ms after queuing, none early; the owner saw it steady. Glide's
  swap and the DOS-GL device's use it (without the retrace: the start alone, no wait, no drain).
- **Together:** Glide's part of a race frame from 30+ ms to ~10 ms, frames unchanged (the replay's CRCs at 150, 240
  and 380). Live, watched (2026-11-23): Screamer Rally "faster, looks right" (4,000 frames in about 2½ minutes with
  loading and menus, ~17-20 fps before); GTA "as smooth as the TNT2".
- **GTA's frame is the LFB:** ~20,000 pixels a frame through the shadow's write-back, the same order as on the TNT2.
  Its locks follow the swap, so they now wait for the flip instead. The TNT2's frame buffer was write-combining
  (GLOS's `write_combining = auto`), the Rage 128's uncached: GLOS adds 1002:5046 to `auto` (its registers are in a
  BAR of their own, unlike the Rage Pro's), on its phase-h branch. Until then `MTRR LFB ON` by hand. GLOS's DRAW
  CACHE takes 4-64 KB of card memory, so a program can't ask for the whole frame buffer itself.
- **DOS/4GW programs didn't find GLOS:** DOS/4GW answers DPMI 0A00h itself (its own name) and passes no other on,
  so `hal/port/ow_dos4g.c` never had GLOS's entry (Matrox Glide's `wc=-2/-1`: its WARP ring never write-combining
  under GLOS). It now tries INT 2Fh AX=168Ah after 0A00h, which GLOS also answers: GLOSFIND under DOS/4GW on the P3
  finds it (`found=1 err=3`). Matrox Glide on bench-g400 owes a run with its ring write-combining before a merge.
- **Glide's forced exit** (`exit_after`) mid-race left Screamer Rally's own interrupt handlers (its sound) in place:
  DOS/4GW faulted while unloading (page fault at 2:000BC101, "deinitializing twice") and the session hung. No
  `MGL-EXC`: Glide had closed and unhooked by then. An exit at frame 20 (the TNT2's) was clean. The P3's game
  settings (`C:\TEST\GAMES\*\MGAGLIDE.CFG`) now keep only `stats=200` for watched runs.

### Bench A: the same card on a 440BX (2026-11-23, night)

The owner moved the card from the P3 into bench A (the IBM 300PL, Pentium II, 440BX; the G400 out).

- **The ring fetches there too** (RRINGP: the PCI GART path, as on the P3's ALi): RPIO's ring cases 20.7 and 15.1
  cycles a dword, RTRI's and RTWOD's scenes through the ring 42 of 42, RGDEV, Glide conformance 26 + t22.
- **CPU-written texels stayed in the chip's pixel cache** (DOS-GL's conformance t17, 0.8% of the frame, a little
  different each run): each texture's last rows drew 32-byte pieces of what the memory held before, from the ring
  only. PIO passed; so did reading the whole texture back after writing it. The chip holds the host's last
  written lines in its pixel cache; with PIO every packet dword is a host access and pushes them out, from a ring
  nothing follows the texels. `r128_vram_written` now flushes it (PC_NGUI_CTLSTAT's flush bits, waited for: the
  DRM's r128_do_pixcache_flush). t14's old cure (a sync before the writes) worked for the same reason: the sync
  ends in that flush. The P3's timing hid it. With the fix: DOS-GL 35 of 35 on bench A.
- **A ring's idle test wants the CCE's own FIFO empty too** (the DRM's r128_do_cce_idle: the ring's indices equal,
  PM4_STAT's count at 192, not busy): what the CCE has fetched waits in that FIFO. Found while looking for the
  above (it did not cure it); `r128_cce_idle_wait` and `r128_cce_drained` now test it.
- **GLQuake on bench A** (`DGL_VSYNC=0`, GLOS's `MTRR LFB ON` by hand): 62.20 and 70.61 fps at 640x480 (two passes,
  `-mtex`), 97.55 and 94.02 at 320x200.
- **The shared Glide changes leave the Matrox cards as they were:** REGCRC builds of the branch's base (virge5)
  and the branch, in Loop A: the 26 compared conformance tests on the G100, G200, G400 and G450 and the GTA and
  Screamer Rally replays on the G200, G400 and G450, every register CRC line and frame the same.

## Next

R5's live games, R6 86Box's Rage 128 in
Loop A, R7 the docs (the plan approved 2026-11-21).
