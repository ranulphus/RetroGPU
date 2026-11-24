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

Still to do in RD2: the same scenes at 8888 with a 24-bit Z and stencil; the alpha test; the second and third units.

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
