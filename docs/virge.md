# The S3 ViRGE/DX: a plan

**Status (2026-11-14): v1, the owner's answers in (below). V0, V1 and V2 done in Loop A and on main (V2:
Gouraud, Z, blending, fog and textures in three formats, NEAREST/LINEAR, perspective, mipmaps, modulate, within
VTRI's tolerances on both boards). B1 has landed; V3 (the ViRGE/DX as a RetroGPU device) built in Loop A (below), GLOS's
desktop on it to come with GLOS's session; then B2 (the owner's order). On bench C (2026-11-18 and 19) the 2D
layer and the device pass byte for byte, and VTRI's 15 cases all pass once two of 86Box's constants were
corrected (below, "On bench C"; 86Box local patch 0135).**

The owner's decisions (2026-11-12, `gl/docs/backends.md`): the ViRGE/DX is the first card after the Matrox ones,
before the Riva TNT2; DOS-GL, GLOS's desktop and Glide all run on it; the card is bench C's (the 486DX4, PCI,
4 MB). The owner then asked for the implementation to begin.

## What V0 found (Loop A, `--machine 486dx4 --card virge`, `VPROBE.EXE`)

- **The chip:** PCI `5333:8A01` revision 00 (the ViRGE/DX and /GX share the ID; CR2F tells them apart), its
  64 MB window at BAR0 (`E0000000` here); CR2D/CR2E repeat the ID; CR30 = `E1`; CR36 = `12`, 4 MB.
- **The BIOS (86Box's generic `86c375_1.bin`):** "S3 Incorporated. 86C375/86C385", **VBE 1.2: no linear
  framebuffer** in any mode. 25 modes: 8-bit up to 1600x1200; 15, 16 and 32 bits up to 1280x1024 (32 bits to
  1024x768); 640x480 at 16 bits is mode `111`, at 15 bits `110`.
- **A linear framebuffer anyway:** set the mode through the BIOS, then open the chip's linear address window
  ourselves (CR58 bit 4, size bits 2:0, at BAR0 through CR59/CR5A). Colour bars drawn through it read back exactly
  on the emulated screen. GLOS's desktop and DOS-GL can therefore use the ViRGE/DX through its own device code
  even where the BIOS has no VBE 2.0 (real ViRGE/DX BIOSes vary: bench C's is checked in V1).
- **What 86Box's model draws** (`vid_s3_virge.c`; to be checked on bench C):
  - 3D at 16 bits is **RGB555**, not RGB565, so a 3D screen is a 15-bit mode (`110`, `113`, `116`); its 24-bit 3D
    format packs 3 bytes a pixel, and the BIOS's true-colour modes are 4 bytes a pixel, so 3D runs in 15 bits;
  - 16-bit Z with eight compare functions; Gouraud; ARGB8888/4444/1555 textures, perspective-correct, bilinear,
    mipmaps; decal, modulate and reflection; one blend (source alpha); fog by the vertex alpha; no stencil;
  - the CPU sets each triangle up (edge slopes and gradients), as for the Matrox cards today;
  - 2D: blits, fills, lines, colour expansion, a hardware cursor; the chip's registers at BAR0 + 16 MB (CR53 bit 3).

## How it fits RetroGPU

B0's two levels (`gl/docs/backend-interface.md`): a **device** beside the HAL (cards, modes, surfaces, present,
memory, 2D, the cursor) for GL, Glide and GLOS's desktop, and **drawing back ends** on top. The interface itself
is B1, built by GLOS v1's session after GLOS's phase B (the owner's answer, 2026-11-09). So the ViRGE work splits
into what needs nothing of B1 (the chip's register layer, its 2D and 3D bring-up, tested on their own) and what
plugs into B1 once it lands.

```
  GL's ViRGE back end (gl/src/backend/virge/)   Glide's (glide/)        GLOS's desktop
        |                                           |                         |
  the ViRGE device (hal/, B1's rg_device): modes (BIOS + the linear window), surfaces, present, memory, 2D, cursor
        |
  the ViRGE register layer (hal/, this plan's V1-V2): MMIO, engine sync, 2D engine, 3D triangle set-up
```

## Steps

| Step | What | Needs | Exit |
|---|---|---|---|
| **V0** | Loop A's `virge` card (86Box `virge_dx_pci`, generic BIOS, 4 MB; its ROM folder in the sparse set); `VPROBE.EXE` | - | Done: PCI, IDs, the BIOS's modes, a linear window through a VBE 1.2 BIOS (Loop A, 486DX4 profile) |
| **V1** | The register layer: the MMIO window, the engine's FIFO and idle wait, mode set by the BIOS plus the linear window, display start (for page flips), the 2D engine (fill, screen-to-screen blit, image upload, colour expansion, lines, the cursor). A 2D test (`VTWOD`) against the software 2D reference the Matrox `twodtest` uses (`tests/hal/sw2d.c`); bench C's own BIOS listed | V0 | `VTWOD` pixel-exact in Loop A on the 486DX4 and Pentium II profiles; bench C: `VPROBE` and `VTWOD` pass |
| **V2** | The 3D triangle: host set-up into the chip's triangle registers (edges, colour, Z, perspective texture coordinates), texture upload in its formats, Z clear. A test (`VTRI`) drawing the conformance scenes' triangles straight through it, compared with OSMesa at the tolerances of DOS-GL's conformance (D16) | V1 | `VTRI` within tolerance in Loop A; bench C agrees with 86Box, or each difference is written down (as `docs/emulated-g200.md` does for the Matrox model) |
| **V3** | The ViRGE device on B1's interface: probe, modes, surfaces (front, back, Z in the 4 MB), present (flip or copy), memory, 2D, cursor | B1 | `DGL_DEVICE=virge` opens, clears and presents; GLOS's desktop on it through the device (with GLOS's session) |
| **V4** | GL's ViRGE back end: state validation, textures (GL's formats converted to the chip's), triangles; what the chip cannot do (blends other than source alpha, fog with blending, stencil) goes to the software renderer once B2 exists, approximated until then and listed | V3; B2 for the fallback | DOS-GL's conformance on the ViRGE/DX against OSMesa with its tolerances, each approximation listed; GLOS's screensavers; Fifth Wheel on bench C |
| **V5** | Glide's ViRGE back end: Glide's state on the chip, its combines and blends approximated where the chip cannot, listed per game (as the G100's were) | V3; Glide's drawing seam (B0's second level for Glide) | The Glide conformance suite with the approximations listed; GTA and Screamer Rally replays with stated tolerances |
| **V6** | Docs and the PRDs: DOS-GL's other-vendor non-goal, the Glide runtime's Matrox-only scope, a decision for the ViRGE's place | V4, V5 | The owner's review |

**Tests in tiers** (the owner's rule): the host for set-up arithmetic (V2's set-up computed natively and checked
against OSMesa's triangles' coverage), a short Loop A smoke on the `virge` card per change, the full matrix before
merges, and bench C as the silicon tier (`ssh bench-486`; GLOS installed from the setup floppy).

**Not copied from 86Box:** its model is GPL and RetroGPU is MIT, so 86Box is used only as an emulator to run
against. The register facts come from S3's 86C375 data book and the bench; the plan's references and their
licences are listed in V1 before any code is written from them.

## V1 so far

- **The register layer** (`hal/include/virge/`, `hal/src/virge/`, its own library `libvirge.a`, apart from the
  Matrox HAL's): the chip found on PCI, its window and new MMIO opened after every BIOS mode set, modes from the
  BIOS's list (15 and 16 bits told apart by their green field), the engine's FIFO and idle wait.
- **2D** (`engine2d.c`), with the Matrox HAL's calls' meaning (`engine_*` in `mga/hal.h`): fills, copies (any
  overlap) and copies between surfaces, colour expansion and images from the CPU. Its 11-bit unsigned coordinates
  mean every operation is trimmed to the clip in software; trimmed CPU data is repacked.
- **The cursor and the display start** (`cursor.c`), as the Matrox HAL's `cursor_*` and `crtc_set_start`: the
  64 x 64 AND/XOR image word-interleaved on a 1 KB boundary (Windows-style), its colours converted to the mode's
  pixels (on this chip they are pixel values, not DAC colours), positions with the edge offsets; the display start
  in 4-byte units, 20 bits (CR0D, CR0C, CR69, and CR31/CR51 kept in step).
- **`VTWOD`** draws each primitive on the screen and two surfaces of other pitches and compares VRAM byte for byte
  with `tests/hal/sw2d.c`'s model at 8 and 16 bits, then reads the cursor (inside and clipped at the top left) and
  two display starts (past 1 MB, and 3 MB) from the emulated monitor: all 19 checks pass on the 486DX4 and
  Pentium II profiles. The 1-bit data's order (the leftmost pixel in bit 7, as GLOS's glyphs) is the chip's own.
- **`tools/regress.sh`:** `VTWOD` on the 486DX4 in the smoke tier; `VPROBE` and `VTWOD` on both boards in the
  merge tier.
- **Not yet:** 24-bit 2D (no 24-bit mode in the BIOS's list; bench C's BIOS may differ), colour patterns, lines and
  polygons (nothing asks for them yet).
- **For GLOS** (its session, 2026-11-12): its desktop takes only VBE modes with a linear framebuffer, in 8, 16
  (RGB565) or 32 bits; on this card the linear window has to come from this layer (later its device level), and
  a 15-bit desktop is a new screen format for GLOS (its windows already carry their format).
- **Bench C:** VPROBE and VTWOD pass on the real card (below, "On bench C").

## V2 so far

- **The set-up** (`hal/src/virge/tri.c`): vertices sorted bottom to top; the lines whose centres are inside
  (01's part from the bottom line `TYS`, then 12's); side 02's X at the first line and every line's step up
  (`TdXdY02`), the other side's likewise (`TXEND01`/`TdXdY01`, then `TXEND12`/`TdXdY12` from the first line of
  12's part); edges set half a pixel left, so the engine's ceil() picks the pixels whose centres are inside:
  OpenGL's rule, top-left included. Attributes: each start value is the attribute's plane at side 02 on the first
  line; its Y delta follows side 02 up a line (`dA/dx * TdXdY02 - dA/dy`); its X delta is per pixel **in the
  drawing direction** (negated when side 02 is on the right).
- **`VTRI`**, against OpenGL's rules worked out in the program: random triangles, a seamless jittered mesh (a gap
  or a pixel drawn twice shows) and Z-tested overlaps. In Loop A on both boards: coverage exact everywhere, colours
  within a step of 5 bits everywhere, Z's choice agreeing but for 0.24% of pixels where two depths nearly tie.
- **Textures** (`virge_3d_texture`, `virge_tex_load`; ARGB8888, ARGB4444 and ARGB1555, square, 1 to 512 texels a
  side, mipmaps largest first and packed after it). The planes: U and V are the texel position in the largest level,
  moved half a texel back for LINEAR (the engine weights the texel at the integer part and the next, with no
  half-texel offset of its own) and by whole textures so none is negative (start values must be; the engine wraps).
  With perspective, U and V are that times W, and W is 1/w scaled so the triangle's largest is 2^30 (fewer bits if U
  would not fit). D, the mipmap level (its integer part; 0 the largest), is OpenGL's level of detail plus a half
  (the nearest level), worked out at each vertex and held to the levels there are. Environments: DECAL (as the
  unlit triangle, unless fog or the vertex's alpha needs the lit one), MODULATE, REFLECTION (the sum). Blending:
  SRC_ALPHA, ONE_MINUS_SRC_ALPHA with the texel's alpha or the vertex's; fog by the vertex's alpha. Always wrapped
  (OpenGL's REPEAT); CLAMP waits for the border colour work in V3.
- **What the engine cannot do as OpenGL does** (approximated; logged by DOS-GL's back end in V3): LINEAR
  magnification beside NEAREST minification (one filter a triangle); MODULATE's alpha (the texel's or the vertex's,
  not their product); the level of detail with perspective (a plane through the vertices' values, not per pixel);
  LINEAR's half-texel move at smaller levels (it is the largest level's half texel); between-level filtering
  (86Box's model has none: `M2TPP`/`M8TPP` sample one level); non-square textures (padded or repeated by the back
  end); more than 15 repeats of a texture across one triangle without perspective (U's 31 bits; the back end splits).
- **`VTRI`'s texture cases** (and `blend`, the vertex's alpha on Gouraud triangles), against OpenGL's sampling
  worked out in the program (perspective-correct coordinates, the level of detail from the derivatives at each
  pixel): `tex-nearest`, `tex-linear`, `tex-persp`, `tex-persp-linear` (ARGB1555, 256 texels), `tex-mip` (128, 8
  levels, minified up to 6 texels a pixel), `tex-modulate` (ARGB4444, 64), `tex-8888` (32, perspective),
  `tex-alpha` (the texel's alpha over a Gouraud layer), `tex-fog`. All pass on both boards, the same pixels
  (P2 board 28 s for the whole program, 486 82 s): NEAREST's texels picked differently at their edges for under 2%
  of pixels, LINEAR within two steps, the rest within one but for under 0.3%. `VTRI only=CASE k=MIN,MAX` runs one
  case at other scales.
- **Settled on bench C** (2026-11-19; below, "On bench C"): Z along a line (`TdZdX`) has the data book's 15
  fractional bits, not the 16 of 86Box's model, and with perspective U is the texel position times W over 2^7
  (`PERSP_UW_SHIFT`), the first ViRGE's scale in 86Box's model, not the 16 times larger one it gives the DX. Both
  were wrong in the model for the DX and are corrected by 86Box local patch 0135. `virge_tri_tune` (VTRI's
  `pshift=N`, `zfrac=N`) still changes them for a sweep. The other open points read as the model has them
  (below); the source stride of flat textures of 2 texels or fewer is still untested.
- **References:** the data book and measurement only. Mesa's old ViRGE driver (`s3v`, Mesa 6-7) carries no licence
  in its files (an author's name only) and is not in Mesa's licence list, so nothing is taken from it; 86Box's
  model is GPL and only run against (its behaviour read for facts, its code not used).

## V3 so far

- **The device table grew 2D and the cursor** (`hal/include/rg/device.h`), as B0 drew them and B1 left out (GL
  did not need them): `surface` (the open context's colour buffers), `target`, `clip`, `fill`, `copy`,
  `copy_from`, `image`, `mono` (`RG_MONO_TRANSPARENT`), and `cursor_load`, `cursor_colours` (0xRRGGBB, the
  Matrox HAL's meaning), `cursor_show`, `cursor_move`. The Matrox device maps them onto the HAL's `engine_*` and
  `cursor_*`; nothing GL draws changes.
- **The ViRGE/DX device** (`hal/src/rg/virge.c`, on the ViRGE layer): the BIOS's 15-, 16- and 24-bit modes (8 is
  palettized, and neither engine draws 32); colour buffers A and B, a 16-bit depth buffer (the 3D engine's Z, its
  pitch rounded to the 2D engine's 8 bytes), the rest for textures, each 4 KB aligned; a swap by the display start
  in the vertical blank; reads to RGBA8 at 15, 16 and 24 bits; no GLOS window (B3's software path will show a
  window's pixels for cards other than Matrox ones). The layer gained `virge_list_modes` and `virge_idle`.
- **The choice:** DGL_DEVICE `mga` or `virge`; unset, the Matrox card first, then the ViRGE/DX. With neither,
  the reasons of both. DOS-GL itself draws on the Matrox device only until V4 (and B2): on a ViRGE-only PC
  `dglInit` refuses ("DOS-GL has no drawing back end for the virge device yet"), while `dglEnumModes` and
  `dglGetDeviceInfo` describe the card.
- **`RGDEV`** (`tests/rg/rgdev.c`): a device driven through the table alone: chosen as DOS-GL chooses, its
  modes listed, a 640x480 context opened (double buffered, with depth) and found black, 200 random 2D operations
  into the hidden buffer compared pixel for pixel through the device's own read with `tests/hal/sw2d.c`'s model,
  shown by a swap and read again from the front, the cursor shown and its colour checked white on the screen (the
  unit tester's capture), closed. In Loop A it passes on the ViRGE/DX (486 and P2 boards, 16 and 15 bits) and on
  the Matrox device (G400 and G200), the same program and the same script: the table fits a second card. In the
  smoke tier on the P2 board, and the merge tier on all four.
- **Still V3's:** GLOS's desktop on the device (its session's plan, with the API above). RGDEV passes on bench C.

## On bench C (the real ViRGE/DX, 2026-11-18 and 19)

Bench C is the 486DX4-100 (PCI) with a ViRGE/DX of 4 MB, under GLOS; the programs run as jobs through GLOS's
SSH (`tools/bench/run.py --pc bench-486`), their lines read from GLOS's log.
- **The chip:** `5333:8A01`, an 86C375 (the ViRGE/DX) revision 1, 4 MB. Its BIOS is VBE 2.0, with a linear window
  at `F0000000` (86Box's ViRGE BIOS is VBE 1.2, banked only), so DOS-GL's VBE path copies to it linearly.
- **2D and the device:** VTWOD's fills, copies, copies between surfaces, colour expansion and images, and RGDEV's
  200 random operations through the device table, are byte for byte the same as `tests/hal/sw2d.c`'s model, as in
  Loop A. (VPROBE's colour bars and VTWOD's cursor and display-start reads use Loop A's screen capture, which
  the bench doesn't have.)
- **VTRI:** with 86Box's two constants, `zbuf`, `tex-persp`, `tex-persp-linear` and `tex-8888` failed on nearly
  every pixel they drew; every other case passed. A sweep of five rounds (`pshift` 7, 9, 11, 13, 15; `zfrac` 15
  beside the model's 16) gave the same counts in every round: only `pshift=7` passes the perspective cases, and
  `zfrac=15` passes `zbuf` (11 pixels off by more than a step, of 67,206). With those as the defaults all 15
  cases pass (`zbuf` 12 off; `tex-persp` 702, `tex-persp-linear` 27, `tex-8888` 283, NEAREST's texel edges as
  in Loop A).
- **What the passing cases settle:**
  - **U and V without perspective:** the model's 2^(27 - s) to a texel is the chip's (`tex-modulate` at 64 texels
    and `tex-mip` at 128 pass); the data book's S12.8.11 reading is not.
  - **LINEAR's sample points:** about the texel's corner, as the model; the half-texel move for OpenGL stays.
  - **The first pixel's offset:** NEAREST picks a neighbouring texel for 1.7-1.9% of pixels (`tex-nearest`,
    `tex-mip`), as in Loop A, so the chip steps from the edge as the model does. The set-up doesn't take half of
    it back; V4's conformance decides whether that is worth doing.
  - **The formats:** ARGB1555, ARGB4444 and ARGB8888 textures, 15-bit output, the 16-bit Z buffer.
  - Dithering and the unlit triangle's alpha are not told apart by VTRI: `blend` and `tex-fog` pass within its
    tolerances.
- **86Box local patch 0135** (`tools/86box/patches/local/`): the emulated ViRGE/DX takes U/W and V/W with the
  86C325's samplers and steps Z along a line by twice `TdZdX`; the other ViRGE models are unchanged. VTRI's
  defaults pass in Loop A with it on both boards, as on the card; with the old constants (`pshift=11 zfrac=16`)
  the patched model fails the same four cases as the card did, by nearly the same counts (`tex-persp` 41,478
  pixels off in both, from the same first pixel; `tex-8888` 42,547 against 42,546).

## The owner's answers (2026-11-12)

1. **Colour depth is the user's choice, not the card's:** GLOS gets a Display control panel and a `GLOS.CFG`
   setting for the desktop's resolution and depth (15, 16, 24 and 32 bits, perhaps 8 and 4), which is GLOS's to
   build. On the ViRGE/DX this means:
   - **15 bits:** 3D draws straight into the desktop and its windows (the chip's own 16-bit 3D format);
   - **16 or 32 bits:** DOS-GL draws into a 15-bit buffer in the card's memory and presenting converts it (a CPU
     copy per frame, which the 486 feels), or the program runs full-screen in a 15-bit mode of its own;
   - **24 bits:** the chip's 3D packs 3 bytes a pixel; whether its display has packed 24-bit modes beyond the
     BIOS's 32-bit list is checked on bench C in V1;
   - **8 or 4 bits:** no 3D on the chip; DOS-GL uses the software renderer (B2 draws 8 bits with a fixed palette).
2. **Until B2:** draws the chip cannot do are approximated (the nearest state it has) and logged once each,
   listed; once B2 exists they go to the software renderer.
3. **Order:** V1 and V2 now (nothing B1 moves is touched); V3 onwards after B1.
