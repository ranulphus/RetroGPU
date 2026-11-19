# The Riva TNT2: a plan

**Status (2026-11-19): v1, the owner's answers in (below). The card is in the P3 (bench B): a TNT2 M64 (NV5,
64-bit memory). T0 done (below); before it, DOS-GL's software renderer through the card's VBE BIOS passed all 35
conformance tests there (B3's bench tier).**

The owner's order of 2026-11-12 (`gl/docs/backends.md`): the S3 ViRGE/DX first (`docs/virge.md`), then the Riva
TNT2, then ATI's Rage 128 Pro, then perhaps the Radeon 7000 VE. Each card gets a plan of its own before its work
starts: the references and their licences, the emulator's part, the tests, the PRD amendments. This is the TNT2's.

## The card

The TNT2 family is NVIDIA's NV5 (1999), the TNT's (NV4) successor with the same engine:
- **Parts:** TNT2 (PCI 10de:0028), TNT2 Ultra (0029), Vanta (002c), TNT2 M64 (002d). The M64 and Vanta have a 64-bit
  memory bus instead of 128 bits: the same features, less fill rate. T0 reads the chip's ID, memory size and bus
  width from the chip itself rather than from these numbers.
- **Windows:** BAR0, 16 MB of registers (the chip's units: PMC, PBUS, PFIFO, PGRAPH, PFB, PCRTC, PRAMDAC, and the
  instance memory PRAMIN); BAR1, the frame buffer (the P3's GLOS made a 32 MB write-combining window for it, at
  da000000).
- **The engine is driven through objects:** the host writes methods into a channel of the command FIFO (PFIFO),
  which hands them to the graphics engine (PGRAPH) for the object bound to each subchannel. Objects live in
  instance memory, found through a hash table (RAMHT). So the bring-up is the FIFO, the instance memory and the
  objects first, then each object's methods. Programmed I/O into the channel first; the FIFO's DMA mode (and AGP)
  only if measurements call for it.
- **What it draws** (the DX5 and DX6 triangle classes): the chip sets triangles up itself from screen-space vertices
  (x, y, z, 1/w, colour, specular and fog, u, v: Direct3D's TLVERTEX), so the host's set-up is a vertex copy, not
  edge arithmetic as on the Matrox cards and the ViRGE. 16- and 32-bit colour, 16- or 24-bit Z with 8 bits of
  stencil, Direct3D's blend factors, alpha test, fog, two textures a pass (the DX6 class), mipmaps with bilinear and
  trilinear filtering. Textures are stored **swizzled** (texels in Morton order), so uploads reorder them.
- **For GL 1.1** that covers what the G100 and the ViRGE cannot do (blending, stencil, two textures); the gaps
  are expected to be small and are listed in T4.

## The references and their licences

RetroGPU is MIT, so code is written from documentation and from MIT-licensed drivers, never copied from GPL ones:
- **X.org's `xf86-video-nv`** (`riva_hw.c`, `riva_xaa.c`: the NV4/NV5 FIFO, PGRAPH and instance-memory set-up and
  the 2D objects). MIT/X11 with NVIDIA's notice: usable, its notice kept where code follows it. T0 checks the exact
  terms of the files used.
- **nouveau:** the DRM's NV04 code (MIT) and Mesa's old classic NV04 driver (MIT), for the 3D triangle classes'
  methods; envytools' register database (`rnndb`, MIT) for the register names.
- **Not used:** Linux's `rivafb` (GPL); NVIDIA's binary drivers; anything whose licence isn't stated.
- 86Box has no NVIDIA model (only a timer helper), so it can't stand in for the chip (below).

## What T0 found (the P3, 2026-11-19, `NPROBE`)

- **PCI:** bus 1 device 0 (behind the AGP bridge), `10de:002d` revision 15h, subsystem `2206163e`, IRQ 10. BAR0
  (registers) at `de000000`, BAR1 (frame buffer, prefetchable) at `da000000`.
- **The chip:** `PMC_BOOT_0` = `20154000` (an NV5). `PMC_ENABLE` = `01110100`: the graphics engine (PGRAPH) is off
  after the BIOS, so its status register reads back stale bus data (it returned the straps' value); T1 enables it.
- **Memory:** `PFB_BOOT_0` = `28`: bits 1-0 = 0, 32 MB; bit 2 = 0, a 64-bit bus. That decoding (riva_hw.c's) is
  confirmed: the M64 has a 64-bit bus, and the BIOS reports 32,768 KB. `PFB_CONFIG_0`/`_1` = `00001114`/`41016293`.
- **Clocks:** the straps (`8000d447`) give a 14.318 MHz crystal; the core PLL 125 MHz, the memory PLL 150 MHz, the
  pixel PLL 28.3 MHz (text mode's) as the BIOS left them.
- **VBE:** 3.0, "NVidia Corporation" / "Riva TNT", 32 MB, 33 modes (the full list in the run's log).
- **DOS-GL before any TNT2 code:** the software renderer full-screen through the VBE BIOS passes the whole conformance
  suite (35 of 35 within the software renderer's tolerances), and in a software GLOS window on GLOS's VBE desktop
  (800x600x16, mode 114h) TEXCUBE draws right; GLOS's `stars` saver runs there as an app.

## How it fits RetroGPU

As for the ViRGE: a **register layer** in `hal/` (the FIFO, the objects, 2D and 3D), a **device** on B1's interface
(modes, surfaces, present, memory, 2D, the cursor) for GL, Glide and GLOS's desktop, and **drawing back ends** above.
The ViRGE's device (V3) and the software renderer's (B2) show the interface fits other cards; the TNT2 checks it
again (B0's rule).

## Steps

| Step | What | Exit |
|---|---|---|
| **T0** | `NPROBE.EXE`: PCI IDs and BARs, the chip's ID (PMC), memory size and bus width (PFB), the BIOS's VBE modes and its linear window. Read-only | The P3's card described in this doc |
| **T1** | The register layer: chip enable, PFIFO and PGRAPH set-up, instance memory and RAMHT, one channel by programmed I/O, the engine's idle wait, mode set by the BIOS, display start (page flips), the 2D objects (fill, blit, image from the CPU, mono expansion), the cursor. `NTWOD` against `tests/hal/sw2d.c` | `NTWOD` pixel-exact on the P3 |
| **T2** | 3D: the 3D surfaces, the DX5 textured triangle (then DX6's two textures), texture upload (swizzled) in the chip's formats, Z clear. `NTRI` draws the conformance scenes' triangles straight through it against OpenGL's rules (as `VTRI` does) | `NTRI` within DOS-GL's conformance tolerances (D16) on the P3 |
| **T3** | The TNT2 device on B1's interface | `DGL_DEVICE=tnt2` opens, clears and presents; GLOS's desktop on it (with GLOS's session) |
| **T4** | GL's TNT2 back end: state, textures, triangles, the gaps listed (the software renderer, B2, for what the chip can't do) | DOS-GL's conformance on the P3 against OSMesa; GLOS's screensavers; Fifth Wheel and the ports on the P3 |
| **T5** | Glide's TNT2 back end (the owner wants it, as V5 for the ViRGE) | The Glide conformance suite with any approximations listed; GTA and Screamer Rally replays with stated tolerances |
| **T6** | Docs and the PRDs | The owner's review |

**Before T0, a tick that needs no new code:** the VBE device's bench tier (B3, `gl/docs/backends.md`), DOS-GL's
software renderer full-screen through the TNT2's VBE BIOS and in a software GLOS window, on the P3.

**Tests in tiers:** the host for what can be checked there (the swizzle, the vertex and method encodings, the
2D reference); the P3 as the silicon tier for everything the chip does, through GLOS's SSH as on the other benches
(`tools/bench/run.py --pc bench-p3`). With no emulator, the Loop A tiers have no TNT2 cases, and a change to the TNT2
code is checked on the P3 before a merge; the merge tier's other cases still run as usual.

## The owner's answers (2026-11-19)

1. **Glide on the TNT2:** yes (T5), as for the ViRGE.
2. **An 86Box model of the NV5:** not now. Silicon only: every TNT2 change is checked on the P3; a model may be
   reconsidered once the driver works.
3. **The FIFO's feeding:** programmed I/O first; DMA or AGP only if the P3 shows the CPU's writes holding it back.
