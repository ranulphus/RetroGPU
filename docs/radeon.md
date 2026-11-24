# The ATI Radeon 7000 / VE (RV100)

The owner's order (2026-11-12): the ViRGE/DX, the TNT2, the Rage 128 Pro, then the Radeon. The card went into the
P3 (bench B) on 2026-11-23, and the owner asked for a start that night. The branch is `radeon`, on the Rage 128's
(`r128`, itself on the ViRGE's `virge5`): the Rage 128's layer is the pattern, and much of it the ancestor.

## The references and their licences

Read for register offsets, sequences and packet layouts; all three carry MIT notices:

- **X.org's `xf86-video-ati` 6.14.6** (Copyright ATI and VA Linux): `radeon_reg.h` (every register), `radeon_accel.c`
  and `radeon_accelfuncs.c` (the 2D engine), `radeon_dri.c` (the command processor's set-up, the PCI GART),
  `radeon_exa_render.c` (a 3D state and vertices, by the command processor and by plain register writes).
- **Linux 2.6.29's DRM `radeon`** (Copyright Precision Insight, VA Linux, AMD): `radeon_cp.c` (microcode load, start,
  idle, stop, reset, the ring, the PCI GART), `radeon_state.c` (packets, the flip, texture uploads),
  `radeon_drv.h`.
- **Mesa 7.0.3's `radeon` DRI driver** (Copyright ATI and VA Linux): state, textures, the vertex layout without TCL
  (`radeon_swtcl.c`).

**The command processor's microcode** is `R100_cp_microcode` in Linux 2.6.29's `radeon_microcode.h`: 256 pairs,
under an MIT notice (Copyright 2007 Advanced Micro Devices), loaded on the R100, RV100, RV200, RS100 and RS200
alike. (linux-firmware's later `R100_cp.bin` is the same words under a binary-only licence: not that one.)
GPL code is not used. 86Box has no Radeon: the card is the only judge.

## What the references say (to be checked on the card)

- **The 2D engine is the Rage 128's** grown up (DP_GUI_MASTER_CNTL, DST_Y_X, the host-data blits): the Rage 128
  layer's `engine2d.c` is the starting point.
- **3D without the command processor:** X.org's EXA code draws through plain register writes too: the state's
  registers, then SE_VF_CNTL (the primitive and vertex count) and each vertex's words to SE_PORT_DATA0. So a first
  triangle needs no microcode (the Rage 128 could not). Each word is then a bus write, as the Rage 128's PIO.
- **The command processor (CP):** microcode by CP_ME_RAM_ADDR and DATAH/DATAL; CP_CSQ_CNTL picks the mode (off,
  PIO, a bus-master ring, with or without indirect buffers); the ring at CP_RB_BASE in the card's address space,
  its size in CP_RB_CNTL, the write index in CP_RB_WPTR, the read index also written by the chip into memory
  (CP_RB_RPTR_ADDR, a bus address) with the scratch registers behind it (SCRATCH_ADDR). **A PCI GART of the chip's
  own** (AIC_CNTL's translate bit, AIC_PT_BASE a table of page addresses, AIC_LO_ADDR and AIC_HI_ADDR the range it
  answers for): the route the Rage 128's ring took from conventional memory.
- **Idle** (radeon_do_wait_for_idle): RBBM_STATUS's FIFO count at 64 and its active bit clear, then the destination
  cache flushed (RB3D_DSTCACHE_CTLSTAT, waited for). In the stream: the caches purged and WAIT_UNTIL's idle bits.
  **Reset** (radeon_do_engine_reset): RBBM_SOFT_RESET's CP, HI, SE, RE, PP, E2 and RB bits, the memory clocks
  forced on around it.
- **The chip:** three texture units; textures I8, AI88, RGB332, ARGB1555, RGB565, ARGB4444, ARGB8888 (and DXT);
  colour buffers 1555, 565, 4444, 8888; depth 16 and 24 bits (integer, and 24-bit floating) with stencil; a
  combiner a unit with add, subtract, signed add, blend and dot3 over the texel, the vertex colours, a constant and
  the unit before; vertex fog or a fog table, from depth or from either colour's alpha; flips in the stream
  (WAIT_UNTIL's CRTC_PFLIP), host-data blits for texels. The RV100 has no transform and lighting engine: none is
  wanted (DOS-GL and Glide send screen vertices).
- **An erratum to respect:** on the RV100, RS100 and RS200, 5 ms must pass after every PLL register access "or the
  chip could hang on a subsequent access" (X.org's RADEONPllErrataAfterData, CHIP_ERRATA_PLL_DELAY).

## What RD0 found (the P3, 2026-11-23, `DPROBE`)

`tests/radeon/dprobe.c`, reads only (the clock index put back, 5 ms after each PLL access).

- **PCI 1002:5159** (RV100 QY), revision 00, subsystem 1787:0F02, on the AGP bus (01:00.0): frame buffer window
  d0000000 (64 MB, prefetchable), I/O cc00, registers dfef0000 (64 KB), ROM dfec0000; AGP 2.0 (status 2f000203:
  1x and 2x, side-band addressing), not enabled.
- **64 MB of DDR** (CONFIG_MEMSIZE 04000000; MEM_CNTL 00002e00, MEM_SDRAM_MODE_REG 50320032); MC_FB_LOCATION
  ffff0000, MC_AGP_LOCATION 003fffc0, DISPLAY_BASE_ADDR 0; AIC_CNTL 00000002; BUS_CNTL 5133a3b0 (bus mastering
  not disabled).
- **The engine idle, the CP off:** RBBM_STATUS 00000140 (64 FIFO entries free, not active), CP_CSQ_CNTL 0,
  CP_CSQ_STAT 00008080, RB3D_DSTCACHE_CTLSTAT 0.
- **The BIOS:** "CONNECT 3D RV100 DDR BIOS", 64 KB, ATI's signature; VBE 2.0, OEM "ATI RADEON III", product
  "R100", 64 MB, 56 modes: 320x200 to at least 640x480 at 8, 15, 16, 24 and 32 bits with the linear frame buffer at
  d0000000 (the list's higher modes in the job's log).
- **PLLs** (15 read, the chip answering after each): PPLL_REF_DIV 3c, PPLL_DIV_0-3 0007029f, 000702f3, 000301c0,
  000301f7, M_SPLL_REF_FB_DIV 014a4a0c, MCLK_CNTL 0a350012.

## RD1: the register layer and the 2D engine (2026-11-24)

The plan (RD1 to RD7) was approved on 2026-11-23. `hal/include/radeon/`, `hal/src/radeon/{chip,engine2d,cursor}.c`,
made from the Rage 128's layer.

- **The 2D engine's registers are the Rage 128's at the same offsets** (66 of the 66 names the two headers share
  have equal values). What differs: the status (RBBM_STATUS: 64 FIFO entries, bit 31 active), the reset
  (RBBM_SOFT_RESET's engine bits and HOST_PATH_CNTL's, X.org's RADEONEngineReset without its PLL accesses), the
  caches (RB3D_DSTCACHE_CTLSTAT and the 2D engine's RB2D_DSTCACHE_CTLSTAT; DSTCACHE_CTLSTAT and WAIT_UNTIL's
  2D-idle-and-clean in the queue before each operation changes the 2D state, as X.org's set-ups do), and the
  pitch-and-offset word: the pitch in 64-byte units at bit 22, the offset in 1 KB units from the frame buffer's
  place in the chip's own address space (MC_FB_LOCATION's start: 0 here). So surfaces' pitches are multiples of 64
  bytes; what is below 1 KB of an offset goes into x.
- **`DTWOD`** (RTWOD's twin, against `tests/hal/sw2d.c`): at 16 bits with 20 mixed operations every case byte-exact
  first time (fills, copies, copies between surfaces, colour expansion, images). The full run fails a few cases by
  a few pixels, and every one of those traces to the card's memory, below.

### This card's memory is faulty (the P3's RV100, 2026-11-24)

`DMEM` (`tests/radeon/dmem.c`): the BIOS's 640x480x16 mode and nothing else (no engine set-up, no reset; after a
fresh `glos reboot` too), a pattern written through the frame buffer window and read back.

- **Megabytes 2 to 15 and 34 to 47 of the 64 read back wrong:** 8% of the dwords in megabyte 2, rising to 24%, then
  18% to megabyte 15; the same again from 34 to 47. Megabytes 0-1, 16-33 and 48-63: none wrong in two whole
  passes, and none over 32 seconds. Stable from read to read: not decay.
- **Only 8 data bits are ever wrong:** bits 18, 19, 21, 22 and 24 to 27 of every second dword (mask 0f6c0000 of the
  even dwords; the odd ones never). One value everywhere (FFFFFFFF, 0, AAAA5555) reads back right.
- **Those bits come from 128 KB further on:** of 60 wrong dwords sampled, all 60 hold, in those 8 bits, what was
  written 32,768 dwords later. Two addresses 128 KB apart share the cells for those bits: an address line to the
  memory chip behind them that doesn't reach it (or a fault inside the chip). No register setting does that to 8
  bits of 64.
- **The chip's own engine sees it too:** a bad megabyte copied to a clean one by the 2D engine carries the same wrong
  dwords; clean data copied into a bad megabyte comes back wrong in the same places.
- **And a little of it below 1 MB under repeated writes:** DTWOD's remaining failures are the same 2 to 9 pixels
  each run (8 bits: the screen at 505-507, 274-280; 16 bits: at 410-411, 241-257), bits lost, in memory (read
  again four ways: the same), 128 KB apart from each other.
- **So:** a hardware fault on this card, there from power-on, nothing of the layer's. In Windows it would show
  as wrong pixels in textures and in high-resolution or deep-colour desktops (640x480 at 16 bits lives below 1 MB).

## Next

RD1's rest (DFLIP watched, the cursor with the device), then RD2: the owner's word first on this card's memory.

## RD2, first part: triangles by plain register writes (2026-11-24)

`hal/src/radeon/tri.c` writes the 3D state's registers when they change and sends each triangle as `SE_VF_CNTL`
followed by its vertices' words to `SE_PORT_DATA0`. There is no command processor and no microcode in this path.

- **The state's first values** are Mesa's (`radeon_state_init.c`), with vertices in window coordinates as X.org's
  EXA code sends them (`SE_COORD_FMT` = x, y and z premultiplied, the fourth coordinate 1/w).
- **Vertex rounding:** Mesa's eighths of a pixel, truncated, moved DTRI0's edges (94 pixels of coverage, 56 of depth
  order). Sixteenths, rounded (`ROUND_MODE_ROUND | ROUND_PREC_16TH_PIX`), brought that to about 25 and 5.
- **Pixel centres** are at halves (`VTX_PIX_CENTER_OGL`): 18 to 37 pixels differ from the reference with centres at
  halves, about 610 with centres at wholes.
- **The 2D and 3D engines take turns:** a `WAIT_UNTIL` for the other engine idle and clean when the kind of drawing
  changes (X.org's `RADEON_SWITCH_TO_2D` and `_3D`).

**DTRI0 on the P3** (three Gouraud triangles, Z16, LESS; three runs): 999 pixels in 1,000 within 8 of 255 of the
reference, the mean error 2.2. Three to seven pixels a frame are far off: each lacks one of its two bytes, at
places that repeat from run to run (near 340,322 and 358,297). That is the card's memory fault, not the layer; the
test lists them (`DTRI0-PIX`) and judges the colours by the 999th pixel in 1,000.

### The scenes: DTRI (2026-11-24)

`DTRI` is RTRI's scenes and reference (OpenGL's rules worked out in the test) through this path. On the P3 all 13
pass on two seeds: random, mesh and zbuf with no pixel of coverage off; blend; and nine textured cases (nearest,
linear, perspective, mipmapped, modulated, 8888, blended by the texel's alpha, fogged), each with 0 to 83 pixels
off of 35,000 to 81,000. What it settled:

- **Perspective needs `TXFORMAT`'s `PERSPECTIVE_ENABLE`** (bit 31). Without it a unit maps its texture affinely
  whatever the vertices' 1/w say (37,966 of 41,566 pixels off). `radeon_3d_init` sets it on every unit.
- **The vertex format that works:** x and y in window coordinates, z in [0, 1], the fourth coordinate 1/w
  (`SE_COORD_FMT` = `XY_PRE_MULT_1_OVER_W0 | Z_PRE_MULT_1_OVER_W0`), s and t as OpenGL's: the chip multiplies them by
  1/w and divides at each pixel. Mesa never uses this combination with textures (it sends unprojected coordinates
  and lets the chip divide), so this was not known from the sources.
- **Mipmap levels in memory:** one after another from `PP_TXOFFSET`, each starting on a 32-byte boundary, a level
  narrower than 32 bytes with its rows padded to 32 (Mesa's `radeonSetTexImages`). Each level alone matched.
- **Which level the chip takes** (`DMIP`: one-colour levels, footprints swept by scale and angle): the largest single
  derivative of the four (not OpenGL's length of the longer axis), with the level changing at 1.5 times a power of
  two. With that rule in the reference, tex-mip is 15 to 36 pixels off of about 47,000; with OpenGL's, 2,246 to
  10,939. The Rage 128 used the same derivative and switched at 1.41.
- **The combiner** computes A x B + C: the texel alone is C = texel; modulate is A = texel, B = the vertex's colour.
- **Fog** is the specular colour's alpha (255: none) with `PP_FOG_COLOR`'s `FOG_VERTEX | FOG_USE_SPEC_ALPHA`.
- **Textures were written by the CPU** with the engine idle, then `radeon_vram_written`; no texture cache needed a
  flush between textures at the same address.

### 32 bits, the stencil, the alpha test and three units: DTRI32 (2026-11-24)

`DTRI32` draws to ARGB8888 with a Z24S8 buffer. All its lines pass on the P3, twice:

- **Coverage is exact** against the top-left rule (58,057 pixels, none outside, none missing), ties included: a
  pixel centre exactly on a right edge is not drawn. (RTRI32's reference had the tie rule the other way round; its
  scene has 24 such pixels, which is how this showed.)
- **Z24S8:** the depth in bits 23-0, the stencil in bits 31-24, as on the Rage 128.
- **The stencil** (`RB3D_CNTL`'s `STENCIL_ENABLE`, the test and operations in `RB3D_ZSTENCILCNTL`, the reference
  and masks in `RB3D_STENCILREFMASK`): a triangle wrote 5, a quad with EQUAL drew only there.
- **The alpha test** (`PP_CNTL`'s `ALPHA_TEST_ENABLE`, the function and reference in `PP_MISC`): GEQUAL 128 along
  an alpha ramp cut at alpha 129.
- **Three texture units** multiplied together gave the product to the last bit; the units' coordinates follow one
  another in the vertex (`SE_VTX_FMT`'s ST0, ST1, ST2), each unit's `TXFORMAT` routed to its own.

RD2 is complete: `DTRI0`, `DTRI`, `DTRI32` and `DMIP` pass on the P3.

## RD3: the command processor and its ring (2026-11-24)

`hal/src/radeon/cp.c` starts the chip's command processor (CP) fetching a ring in system memory, and
`hal/src/radeon/cp_ucode.c` holds its microcode (`R100_cp_microcode` of Linux 2.6.29's `radeon_microcode.h`, under
that file's MIT notice). It started at the first attempt on the P3, and `DTRI ring` draws all 13 scenes through it
with the same pixel counts as by register writes.

- **The memory map first.** The BIOS leaves `MC_FB_LOCATION` at ffff0000: the frame buffer claims every address of
  the chip's own address space, so nothing could be fetched from outside it. Its top is lowered to 03ff0000 (the
  frame buffer's 64 MB), X.org's way: only when it must change, with the engine idle, the display's memory requests
  stopped (`CRTC_DISPLAY_DIS`, `CRTC_DISP_REQ_EN_B`) and `MC_STATUS` idle, then the engine reset and the display's
  registers put back. It is left lowered at the end, as every driver leaves it.
- **The PCI GART.** A page of 1,024 entries (`AIC_PT_BASE`), each a page's physical address, covers 4 MB of the
  chip's address space from 04000000 (`AIC_LO_ADDR`, `AIC_HI_ADDR`), right after the frame buffer; translation is
  `AIC_CNTL` bit 0. The ring is the range's first pages; entries past it point at the ring's first page.
- **The ring** (64 KB by default) and the table are in conventional memory, where linear addresses are physical.
  `CP_RB_BASE`, `CP_RB_CNTL` (its size, with `RB_NO_UPDATE`), `CP_RB_WPTR`; the chip started with `CP_CSQ_CNTL` =
  `CSQ_PRIBM_INDBM`.
- **The chip writes nothing to system memory:** the read index is read from `CP_RB_RPTR` and the scratch
  registers' copy-out is masked off (`SCRATCH_UMSK` 0).
- **A first fetch is checked** (sixteen no-ops within 50 ms). On failure the CP is stopped, the engine reset, the
  GART and bus-master bits put back, and drawing stays on plain register writes.
- **While it runs,** every drawing-engine register write is a type-0 packet (`radeon_out`), a triangle is a
  `3D_DRAW_IMMD` packet (the vertex format, the control word, the vertices), and `radeon_sync` purges the caches in
  the stream and waits for the ring fetched, the FIFO empty and the engine not active.

What the first start read on the P3: `AIC_CNTL` 2 before, `CP_RB_RPTR` 0, `MC_STATUS` 7 with the display's requests
off, `RBBM_STATUS` 140 (64 FIFO entries free, not active) before and after the first fetch.

**2D through the ring** (`DTWOD 16 ring`): the same results as by register writes (the same four stray pixels of
the card's in `mixed-16`), the host-data operations as type-0 packets to the `HOST_DATA` registers.

**Texels through the stream** (`radeon_upload`, a `CNTL_HOSTDATA_BLT` packet as X.org's `RADEONHostDataBlit`): the
destination is taken as an 8-bit surface 64 bytes wide, so a level's bytes land as they are whatever its format,
with no engine sync and in order with the drawing around them. `DTRI ring` uploads every texture this way: all 13
scenes pass. A mode set stops the command processor first (`radeon_set_mode`, `radeon_text_mode`).

**Flips in the stream** (`radeon_flip_queue`: the new start, `WAIT_UNTIL` = `WAIT_CRTC_PFLIP`, a sequence number
into `GUI_SCRATCH_REG0`). `DFLIP ring` on the P3: with the wait a frame lasts 16,495 us (one 60 Hz refresh; 182
flips in 3 s, every one seen); without it 553 us.

- **When the display takes a new start was measured** (`CRTC_OFFSET`'s pending bit 30, writes 0, 3, 8 and 13 ms
  into a frame): with `CRTC_OFFSET_CNTL`'s `FLIP_CNTL` clear, as the BIOS leaves it, once a frame at the end of
  the vertical blank (pending 1.4, 15.1, 10.1 and 5.1 ms); with it set, at the next scan line (6 to 31 us). The
  DRM sets the bit for page flipping; on this chip that is the tearing kind. The layer leaves it clear, and
  `radeon_flip_at_line(1)` sets it for a swap without the vertical sync.
- **The scratch register keeps its value** across programs and mode sets: a user of the sequence number writes 0
  first, in the stream.

Still to do in RD3: the override file and the cycles a word (`DPIO`), with the device (RD4).

## RD4: the device (2026-11-24)

`hal/src/rg/radeon.c` puts the layer behind the device table (`rg_radeon_device`, after the Rage 128's in the
list), made from the Rage 128's device.

- **Open:** the BIOS's mode (16 or 32 bits), a requested pitch (Glide's 1,024 pixels: multiples of 8 pixels and 64
  bytes), the command processor from a 64 KB ring (plain register writes if it doesn't start, or with
  `DGL_RADEON_RING=0`), colour buffers, a depth buffer (Z16, or Z24S8 with 32-bit colour) and the heap.
- **Swap:** a flip in the stream with the ring (the display waited for when vsync is asked; one flip in flight);
  without vsync the start is taken at the next scan line (`radeon_flip_at_line`).
- **The microcode's override:** `R100_CP.BIN` beside the program (linux-firmware's `radeon/R100_cp.bin` as it is:
  512 big-endian dwords, each entry's high word first). The embedded table is used without it.
- **`DGL_RADEON_VRAM=A-B`** (megabytes; `rg_radeon_vram_mb` for programs): the device keeps everything inside that
  part of the card's memory and reports its size as the card's. For the P3's faulty card: `16-33`.

`RGDEV dev=radeon` on the P3 with `DGL_RADEON_VRAM=16-33`: every line passes at 16 bits, at the mode's pitch and
at 1,024 pixels, through the ring. (RGDEV's reference is 16-bit: its `bpp=32` is not a test of a 32-bit device;
`DTWOD 32` covers the 2D engine there, and passes but for the card's one or two lost bytes a case.)

## RD5: DOS-GL's back end (2026-11-24)

`gl/src/backend/radeon/backend.c`, made from the Rage 128's, on the Radeon device and layer.

- **The texture environments** are on the chip's combiner after Mesa's `radeonUpdateTextureEnv`: every
  `GL_COMBINE_ARB` function (replace, modulate, add, add signed, subtract, interpolate) with any of its sources and
  operands and a scale of 1, 2 or 4 has its own form, and the classic environments are their combine equivalents
  by the texture's family. Nothing is approximated there (the Rage 128's combiner had one function a unit).
- **Two texture units** of GL's on the chip's first two (the chip has a third). GL's second alone is drawn by the
  chip's first with the second's coordinates.
- **Textures** go through the command stream (`radeon_upload`) into the heap, each level after the last with rows
  padded to 32 bytes; `glTexSubImage2D` on a single-level texture sends the rectangle's rows again in place. To
  2,048 texels a side.
- **Trilinear filtering** is the chip's own (`LINEAR_MIP_LINEAR`).

Two things the first runs on the P3 found:

- **The Rage 128's probe took any ATI display card**, and its device is before the Radeon's in the list: DOS-GL
  drove the Radeon as a Rage 128 and the first test hung. `r128_find` now takes the Rage 128's IDs only.
- **The chip keeps texels it has read until a unit's texture registers are written again**, as the G400 does: a
  16 x 16 lightmap's rectangles updated in place never showed (t18, t32), because the layer writes a register only
  when its value changes. `radeon_3d_tex_written` (called by `radeon_upload` and after the CPU's writes) has the
  units' registers written again at the next primitive. `radeon_upload` also flushes the pixel cache after its
  blit, as the DRM does.

**Conformance on the P3** (`DGL_RADEON_VRAM=16-33`, through the ring): 33 of 35 at the first whole run, the two
texture-update tests failing; with the fix above those pass.

**GLQuake on the P3** (Pentium III, `timedemo demo1`, no vertical sync, the ring, megabytes 16-33), frames a second,
beside the other cards that have been in the same PC:

| | 640x480 | 640x480 `-mtex` | 320x200 | 320x200 `-mtex` |
|---|---|---|---|---|
| Radeon 7000 (RV100), first run | 80.6 | 87.8 | 103.8 | 97.9 |
| Rage 128 Pro | 62.8 | 74.4 | 105.7 | 102.2 |

At 320x200 the processor sets the pace on both; at 640x480 the Radeon is the faster chip. Nothing here has been
tuned yet (the Rage 128's figures are after its tuning).

## RD6: Glide's back end (2026-11-24)

`glide/src/backend/radeon/radeon.c`, made from the Rage 128's. What differs is the combine: the Rage 128 has one
function a unit over the vertex colour and the texel, so its back end works most of a combine out at the vertices.
The Radeon's units take the texel, the vertex's colour and alpha, a factor colour and the unit before, each or its
complement, and compute A x B + C, A x B - C or a blend. So with a texture the Voodoo's combines run on the chip:

- Glide's iterated colour and alpha are the vertex's; its constant colour is the units' factor colour
  (`PP_TFACTOR`); its texel is the first unit's.
- One unit: zero, local, local alpha, `f x other`, `f x other + local`, `f x other + local alpha`,
  `f x other + (1 - f) x local` (the blend), `(1 - f) x local`.
- Two units: `f x (other - local)` (the difference first, never below 0 either way), `local alpha - f x local`,
  and `f x (other - local) + local alpha` (approximated where other is less than local: said once).
- A unit more for an inverted result, and one for chroma key's alpha (the keyed texels' alpha 0 multiplied in when
  the alpha combine doesn't take the texel's).
- Every unit a combine uses has the texture enabled, all reading the vertex's one pair of coordinates.
- Without a texture the combine is worked out at the vertices, as on the other cards.

The core gained two small things for it: a back end's shortest texture row (`tex_min_w`: 16 texels here, the
chip's 32 bytes; 8 for the others as before), and the setting `MGAGLIDE radeon_vram=A-B` (megabytes, as
`DGL_RADEON_VRAM`).

**Conformance on the P3** (`MGAGLIDE radeon_vram=16-33`, through the ring): 24 of 27 at the back end's first run on
the card, then 26 with what follows; the one left is t22, the gamma test, which fails on every real card (the
frame read back has no DAC ramp in it).

- **t13, the combines: every cell exact,** the first card of which that is so (the others approximate cells 12 and
  13). Cell 13 showed that the runtime's own `GR_COLORCOMBINE_CCRGB_BLEND_ITRGB_ON_TEXALPHA` had its local and
  other colours the wrong way round against the Voodoo's frame (the constant colour belongs where the texel's
  alpha is 0): corrected in `glide/src/tex/texstate.c`, for every card.
- **t04, the edges: 0.08% of pixels differ** from the Voodoo's. The chip rounds vertices to sixteenths of a pixel,
  Glide's own grid, and applies an exact top-left rule to a centre on an edge; the 64th of a pixel that brings the
  Rage 128 and the TNT2 into line never reaches it. Classed in the manifest (`edge`), as the ViRGE's and the
  WARP's own edge rules are.

**DOS-GL's whole suite again** after the texture-register fix: 35 of 35.

**The games' replays on the P3** (GLPLAY with the recorded call traces, the frames against the emulated Voodoo's at
40 levels and 5% of pixels, as for the Rage 128): GTA's frames 60, 150, 240 and 295 with no pixel past the
tolerance (the worst 36 levels); Screamer Rally's 60, 150, 200 and 240 with at most 0.054% past it. Every swap a
flip in the command stream (295 and 240 queued), no engine time-out, no approximation said.

**The other cards after the core's changes** (the preset's correction, `tex_min_w`, the back ends' tables): Glide's
conformance on the emulated G100, G200, G400 and G450 in Loop A, 27 of 27 on each, and on the emulated ViRGE/DX's
two boards, 27 of 27 on each; the host tier's 14 steps pass. (The TNT2 and the Rage 128 have no emulated card:
theirs wait for a bench.)

Still owed, with someone watching the P3: the games live, DFLIP's phases, the hardware cursor.

## The rest of the family: the RV200 (Radeon 7500) and the first Radeon (R100)

A Radeon 7500 LE (RV200) is to follow this card. What the references say differs, gathered here so it isn't
researched twice. Only the RV100 has been on a bench; the rest of this section is from the sources.

| | RV100 (7000 / VE) | RV200 (7500) | R100 (Radeon / 7200) |
|---|---|---|---|
| PCI IDs (desktop) | 5159, 515A | 5157, 5158 | 5144-5147 |
| Transform and lighting engine | no | yes | yes |
| Hierarchical Z, Z compression, fast Z clear | no | yes | yes |
| PLL erratum | 5 ms after each data access | two dummy reads after each index write | none listed |
| Stencil | no fault listed | no fault listed | a fault in some operations (Mesa falls back) |
| CP microcode | `R100_cp_microcode` | the same | the same |
| Texture units | 3 | 3 | 3 |

- **The layer already knows them:** `radeon_find` takes the eight desktop IDs, and `radeon.flags` carries
  `RADEON_HAS_TCL`, `RADEON_HAS_HIERZ`, `RADEON_PLL_DELAY`, `RADEON_PLL_DUMMYREADS` and `RADEON_BROKEN_STENCIL`.
  `radeon_pll_rd` applies the right erratum.
- **`SE_CNTL_STATUS`:** a chip with the engine gets 0, one without gets `TCL_BYPASS` (Mesa and X.org agree).
  Vertices the CPU transformed go the same way on both: `SE_VF_CNTL` without its TCL bit.
- **Hardware transform and lighting** (Mesa's `radeon_tcl.c`, `radeon_maos_verts.c`, `radeon_state.c`):
  - a primitive asks for it with `RADEON_CP_VC_CNTL_TCL_ENABLE` in its `SE_VF_CNTL` word;
  - the matrices, lights, materials, texture-coordinate generation and clip planes are vectors and scalars loaded
    through `SE_TCL_VECTOR_INDX_REG` (0x2200) and `SE_TCL_SCALAR_INDX_REG` (0x2208) and their data registers;
  - `SE_TCL_OUTPUT_VTX_FMT` (0x2254) says what the engine passes on to the rasteriser;
  - vertices come from buffers the chip reads itself (`3D_LOAD_VBPNTR`, then `3D_RNDR_GEN_INDX_PRIM` with indices),
    so they need memory the chip can fetch: the GART range RD3 sets up for the ring.
- **Hierarchical Z** is `RB3D_ZSTENCILCNTL`'s `Z_HIERARCHY_ENABLE` (bit 8) and `Z_COMPRESSION_ENABLE` (bit 28), with
  the DRM's clear path (`radeon_state.c`'s `radeon_cp_dispatch_clear`) initialising the hierarchy.
- **X.org's power code** forces the RV200's `SCLK_MORE_CNTL` clocks on. That is a PLL write, and this layer makes
  none: the BIOS's setting is kept unless the card shows a need.
- **What all three share beyond what DOS-GL uses today** (register fields in the DRM's `radeon_drv.h` and Mesa's
  `radeon_texstate.c`): anisotropic filtering to 16:1 and a level bias (`PP_TXFILTER`), DXT1, DXT3 and DXT5
  textures (`TXFORMAT` 12, 14, 15), cube maps (`PP_CUBIC_FACES_0-2` at 0x1D24), textures to 2048 a side, and a
  combiner per unit with add, subtract, signed add, blend, dot3 and a scale of 2 or 4.
