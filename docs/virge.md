# The S3 ViRGE/DX: a plan

**Status (2026-11-12): v1, the owner's answers in (below). V0 done; V1 done in Loop A (VTWOD 19/19 on both boards); bench C pending; V2 next.**

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
- **Bench C** (VPROBE, VTWOD on silicon, its BIOS's modes) waits for the PC to boot from its disk.

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
