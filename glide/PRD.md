# Product Requirements Document — MGA-Glide

**A Glide 2.x-compatible runtime for Matrox G-series cards under 32-bit protected-mode MS-DOS**

| | |
|---|---|
| Status | v0.4 — implementation started (plan approved 2026-09-26) |
| Last updated | 2026-09-25 |
| Supersedes | v0.1 (initial draft). Corrections are listed in Appendix A |
| Licence | MIT |
| Companion project | DOS-GL (`~/DOSGL`), which shares the Matrox HAL and test harness (D3) and is implemented after this project (D16, Appendix B) |

> Glide and 3dfx are trademarks of their respective owners. MGA-Glide is an independent, Glide-compatible implementation. It is not affiliated with or endorsed by 3dfx or its successors, and it contains no 3dfx code (D14).

---

## 1. Overview & Value Proposition

MGA-Glide is a drop-in replacement for 3dfx's DOS Glide 2.x runtime, `GLIDE2X.OVL`. A DOS game written for a Voodoo Graphics card loads MGA-Glide instead of 3dfx's driver. MGA-Glide translates each Glide call into register programming for a Matrox **G100, G200 or G400-family (G400/G450)** card.

3dfx cards are scarce and expensive. Matrox G-series cards are common and cheap, and unlike almost every other chip of the era they are **publicly documented**: Matrox's G100, G200 and G400 register specifications are still obtainable (§14.2).

### What makes this credible

- **The runtime's binary interface is understood.** `GLIDE2X.OVL` is an ordinary DOS/4G LE-format DLL built with Watcom C. Games load it through DOS/4GW's own DLL loader and look up `__stdcall` exports by name. An Open Watcom DLL with the same export names has already been shown to work, by the OpenGlide/DOSBox pass-through OVL (§6).
- **The register interface is documented.** All three chips share the textured-trapezoid drawing engine MGA-Glide is built on (§5.1).
- **Glide is a thin, screen-space API.** Games hand Glide vertices that are already transformed, projected and lit. Every Glide triangle maps onto one or two Matrox trapezoids; there is no transform or lighting pipeline to write.
- **86Box emulates the G100's 3D engine.** Its model covers perspective-correct texturing, bilinear filtering, 16/32-bit Z, fog, dithering and stipple alpha. The G100 BIOS is in 86Box's official ROM set. The G100 is compiled only into development builds of 86Box, so Loop A uses a locally built 86Box (§5.4, §11.2).
- **86Box also emulates the Voodoo Graphics and Voodoo 2.** One emulated machine can hold both a G100 and a Voodoo, so every test can be run under 3dfx's real Glide to produce a reference frame (§11.4).
- **Real silicon is on the bench.** Physical G100, G200 and G450 cards sit in DOS PCs reachable through the same automated remote loop DOS-GL uses.

### What makes this hard

- **Voodoo features that Matrox chips lack.** The G100 has only stipple ("screen-door") alpha, no alpha test, no hardware mipmapping, and a texture palette shared with the display DAC. The G200 closes most of these gaps. Feature coverage is therefore per chip (§5.3, §7).
- **Host-side triangle setup.** Without the WARP setup engine (D2), the CPU computes every trapezoid's edge and gradient values. This is the principal performance risk (§10, R1).
- **Only games that load Glide dynamically can be helped.** Several well-known titles, including the original *Tomb Raider* and the official *Descent II* 3dfx build, link Glide statically. They program Voodoo registers directly and are out of scope (D13).
- **No memory protection.** A bad MMIO write hangs the machine. Debug infrastructure is a first-class requirement (§9).

---

## 2. Goals & Non-Goals

### 2.1 Goals

| # | Goal |
|---|---|
| G1 | Load in place of 3dfx's `GLIDE2X.OVL` with no modification to the game binary (§6). |
| G2 | Render the acceptance games (§11.6) on the G100 in 86Box, then on physical G100, G200 and G450 cards, with per-chip limitations documented. |
| G3 | Sustain ≥30 FPS at 640×480×16 in the acceptance games on a Pentium II 266–450 MHz reference machine with G200 and G450 (§10). |
| G4 | Offer optional enhancements beyond Voodoo capability from v1.0, off by default (D5, §8). |
| G5 | Share one Matrox HAL and one test harness with DOS-GL (D3). |
| G6 | Maintain local 86Box patches: G100 fixes and a new G200 device model (D4, §12). |
| G7 | Leave the machine at a usable DOS prompt after any crash (§9). |

### 2.2 Non-Goals

| Area | Position |
|---|---|
| Mystique, Mystique 220, Millennium I/II | Not supported (D1). No bilinear filtering, no fog, only fixed stipple patterns for translucency, and no bench hardware. |
| G550 and later | Not targeted. The G550 (`102B:2527`) may work through the G400 path but is untested. |
| Windows Glide (`glide2x.dll`) | Out of scope. Windows already has nGlide, dgVoodoo and OpenGlide. |
| Glide 3.x and Glide 1.x | Out of scope for v1.0. No DOS game is known to load a Glide 1.x runtime dynamically; the early titles link it statically. |
| Games that link Glide statically | Out of scope (D13). |
| DOS extenders other than DOS/4GW | Not supported. The game's Glide loader depends on DOS/4G's DLL services, which DOS/32A does not provide (§6.4). |
| DJGPP DXE build | Not possible. Games only accept a DOS/4G LE DLL (§6). |
| Software fallback | None. Unsupported features degrade (§7); they never fall back to software rasterisation. |
| WARP microcode | Not in v1.0 (D2). |
| AGP texturing / GART | Not used. AGP cards are driven as PCI devices, and all textures live in local video memory. |

---

## 3. Decision Register

Decisions taken during PRD refinement, recorded with rationale so they are revisited deliberately rather than drifted from.

| ID | Decision | Rationale | Revisit at |
|---|---|---|---|
| **D1** | **Card order: G100 (proof of concept) → G200 → G450. Mystique-class parts are dropped.** | The G100 is the only Matrox part 86Box emulates with a usable 3D engine, and a physical G100 is on the bench, so one code path runs in the emulator and on silicon. The G100 cannot do true alpha blending (hardware limit, §5.3), so the G200 is the first card where games are expected to look right. The G450 adds dual texturing and more mip levels. | After M5 |
| **D2** | **Host-side triangle setup on every chip in v1.0. WARP is an optional post-1.0 path for G200/G450.** *Amended:* the WARP drives the G400 family since the speed plan (W0-W6, `docs/g400-warp.md`). The owner brought the G200 forward on 2026-11-01, for DOS-GL's games; it passed on the real card on 2026-11-03 and is on by default, by PCI (`docs/g200-warp.md`). The CPU's setup stays on every chip as the fallback. | Host setup is fully documented in the public specs and is proven to work without WARP: X.org's `mga_exa.c` drives G400 `TEXTURE_TRAP` directly. The WARP instruction set was never published; only the microcode exists. The microcode is redistributable under Matrox's MIT-style `LICENSE.mga` (linux-firmware `matrox/g200_warp.fw`, `g400_warp.fw`), so licensing is not the blocker; debuggability is. | M3 exit, against measured triangle throughput |
| **D3** | **A shared Matrox HAL and test harness with DOS-GL**, extracted into its own repository and consumed by both projects as a git submodule. **MGA-Glide builds the HAL first, for all three chips** (D16); DOS-GL adopts it afterwards. | PCI probing, aperture mapping, FIFO pacing, trapezoid setup, mode setting, crash recovery and both test loops are the same problem in both projects. **Consequence:** MGA-Glide must build with Open Watcom (D11) while DOS-GL builds with DJGPP, so the HAL is written in toolchain-neutral C with a thin per-toolchain shim for DPMI calls, port I/O and inline assembly, and CI builds it with both compilers. | M0 |
| **D4** | **86Box work is a project deliverable, carried as local patches only and never upstreamed:** G100 bug fixes and an emulated G200 device, applied to a pinned upstream commit. | Makes G200-specific code (true alpha blending, alpha test, texture LUT, mip levels, specular) developable without silicon. 86Box's contribution rules require a human author who owns each change, so the patches stay local. §12 defines the work package. | M4 |
| **D5** | **Voodoo fidelity by default; enhancements available in v1.0 through a config file.** | Faithful output is the correctness baseline and what the reference-frame comparison (§11.4) checks. Enhancements such as higher resolutions, forced bilinear filtering, 32-bit Z and true colour are opt-in (§8). | — |
| **D6** | **Performance reference: Pentium II 266–450 MHz.** | Era-matched to G100 and G200. Same reference class as DOS-GL, so HAL optimisation benefits both. | — |
| **D7** | **Same automated remote bench as DOS-GL**, with cards or machines swapped between G100, G200 and G450. | The implementer works from the Linux build host. The number of iterations that need nobody present dominates progress (§11). | M0 |
| **D8** | **Translucency on G100 is stipple.** Glide alpha blending maps to the G100's dithered screen-door alpha. Multiplicative blends such as lightmap passes are unsupported on G100 and are skipped with a logged warning. | Hardware limit. The G100's `ALPHACTRL` blend-factor bits are reserved: the G100 spec says they "must be set to 01010100b for compatibility with future products". | — |
| **D9** | **Milestones are defined by verifiable exit criteria, not dates.** | Calendar estimates on a hardware bring-up project are fiction. Replaces v0.1's week-based plan. | — |
| **D10** | **Serial logging, Glide call tracing, register write tracing and crash recovery are in-scope requirements.** | On a platform with no protection and no debugger, these are the development environment. | — |
| **D11** | **The runtime is an Open Watcom–built DOS/4G LE DLL named `GLIDE2X.OVL`,** cross-compiled on Linux, exporting 3dfx's uppercase `__stdcall`-decorated names. | Forced by the game-side loader (§6). DJGPP cannot produce a loadable module. | — |
| **D12** | **Clean-room headers.** MGA-Glide's Glide types, constants and prototypes are written from the public API documentation and the observed ABI. No 3dfx SDK headers, libraries or source are copied. | The 1999 Glide source licence has copyleft terms for derivative works and is incompatible with MIT. The retail SDK EULA forbids use in developing competing products. | — |
| **D13** | **Games that link Glide statically are out of scope**, recorded as a possible future research track with no commitment. | They program Voodoo registers directly. Supporting them would mean trapping Voodoo MMIO accesses through a custom DPMI host that emulates a Voodoo, which is a different project. | After M7 |
| **D14** | **The name "MGA-Glide" is kept**, with a trademark disclaimer and no 3dfx logos. | Common practice among Glide-compatible projects (OpenGlide, nGlide). The 1999 Glide licence forbids use of the GLIDE mark without permission; the current owner of the mark is unverified (Q5). | Before first public release |
| **D15** | **Acceptance games: *Screamer Rally* (`STRT3FX.EXE`) and *GTA* (1997 ECTS build, `COMPFX.EXE`).** | Both load `GLIDE2X.OVL` dynamically, run under DOS/4GW, need no CD for data, and are on hand. Screamer 2 was dropped because it is not available. | M1 |
| **D16** | **MGA-Glide is implemented first; DOS-GL follows.** The shared HAL, the test harness and the 86Box work (G100 fixes and the G200 model) are delivered by this project. Once they land, DOS-GL's PRD is revised to build on them (Appendix B). | DOS-GL's own plan assumed it would build the HAL and harness itself, targeting the G400 first. Building them once, here, avoids duplicated bring-up, and DOS-GL then starts with working G100, G200 and G450 support, two test loops and an emulated G200. | MGA-Glide M7 |
| **D17** | **M0–M4 (the G100 proof of concept) exit in 86Box alone.** Milestone S then re-verifies M1–M4 on the physical G100 before G200 work starts. | The physical cards come after the emulated proof of concept; bench provisioning runs in parallel. | S |
| **D18** | **Test programs load an OVL with `leload`, a clean-room LE loader** written from the public LE format. | DOS/4GW 1.97 may refuse DLL imports, and 3dfx's import-library resolver is off-limits (D12). The same test binary then runs against MGA-Glide or a retail OVL. | M0 spike |

---

## 4. Target Hardware & Environment

### 4.1 Graphics hardware

| Chip | PCI `102B:` | Bus | Role |
|---|---|---|---|
| MGA-G100 (Productiva G100) | `1000` PCI, `1001` AGP | PCI/AGP | **v1.0 proof-of-concept card.** Emulated in 86Box development builds; physical card on the bench |
| MGA-G200 (Millennium/Mystique/Marvel G200) | `0520` PCI, `0521` AGP | PCI/AGP | **v1.0, the first card expected to look right.** Physical card on the bench; 86Box model built under D4 |
| G400 / G450 | `0525` (G450 is revision ≥ `0x80`) | AGP/PCI | **v1.0, the full-featured card.** Physical G450 on the bench |
| G200eR2 (server management-controller variant) | `0534` | PCI (embedded) | **Reference only**, not a target. See §4.4 |
| Mystique (1064SG), Mystique 220 (1164SG), Millennium (2064W), Millennium II (2164W) | `051A`, `051A`, `0519`, `051B`/`051F` | | Not supported (D1) |

PCI IDs are from Linux `include/linux/pci_ids.h`, xf86-video-mga `src/mga.h` and 86Box `vid_mga.c`. The Mystique and Mystique 220 share `051A` and differ only by revision.

### 4.2 PCI aperture layout

This is the X.org `mga` "new BARs" layout, which covers the Mystique onward. It was confirmed on the G200eR2 (§4.4).

| BAR | Offset | Contents |
|---|---|---|
| BAR0 | `0x10` | Linear framebuffer (local video RAM) |
| BAR1 | `0x14` | MMIO control aperture (16 KB) |
| BAR2 | `0x18` | ILOAD / pseudo-DMA window (8 MB) |

Confirming this on the G100, G200 and G450 is an M1 exit criterion.

### 4.3 Host platform

| Item | Requirement |
|---|---|
| OS | MS-DOS 6.22, MS-DOS 7.x, or FreeDOS 1.3 |
| CPU | Pentium II 266–450 MHz reference (D6). A Pentium MMX may work, with no performance guarantee |
| Extender | **DOS/4GW**, as shipped with the game (§6.4) |
| Mode | 32-bit protected mode, inside the game's process |
| System RAM | Whatever the game requires, plus MGA-Glide's texture shadow copies and LFB shadow buffers (§7.5, §7.8) |
| Video RAM | G100: 4–8 MB. G200: 8–16 MB. G450: 16–32 MB |

### 4.4 The G200eR2 on `cuda6`

A Dell PowerEdge T620 on the network (`retro@cuda6`, Ubuntu 14.04, kernel 4.4, passwordless sudo) carries a Matrox **G200eR2** (`102B:0534`, subsystem Dell `1028:04CF`, revision 0) at PCI `0a:00.0`. It is the machine's boot VGA device and has no kernel driver bound. It was inspected read-only on 2026-09-25:

- The apertures are BAR0 16 MB prefetchable framebuffer, BAR1 16 KB MMIO and BAR2 8 MB ILOAD window, matching §4.2.
- xf86-video-mga names it "mgag200 ER SH7757". The G200 core is embedded in the iDRAC's Renesas SH7757 management controller.
- **Whether the G200e server variants kept a working 3D engine is unknown.** No open driver ever enabled 3D on any G200e. X.org's EXA Composite path is limited to G400/G550, and the old DRM `mga` driver only matched `0520`, `0521` and `0525`.

**Role:** a Linux-accessible G200-family register reference for building the 86Box G200 model (§12). Linux userspace can map BAR1 through `/sys/bus/pci/devices/0000:0a:00.0/resource1` and read or write registers with no DOS in the loop.

**Constraint:** `cuda6` runs production workloads (`rllm-server` on its NVIDIA GPUs). A drawing-engine hang on the Matrox could wedge the PCI bus and take the whole server down. **MMIO reads and writes are approved (2026-09-26), scoped to `/sys/bus/pci/devices/0000:0a:00.0/*`, `~/mga-rig/` and `apt-get install gdb`.** Nothing else on the host is touched. The staged probe plan is Q9.

---

## 5. Hardware Capability Matrix

### 5.1 Why the trapezoid model transfers

All three chips share one drawing-engine model:

- `DWGCTL` selects an opcode: `TRAP`, `TEXTURE_TRAP`, `BITBLT`, `ILOAD` and others.
- Edge slopes go in `AR0`–`AR6`.
- Colour and Z gradients go in `DR0`–`DR15`.
- Texture gradients go in `TMR0`–`TMR8`.
- Alpha and fog gradients go in `ALPHASTART/XINC/YINC` and `FOGSTART/XINC/YINC`.

Setup code written against the G100 carries to the G200 and G400, with per-chip additions. Where encodings differ between chips, the HAL's capability table records the difference and it is verified on silicon (R6).

### 5.2 Voodoo reference

From the Glide 2.43 SDK documentation and `glide.h`.

| Voodoo feature | Voodoo Graphics / Voodoo 2 |
|---|---|
| Colour | 16-bit RGB565 framebuffer, dithered |
| Depth | 16-bit Z or W buffer, plus Z/W compare-to-bias |
| Texture size | 256×256 maximum (`GR_LOD_256` to `GR_LOD_1`); aspect ratios 8:1 to 1:8 |
| Texture formats | 8-bit: RGB_332, YIQ_422 (NCC), ALPHA_8, INTENSITY_8, ALPHA_INTENSITY_44, P_8. 16-bit: ARGB_8332, AYIQ_8422 (NCC), RGB_565, ARGB_1555, ARGB_4444, ALPHA_INTENSITY_88, AP_88 |
| Texture tables | NCC0, NCC1, palette |
| Filtering | Point, bilinear; mipmapping disabled, nearest or nearest-dithered; LOD bias in quarter steps; trilinear with two TMUs |
| TMUs | Voodoo Graphics: 1 (2 on some boards). Voodoo 2: 2 |
| Blending | Full source/destination factor set, including `ALPHA_SATURATE` and `PREFOG_COLOR` |
| Alpha test | Yes |
| Chroma key | Yes, on the post-combine colour |
| Fog | 64-entry table, per pixel |
| Vertex precision | 12.4 fixed point; applications are told to snap coordinates to 1/16 pixel |
| LFB | Direct read/write, returned stride, upper-left or lower-left origin |

### 5.3 Matrox capabilities

Sources: G100 spec (Feb 1998), G200 spec (Nov 1998), G400 spec (Jun 1999), Mesa 7.11 `mga` driver, 86Box `vid_mga.c`.

| Capability | G100 | G200 | G400 / G450 |
|---|---|---|---|
| Alpha blending | **Stipple only.** `ALPHACTRL` has only `astipple` and `alphasel`; the blend-factor bits are reserved | **True**, with full source/destination factors, plus stipple | True |
| Alpha test | No | Yes | Yes |
| Alpha source | Texture, interpolated, or modulated | Same | Same |
| Bilinear filtering | Yes | Yes | Yes |
| Mipmapping | **None in hardware.** A single `TEXORG`; the driver picks one level per polygon | 5 levels (`TEXORG`–`TEXORG4`); trilinear possible | 11 levels |
| Texture formats | TW4 and TW8 (through the **display DAC** palette), TW12 (4444), TW15 (1555), TW16 (565) | Adds TW32 (8888) and TW422 (YUV). TW4/TW8 go through a **dedicated texture LUT** | As G200, plus dual texture |
| Max texture | Up to 2048 wide (11-bit `twmask`) | 2048×2048 | 2048×2048 |
| Texture colour key | Yes (`TEXTRANS`; alpha key on TW15/TW12) | Yes | Yes |
| Z buffer | 16 or 32 bit | 16 or 32 bit, no stencil | 16, 32, 15+1 stencil, 24+8 stencil |
| Fog | Yes, interpolated factor | Yes | Yes |
| Specular | No | Yes | Yes |
| Dual texture | No | No | Yes (`TEXCTL2`, `TDUALSTAGE0/1`) |
| Setup engine | None | WARP (unused in v1.0, D2) | WARP (unused in v1.0) |
| Bus FIFO | 64 entries (`FIFOSTATUS.fifocount`) | To confirm (Q6) | To confirm (Q6) |

### 5.4 What 86Box models

Verified against 86Box source at tag `v6.0` (released 2026-05-31) and master `bcce80a` (2026-09-25).

**Build requirement.** The G100 is compiled only when 86Box is configured with `-DDEV_BRANCH=ON` (the `G100` CMake option depends on `DEV_BRANCH`). Official release binaries, v6.0 included, do not offer it. Loop A therefore always runs a locally built 86Box, which is also where §12's patches are carried.

**Version.** v6.0 and master share the same G100 3D code. Changes since v6.0 are display fixes: CRTC start address handling, a 27 MHz PLL reference clock for the G100, a 2 MB G100 variant, and a Millennium II AGP variant. Loop A builds from master.

| Area | 86Box G100 behaviour | Matches G100 silicon? |
|---|---|---|
| `TEXTURE_TRAP` | Implemented for `ATYPE` I and ZI, 16- and 32-bit destinations | Yes, as far as it goes |
| `TRAP` | BLK, RPL, RSTR, I and ZI | Yes |
| Texture formats | TW4, TW8, TW12, TW15, TW16 | Yes; these are the G100's formats |
| Palette for TW4/TW8 | The DAC LUT | Yes, per the spec |
| Perspective correction | Through `TMR8` (q) | Yes |
| Bilinear filtering | Two sequential linear interpolations | **Bug:** with perspective on, the T fraction is computed from S (`persp_correct` sets `*t_frac` from `s_d`) |
| Alpha | `alphasel` honoured; result applied as a 4×4 Bayer screen-door | Yes; stipple is what the G100 does |
| Fog | G100-gated, per-pixel blend with `FOGCOL` | Yes |
| Z | 16 and 32 bit, all `zmode` compares | Yes |
| Unsupported combinations | **`fatal()` aborts the emulator** on an unknown texture format, `TEXCTL` combination or `ATYPE` | Emulator robustness bug |
| FIFO depth, engine timing, alignment | Not modelled strictly | No (R3) |
| Pseudo-DMA / bus-master DMA | `PRIMADDRESS` / `SECADDRESS` present | Partially; to confirm per use |

No other emulator models a G200 or G400: not 86Box, PCem, MAME, DOSBox-X, QEMU or Bochs.

---

## 6. Binary Interface (ABI)

Established from a retail Voodoo Graphics `GLIDE2X.OVL`, the Glide 2.43 DOS SDK (Oct 1997), and the OpenGlide DOSBox OVL. Only the observable binary interface is used; no 3dfx code or headers are copied (D12).

### 6.1 What `GLIDE2X.OVL` is

- **Format:** an MZ stub ("this is a DOS/4G dynamic link library") followed by a standard **LE DLL**. Module flags are `0x8200` (library bit set), with two objects and **no imported modules**: the DLL carries its own statically linked C runtime.
- **Producer:** Watcom C/C++ 10.x, linked as `SYS dos4g dll`.
- **Initialisation:** a DLL init entry point, run once at load.
- **Exports:** by name in the resident-name table. The retail Voodoo Graphics OVL exports about 150 names: the public API plus internal `__GR*` helpers and `_PCI*` functions.

### 6.2 Export naming and calling convention

- Every public function is `__stdcall`: arguments on the stack, callee pops, floats passed as 4-byte stack values.
- Names are uppercase and decorated with the argument byte count, for example `_GRGLIDEINIT@0`, `_GRDRAWTRIANGLE@12`, `_GRSSTWINOPEN@28`, `_GRLFBLOCK@24`, `_GUTEXALLOCATEMEMORY@60`. The uppercasing comes from linking with `option nocaseexact`.
- 3dfx shipped two import libraries (register and stack builds of the game). Both reference the same decorated names, so one export table serves both.
- **Requirement:** MGA-Glide exports every name referenced by the Glide 2.43 import library (136 decorated names), and every additional name referenced by each acceptance game. A build tool extracts the names a game references from its EXE (the loader thunks carry them as strings), and M1 checks that MGA-Glide exports all of them.

### 6.3 How games load it

- The game statically links 3dfx's import library, `glide2x.lib`. Each API symbol in it is a small thunk that calls a resolver on first use.
- The resolver loads `GLIDE2X.OVL` through DOS/4G's built-in LE loader and looks each function up by name.
- The search covers at least the game's directory, `PATH` and `C:\WINDOWS\SYSTEM\`. The exact order is unverified (Q10).
- On failure the game prints "Fatal error: unable to load DLL." or "…unable to find entry point in DLL."
- **Installation is therefore:** replace the game's own `GLIDE2X.OVL` (GTA and others ship one) or place MGA-Glide where the loader finds it first.

### 6.4 Execution environment

- MGA-Glide runs inside the game's process under **DOS/4GW**. DOS/32A does not provide the DOS/4G DLL services, so games whose extender has been swapped for DOS/32A will not load any Glide OVL.
- It must not assume CWSDPMI or DJGPP's runtime, and must restore every interrupt vector and exception handler it touches.
- Aperture mapping uses DPMI function `0x0800` (physical address mapping) under DOS/4GW (to verify on the bench, Q3).
- **FPU state belongs to the game.** 3dfx told applications to run the FPU at 24-bit precision for vertex snapping. Setup arithmetic must be correct at single precision, and MGA-Glide must not change the control word without restoring it.

### 6.5 Export tiers

- **Tier 1, needed for any game to start:** `grGlideInit`, `grGlideShutdown`, `grSstQueryHardware`, `grSstSelect`, `grSstWinOpen`, `grSstWinClose`, `grGlideGetVersion`, `grErrorSetCallback`, `grBufferClear`, `grBufferSwap`, `grDrawTriangle`, `grClipWindow`, `grRenderBuffer`, `grSstIdle`.
- **Tier 2, state and texturing used by the acceptance games:** depth, blend, cull, combine, texture download/source/filter/clamp/mipmap, fog, chroma key, LFB lock/unlock, and the `gu*` utility functions.
- **Tier 3, everything else:** exported and logged. Unimplemented calls return plausible success values and emit a one-time serial warning with the call name and arguments.

The per-function status table lives in `docs/api-coverage.md` and is the tracker for §15.

### 6.6 Hardware identity reported to the game

`grSstQueryHardware` reports a **Voodoo Graphics** (`GR_SSTTYPE_VOODOO`) with one TMU and 2 MB of texture memory by default. Games branch on this data, so it is configurable (§8). Reporting two TMUs on the G450 is a post-1.0 enhancement.

---

## 7. Translation Design

### 7.1 Layers

```
+----------------------------------------------------------+
|  DOS/4GW game, linked with 3dfx's glide2x.lib stub       |
+----------------------------------------------------------+
        | loads GLIDE2X.OVL, calls __stdcall exports (§6)
        v
+----------------------------------------------------------+
|  MGA-Glide (Open Watcom LE DLL)                          |
|   - Export table and DLL init                      §6    |
|   - Glide state mirror + call trace                §9    |
|   - Combine / blend / depth translation            §7.3  |
|   - Texture manager (TMU address virtualisation)   §7.5  |
|   - LFB emulation                                  §7.8  |
+----------------------------------------------------------+
        | HAL calls (trapezoids, state words, uploads)
        v
+----------------------------------------------------------+
|  Shared Matrox HAL (with DOS-GL, D3)                     |
|   - PCI probe, BAR mapping, capability table             |
|   - Triangle → trapezoid setup (host-side, D2)           |
|   - FIFO pacing, engine idle, mode set, page flip        |
|   - Crash recovery, register write trace                 |
+----------------------------------------------------------+
        | MMIO through a DPMI 0x0800 mapping
        v
+----------------------------------------------------------+
|  G100 / G200 / G450, physical or 86Box                    |
+----------------------------------------------------------+
```

Chip differences are resolved at runtime from the PCI device ID through the HAL capability table, not by compile-time profiles as v0.1 proposed. One binary supports all three chips.

### 7.2 Triangle setup

Glide vertices (`GrVertex`) arrive in screen space: `x`, `y`, an ignored `z`, `r g b` (0–255), `ooz` (65535/Z), `a` (0–255), `oow` (1/w), then per-TMU `sow`, `tow`, `oow`. With the default of two TMUs in the header, the structure is 60 bytes, which matters for `grDrawPolygonVertexList`.

For each triangle the HAL:

1. Snaps x and y to 1/16 pixel itself, because not every game follows 3dfx's snapping advice.
2. Culls by signed area when `grCullMode` requires it.
3. Sorts vertices by y and splits the triangle into an upper and a lower trapezoid.
4. Computes edge slopes (`AR*`) and the x/y gradients of every enabled interpolant (Z or 1/w, R, G, B, A, fog, s/w, t/w, 1/w), and writes the start values at the first scanline.
5. Issues `TEXTURE_TRAP` or `TRAP` with the `DWGCTL` word for the current state.

Sub-pixel coverage must match the Voodoo's fill convention, verified against reference frames (§11.4).

### 7.3 Combine, blend and depth mapping

| Glide feature | G100 | G200 / G450 |
|---|---|---|
| Colour combine: iterated, texture, texture × iterated, constant | `TEXCTL` decal/modulate; constant colour through flat `DR` values | Same |
| Colour combine: texture + iterated (additive) | Unsupported; logged | Specular path |
| Other combine modes | Classified in `docs/combine-coverage.md` as supported, approximated or unsupported | Same, with a larger supported set |
| `grAlphaBlendFunction` | Stipple from the chosen alpha source (D8). Blends other than src-alpha / one-minus-src-alpha are skipped with a warning | Native `ALPHACTRL` factors |
| `grAlphaTestFunction` | Approximated with the texture alpha key (`TEXTRANS`) for 1-bit-alpha cut-out textures | Native alpha test |
| `grChromakeyMode` | Texture colour key when the combine output is the texture; otherwise unsupported | Same |
| `GR_DEPTHBUFFER_ZBUFFER` | 16-bit Z from `ooz` | Same, or 32-bit (enhancement) |
| `GR_DEPTHBUFFER_WBUFFER` | 32-bit Z buffer holding 2³¹·(1/w), compare sense reversed (nearer is larger). More precise than the Voodoo's 16-bit float everywhere. Z16 fallback only when VRAM is short | Same |
| Depth bias | Added to the Z start value | Same |
| Fog table | Evaluate the 64-entry table at each vertex's w; interpolate the factor linearly | Same |
| Dithering | `MACCESS` dither modes | Same |
| Clip window | `CXBNDRY`, `YTOP`, `YBOT` | Same |

W-buffer emulation is approximate, because the Voodoo stores a floating-point-encoded depth with a different precision distribution. Z-fighting differences against reference frames are expected and tolerated within the comparison threshold.

### 7.4 Mipmapping

- **G100:** no hardware mip chain. The texture manager selects one level per triangle from its screen-space texel density and programs that level's origin. Level transitions will be visible where a Voodoo changes level per pixel.
- **G200:** up to 5 levels per texture. Glide mip chains of up to 9 levels (256 down to 1) are truncated to the 5 largest levels needed.
- **G450:** up to 11 levels, native.

### 7.5 Texture memory

Glide exposes raw TMU address space. Games call `grTexMinAddress` and `grTexMaxAddress` and choose texture addresses themselves. MGA-Glide:

- reports a TMU address range sized for the reported board (2 MB by default, §6.6);
- keeps a table from Glide TMU address ranges to Matrox VRAM allocations, because formats that expand on upload (P_8, NCC, 8-bit formats) take more Matrox memory than the game believes;
- evicts least-recently-used textures when VRAM runs out, re-uploading from a system-RAM shadow copy.

### 7.6 Texture format conversion

| Glide format | G100 | G200 / G450 |
|---|---|---|
| RGB_565, ARGB_1555, ARGB_4444 | Native (TW16, TW15, TW12) | Native |
| P_8 | Expand to 16-bit on upload and re-expand on palette change. The G100's TW8 palette is the DAC's, which also drives the display and `grGammaCorrectionValue`, so native TW8 is not used | Native TW8 through the texture LUT, loaded with `MACCESS.tlutload` |
| AP_88 | Expand to ARGB_4444 | To confirm (Q7) |
| RGB_332, ALPHA_8, INTENSITY_8, ALPHA_INTENSITY_44, ALPHA_INTENSITY_88, ARGB_8332 | Expand to the nearest native 16-bit format on upload | Same, or TW32 when the enhancement is on |
| YIQ_422, AYIQ_8422 (NCC) | Decode with the current NCC table to RGB_565 / ARGB_4444 on upload | Same |

Texture download cost matters for games that stream textures every frame. Conversion routines are unit-tested on the host (§11.5).

### 7.7 Framebuffer, swap and modes

- Glide defines resolutions from 320×200 to 1600×1200. v1.0 supports those a Voodoo Graphics could display: 320×200, 320×240, 400×256, 512×384, 640×200, 640×350, 640×400 and 640×480. 800×600 is supported as a Voodoo 2 would. Larger modes are an enhancement (§8).
- Front, back and depth buffers are allocated in VRAM. Buffer pitch is 1024 pixels where VRAM allows, matching the 2048-byte LFB stride of a Voodoo for games that ignore the returned `strideInBytes`; display modes wider than 1024 use their width rounded up to 32. Resolutions the BIOS lacks are drawn into render buffers at pitch 1024 and scaled into a BIOS mode at each swap (docs/config.md, Resolutions).
- `grBufferSwap` flips by CRTC start address and waits on vertical retrace when the swap interval requires it.
- Mode setting uses the card's VBE BIOS in v1.0, behind the HAL interface, as in DOS-GL. Modes that the BIOS does not offer (Q8) are set by programming the CRTC directly, through the same HAL interface.

### 7.8 Linear framebuffer access

- `grLfbLock` returns a pointer straight into mapped VRAM when the requested format and origin match the buffer: RGB565, upper-left origin, no pixel pipeline.
- Every other case uses a system-RAM shadow buffer that is converted and written back on `grLfbUnlock`. That covers the 555, 1555, 888 and 8888 write modes, the `*_DEPTH` variants, ZA16, lower-left origin and pixel-pipeline writes.
- `grLfbReadRegion` and `grLfbWriteRegion` use direct reads and `ILOAD`.

---

## 8. Enhancements (D5)

Off by default. Enabled through `MGAGLIDE.CFG`, found next to the runtime or through an environment variable.

| Option | Chips | Notes |
|---|---|---|
| Resolution override (800×600, 1024×768) | All, VRAM permitting | Scales coordinates at setup; LFB access returns a shadow at the game's resolution |
| Forced bilinear filtering | All | Replaces point sampling |
| 32-bit Z | All | Reduces Z-fighting; costs VRAM and fill rate |
| 32-bit colour | G200, G450 | TW32 framebuffer; LFB access through a shadow buffer |
| Reported board (Voodoo Graphics / Voodoo 2, TMU count, texture memory) | All | Compatibility setting |
| Trilinear filtering | G200, G450 | Uses the hardware mip chain |

Every option is written to the serial log at startup, so a bug report always shows the active configuration.

---

## 9. Reliability, Debug and Safety (D10)

Shared with DOS-GL through the HAL (D3):

- **Serial log:** COM1 16550 at 115200 8N1, unbuffered and synchronous so the last line survives a hang. Levels are compiled out of release builds.
- **Register write trace:** a ring buffer of recent MMIO writes, dumped on crash, on demand, or at exit. A host-side replay tool re-runs a trace against 86Box or a register stub.
- **Crash recovery:** DPMI exception handlers for at least #GP, #PF and #UD. On exception or exit, MGA-Glide idles and resets the drawing engine, restores text mode, flushes the trace, prints a diagnostic, and chains to the game's previous handlers.

Specific to MGA-Glide:

- **Glide call trace:** every `gr*` and `gu*` call with its arguments, with texture and LFB payloads by reference, recorded to a file or serial. A trace captured from a game replays on the host (§11.3), so rendering bugs can be studied without the game, the DOS machine or redistributing game data.
- **Unimplemented-call warnings:** one per function (§6.5).

---

## 10. Performance Requirements

### 10.1 Targets

| Metric | Target | Conditions |
|---|---|---|
| Frame rate | ≥30 FPS | Acceptance games, 640×480×16, Pentium II 266–450 reference (D6), G200 and G450 |
| Frame rate on G100 | Measured and published | The G100 is the proof-of-concept card and carries no FPS guarantee |
| Triangle throughput | Measured at M3 | Textured, Gouraud, Z-tested, perspective-correct, host setup |
| Glide call overhead | Measured per call at M3 | Replaces v0.1's unmeasurable "<5% overhead" |

### 10.2 Where the budget goes

Voodoo-era games draw on the order of a few thousand triangles per frame. At 30 FPS that means a sustained rate in the low hundreds of thousands of triangles per second, all set up on the CPU (D2). This is an estimate to be replaced by measurement at M3. Fill rate is not the constraint at 640×480. **Host setup is the principal risk (R1)**, the same risk DOS-GL carries, and optimisation benefits both projects through the shared HAL.

### 10.3 Optimisation levers, in expected order of value

1. Write-combining on the MMIO and framebuffer apertures, where DOS/4GW allows it. This is an open investigation shared with DOS-GL.
2. Pseudo-DMA through the BAR2 ILOAD window instead of individual register writes.
3. A hand-optimised setup inner loop.
4. WARP on the G200: brought forward (D2), done 2026-11-03 (`docs/g200-warp.md`).

---

## 11. Test Strategy

### 11.1 Bench

| Machine | Role | Cards |
|---|---|---|
| Linux build host | Cross-compiles; runs 86Box and host unit tests; receives serial logs; captures video from the DOS PCs | — |
| Remote DOS PC(s) | Real-silicon loop, as DOS-GL's Loop B: mTCP fetch-and-run on boot, serial log, video capture | G100, G200, G450, swapped or in separate machines |
| Pentium II reference | Performance numbers only | G200 and G450 |
| `cuda6` | Linux register reference for the 86Box G200 model (§4.4); MMIO approved within the stated scope | G200eR2 |

### 11.2 Loop A: 86Box with the G100 (autonomous, emulated)

The harness is shared with DOS-GL: 86Box built from source with `DEV_BRANCH` on (§5.4) and run under Xvfb, a FAT disk image built with `mtools`, an `AUTOEXEC.BAT` running the test, COM1 bound to a host pty, and screenshots driven by `xdotool`. **Green in 86Box means "semantically plausible", never "works on silicon"** (R3).

### 11.3 Glide trace replay (host only)

Traces captured in Loop A or Loop B replay on Linux against the HAL's register stub and a reference software rasteriser. This catches setup-arithmetic bugs with no emulator in the loop.

### 11.4 Reference frames from the emulated Voodoo

The emulated machine holds a G100 (AGP) **and** a Voodoo Graphics (PCI). Both acceptance games ship Voodoo Graphics OVLs, so a Voodoo 2 would need a separate OVL. Each test or game scene runs twice: once with a retail 3dfx `GLIDE2X.OVL` rendering on the Voodoo, and once with MGA-Glide on the G100. The Voodoo frame is the reference. Comparisons use a per-chip tolerance, and known limitations (D8, §7.3, §7.4) are masked or recorded as expected differences.

- 86Box's Voodoo model is a reference for behaviour, not proof of silicon behaviour.
- The retail 3dfx OVL is a local test fixture. It is never committed to the repository or redistributed.

### 11.5 Host unit tests

Setup arithmetic, texture format conversion, NCC decoding, fog-table evaluation, W-to-depth mapping and TMU address virtualisation are ordinary C and are unit-tested natively on Linux.

### 11.6 Conformance programs and acceptance games

**Conformance programs** are written for this project under MIT and produce screenshots comparable under §11.4. They cover:

- clearing, and flat and Gouraud triangles;
- Z and W buffering, and culling;
- each texture format, point and bilinear filtering, and perspective correction;
- each alpha blend function, alpha test and chroma key;
- the fog table and the clip window;
- LFB read and write in each format and origin;
- buffer-swap timing.

They load the OVL through `leload`, the project's clean-room LE loader (D18), or link the runtime statically. Only the real games exercise the game-side loader.

Frames are read back with `grLfbReadRegion`, so the same program runs on a retail OVL with a Voodoo and on MGA-Glide. Reference frames rendered through the retail OVL are committed as PNGs; the OVL itself never is.

**Game references.** `GLTRACE.OVL` is a proxy that forwards every call to the retail OVL and records a binary call trace. `GLPLAY.EXE` replays a trace through any OVL. Replaying a Voodoo-recorded game trace on both paths is the repeatable in-game image check.

**Acceptance games (D15):**

| Game | Executable | Notes |
|---|---|---|
| *Screamer Rally* | `STRT3FX.EXE` | External DOS/4GW 1.97; ships Glide 2.42 Voodoo Graphics/Rush OVLs |
| *GTA* (1997 ECTS build) | `GTADOS\COMPFX.EXE` | DOS/4GW Professional built in; ships a Voodoo Graphics `GLIDE2X.OVL`, which is the default reference OVL |

Each game is run in Loop A against the emulated-Voodoo reference, then in Loop B on every card.

### 11.7 Loop B: physical cards

Same as DOS-GL: a remote DOS PC, fetch-and-run over mTCP, serial log, video capture. Every milestone that touches the card is developed in Loop A and passes only in Loop B.

---

## 12. 86Box Work Package (D4)

Local patches in `tools/86box/patches/`, applied to a pinned upstream commit by `tools/86box/build.sh`. They are never upstreamed (D4). DOS-GL consumes the same patch set.

| Item | Scope | Needed by |
|---|---|---|
| WP-1 | Fix the bilinear T fraction under perspective correction (`persp_correct`). | M4 |
| WP-2 | Replace `fatal()` on an unknown texture format, `TEXCTL` combination or `ATYPE` with a logged warning and a skipped primitive, so a driver bug does not kill the emulator (`fatal()` shows a blocking message box, which hangs a headless run). Widen the legal `TEXCTL` set to the spec's table. | M0 |
| WP-1b | True 2×2 bilinear, `floor` for negative coordinates, wrap at the texture edge, and precision before the divide, each checked against physical-G100 captures. | S |
| WP-3 | **Emulated G200 device** (`102B:0520` PCI, `0521` AGP): register map where it differs from the G100; true `ALPHACTRL` blending and alpha test; texture LUT with `tlutload`; TW32 and TW422; 5 mip levels with `TEXORG1`–`TEXORG4` and trilinear; specular (`SPECR/G/B START/XINC/YINC`); 16/32-bit Z. WARP registers present but inert, logging any use. Gated behind `DEV_BRANCH`, like the G100. | Skeleton M4, complete M5 |
| WP-4 | A G200 BIOS image for the local ROM set, dumped from the bench G200's PCI expansion ROM by `ROMDUMP.EXE`. | S / M5 |

**Ground truth for WP-3** comes from the G200 specification, the Mesa 7.11 `mga` driver, register experiments on the bench G200 in Loop B, and, subject to Q9, register experiments on the `cuda6` G200eR2.

---

## 13. Build and Toolchain

| Item | Choice |
|---|---|
| Runtime compiler | **Open Watcom** (`wcc386`), cross-hosted on Linux |
| Runtime link | `wlink`, `format os2 le dll initglobal`, uppercase exports (`option nocaseexact`), `__stdcall` exports, statically linked C runtime, no imported modules. This matches the working OpenGlide DOSBox OVL |
| Output | `GLIDE2X.OVL` |
| Shared HAL | Toolchain-neutral C, built by CI with both Open Watcom (MGA-Glide) and DJGPP (DOS-GL) (D3) |
| Test programs | Open Watcom, DOS/4GW, linked against the project's clean-room import stub |
| Host tests | Native GCC on Linux |
| 86Box | Built from source with `DEV_BRANCH` on, plus §12 patches |
| Build system | Makefile |
| CI | GitHub Actions: cross-build with both compilers, host unit tests, and Loop A with conformance programs and reference-frame comparison, with artefacts attached |

---

## 14. Licensing & Provenance

### 14.1 Project licence

MIT. Register definitions may be derived, with attribution, from the MIT-licensed X.org `xf86-video-mga` driver and Mesa `mga` driver, and from Matrox's published specifications.

### 14.2 Specifications

| Document | Location |
|---|---|
| MGA-G100 Specification (Feb 1998) | `bitsavers.informatik.uni-stuttgart.de/components/matrox/_dataSheets/MGA-G100_199802.pdf`; also on vgamuseum.info |
| MGA-G200 Specification (Nov 1998) | Same directory, `MGA-G200_199811.pdf` |
| MGA-G400 Specification (Jun 1999) | `bitsavers.informatik.uni-stuttgart.de/pdf/matrox/G400SPEC_Jun1999.PDF` |

`www.bitsavers.org` refuses scripted downloads, so use the Stuttgart mirror. Specs are fetched by a script, not committed, unless their redistribution terms are confirmed.

### 14.3 Glide material (D12)

- The 1999 Glide 2.x/3.x source release is under the "3DFX GLIDE Source Code General Public License". Derivative works must carry the same licence and 3dfx's notices, and it forbids use of the GLIDE trademark without permission. It is incompatible with MIT.
- The retail SDK EULA forbids reverse engineering and forbids using the SDK "in connection with the development of products competitive with 3Dfx chips, drivers, APIs".
- MGA-Glide therefore uses **clean-room headers** written from public API documentation and the observed ABI: names, argument sizes, constants and structure layouts. No 3dfx header, library, source file or binary is committed.
- OpenGlide ships copies of 3dfx SDK headers and is not a model to follow here.

### 14.4 WARP microcode

Not used in v1.0 (D2). If adopted later, it is redistributable under `LICENSE.mga` and ships as a separate file, never embedded in the runtime.

---

## 15. Milestones

Defined by exit criteria (D9). M0–M4 exit in Loop A alone (D17); milestone S re-verifies them on the physical G100; M5 onward pass in Loop B.

**M0: harness and skeleton.** The shared HAL repository exists; both test loops work for MGA-Glide; an empty Open Watcom LE DLL builds.
*Exit:* one command builds the runtime, boots 86Box with a G100 and a Voodoo Graphics, runs a test program under both a retail 3dfx OVL and MGA-Glide, and returns serial logs and screenshots. The loader spike's results are recorded in `docs/loader.md`.

**M1: loads and identifies.** The full export table; the export-name extraction tool; PCI probe, BAR mapping and the capability table for G100, G200 and G450.
*Exit:* both acceptance games load MGA-Glide in 86Box without a loader error, call `grGlideInit`, `grSstQueryHardware` and `grSstWinOpen`, log every Glide call, and exit cleanly. Call traces give the first feature census.

**M2: clears and swaps.** Mode set, buffer allocation, `grBufferClear`, `grBufferSwap` and crash recovery. WP-2 merged or carried locally.
*Exit:* the clear and swap-timing conformance programs match the Voodoo reference. A forced #GP returns to a working DOS prompt in 86Box.

**M3: untextured triangles.** Host setup for flat and Gouraud triangles with Z and W buffering and culling. Triangle throughput and per-call overhead measured on the reference machine.
*Exit:* the flat, Gouraud, depth and culling conformance programs pass against the Voodoo reference on the 86Box G100.

**M4: textures on the G100.** Texture manager, all format conversions, perspective correction, point and bilinear filtering, stipple alpha, fog, and per-polygon mip selection. WP-1 done; WP-3 and WP-4 started.
*Exit:* the texture and fog conformance programs pass on the 86Box G100. Both acceptance games render in-game scenes, and their replayed traces match the Voodoo reference with differences only from D8 and §7.4.

**S: silicon re-verification.** The bench is live. M1–M4 are re-run on the physical G100: BARs, DPMI mapping under each game's DOS/4GW, VBE modes, FIFO pacing, edge coverage, texture scaling, alpha and fog behaviour. Triangle throughput and per-call overhead are measured on the Pentium II reference.
*Exit:* conformance and game replays pass on the physical G100; its readbacks become the G100 baseline; every 86Box/silicon difference is a capability flag or a local patch; the D2 revisit is decided.

**M5: G200.** True blending, alpha test, texture LUT, hardware mip levels and the specular path. The WP-3 G200 model is available in Loop A.
*Exit:* the full conformance suite passes on the emulated G200 and the physical G200, and all acceptance games render correctly on the G200.

**M6: G450 and performance.** The G400-family path, and performance work to meet G3.
*Exit:* all acceptance games render correctly on the G450, and reach ≥30 FPS at 640×480 on the Pentium II reference with the G200 and the G450, or a measured shortfall is published.

**M7: enhancements and release.** The §8 options, documentation and release packaging.
*Exit:* every enhancement works on every chip it lists and is covered by at least one conformance run. The DOS-GL handoff (Appendix B) is written up with the measured numbers and answered questions it needs.

---

## 16. Risks

| ID | Risk | Likelihood | Mitigation |
|---|---|---|---|
| R1 | Host setup cannot sustain 30 FPS on a Pentium II | High | Measure at M3; shared optimisation with DOS-GL; WARP fallback (D2) |
| R2 | An acceptance game references an export or behaviour not in the Glide 2.43 import library | Medium | Extract each game's referenced names at M1 (§6.2); call trace shows first use |
| R3 | False green from 86Box: FIFO overruns, alignment and timing are not modelled | High | FIFO pacing written against spec depths; nothing is done until Loop B passes |
| R4 | G100 translucency looks wrong in games that rely on blending | Certain | Documented hardware limit (D8); the G200 is the "looks right" card |
| R5 | Visible mip transitions on the G100 | Certain | Documented (§7.4) |
| R6 | Register encodings differ between G100, G200 and G400 in places | Medium | Per-chip capability table; differences flagged and verified on silicon |
| R7 | The 86Box G200 model encodes guesses, not silicon behaviour | Medium | Validate every WP-3 behaviour against the physical G200 in Loop B |
| R8 | A probe on `cuda6` hangs a production server | Medium, if probed | Staged probes (Q9); scope limited to the device's sysfs files and `~/mga-rig/` |
| R9 | The shared HAL has to satisfy two compilers and two execution models (inside a game's DOS/4GW process here; a standalone DJGPP/CWSDPMI program in DOS-GL) before DOS-GL has written any code against it | Medium | CI builds the HAL with DJGPP from M0 and runs a minimal DJGPP smoke test in Loop A, so DOS-GL's side cannot silently rot; versioned HAL releases |
| R10 | A game uses Glide features with no Matrox equivalent: additive combine or multiplicative lightmaps on G100, two-TMU effects | High on G100, low on G200+ | Per-game coverage table; log and degrade rather than crash |
| R11 | Some retail OVL-loading games are known to fail with newer 3dfx OVLs in pure DOS (reported for *Tomb Raider: Unfinished Business*), so version-specific behaviour exists | Medium | Record which OVL version each acceptance game ships with; compare behaviour against that version in §11.4 |
| R12 | Trademark objection to the name | Low | Disclaimer, no logos; rename if asked (D14) |

---

## 17. Open Questions

| # | Question | Needed by |
|---|---|---|
| Q3 | Does DPMI `0x0800` behave as expected under the DOS/4GW versions the acceptance games ship with? Screamer Rally uses external DOS/4GW 1.97; GTA has DOS/4GW Professional built in | M1 |
| Q5 | Who currently owns the Glide trademark? | Before first public release |
| Q6 | Bus FIFO depth on the G200 and G400 | M2 |
| Q7 | G200 support for palette-plus-alpha (AP_88-style) textures | M5 |
| Q8 | Do the bench G100 and G200 BIOSes offer VBE linear-framebuffer modes at 512×384 and 640×400? If not, the CRTC path in §7.7 is needed at M2 | M2 |
| Q9 | Approved 2026-09-26. Staged probes on the `cuda6` G200eR2: (1) a read-only register dump; (2) a single `TRAP` fill into an unused VRAM region; (3) one `TEXTURE_TRAP`, to learn whether the server variant kept its 3D engine | M5 |
| Q10 | The exact search order of the game-side loader for `GLIDE2X.OVL`. GTA's built-in DOS/4GW Professional contains `DLLPATH`, `DLL32PATH` and a `LINEXEVERBOSE` switch, which should reveal it | M1 |
| Q11 | Which Glide version each acceptance game's shipped OVL reports, and whether any game checks `grGlideGetVersion` | M1 |

Resolved during refinement: the binary format and toolchain (§6, D11), header licensing (D12), static-linked games (D13), the project name (D14), and the acceptance games (D15).

---

## Appendix A: Corrections to v0.1

| v0.1 claim | Finding |
|---|---|
| Runtime is `glide2x.ovl` / `glide2x.dxe` | The DOS runtime is a Watcom-built **DOS/4G LE DLL** named `GLIDE2X.OVL`. Games load it through DOS/4GW's DLL loader. A DJGPP DXE cannot be loaded (§6). |
| Build with DJGPP | The runtime must be built with **Open Watcom** (D11). |
| Mystique chip is "MGA-S1000" | No such part exists. The Mystique is the **MGA-1064SG**; the Mystique 220 is the **MGA-1164SG**. |
| Validate in 86Box on the Mystique profile | The Mystique is dropped (D1). The **G100** is the 86Box target; it adds bilinear filtering, fog, 32-bit Z and alpha selection. |
| 86Box v6.0 | v6.0 exists (2026-05-31), but its release binaries do not include the G100, which needs a `DEV_BRANCH` build (§5.4). |
| G100 grouped with G200 as a capable 3D part | The G100 has **only stipple alpha**, no alpha test and no hardware mipmaps (§5.3). |
| G200/G400 "Macro DMA command bundles" through WARP | WARP is a programmable setup engine whose instruction set is unpublished. The trapezoid engine can be driven directly without it (D2). |
| Expand textures to 1024×1024 on G400 | Irrelevant to Glide, which caps at 256×256. The G200 and G400 both support 2048×2048. |
| Compiler profile flags per chip | One binary, with a runtime capability table keyed by PCI ID (§7.1). |
| *Tomb Raider* as a test title | The original Voodoo *Tomb Raider* links Glide 2.1.1 **statically** and cannot be supported. *Unfinished Business* and the Voodoo Rush patch load the OVL. |
| *Descent II* as a test title | The official `D2VOODOO.EXE` links Glide statically. Only the unofficial `D2_3DFX.EXE` loads the OVL. |
| *Blood* as a test title | *Blood*'s 1998 3DFX patch does load the OVL, but it is alpha quality and not in the acceptance list. |
| *ClassiCube* as a validation title | ClassiCube uses OpenGL, not Glide. It belongs to DOS-GL. |
| "<5% CPU overhead" | Not measurable as stated. Replaced by measured per-call overhead and triangle throughput (§10). |
| Week-based milestones | Replaced by exit-criteria milestones (D9). |
| P_8 textures "unpacked if local registers demand" | The G100 must expand P_8, because its TW8 palette is the display DAC's. The G200 has a dedicated texture LUT (§7.6). |
| Glide state traps via "symbol export tracker" | The runtime simply exports the decorated `__stdcall` names the game's import stub looks up (§6.2). |

---

## Appendix B: DOS-GL Follow-Up (D16)

DOS-GL is implemented after MGA-Glide. When MGA-Glide's HAL, harness and 86Box work land, DOS-GL's PRD (`~/DOSGL/PRD.md`) is revised as follows. Each item names the DOS-GL section it affects.

| # | DOS-GL item | Change after MGA-Glide lands |
|---|---|---|
| B1 | D3: G400 family first, G200 as a follow-on | The HAL already supports G100, G200 and G450. G200 support can move into DOS-GL v1.0 at little cost. |
| B2 | D13 and §4.1: G100 is development-only | Revisit. The HAL drives the G100 on silicon. Whether DOS-GL lists it as supported depends on whether GL blending can accept stipple (MGA-Glide D8). |
| B3 | D2, §11.2: "86Box emulates the G100" | Correct it: the G100 needs a `DEV_BRANCH` build of 86Box. Point Loop A at MGA-Glide's locally patched 86Box build (local patches only, D4). |
| B4 | D13, §11.2: G100 model includes `ALPHACTRL` | Correct it: the G100 has stipple alpha only, in 86Box and on silicon. |
| B5 | §11.2: no emulated G200/G400 | The WP-3 G200 model gives DOS-GL an emulated card with true blending, alpha test and mip levels. Add it to Loop A and move the blending and alpha-test conformance tests there. |
| B6 | M0 to M2 (harness, PCI/BAR, clears) | Replace with "adopt the shared HAL and harness". Most exit criteria are already met by MGA-Glide M0 to M2. |
| B7 | D1, R1, §10: host-setup throughput unknown | Carry across MGA-Glide's measured triangle throughput and per-call overhead from M3 and M6, and its WARP decision. |
| B8 | Open questions: BAR layout, G450 revision ID, VBE mode list, FIFO depth | Carry across MGA-Glide's answers (§4.2 on all three cards, Q6, Q8). |
| B9 | §4.4, §9.3: CWSDPMI, near pointers, crash handlers owned by the program | The HAL's DPMI shim has a DJGPP/CWSDPMI variant. Its crash handling chains to previous handlers, which DOS-GL keeps as its own top-level handler. |
| B10 | §12: DJGPP only | DOS-GL stays on DJGPP but consumes the HAL as a submodule. Note that the HAL is also built by Open Watcom and must stay toolchain-neutral. |
| B11 | D14: no Linux register harness | The `cuda6` G200eR2 (§4.4) is a Linux-accessible G200-family register reference, with MGA-Glide's rig tools in `tools/rig/`. |
| B12 | §11.5 conformance suite | Share test infrastructure: the reference-frame comparison, the serial-log parser and the screenshot tooling are in the shared harness. |

