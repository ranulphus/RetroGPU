# The Riva TNT2: a plan

**Status (2026-11-19): v1, the owner's answers in (below). The card is in the P3 (bench B): a TNT2 M64 (NV5,
64-bit memory). T0 done (below); before it, DOS-GL's software renderer through the card's VBE BIOS passed all 35
conformance tests there (B3's bench tier). T1's 2D is byte for byte the software model's on the card, and its
cursor and display start read back as written; seeing them on the screen is still owed.**

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
  objects first, then each object's methods. The FIFO reads the methods from a command ring in video memory (the
  owner's answer 3); AGP only if measurements call for it.
- **What it draws** (the DX5 and DX6 triangle classes): the chip sets triangles up itself from screen-space vertices
  (x, y, z, 1/w, colour, specular and fog, u, v: Direct3D's TLVERTEX), so the host's set-up is a vertex copy, not
  edge arithmetic as on the Matrox cards and the ViRGE. 16- and 32-bit colour, 16- or 24-bit Z with 8 bits of
  stencil, Direct3D's blend factors, alpha test, fog, two textures a pass (the DX6 class), mipmaps with bilinear and
  trilinear filtering. Textures are stored **swizzled** (texels in Morton order), so uploads reorder them.
- **For GL 1.1** that covers what the G100 and the ViRGE cannot do (blending, stencil, two textures); the gaps
  are expected to be small and are listed in T4.

## The references and their licences

RetroGPU is MIT, so code is written from documentation and from MIT-licensed drivers, never copied from GPL ones:
- **X.org's `xf86-video-nv`** (release 2.1.23, read 2026-11-19): every file used carries NVIDIA's own notice
  ("Copyright (c) 1993-1999 NVIDIA, Corporation", 1993-2003 for `nv_hw.c`), an MIT-style permission: usable, the
  notice kept in any file whose code follows it.
  - `nv_hw.c`'s `NVLoadStateExt`: the chip's set-up for every architecture, NV4 (the TNT and TNT2) included:
    PGRAPH, PFIFO, instance memory (RAMHT, RAMFC, RAMRO) and the objects; `nv4CalcArbitration` for the FIFO's
    arbitration from the memory and core clocks.
  - `riva_hw.c`, `riva_hw.h`, `riva_xaa.c`: the older driver's NV4 path by programmed I/O (the owner's choice for
    T1): the FIFO's subchannels as structures (`RivaRop`, `RivaPattern`, `RivaClip`, `RivaBitmap`, `RivaBlt`) and the
    2D operations on them. The newer `nv_xaa.c` drives the same objects through a DMA command buffer instead.
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

## T1 so far (the P3, 2026-11-19)

- **The register layer** (`hal/include/tnt2/tnt2.h`, `hal/src/tnt2/tnt2.c`, built as `libtnt2.a`): the chip found and
  mapped (memory size from `PFB_BOOT_0`); `tnt2_accel_init` sets up the units, instance memory (the hash table and
  the eight objects), PGRAPH and PFIFO as `nv_hw.c` does for an NV4 with no display state (the BIOS's mode stays),
  and starts the command ring (32 KB at the top of video memory, less 128 KB, as the reference). Fill and
  screen-to-screen copy through the GDI rectangle and blit objects, the target surface's format, pitch and offset.
- **The clip object** doesn't clip solid rectangles or the blit (a fill drew outside it on the first run), so fills
  and copies are clipped in software against the stored clip; the clip object is still set for what takes it.
- **The timer:** `nv_hw.c` writes PTIMER through a dword pointer without dividing the offsets by 4 (so at 0x9800,
  0x9840, 0x9500, 0x9400); the layer writes the NV4's timer registers those values fit (0x9200, 0x9210, 0x9140,
  0x9100).
- **`NTWOD`** (`tests/tnt2/ntwod.c`, against `tests/hal/sw2d.c`): at 16 and 8 bits (BIOS modes 111h and 101h), clear,
  fills, fills in random clips, copies (overlapping too), copies on a surface off the screen of another pitch, and a
  mixed run: all byte for byte the same as the software model on the P3's M64.
- **Then the rest of the 2D** (as `nv_xaa.c`):
  - **Copies from another surface:** the surfaces object's source pitch and offset, then the blit.
  - **Colour expansion** through the GDI object's one- and two-colour expansion: the bits in the ring, rows padded
    to 32 pixels, the leftmost pixel in a dword's bit 0 (the layer turns the callers' bit-7-first rows round),
    the command's own clip trimming the padding; colours with ones above the depth, as the driver's. Only each
    row's visible part is sent, so a rectangle may start anywhere.
  - **Images from the CPU:** a band of rows at a time into a 64 KB scratch area after the ring, then a blit from
    there.
  - **Coordinates outside the surface:** every operation is trimmed to the clip in software first (as on the
    ViRGE), so the methods' 16-bit x and y only ever see the visible part; NTWOD's cases start up to 40 pixels
    off the top and left.
- **The cursor** (`nv_cursor.c`, `nv_hw.c`): the NV4's is 32 x 32 pixels of A1R5G5B5 in instance memory (dword
  0x1E00; CR30, CR31 and CR2F point there, CR31 bit 0 shows it), so of the HAL's 64 x 64 AND/XOR image the top
  left quarter shows, and the pixels the Matrox and ViRGE cursors invert take colour 1. Its position register
  keeps 12 bits of each coordinate, signed (-5 reads back as 0xFFB).
- **The display start:** PCRTC 0x600800, in bytes (a multiple of 4), and the vertical retrace from port 3DAh.
- **`NTWOD`** also covers copies from the surface off the screen, colour expansion (opaque and transparent, in
  random clips) and images, against the same model, all byte for byte on the P3 at 16 and 8 bits; with no screen
  capture there, the cursor's image in instance memory, CR31 and the position, and the display start are read back
  (all as written).
- **Still T1's:** the cursor and a page flip seen on the P3's screen (the owner's eyes, as the ViRGE's mode test).

## T2 so far (the P3, 2026-11-19)

- **References:** Mesa's classic nouveau driver for the NV04 family (Mesa 20.3.5, `src/mesa/drivers/dri/nouveau/nv04_*`
  and `nv04_3d.xml.h`: MIT, Francisco Jerez and the Nouveau Project) for the 3D objects' methods, state words and the
  texture layout; the Linux kernel's nouveau NV04 graphics engine (`nvkm/engine/gr/nv04.c`, MIT, Stephane Marchesin)
  for the objects in instance memory and which methods the hardware handles itself.
- **What only a driver does on the NV04 family:** many object-binding methods and the 3D surfaces' clip methods
  (0x2F8, 0x2FC) trap to the driver (nouveau emulates them in its interrupt handler); with no interrupt handler here
  a trap would stall the engine. So the layer sends only methods the hardware takes: the objects' DMA instances are
  written into their instance words (VRAM, instance 0x1200), as the X driver's 2D objects are, and the 3D clip goes
  straight into PGRAPH (0x40053C-0x400548) with the engine idle, as nouveau's emulation does. The textured triangle's
  SURFACES method is the hardware's own.
- **The bring-up:** the 3D surfaces (0x53) on subchannel 4 and the textured triangle (0x54) on 7, through RAMHT
  slots 8 and 9; the surfaces' offsets, format (R5G6B5 or X8R8G8B8, pitch-linear) and pitch; the triangle's state
  block (the texture, BLEND, CONTROL, fog colour); vertices as Direct3D's TLVERTEX into slots 0-2 and DRAWPRIMITIVE
  0x102; untextured drawing through a 1x1 white texel modulated (Mesa's way).
- **`NTRI0`** (`tests/tnt2/ntri0.c`): one Gouraud triangle at 640x480x16: nothing trapped, its coverage exactly
  OpenGL's (58,057 pixels, none outside, none missing) and the corners' colours right, at the first attempt.
- **Next in T2:** textures (swizzled upload, the formats, filters, mipmaps), Z (clear, test, write), blending, fog,
  culling; then `NTRI` with VTRI's scenes against OpenGL's rules.

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
3. **The FIFO's feeding:** first programmed I/O; then (the same day, once the reference was read: X.org's nv driver
   feeds the NV4 family only through a command buffer, and its older programmed-I/O path was the Riva 128's) the
   reference's way: a command ring in the card's own video memory, the CPU writing commands there and moving the
   FIFO's put pointer. No AGP or system-memory DMA; AGP only if the P3 shows it matters.
