# DOS-GL with more than one back end: the software renderer first, a plan

**Status (2026-11-06): plan v1, approved by the owner. B0 next, beside GLOS's phase A; B1 once DOS-GL's merge into
MGA-Glide has landed. Nothing built yet.**

The owner's decision (2026-11-06), from GLOS's v1 route (GLOS `docs/v1-route.md`): **DOS-GL gains a software
renderer**, so that DOS-GL programs run on any graphics card. GLOS v1 needs it for two things:
- its screensavers (a starfield, pipes, a 3D maze, after Windows 98's OpenGL ones) on every PC;
- windowed DOS-GL apps under GLOS on any VBE card.

It also gives DOS-GL the shape the other vendors' cards need. The owner's order is the Riva TNT2 next, then ATI's
Rage 128 Pro, then perhaps the Radeon 7000 VE. The owner chose to give it a plan of its own, now: designed for
those back ends too, and built alongside GLOS v1's phases B and C, so that it is ready for GLOS's phase D.

**This reverses two of DOS-GL's own decisions.** The PRD amendment is part of this plan (step B5):
- D4 ("no software fallback") and the non-goal "Software rasteriser fallback: No";
- the non-goal "Other vendors": the software renderer comes first, then the TNT2 and the rest, each with its own
  plan.

## Where DOS-GL stands

- **The GL layer reaches the Matrox hardware directly.** `src/gl` and `src/dgl` include the HAL's Matrox headers
  (`mga/setup.h`, `mga/tex.h`, `mga/regs_mga.h`, `mga/warp.h`). They:
  - validate GL state into Matrox registers (DWGCTL, ALPHACTRL, MACCESS, FOGCOL, PLNWT);
  - lay textures out in the card's formats;
  - hand triangles to the HAL's setup.

  Today's references: emit.c 44, context.c 30, texture.c 18, device.c 13, and fewer in crash.c, glos.c, ftrace.c,
  readpix.c and snap.c.
- **`dglInit` refuses to start without a Matrox card** (D4).
- **Conformance (D16)** compares each test's DOS render with the same test built on the host against Mesa's OSMesa,
  so the references don't depend on the card. A software renderer is judged the same way.
- **DOS-GL is about to merge into MGA-Glide** (the owner's choice, 2026-11-06). This plan's file moves have to fit
  around that merge (see [Order](#order-and-coordination)).

## The design: a back-end interface

The GL layer keeps everything that is GL: state, matrices, lighting, vertex arrays, display lists, clipping,
texture objects and their GL formats. Below one interface, a back end does what is the card's:

| Group | What the back end does |
|---|---|
| **Capabilities** | Its name (`glGetString(GL_RENDERER)`); texture units, sizes and formats; depth and stencil; colour formats; what it does in hardware (dual texture, fog, blend modes) |
| **Surfaces** | Colour and depth buffers: full-screen (VBE or a native mode), or a GLOS window's pixels; resize; present (swap, or copy to a window) |
| **State** | Blend, depth test and write, alpha test, fog, scissor, colour mask, shading, texture environment and combiners, as a validated block, not registers |
| **Textures** | Allocate, upload (sub-images too), free; its own memory and formats (VRAM for a card, system memory for software) |
| **Drawing** | Triangles after clipping and projection, with their attributes (colour, texture coordinates, fog, depth); clear; finish and flush |
| **Reading** | Read pixels; snapshots and crash dumps (`snap.c`, `crash.c`) |
| **Selection** | Found and chosen at `dglInit`: `DGL_DEVICE=mga` / `soft` / later `tnt2`, or the first that works |

Matrox-only features stay reachable through capabilities, not as the GL layer's knowledge of registers. These
include the WARP, AGP, dual texture, the HAL's 2D engine and GLOS's GPU lock. Three settle the interface's details:
- **B1:** moving the Matrox code behind it.
- **B2:** the software renderer.
- **The TNT2's plan, later:** it checks the interface fits a second card before that card's work starts.

## The steps

- **B0, the interface in detail (a document):** every call the GL layer makes into Matrox code today, listed, and
  the interface's calls each maps to. Reviewed with the owner before B1.
- **B1, the Matrox code behind the interface, with nothing changed:**
  - `src/backend/mga/` (or the merged repository's equivalent): the register validation, the texture layouts, the
    triangle hand-off, the surfaces and present.
  - **The test: pixel for pixel the same.** DOS-GL's conformance on the G200, G400 and G450 in Loop A; the games
    (ClassiCube, GLQuake, Quake 2, Half-Life); GLOS's windowed suites; DOSBench's DOS-GL timings within noise.
  - The bench (bench-g400) by the usual checks.
- **B2, the software renderer** (`src/backend/soft/`):
  - **Rasterising:** a half-space or scanline rasteriser in fixed point, with sub-pixel precision, top-left fill
    rule and perspective-correct attributes.
  - **Textures:** nearest and bilinear, mipmaps, the GL 1.1 environments (replace, modulate, decal, blend), and the
    multitexture DOS-GL already offers (two units).
  - **Per-pixel work:** depth (16 bits, 32 where asked), alpha test, blend, fog (per vertex and per pixel as GL
    allows), scissor, colour mask; stencil if B0 finds a user.
  - **Colour formats:** 16 bits (RGB565, GLOS's windowed format), 32 bits, and 8 bits with a fixed palette, for
    VBE cards whose useful modes are 8-bit (the owner's answer).
  - **Speed:**
    - MMX inner loops from the first version, on the CPUs that have it (Pentium MMX, Pentium II and III; the
      owner's answer);
    - plain loops beside them for the rest, kept correct on a 486.
  - **The CPU floor, per program:**
    - GLOS's screensavers at modest sizes on a 486DX2 or better;
    - games at 320x200 on a Pentium II: a goal, tested and tuned (the owner's answer);
    - stated in GLOS's app plans (GLOS's per-app minimum).
- **B3, where it draws:**
  - **Full screen:** VBE 2.0 linear framebuffers, or banked ones.
  - **A GLOS window:** the window's pixels (GLOS API 2.0's call for them), so windowed DOS-GL needs no Matrox card.
  - `dglInit` no longer refuses a PC without a Matrox card: it takes the software renderer.
- **B4, tests, in three tiers** (the owner's choice, 2026-11-06: 86Box's speed must not make suites hours long):
  - **Tier 1, the host, on every change:**
    - DOS-GL's GL layer and the software renderer built for Linux (32-bit, its MMX loops included), drawing into
      memory;
    - each conformance test compared with its OSMesa reference (D16), with the same tolerances, at 8, 16 and 32 bits,
      with and without MMX;
    - natively, in seconds. Most of the renderer's testing happens here.
  - **Tier 2, 86Box, a short smoke:**
    - a few conformance tests at one depth on the S3 card (VBE full screen), the GLOS window path, one game's first
      frames;
    - enough to show the DOS side and GLOS's windows reach the screen;
    - unattended, minutes.
  - **Tier 3, the Pentium 4 bench (G450), the silicon tier:**
    - the full conformance matrix on the software renderer (`DGL_DEVICE=soft`) and on the G450 (`mga`);
    - the games' timings, which set the CPU floors;
    - before merges and in batches, through the Loop B tools over SSH;
    - power-cycled by a network-switched plug, so it runs unattended.
  - **The Matrox back end (B1):** still pixel-identical in tier 2 on the G200, G400 and G450, as today, and on
    bench-g400.
- **B5, the docs:** the PRD amended (D4 and the two non-goals, with a new decision for back ends), the porting and
  testing guides, the release notes.

## Order and coordination

- **After the merge into MGA-Glide:** B1 moves files across the GL layer, so it waits for that merge to land, and
  works in the merged repository. B0 can be written before.
- **With GLOS's API 2.0:** GLOS's phase B moves DOS-GL's GLOS path to API 2.0 at the same time. B1 and that move
  touch the same files (`src/dgl/glos.c`, the window surfaces). Doing B1 first gives GLOS's phase B a cleaner place
  to change (the GLOS window surface in one back-end call).
- **Ready by GLOS's phase D,** for the screensavers.
- **Merges and pushes** in DOS-GL (or MGA-Glide) with the owner's approval, each after the full regression (B1:
  pixel-identical on every card).

## The owner's answers (2026-11-06)

1. **Games run on the software renderer: a goal.** ClassiCube and Quake at 320x200 on a Pentium II, tested in Loop A
   on the S3 card.
2. **8 bits too:** a fixed palette for VBE cards with only 8-bit modes, beside 16 and 32 bits.
3. **MMX from the start,** with plain loops for other CPUs.
4. **The plan approved as it stands.** Merges and pushes stay with the owner's approval.

Still to settle in B0: stencil in the software renderer from the start, or only when a program needs it.
