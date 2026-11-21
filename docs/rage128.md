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

## Next

R5 Glide's back end, R6 86Box's Rage 128 in
Loop A, R7 the docs (the plan approved 2026-11-21).
