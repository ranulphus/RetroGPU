# DOS-GL with more than one back end: the software renderer first, a plan

**Status (2026-11-13): plan v1, approved by the owner. B0 written and reviewed (`docs/backend-interface.md`); the
merge into RetroGPU has landed (2026-11-08). B1 is done (2026-11-14, `docs/b1.md`): the Matrox code behind the
device level (`hal/src/rg`) and GL's drawing back end (`src/backend/mga`), pixel and register identical. Next, by
the owner's order, the ViRGE/DX's device (`docs/virge.md` V3), then B2. 2026-11-12: the S3 ViRGE/DX added to the order of the other cards, first (see the end). 2026-11-13: B1 to B5
built by the DOS-GL session (below).**

The owner's decision (2026-11-06), from GLOS's v1 route (GLOS `docs/v1-route.md`): **DOS-GL gains a software
renderer**, so that DOS-GL programs run on any graphics card. GLOS v1 needs it for two things:
- its screensavers (a starfield, pipes, a 3D maze, after Windows 98's OpenGL ones) on every PC;
- windowed DOS-GL apps under GLOS on any VBE card.

It also gives DOS-GL the shape the other vendors' cards need. The owner's order is the S3 ViRGE/DX first (added
2026-11-12, below), then the Riva TNT2, then ATI's Rage 128 Pro, then perhaps the Radeon 7000 VE. The owner chose to give it a plan of its own, now: designed for
those back ends too, and built alongside GLOS v1's phases B and C, so that it is ready for GLOS's phase D.

**This reverses two of DOS-GL's own decisions.** The PRD amendment is part of this plan (step B5):
- D4 ("no software fallback") and the non-goal "Software rasteriser fallback: No";
- the non-goal "Other vendors": the software renderer comes first, then the ViRGE/DX, the TNT2 and the rest, each
  with its own plan.

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
- **DOS-GL is RetroGPU's `gl/`** since 2026-11-08 (the owner's choice: one repository for the HAL, GL and Glide).
  B1 works here, and its device level sits beside the HAL at RetroGPU's root (B0, the owner's answer of
  2026-11-09).

## The design: a back-end interface

The GL layer keeps everything that is GL: state, matrices, lighting, vertex arrays, display lists, clipping,
texture objects and their GL formats. Below one interface, a back end does what is the card's:

| Group | What the back end does |
|---|---|
| **Capabilities** | Its name (`glGetString(GL_RENDERER)`); texture units, sizes and formats; depth and stencil; colour formats; what it does in hardware (dual texture, fog, blend modes) |
| **Surfaces** | Colour and depth buffers: full-screen (VBE or a native mode), or a GLOS window's pixels; resize; present (swap, or copy to a window) |
| **State** | Blend, depth test and write, alpha test, fog, scissor, colour mask, shading, texture environment and combiners, as a validated block, not registers |
| **Textures** | Allocate, upload (sub-images too), free; its own memory and formats (VRAM for a card, system memory for software) |
| **Drawing** | Triangles in clip space, as DOS-GL's sink has them today: each back end clips (with a shared clipper and its own guard band) and projects to its own precision (B0, settled at the review); clear; finish and flush |
| **Reading** | Read pixels; snapshots and crash dumps (`snap.c`, `crash.c`) |
| **Selection** | Found and chosen at `dglInit`: `DGL_DEVICE=mga` / `soft` / later `virge`, `tnt2`, or the first that works |

Matrox-only features stay reachable through capabilities, not as the GL layer's knowledge of registers. These
include the WARP, AGP, dual texture, the HAL's 2D engine and GLOS's GPU lock. Three settle the interface's details:
- **B1:** moving the Matrox code behind it.
- **B2:** the software renderer.
- **The ViRGE/DX's plan, later:** the first card after the Matrox ones (the owner's order, 2026-11-12), so it checks
  the interface fits a second card before that card's work starts; the TNT2's plan checks it again.

## The steps

- **B0, the interface in detail (a document):** every call the GL layer makes into Matrox code today, listed, and
  the interface's calls each maps to. Reviewed with the owner before B1.
- **B1, the Matrox code behind the interface, with nothing changed:**
  - `gl/src/backend/mga/` and the Matrox device beside the HAL (B0's two levels): the register validation, the texture layouts, the
    triangle hand-off, the surfaces and present.
  - **The test: pixel for pixel the same.** DOS-GL's conformance on the G200, G400 and G450 in Loop A; the games
    (ClassiCube, GLQuake, Quake 2, Half-Life); GLOS's windowed suites; DOSBench's DOS-GL timings within noise.
  - The bench (bench-g400) by the usual checks.
- **B2, the software renderer** (`src/backend/soft/`, its core shared with Glide: the owner's answer of
  2026-11-09, below; its own plan is `docs/b2.md`):
  - **Rasterising:** a half-space or scanline rasteriser in fixed point, with sub-pixel precision, top-left fill
    rule and perspective-correct attributes.
  - **Textures:** nearest and bilinear, mipmaps, the GL 1.1 environments (replace, modulate, decal, blend), and the
    multitexture DOS-GL already offers (two units).
  - **Per-pixel work:** depth (16 bits, 32 where asked), alpha test, blend, fog (per vertex and per pixel as GL
    allows), scissor, colour mask; 8-bit stencil (the owner's answer at B0's review).
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

- **In RetroGPU:** the merge has landed (2026-11-08), so B1 moves files across the GL layer there.
- **Who and when:** the DOS-GL session (RetroGPU's, which also builds the ViRGE/DX's layer), B1 to B5, from
  2026-11-13 (the owner's answers below). GLOS's phase B moved `src/dgl/glos.c` to API 2.0 first (step 14); B1
  moves it behind the back end. GLOS's screensavers on cards other than Matrox (its phase D) wait for B2 and B3,
  so the software renderer is wanted by GLOS's phase D.
- **With GLOS's session:** its gate suites check B1 (`docs/b1.md`); it runs full gates on devserver several times
  a day from one checkout there, so a B1 gate run is agreed with it first, and it is told when B1 lands on main
  (its gates build against RetroGPU's main).
- **Merges and pushes** in RetroGPU with the owner's approval, each after the full regression (B1:
  pixel-identical on every card).

## The owner's answers (2026-11-13)

1. **Who:** the DOS-GL session takes B1 over from GLOS v1's session (the owner, to this session: "suggest that you
   take over B1 to the other session unless they have any reasonable rejections"; GLOS's session had none), and
   B2 to B5 with it (the owner, to GLOS's session: "The DOS-GL session carries on").

## The owner's answers (2026-11-09)

1. **Who and when:** GLOS v1's session, after GLOS's phase B (since moved to the DOS-GL session: 2026-11-13, above).
2. **One rasteriser core for GL and Glide.** Its state covers GL's environments and Glide's combiners, fog table,
   chroma key and W-buffer. GL's back end comes first and Glide's next, both on B0's two levels. A full software
   Glide and GL lets 3D games run on the CPU alone, on a PC with no card RetroGPU drives. A Haswell-class CPU's
   single core with SSE2 should play the 1997-2000 games at 640x480.
3. **Rendering in screen tiles,** so other cores can be used later. Multi-core isn't in GLOS v1.

## The owner's answers (2026-11-06)

1. **Games run on the software renderer: a goal.** ClassiCube and Quake at 320x200 on a Pentium II, tested in Loop A
   on the S3 card.
2. **8 bits too:** a fixed palette for VBE cards with only 8-bit modes, beside 16 and 32 bits.
3. **MMX from the start,** with plain loops for other CPUs.
4. **The plan approved as it stands.** Merges and pushes stay with the owner's approval.

B0's review (2026-11-06): the back-end interface in clip space; stencil (8 bits) in the software renderer from B2's
start. B0 is `docs/backend-interface.md`.

## The S3 ViRGE/DX, first of the other cards (the owner's addition, 2026-11-12)

The ViRGE/DX (S3 86C375) goes into the 486DX4 bench (bench C: PCI, 8 MB of RAM, GLOS from its setup floppy), with
4 MB of video memory. The owner's answers:
- **Order:** first of the other cards, after the software renderer (B1-B5) and before the Riva TNT2. 86Box emulates
  its 3D engine, so it can be built and tested in Loop A as well as on the bench, and it is the cheapest check that
  the back-end interface fits a second card.
- **Scope:** DOS-GL's drawing back end on its 3D engine; GLOS's desktop on its 2D engine through RetroGPU's device
  level; and Glide on it too, accepting that the effects the chip cannot blend are missing from the games.

**What the chip has,** read from 86Box's model (`src/video/vid_s3_virge.c`, `virge375_pci`); S3's 86C375 data book
and the bench check it before the work starts:
- **3D:** drawn into 15-bit (RGB555) or 24-bit colour, not RGB565; 16-bit Z with all eight compare functions;
  Gouraud shading; textures in ARGB8888, ARGB4444 and ARGB1555, perspective-correct, bilinear, with mipmaps; texture
  modes decal, modulate and reflection. The CPU computes each triangle's slopes and gradients, as with the Matrox
  cards' setup today.
- **Blending:** one mode, source alpha (from the texture or the vertex). Fog blends towards one colour by the
  vertex's alpha, so not together with blending. No stencil.
- **2D:** blits, fills, lines, colour expansion and a hardware cursor.

**What follows:**
- **GL:** draws whose state the chip can do go to it; the rest to the software renderer into the same buffers, which
  reads video memory back over PCI and so is slow on a 486. Programs that draw only what the chip can stay fast:
  Gouraud and textured scenes, alpha-blended HUDs, GLOS's screensavers. Quake's lightmaps (a multiplying blend) are
  beyond the chip. Its 16-bit colour is RGB555, so the back end converts where DOS-GL or GLOS windows use RGB565.
- **GLOS:** the desktop's blits, fills and cursor on the 2D engine through the device; modes and the framebuffer
  through VBE as on any card.
- **Glide:** through the device and a Glide drawing back end; the combine and blend states the chip cannot do are
  approximated or dropped, listed per game, as the G100's approximations were.
- **Memory:** 640x480 with double buffering and a 16-bit Z buffer takes about 1.8 MB of the 4 MB, leaving about
  2.2 MB for textures.

**Tests:** Loop A gets a ViRGE/DX card (86Box's `virge375_pci`; its BIOS folder, `video/s3virge`, joins the
sparse ROM set), and bench C is its silicon tier. The ViRGE/DX gets its own plan before its work starts, as the
TNT2 will: the references and their licences (the data book, the open drivers of the time), 86Box's model against
the bench, and the PRD amendments (DOS-GL's other-vendor non-goal, and the Glide runtime's Matrox-only scope).
