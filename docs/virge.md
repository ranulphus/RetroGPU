# The S3 ViRGE/DX: a plan

**Status (2026-11-12): v0, for the owner's review. Bring-up step V0 done (below); nothing else built.**

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

## Open questions for the owner

1. **Colour for 3D:** 15 bits (RGB555, the chip's 16-bit 3D format) as DOS-GL's 16-bit mode on the ViRGE/DX. GLOS's
   windows are RGB565: either GLOS's desktop runs in 15 bits on this card, or presenting a DOS-GL window converts.
   Which, is GLOS's and the owner's call (recommendation: 15 bits for the desktop on the ViRGE/DX, no conversion).
2. **Until the software renderer exists (B2):** draws the chip cannot do are approximated (the nearest blend the
   chip has) and logged once, rather than refused.
3. **Order with B1:** V1 and V2 start now (they touch no file B1 moves); V3 onwards waits for B1, or B1 is brought
   forward if the ViRGE work overtakes GLOS's phase B.
