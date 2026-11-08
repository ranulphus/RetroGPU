# Product Requirements Document — DOS-GL

**Hardware-accelerated OpenGL 1.1 subset for Matrox G400/G200 under 32-bit protected-mode MS-DOS**

| | |
|---|---|
| Status | Draft v0.3 — rebased on MGA-Glide's shared HAL and harness |
| Last updated | 2026-09-27 |
| Primary consumer | ClassiCube (MS-DOS build) |
| Licence | MIT/X11 |
| Toolchain | DJGPP (GCC) cross-compiler, CWSDPMI |
| Companion project | MGA-Glide (`~/MGA-Glide`), which built the shared Matrox HAL, test harness and 86Box work DOS-GL uses (D15; MGA-Glide PRD D16, Appendix B) |

---

## 1. Overview & Value Proposition

DOS-GL is an open-source static library (`libGL.a`) implementing a hardware-accelerated subset of OpenGL 1.1 for 32-bit protected-mode MS-DOS, targeting the Matrox G400 family (G400 and G450) and G200 graphics processors.

Hardware 3D in DOS is today almost synonymous with 3dfx and Glide. That hardware is scarce and increasingly expensive. The Matrox G-series is common, cheap, and — critically — **publicly documented**: Matrox released full G200/G400 programming specifications, and the MIT-licensed X.org `mga` DDX and Mesa/DRI `mga` drivers provide battle-tested register definitions and initialisation sequences we can lawfully reuse.

The reference consumer is ClassiCube. ClassiCube **already ships a working MS-DOS port** (`src/msdos/Platform_MSDOS.c`, `src/msdos/Window_MSDOS.c`, built with DJGPP), but it renders through its own software rasteriser into VGA mode 0x13 — **320×200 at 8bpp**. DOS-GL's success case is replacing that backend with `CC_BUILD_GL11` at **640×480 in 16-bit colour, hardware accelerated**. The platform layer — window, input, timing, file I/O, cooperative threading — already exists and is not our problem.

### What makes this credible

- The register interface is documented, not reverse-engineered.
- The integration target already runs on the platform; only the graphics backend changes.
- ClassiCube's OpenGL 1.1 backend uses a small, enumerable, and *verified* set of entry points (§6).

### What makes this hard

- No memory protection. A malformed MMIO write typically hard-hangs the machine with a blank screen and no diagnostics. Debug infrastructure is a first-class requirement, not an afterthought (§9).
- Stock 86Box stops at the G100. MGA-Glide's local 86Box patches add G200, G400 and G450 models that boot Matrox's own BIOSes, but they are models: every claim about the target card, 2D or 3D, still requires real silicon (§11). Fortunately real silicon is on the bench.
- Without the WARP microcode, per-triangle setup runs on a Pentium II. This is the principal risk to the performance target (§10).

---

## 2. Goals & Non-Goals

### 2.1 Goals

| # | Goal |
|---|---|
| G1 | Initialise, configure and drive a Matrox G400-family chip (G400, G450) from a clean MS-DOS prompt with no host OS. |
| G2 | Present a standard `<GL/gl.h>` interface so consumers link against DOS-GL with no engine-level hardware abstraction. |
| G3 | Run ClassiCube's `CC_BUILD_GL11` backend unmodified except for its `GLContext_*` platform shims. |
| G4 | Sustain ≥30 FPS at 640×480×16 in ClassiCube on a Pentium II 266–400 MHz, within the triangle budget defined in §10. |
| G5 | Build out of the box with DJGPP + CWSDPMI, cross-compiled from Linux. |
| G6 | Contain no binary blobs and no code of unclear provenance. |
| G7 | Leave the machine in a usable text-mode state after any crash. |

### 2.2 Non-Goals

Explicitly out of scope for v1.0. Listing these is as important as listing the goals.

| Area | Decision |
|---|---|
| Software rasteriser fallback | **No.** If no supported card is found, `dglInit` fails with a clear message. DOS-GL is a hardware driver, not a renderer. |
| 32-bit colour | Not in v1.0. 16-bit only (§8). |
| Custom / non-BIOS resolutions | Not in v1.0. Resolution is selectable, but only from modes the card BIOS advertises (§8.1). Arbitrary timings need the native CRTC backend deferred to v1.1. |
| Other vendors | No 3dfx, S3, ATI, nVidia, or generic VESA 3D. |
| Real mode | Protected mode (DPMI) only. 16-bit programs cannot use this library. |
| OpenGL lighting | `glLight*`, `glMaterial*`, `glNormal*` are not implemented. ClassiCube bakes lighting into vertex colours. |
| Stencil buffer, accumulation buffer | Not implemented. |
| Multitexturing | Not in v1.0, despite G400 capability. |
| GLU, GLUT | Not provided. |
| Input, sound, timing, file I/O | Not provided. The consumer's platform layer owns these. |
| Automatic mipmap generation | Not provided. The consumer uploads each level via `glTexImage2D`. |
| WARP microcode path | Deferred to v1.1 (§3, D1). |

---

## 3. Decision Register

Decisions taken during PRD refinement, recorded with rationale so they can be revisited deliberately rather than drifted away from.

| ID | Decision | Rationale | Revisit at |
|---|---|---|---|
| **D1** | **Host-side triangle setup in v1.0; WARP as a v1.1 optimisation.** The CPU computes edge and gradient parameters and writes the trapezoid setup registers directly. | Keeps the library blob-free and debuggable. WARP requires Matrox's binary vertex-setup microcode (the blobs Linux ships as `matrox/g400_warp.fw`). Host setup is documented in the public Matrox spec. | M3 exit, against measured triangle throughput |
| **D2** | **Two autonomous iteration loops plus a human-confirmed performance machine** (§11): (1) 86Box with the shared local patches: emulated G400/G450 (primary), G200 and G100, for the full DOS stack; (2) a remote DOS PC with the G450, reachable from the build host over mTCP, serial and video capture, for real target silicon; (3) the PII reference machine for G4 only. | The implementer works from the Linux build host and cannot physically touch the bench. The dominant variable is therefore *how many iterations happen with nobody present*, not emulator fidelity. Stock 86Box emulates no G200/G400-family part; MGA-Glide's local patches add G200, G400 and G450 models on Matrox's genuine BIOSes, so DPMI, VBE and the whole `dglInit`→frame→`dglShutdown` lifecycle run against the target family's BIOS and drawing model. Real silicon remains the only source of truth (R10). | — |
| **D3** | **G400 family first (G400 and G450 together); G200 also in v1.0** (revised 2026-09-27: the shared HAL, an emulated G200 and a bench card already exist, so the cost is conformance runs). The G450 PCI is the primary bring-up card. | G400 and G450 share PCI device ID `102B:0525` and the same 3D core — the driver cannot distinguish them without reading the revision byte, so supporting one is supporting both. G450 is on the bench in a DOS machine today; G400 (AGP) and G200 are also available. G400-family is the more capable part; narrowing to G200's limits later is easier than widening. Note this inverts the original project framing. | v1.1 |
| **D4** | **No software fallback.** | Keeps scope honest and the codebase small. | — |
| **D5** | **API surface = ClassiCube's verified call list + common GL 1.1 core.** | ClassiCube defines "done"; the small neighbouring set makes ordinary demos and tutorials link cleanly. | — |
| **D6** | **`glBegin`/`glEnd` supported, implemented as a thin wrapper over the internal vertex stream.** | Not optional — ClassiCube's GL11 display-list path records immediate-mode-compatible draws, and every GL tutorial uses it. Cheap given the vertex stream exists anyway. | — |
| **D7** | **Custom context API (`dgl*`) in `<GL/dosgl.h>`.** | OpenGL has no display/context API of its own; DOS has no WGL/GLX/EGL. A small honest API maps 1:1 onto ClassiCube's `GLContext_*` contract. No GLUT shim in v1.0. | — |
| **D8** | **VBE 2.0/3.0 via the card BIOS for mode setting in v1.0**, behind a HAL interface so a native modesetting backend can replace it later. | Fast path to a working Milestone 1; robust across board variants. Limits us to BIOS-advertised modes and VBE-based page flipping. | v1.1 |
| **D9** | **MIT/X11 licence; reuse of X.org `mga` and Mesa/DRI `mga` register definitions permitted with attribution.** | Saves weeks of error-prone transcription. Matches upstream terms and lets retro projects of any licence link `libGL.a`. | — |
| **D10** | **DJGPP cross-compiler on Linux is the reference build.** Native DJGPP-in-DOS builds documented but not CI-tested. | Makes CI, version control and iteration tractable. | — |
| **D11** | **Milestones defined by verifiable exit criteria, not dates.** Solo, part-time cadence. | Calendar estimates on a hardware bring-up project of this kind are fiction. Exit criteria make "done" unambiguous. | — |
| **D12** | **Serial logging, register write-trace/replay, and crash recovery are in-scope requirements**, not nice-to-haves. | On a platform with no memory protection and no debugger, these *are* the development environment. | — |
| **D13** | **The Matrox G100 (`102B:1000`/`1001`) is a development-only HAL target: supported in the tree, exercised in 86Box, never listed as a supported card.** | It is the newest MGA part stock 86Box emulates (a `DEV_BRANCH` build), with textured trapezoids, z, dither and fog; its `ALPHACTRL` is **stipple alpha only**, in 86Box and on silicon, so it cannot provide GL blending. It shares the `AR`/`DR`/`TMR`/`DWGCTL` model the G400 inherits, so setup code developed against it transfers. It is not shipped because 8 MB, an older engine and no G400 features are not what the product promises. The chip-capability table it forces is needed for G200 anyway. | — |
| **D14** | **Superseded (2026-09-27).** A Linux register harness exists after all: the cuda6 server's G200eR2 (`102B:0534`), reached through sysfs by MGA-Glide's `mgarig` (Loop C, §11.3a). It keeps its 3D engine and serves as a G200-family register reference; the G400 family stays on the DOS bench. | — |
| **D15** | **The Matrox HAL, test harness, bench tooling and 86Box patches come from MGA-Glide.** DOS-GL builds them from MGA-Glide's checkout beside it (`MGA_GLIDE`, default `~/MGA-Glide`), from its main as it is: no copy, no pinned version (the owner's decision, 2026-11-02: before alpha everything is on main trunk). HAL changes are made in MGA-Glide and pass its regression, and DOS-GL's, first. | Built and tested once, shared by both projects (MGA-Glide PRD D3/D16). One version in flight: a HAL fix reaches DOS-GL at its next build. | HAL publication |
| **D16** | **Conformance references come from host OpenGL (Mesa OSMesa).** Each conformance program is plain GL 1.1 C, built for DOS against DOS-GL and for the Linux host against OSMesa; the host render is the reference. | An independent implementation of the same API, as the Voodoo is for MGA-Glide; references are regenerated rather than blessed by eye. The G450 readback becomes the release baseline once the bench runs. | — |

---

## 4. Target Hardware & Environment

### 4.1 Supported graphics hardware

| Chip | PCI Vendor:Device | Bus | Status |
|---|---|---|---|
| Matrox Millennium G400 / G400 MAX | `102B:0525` | AGP | **v1.0 primary target** |
| Matrox Millennium G450 | `102B:0525` | AGP / PCI | **v1.0 — primary bring-up card** (PCI variant on the bench). Same device ID and 3D core as G400; revision byte distinguishes |
| Matrox Millennium G200 / Mystique G200 | `102B:0520` (PCI), `102B:0521` (AGP) | PCI / AGP | **v1.0** (D3, revised) — emulated in Loop A and on the bench |
| Matrox Millennium G550 | `102B:2527` | AGP | Not planned |
| Matrox Productiva G100 | `102B:1000` (PCI), `102B:1001` (AGP) | PCI / AGP | **Development only** — emulated in 86Box; drives the autonomous loop; never a supported card (D13) |
| Mystique, Millennium I/II | various | | Not supported — different 3D capability |

**AGP note.** G400 boards are AGP; the G450 exists in PCI form and that is the primary bench card. DOS-GL treats AGP purely as PCI: configuration space, MMIO aperture and framebuffer aperture. It does **not** program the GART or use AGP texturing, so **all textures must reside in local video memory**. This is not a practical constraint — see §8.3.

> The G450 reports revision `0x80` and up (the G400 BIOS applies extra fixes only to revision 2). The bench G400 and G450 revisions are recorded at M1.

### 4.2 PCI aperture layout

Per the X.org `mga` driver's `new_BARs` configuration, which covers Mystique, G100, G200, G400 and G550:

| BAR | Offset | Contents |
|---|---|---|
| BAR0 | `0x10` | Linear framebuffer (local video RAM) |
| BAR1 | `0x14` | MMIO control aperture (16 KB) |
| BAR2 | `0x18` | ILOAD / pseudo-DMA window |

> Note this is **reversed** relative to the older Millennium I/II parts. **Confirmed on silicon** on the cuda6 G200eR2 (2026-09-25) and by Matrox's own G200/G400/G450 BIOSes in the emulated cards; the bench G400 and G450 confirm it at M1.

### 4.3 Host platform

| Item | Requirement |
|---|---|
| OS | MS-DOS 6.22+, MS-DOS 7.1, or FreeDOS 1.3 |
| CPU | Intel Pentium II 266 MHz minimum (Pentium/K6 may work; unsupported) |
| Mode | 32-bit protected mode via DPMI |
| DPMI host | CWSDPMI r7. `CWSDPR0.EXE` (ring 0) evaluated as an option for MTRR configuration — see §10.3 |
| System RAM | 32 MB minimum, 64 MB recommended |
| Video RAM | 16 MB (G400 baseline) |

### 4.4 Memory access strategy

- **PCI configuration space:** direct access via Configuration Mechanism #1 (ports `0xCF8`/`0xCFC`) as the primary path. This is simpler and more reliable than the real-mode PCI BIOS and needs no DPMI mode transitions. PCI BIOS via `__dpmi_int(0x1A)` is retained as a fallback and as a cross-check during M1.
  > This refines the original PRD, which specified INT 0x1A as the only mechanism.
- **Aperture mapping:** `__dpmi_physical_address_mapping` (DPMI function `0x0800`) for both the MMIO and framebuffer apertures, followed by LDT descriptor allocation and base/limit configuration.
- **Access method:** `__djgpp_nearptr_enable()` for flat near-pointer access to mapped apertures, with `_farpeek`/`_farpoke` through an explicit selector as the portable fallback. Near pointers are substantially faster and matter for framebuffer and FIFO throughput.
- **MMIO must be mapped uncached.** Framebuffer and FIFO regions want write-combining — see §10.3.

---

## 5. Architecture

```
+---------------------------------------------------------------+
|                 Client Application (ClassiCube)               |
+---------------------------------------------------------------+
        |                                        |
        | GL 1.1 subset (<GL/gl.h>)              | Context API (<GL/dosgl.h>)
        v                                        v
+-----------------------------------+  +--------------------------+
|        DOS-GL State Engine        |  |    Context / Display     |
|  - Fixed-function state tracker   |  |  - dglInit / dglShutdown |
|  - Matrix stack (MV/Proj/Tex)     |  |  - dglSwapBuffers        |
|  - Vertex array + immediate mode  |  |  - dglSetVSync           |
|  - Display list capture           |  +--------------------------+
|  - Texture object manager         |               |
+-----------------------------------+               |
        |                                           |
        | Internal vertex stream (position, colour, |
        | texcoord, fog factor)                     |
        v                                           |
+---------------------------------------------------------------+
|                   Triangle Setup (host CPU)                   |
|  - Clip, project, viewport transform                          |
|  - Edge walk + gradient computation (RGB, A, Z, S/W, T/W, fog)|
|  - Emits trapezoid setup register writes                      |
|  [v1.1: alternate WARP DMA path emits vertex packets instead] |
+---------------------------------------------------------------+
        |
        v
+---------------------------------------------------------------+
|                     Matrox HAL                                |
|  - PCI enumeration (mech #1) + DPMI aperture mapping          |
|  - VBE mode set, surface/Z allocation in VRAM                 |
|  - Register I/O + FIFO pacing (FIFOSTATUS / STATUS polling)   |
|  - Serial log, write trace, exception handler, card reset     |
+---------------------------------------------------------------+
        |
        v  MMIO registers + local framebuffer
+---------------------------------------------------------------+
|              Physical Matrox G400 / G200 GPU                  |
+---------------------------------------------------------------+
```

### 5.1 Hardware Abstraction Layer

**FR-HAL-1 — PCI discovery.** Enumerate PCI configuration space via mechanism #1, matching vendor `0x102B` against the device ID table in §4.1. Read and mask BAR values (memory BARs: clear the low 4 bits), determine aperture sizes, and expose them through a device descriptor.

**FR-HAL-2 — Aperture mapping.** Map the MMIO control aperture and framebuffer aperture into the 32-bit linear address space via DPMI `0x0800`. MMIO mapped uncached; framebuffer write-combined where achievable.

**FR-HAL-3 — Mode enumeration and setting.** Resolution is **selectable at runtime, not hardcoded** — see §8.1.

- Enumerate the BIOS mode list via VBE function `4F00h`, then query each with `4F01h`.
- Accept a mode only if it advertises a linear framebuffer (ModeAttributes bit 7), direct-colour memory model, 16 bits per pixel, and an engine-compatible pitch (FR-HAL-8).
- Set the chosen mode with `4F02h` (LFB bit set); page-flip with `4F07h`; restore text mode on teardown.
- If the BIOS reports VBE 3.0, expose refresh-rate control via `4F02h` with the CRTC block; otherwise refresh is whatever the BIOS picks.
- All INT 10h calls go through `__dpmi_int`.
- Mode setting lives behind a `dgl_modeset_ops` vtable so a native CRTC backend can replace it without touching layers above.

**FR-HAL-8 — Pitch validation.** The MGA drawing engine constrains the destination pitch (a multiple of 32 pixels; to be confirmed against the specification at M2). The BIOS chooses `BytesPerScanLine` and may or may not satisfy this. Every candidate mode's reported pitch must be validated before the mode is offered to the application, and modes that fail are filtered out of the enumeration rather than failing at draw time.

**FR-HAL-4 — Surface allocation.** Allocate front buffer, back buffer and 16-bit depth buffer in local VRAM with the alignment and pitch constraints the drawing engine requires (pitch a multiple of 32 pixels; 640 satisfies this). Program the destination and depth origin registers accordingly. Remaining VRAM forms the texture heap.

**FR-HAL-5 — Register I/O.** Typed 8/16/32-bit MMIO accessors that route through the write-trace ring buffer (§9.2) in debug builds and compile down to direct stores in release builds.

**FR-HAL-6 — FIFO management.** Poll `FIFOSTATUS` for available slots before bursts of register writes, and `STATUS` for engine-busy state before operations requiring a drained pipeline. The pacing strategy must be a single chokepoint in the code, not scattered polls — FIFO overrun is the most likely cause of unexplained hangs.

**FR-HAL-7 — Reset and restore.** A `dgl_reset_card()` path that returns the chip to a known state, and a teardown path that restores text mode. Both must be callable from an exception handler and from `atexit`.

### 5.2 Triangle setup (host CPU)

**FR-TS-1.** Transform incoming vertices by the modelview and projection matrices, clip against the view frustum (guard-band where possible), perform perspective divide and viewport transform.

**FR-TS-2.** For each resulting triangle, compute the trapezoid setup parameters the MGA drawing engine expects: left/right edge start values and slopes, and the per-pixel gradients for R, G, B, A, Z, S/W, T/W and fog.

**FR-TS-3.** Emit the setup register writes and the draw command through the FIFO chokepoint.

**FR-TS-4.** The setup routine is the hot path and the primary risk to G4. It must be isolated behind a single interface so that (a) it can be optimised or hand-written in assembly independently, and (b) the v1.1 WARP path can substitute for it wholesale.

### 5.3 State engine

**FR-ST-1.** Track all fixed-function state the supported entry points can modify, and translate it into the corresponding hardware register configuration lazily — recompute and write dirty register groups at draw time, not at `glEnable` time.

**FR-ST-2.** Maintain three matrix stacks (`GL_MODELVIEW`, `GL_PROJECTION`, `GL_TEXTURE`). Depth per the GL 1.1 minimums: 32 for modelview, 2 for projection and texture.

**FR-ST-3.** Maintain an OpenGL error flag settable by any entry point and retrievable via `glGetError`.

### 5.4 Display lists

ClassiCube's GL11 backend uses display lists as a stand-in for vertex buffer objects: it wraps `glVertexPointer`/`glColorPointer`/`glTexCoordPointer` plus a single `glDrawElements` in `glNewList(GL_COMPILE)` … `glEndList`, then replays with `glCallList`. This is the mechanism by which every world chunk is drawn, so it is on the critical path for both correctness and performance.

**FR-DL-1.** Support `glGenLists`, `glNewList(GL_COMPILE)`, `glEndList`, `glCallList`, `glDeleteLists`.

**FR-DL-2.** On `glEndList`, **snapshot the referenced vertex data into a driver-owned buffer.** The application's arrays must not be aliased — ClassiCube allocates them from temporary memory that is reused immediately. Storing a compact, setup-ready copy is also the main lever available for reducing per-frame work.

**FR-DL-3.** v1.0 scope limits: no nesting, no `GL_COMPILE_AND_EXECUTE`, and only array-pointer, immediate-mode and draw calls may be captured. Any other call inside a list is a documented no-op. Full GL 1.1 display-list semantics are a non-goal.

### 5.5 Context / display API (`<GL/dosgl.h>`)

Illustrative shape, to be fixed at M1:

```c
typedef struct {
    int width, height;      /* any enumerated mode; 640x480 default */
    int color_bits;         /* 16 only in v1.0                      */
    int depth_bits;         /* 0 or 16                              */
    int refresh_hz;         /* 0 = BIOS default; VBE 3.0 only       */
    int double_buffer;      /* nonzero for double buffering         */
    int vsync;              /* nonzero to sync swaps to retrace     */
} DGLConfig;

typedef struct {
    int width, height;
    int color_bits;
    int max_depth_bits;     /* 16, or 0 if VRAM cannot fit a Z buffer */
    int can_double_buffer;
} DGLMode;

/* Enumeration — callable before dglInit, after device discovery.
   Returns the number of usable modes; fills up to max_modes entries. */
int          dglEnumModes(DGLMode* modes, int max_modes);

int          dglInit(const DGLConfig* cfg);   /* 0 on success  */
void         dglShutdown(void);
void         dglSwapBuffers(void);
void         dglSetVSync(int enabled);
const char*  dglGetErrorString(void);
const DGLDeviceInfo* dglGetDeviceInfo(void);  /* chip, VRAM, BARs */
```

`dglEnumModes` reports only modes that passed every filter in FR-HAL-3 and FR-HAL-8 **and** fit in available VRAM at the requested buffering and depth. An application can therefore present a resolution menu without knowing anything about VBE, MGA pitch rules or VRAM budgeting.

**Mapping onto ClassiCube's platform contract** (`src/Window.h`):

| ClassiCube | DOS-GL |
|---|---|
| `GLContext_Create` | `dglInit` |
| `GLContext_Free` | `dglShutdown` |
| `GLContext_SwapBuffers` | `dglSwapBuffers` |
| `GLContext_SetVSync` | `dglSetVSync` |
| `GLContext_GetAddress` | returns `NULL` — static linking, no dynamic resolution |
| `GLContext_GetApiInfo` | `dglGetDeviceInfo` formatted |
| `GLContext_Update` / `TryRestore` | no-ops — no resizable window in DOS |

---

## 6. API Surface (normative)

### 6.1 Tier 1 — required by ClassiCube

This list was extracted from ClassiCube's `src/Graphics_GL11.c`, `src/Graphics_GL1.c` and `src/_GLShared.h`. It is the definition of v1.0 completeness: DOS-GL is functionally done when every entry point below behaves correctly.

| Group | Entry points |
|---|---|
| Context/frame | `glClear`, `glClearColor`, `glViewport`, `glScissor`, `glColorMask`, `glDepthMask`, `glReadPixels` |
| State | `glEnable`, `glDisable`, `glEnableClientState`, `glDisableClientState`, `glHint`, `glGetIntegerv`, `glGetString`, `glGetError` |
| Matrices | `glMatrixMode`, `glLoadIdentity`, `glLoadMatrixf` |
| Vertex arrays | `glVertexPointer`, `glColorPointer`, `glTexCoordPointer` |
| Drawing | `glDrawArrays`, `glDrawElements` |
| Immediate mode | `glBegin`, `glEnd`, `glVertex3f`, `glColor4ub`, `glTexCoord2f` |
| Display lists | `glGenLists`, `glNewList`, `glEndList`, `glCallList`, `glDeleteLists` |
| Textures | `glGenTextures`, `glBindTexture`, `glDeleteTextures`, `glTexImage2D`, `glTexSubImage2D`, `glTexParameteri` |
| Raster state | `glAlphaFunc`, `glBlendFunc`, `glDepthFunc` |
| Fog | `glFogf`, `glFogfv`, `glFogi` |

**Required enum support**, also verified against the source:

| Token group | Values |
|---|---|
| `glEnable`/`glDisable` caps | `GL_TEXTURE_2D`, `GL_DEPTH_TEST`, `GL_ALPHA_TEST`, `GL_BLEND`, `GL_CULL_FACE`, `GL_FOG`, `GL_SCISSOR_TEST` |
| Client state | `GL_VERTEX_ARRAY`, `GL_COLOR_ARRAY`, `GL_TEXTURE_COORD_ARRAY` |
| Array formats | position `3 × GL_FLOAT`; colour `4 × GL_UNSIGNED_BYTE`; texcoord `2 × GL_FLOAT`; interleaved strides of 16 and 24 bytes |
| Primitives | `GL_TRIANGLES` (via `glDrawElements`, `GL_UNSIGNED_SHORT` indices), `GL_LINES` (via `glDrawArrays`) |
| Matrix modes | `GL_PROJECTION`, `GL_MODELVIEW`, `GL_TEXTURE` |
| Depth func | `GL_LEQUAL` |
| Alpha func | `GL_GREATER` with reference `0.5` |
| Blend func | `GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA` |
| Fog modes | `GL_LINEAR`, `GL_EXP`, `GL_EXP2`; params `GL_FOG_MODE`, `GL_FOG_COLOR`, `GL_FOG_DENSITY`, `GL_FOG_END` |
| Texture params | `GL_TEXTURE_MIN_FILTER`, `GL_TEXTURE_MAG_FILTER` with `GL_NEAREST`, `GL_LINEAR`, `GL_NEAREST_MIPMAP_LINEAR`; `GL_TEXTURE_MAX_LEVEL` |
| Texture formats | internal `GL_RGBA`; transfer `GL_RGBA`/`GL_UNSIGNED_BYTE`, and `GL_BGRA_EXT` if the extension is advertised |
| Queries | `GL_MAX_TEXTURE_SIZE`, `GL_DEPTH_BITS`, `GL_VIEWPORT`, `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`, `GL_EXTENSIONS` |
| Clear bits | `GL_COLOR_BUFFER_BIT`, `GL_DEPTH_BUFFER_BIT` |
| List mode | `GL_COMPILE` |

**Version string.** `glGetString(GL_VERSION)` must report a string beginning `1.1` so consumers select the GL 1.1 path.

**`GL_EXT_bgra`.** Recommended for v1.0. MGA texture formats are ARGB-ordered, so accepting BGRA input avoids a per-texel swizzle on every upload. If advertised, it must work; if not advertised, ClassiCube falls back to RGBA and swizzles itself.

### 6.2 Tier 2 — common GL 1.1 core

Not required by ClassiCube, but included so ordinary demos and tutorial code link and run (per D5). Lower priority than Tier 1; may slip to v1.1 if it threatens the schedule.

`glPushMatrix`, `glPopMatrix`, `glMultMatrixf`, `glTranslatef`, `glRotatef`, `glScalef`, `glOrtho`, `glFrustum`, `glColor3f`/`glColor4f`/`glColor3ub`, `glVertex2f`/`glVertex3fv`, `glNormal3f` (accepted, ignored), `glShadeModel`, `glCullFace`, `glFrontFace`, `glPolygonMode` (fill only), `glFlush`, `glFinish`, `glGetFloatv`, `glGetBooleanv`, `glIsEnabled`, `glDepthRange`, `glClearDepth`, `glTexEnvi`, `glTexParameterf`, `glPixelStorei`, `glBindTexture` variants, plus `GL_QUADS`, `GL_TRIANGLE_STRIP`, `GL_TRIANGLE_FAN` and `GL_LINE_STRIP` in immediate mode.

### 6.3 Explicitly absent

The original PRD listed `glPushMatrix`, `glPopMatrix`, `glScalef`, `glTranslatef` and `glRotatef` as core requirements. **ClassiCube calls none of them** — it composes matrices itself and submits them via `glLoadMatrixf`. They are demoted to Tier 2. Conversely, display lists, immediate mode, fog, alpha test, scissor and mipmap levels were absent from the original PRD and are all Tier 1.

---

## 7. Rendering Pipeline Requirements

| Capability | Requirement |
|---|---|
| Depth buffer | 16-bit Z, hardware test and write, `GL_LEQUAL` minimum; full comparison set in Tier 2. Depth mask honoured. |
| Face culling | Hardware back-face culling; `GL_CCW` front face default. |
| Alpha test | Hardware alpha test via the MGA alpha control register; `GL_GREATER` minimum. |
| Blending | Hardware source/destination blend; `GL_SRC_ALPHA`/`GL_ONE_MINUS_SRC_ALPHA` minimum. |
| Fog | Hardware fog colour with per-vertex fog factor interpolated by the rasteriser. `GL_EXP` and `GL_EXP2` factors are evaluated per-vertex on the CPU and submitted as a linear-interpolated coordinate — a documented approximation. |
| Scissor | Hardware clipping rectangle registers. |
| Texture filtering | `GL_NEAREST` and `GL_LINEAR` (bilinear) required. Mipmapped min filters (`GL_NEAREST_MIPMAP_LINEAR`) use a 5-level window with absolute `TEXORG1..4` on every chip (the G400's 11-level offset mode is optional per its specification and deferred); if unmet, the driver silently substitutes the non-mipmapped filter and documents it. |
| Texture wrap | `GL_REPEAT` (the GL default; ClassiCube never overrides it). |
| Perspective correction | Required. `glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST)` is requested by the consumer and must be honoured. |
| Shading | Gouraud interpolation of vertex colour. |
| Line rendering | `GL_LINES` must render; hardware line drawing preferred, triangle-expansion acceptable. |

---

## 8. Pixel and Texture Formats

### 8.1 Framebuffer, resolution and refresh

**Colour depth is fixed at 16-bit in v1.0** (§2.2), format **RGB565**. **Resolution is not fixed.** DOS-GL enumerates whatever 16bpp linear-framebuffer modes the card BIOS advertises and lets the application choose (FR-HAL-3, `dglEnumModes`).

Typical VBE mode numbers we expect a G400 BIOS to offer at 16bpp are `0x111` (640×480), `0x114` (800×600), `0x117` (1024×768) and `0x11A` (1280×1024), plus OEM-specific entries. **The actual list is a BIOS property and must be dumped from real hardware** — an M1 deliverable (Q8), not an assumption.

**VRAM is not the limiting factor.** Double-buffered 16bpp colour plus a 16-bit Z buffer costs three framebuffers' worth of memory:

| Resolution | Front + back + Z | Remaining on 16 MB G400 |
|---|---|---|
| 640×480 | 1.8 MB | ≈14.2 MB |
| 800×600 | 2.9 MB | ≈13.1 MB |
| 1024×768 | 4.7 MB | ≈11.3 MB |
| 1280×1024 | 7.9 MB | ≈8.1 MB |

Even 1280×1024 leaves more texture memory than ClassiCube needs.

**Nor, most likely, is fill rate.** The G400's fill rate exceeds what any of these resolutions demand at 30 FPS with realistic overdraw (§10.2). Because the v1.0 bottleneck is *host-side triangle setup* (D1, R1), and **triangle count does not change with resolution**, raising the resolution should cost far less frame rate than intuition suggests. Larger triangles mean more rasteriser and memory-bandwidth work, so it is not free — but the dominant cost is per-triangle, not per-pixel.

This is worth stating plainly because it inverts the usual expectation: on this driver, 800×600 or 1024×768 may be close to free, while adding geometry is expensive.

**What we do not get from VBE (D8):** resolutions the BIOS does not advertise, arbitrary custom timings, and fine refresh-rate control below VBE 3.0. Those require the native CRTC backend deferred to v1.1. Modes whose BIOS-chosen pitch violates the engine's alignment rule are filtered out (FR-HAL-8) and may remove some low or unusual resolutions from the list.

**Defaults.** `dglInit` with a zeroed `DGLConfig` selects 640×480×16, double-buffered, 16-bit Z, BIOS-default refresh.

### 8.2 Depth

16-bit Z buffer, allocated in local VRAM.

### 8.3 Textures

The consumer uploads 32-bit BGRA/RGBA. DOS-GL converts on upload and **selects the destination format per texture** based on the alpha content it observes:

| Observed alpha in source | Hardware format | Rationale |
|---|---|---|
| All opaque | RGB565 | Best colour fidelity; no alpha needed |
| Only 0 or 255 | ARGB1555 | 1-bit alpha is sufficient for alpha-tested cutouts (foliage, glass) |
| Intermediate values present | ARGB4444 | 4-bit alpha needed for blending (water, UI) |

This matters: naively forcing ARGB4444 everywhere would visibly band ClassiCube's terrain atlas.

- Textures must be power-of-two in both dimensions — an MGA hardware requirement. Non-power-of-two uploads set `GL_INVALID_VALUE`.
- Maximum texture size: **2048×2048 on G400, 1024×1024 on G200** (to confirm against the specification). `glGetIntegerv(GL_MAX_TEXTURE_SIZE)` reports the true per-chip value.
  > The original PRD stated 1024×1024 as "exclusive to G400". This is incorrect in both directions and is corrected here.
- Mipmap levels are uploaded individually by the consumer; DOS-GL allocates and tracks them.
- Texture memory is allocated from the VRAM heap remaining after the framebuffers and Z buffer (≈14 MB on a 16 MB G400 — ample for ClassiCube). Eviction policy for heap exhaustion: v1.0 sets `GL_OUT_OF_MEMORY`, which the consumer already handles.

### 8.4 Readback

`glReadPixels` with `GL_RGBA`/`GL_UNSIGNED_BYTE` must expand RGB565 to 32-bit with alpha forced to 255. Used for screenshots; performance is not a concern.

---

## 9. Reliability, Debug and Safety

DOS offers no memory protection, no debugger worth the name, and no post-mortem. A bad MMIO write hangs the machine with a blank screen. These requirements are what make the project tractable at all (D12).

### 9.1 Serial logging

**FR-DBG-1.** Log driver state and significant operations out COM1 to a second machine over a null-modem cable. Direct 16550 UART port I/O; default 115200 8N1; port and baud configurable.

**FR-DBG-2.** **Writes must be unbuffered and synchronous.** The entire value of serial logging is that a hard hang still leaves the last line transmitted. Buffering defeats the purpose.

**FR-DBG-3.** Severity levels, compiled out entirely in release builds.

### 9.2 Register write trace and replay

**FR-DBG-4.** A ring buffer recording recent MMIO writes (address, value, width, call site) in debug builds.

**FR-DBG-5.** Dump the ring buffer to serial or to a file on crash, on demand, or at exit.

**FR-DBG-6.** A replay harness on the host that can re-run a captured trace against 86Box or a register-level stub, so a crashing sequence can be studied without the target machine.

### 9.3 Crash recovery

**FR-DBG-7.** Install a DPMI exception handler covering at minimum #GP, #PF and #UD.

**FR-DBG-8.** On exception or normal exit: reset the card to a known state, restore text mode, flush the write trace, and print a diagnostic. **The user must land back at a usable DOS prompt, not a black screen or a power cycle.**

**FR-DBG-9.** Register the same teardown via `atexit` and on `Ctrl-Break`.

### 9.4 Debug build

A `-DDGL_DEBUG` configuration enabling the trace buffer, GL state validation, FIFO accounting assertions and verbose serial output. Fully compiled out of release builds — the performance cost would be unacceptable at runtime.

---

## 10. Performance Requirements

### 10.1 Targets

| Metric | Target | Conditions |
|---|---|---|
| Frame rate | ≥30 FPS | ClassiCube, PII-300, default view distance, at the **reference configuration** of 640×480×16 |
| Triangle throughput | To be established at M3 | Sustained, textured, gouraud, Z-tested, perspective-correct |
| Fill rate | Not a constraint | See §10.2 |
| Mode-set to first frame | <2 s | From `dglInit` to first `dglSwapBuffers` |

**FPS alone is not a sufficient specification** — it is entirely determined by the scene. The real target is a triangle rate, and M3's exit criterion includes establishing the measured number so G4 can be assessed honestly rather than argued about.

640×480 is the *reference configuration against which G4 is judged*, **not a product limit**. Higher resolutions are supported wherever the BIOS offers them (§8.1); they simply carry no performance guarantee. M5 should publish a measured FPS-versus-resolution curve, which will also serve as evidence for or against the setup-bound analysis in §10.2.

### 10.2 Where the budget goes

At 640×480 with an overdraw factor of ~3 at 30 FPS, the card must fill roughly **27 Mpixel/s**. The G400's fill rate is an order of magnitude beyond this. **Fill rate is not the bottleneck.**

The bottleneck is host-side triangle setup (D1). Each textured, gouraud-shaded, Z-buffered, perspective-correct triangle requires computing edge slopes and eight interpolant gradients, then issuing on the order of 30 register writes across the bus. A Pentium II at 300 MHz will plausibly sustain something in the low hundreds of thousands of triangles per second on this path — an order-of-magnitude estimate, not a measurement.

Whether that clears 30 FPS depends directly on ClassiCube's per-frame triangle count at the chosen view distance. **This is the project's principal technical risk (R1).**

### 10.3 Optimisation levers, in expected order of value

1. **WARP microcode path (v1.1).** Moves setup entirely onto the chip. The definitive fix if host setup proves insufficient, at the cost of shipping a binary blob — a decision D1 deliberately defers rather than forecloses.
2. **Write combining on the framebuffer and FIFO apertures.** Uncached MMIO writes are expensive and this is the hot path. Approach to be investigated at M2: MTRR configuration requires ring 0, which suggests evaluating `CWSDPR0.EXE` in place of `CWSDPMI.EXE`, or DPMI 1.0 function `0x0508` (Set Page Attributes) where the host supports it. **Treat as an open investigation, not a settled plan.**
3. **Display lists holding setup-ready vertex data** (FR-DL-2), avoiding re-transformation of static world geometry.
4. **Hand-optimised assembly for the setup inner loop**, with attention to the Pentium II's FPU characteristics.
5. Reducing the view distance in ClassiCube — a legitimate outcome to document if the hardware target simply cannot sustain the default.

---

## 11. Test Strategy

The implementer works from the Linux build host and cannot touch the bench. Every run on real hardware that needs a human — to copy a file, press a key, or reset after a hang — is a run that mostly does not happen. The strategy below is built so that the great majority of iterations need nobody present, and real silicon is used to *confirm* rather than to *discover*.

### 11.1 The bench

| Machine | Role | Cards |
|---|---|---|
| **Linux build host** | Cross-compiles; runs 86Box and the host-side unit tests; receives the serial log; captures the DOS PC's video. **Never booted into DOS.** | none |
| **Remote DOS PCs** (shared with MGA-Glide) | Real-silicon loop. Boot MS-DOS; poll the build host over mTCP, run each job, upload results by FTP, reboot; serial log to the build host; display into a capture device (Epiphan DVI2USB 3.0). | G450 PCI (primary), G400 AGP, G200 |
| **Pentium II-class reference machine** | Performance target for G4. Produces the numbers in §10.1 and nothing else. | G400 AGP (to confirm — Q7) |
| **86Box on the build host** | Autonomous full-DOS-stack loop against the emulated G400/G450 (primary), G200 and G100 (§11.2). | emulated G400, G450, G200, G100 |
| **cuda6 server** | Linux register reference (Loop C, §11.3a). | G200eR2 |

### 11.2 Loop A — 86Box with the emulated Matrox cards (autonomous, emulated)

Stock 86Box's `vid_mga.c` stops at the Productiva G100 (`DEV_BRANCH` builds only). MGA-Glide's local patch series (never upstreamed) adds a Millennium G200, G400 and G450, each booting Matrox's genuine BIOS (900-33, 897-21, 935-20, unpacked from Matrox's public BIOS package into the local ROM cache, never committed), with blending, alpha test, fog, specular, 5-level mipmaps and the G400 family's 16-entry bus FIFO. DOS-GL runs on the pinned, patched build from the vendored harness (D15).

**What it covers, without a human:** PCI enumeration and aperture mapping of the target family; VBE mode set through the target family's own BIOS from protected mode; DPMI mechanics; the exception handler and text-mode restore; serial log transport; the full `dglInit`→frame→`dglSwapBuffers`→`dglShutdown` lifecycle; the trapezoid-setup register programming including blending and alpha test; every conformance test in §11.5; and ClassiCube end-to-end. Cards: G450 and G400 primary, G200 secondary, G100 smoke only.

**Harness:** MGA-Glide's `tools/loopa` (disk images with `mtools`, serial to a log file, the 86Box unit tester for screenshots and exit codes, statuses PASS/FAIL/TIMEOUT/HANG/CRASH/EMU-FATAL, `--card`). One command from build to captured screen and log; runs in CI.

**What it does not cover (R10):** FIFO *timing* (the model reports the right depth but never stalls); the colour-mask register (PLNWT, not modelled); the G400's sub-pixel trapezoid mode, 11-level mip addressing and second texture unit; anything a real BIOS or board leaves differently; and performance. **Green in 86Box means "semantically plausible", never "works on silicon".**

### 11.3 Loop B — remote DOS PCs (autonomous where possible, real silicon)

Shared with MGA-Glide (`tools/bench`, `docs/bench.md`):
- **Delivery:** each PC's `BENCH.BAT` polls the build host's HTTP queue with mTCP `HTGET`, fetches the job into `C:\TEST`, runs it, uploads `C:\OUT` with mTCP `FTP` to a per-job receiver, and reboots. `tools/bench/run.py` publishes a job and collects serial log, files and captures into `out/bench/<pc>/<job>/`. DJGPP jobs ship `CWSDPMI.EXE`.
- **Logs:** serial to the build host, unbuffered (FR-DBG-2).
- **Eyes:** the card's output into an Epiphan DVI2USB 3.0 (UVC on Linux); a program's snapshot prints `HX-CAPTURE` and holds the frame while the host grabs it. **Captured frames are for looking at, never for pixel-exact comparison**, which stays on `glReadPixels` (§11.5). Q11 still applies: TMDS in DOS on the G450, and 720×400 @ 70 Hz text mode.
- **Reset:** an optional per-PC reset command (a USB relay on the reset header, Q12) runs after a hang; without it a hang needs a person.
- **Dry run:** the whole loop runs against an emulated PC in 86Box (NE2000 on SLiRP, mTCP, the real poller) before any hardware is involved.

Every 3D exit criterion in §13 is *developed* in Loop A and *passes* only in Loop B.

### 11.3a Loop C — cuda6 G200eR2 (Linux register reference)

MGA-Glide's `mgarig` drives the server's G200eR2 through its sysfs resource files (scope approved: that device's sysfs files, the rig directory, `gdb`). It has measured FIFO depth and sync, `ALPHACTRL` reset, blend arithmetic, texel expansion, mip-level rounding and bilinear colour keys. Used for register experiments; never a target.

### 11.4 Host-side unit tests

Matrix arithmetic, clipping, gradient computation, texture format conversion and display-list capture are ordinary C and are unit-tested natively on Linux, outside DJGPP. Fast, and it keeps the class of bug that is merely arithmetic away from the hardware bring-up.

### 11.5 Conformance and visual tests

A small suite of programs, each producing a reference image:

1. Clear to a solid colour
2. Single flat-shaded triangle
3. Gouraud triangle
4. Depth-tested interpenetrating triangles
5. Back-face culling
6. Textured quad, point-sampled
7. Textured quad, bilinear, perspective-correct
8. Alpha test cutout
9. Alpha blending
10. Fog, all three modes
11. Scissor rectangle
12. Display list replay
13. Spinning textured cube (integration)

Captured via `glReadPixels` and compared, after RGB565 quantisation and with edge masks, against references rendered by host OpenGL (Mesa OSMesa) from the same program source (D16), using the shared comparison tool. Run in Loop A on every change and every card; on the G450 as the baseline for release; on the G400 and G200 for portability.

### 11.6 Integration test

ClassiCube built with `CC_BUILD_GL11` against DOS-GL, replacing `CC_GFX_BACKEND_SOFTGPU`, running a real world at 640×480×16. Correctness in Loop A then Loop B; frame rate on the PII reference machine only.

---

## 12. Build and Toolchain

| Item | Choice |
|---|---|
| Reference build | DJGPP cross-compiler (GCC) hosted on Linux |
| DPMI host | CWSDPMI r7, shipped with the examples |
| Native build | DJGPP under DOS — documented, not CI-tested |
| Other toolchains | Open Watcom / DOS4GW explicitly out of scope for v1.0 |
| Build system | Makefile; no autotools, no CMake |
| Output | `libGL.a`, plus `GL/gl.h`, `GL/glext.h` (minimal) and `GL/dosgl.h` |
| Shared code | Matrox HAL, harness, bench tooling and 86Box patches from MGA-Glide's checkout beside DOS-GL (D15). The HAL is also built by Open Watcom for MGA-Glide and stays toolchain-neutral |
| CI | GitHub Actions: cross-build, host-side unit tests, Loop A (patched 86Box, emulated G450/G400) running the conformance suite with screenshot and log artefacts |

Consumers link with `-lGL` and include `<GL/gl.h>`. No dynamic symbol resolution — `GLContext_GetAddress` returns `NULL` and the consumer links directly.

---

## 13. Milestones

Defined by verifiable exit criteria rather than dates (D11). Each milestone's exit criteria must be demonstrable and, where applicable, captured in a serial log or reference image. Anything touching the card is developed in Loop A (86Box, emulated G450/G400) and counts as done only when it passes in Loop B (G450 on the DOS bench).

> **Revised 2026-09-27 (D15).** Most of M0–M2's hardware groundwork (harness, bench tooling, PCI/BAR, VBE, engine init, clears, FIFO pacing, trapezoid setup) already exists in the shared HAL. M0–M2 therefore become "adopt the shared HAL and harness" plus DOS-GL's own context API, and every milestone gets an 86Box exit ahead of its silicon exit.

### M0 — Scaffolding

- Repository layout, MIT licence with upstream attribution file, Makefile, DJGPP cross-toolchain setup documented and reproducible.
- Shared HAL and harness from MGA-Glide's checkout (D15); the guest test shim and bench jobs work for DJGPP programs.
- Loop A (§11.2) and Loop B tooling (§11.3) from the shared harness; the bench dry run in 86Box.
- Host-side unit test harness running in CI.

**Exit (86Box):** `make` produces an empty `libGL.a`; one command builds a hello-world DJGPP program, runs it in 86Box on the emulated G450, and returns its serial output and a screenshot; the same binary runs as a bench job on the emulated bench PC. **Exit (silicon):** the same binary, pushed to the DOS PC, produces serial output and a captured frame on the build host without anyone touching the DOS PC.

### M1 — Hardware probe

- PCI enumeration via mechanism #1, with PCI BIOS cross-check; device table covering G400/G450, G200 and the G100 development target.
- BAR decode and DPMI aperture mapping.
- VBE mode enumeration and filtering (FR-HAL-3, FR-HAL-8); mode set to 640×480×16 with LFB; text mode restored cleanly.
- Exception handler and reset path installed.
- `<GL/dosgl.h>` API frozen.

**Exit:** first in 86Box against the emulated G450, G400, G200 and G100, then on the G450 DOS machine (and repeated on the G400), a diagnostic program prints chip identity, revision, VRAM size and all three BAR addresses/sizes over serial; **dumps the full VBE mode list with per-mode resolution, bpp, pitch and LFB support**; sets 640×480×16; draws a recognisable pattern by direct framebuffer writes; and returns to a working DOS prompt. The §4.2 BAR layout is confirmed or corrected in this document, and Q8/Q9 are answered.

### M2 — Command pipeline

- Register I/O layer with write trace.
- FIFO pacing chokepoint.
- Surface allocation: front, back, depth.
- Engine-driven colour buffer clear.
- One flat-shaded, untextured triangle via direct trapezoid setup register writes.
- Page flipping and `dglSwapBuffers`.
- Write-combining investigation concluded and its outcome recorded here.

**Exit:** on real hardware, a program clears to a colour using the drawing engine, draws a stationary flat-shaded triangle, and page-flips at a stable rate for 60 seconds without a FIFO stall or hang.

### M3 — API and pipeline conformance

- Matrix stacks, state tracker, vertex arrays, immediate mode.
- Full host-side setup: clipping, projection, gradients for colour, Z, texture and fog.
- Depth test, culling, blending, alpha test, scissor.
- Conformance tests 1–5 and 8–11 passing.
- **Triangle throughput benchmarked and recorded in §10.1.**

**Exit:** a spinning, gouraud-shaded, depth-tested, untextured cube renders correctly via `glDrawArrays` and via `glBegin`/`glEnd`; the measured triangle rate is documented; a go/no-go assessment of G4 against that measurement is recorded, and D1 is formally revisited.

### M4 — Texture engine

- Texture object management, VRAM heap allocation.
- `glTexImage2D`/`glTexSubImage2D` with per-texture format selection (§8.3).
- Point and bilinear filtering; perspective-correct sampling; mipmap levels.
- Display list capture and replay (§5.4).
- `GL_EXT_bgra`.
- Conformance tests 6, 7, 12, 13 passing.

**Exit:** a textured, perspective-correct, alpha-tested, fogged scene renders correctly from a display list on real hardware.

### M5 — ClassiCube integration and release

- `GLContext_*` shims in ClassiCube's `src/msdos/Window_MSDOS.c`.
- ClassiCube built with `CC_BUILD_GL11` against DOS-GL.
- Frame rate measured on the reference machine.
- Documentation: porting guide, limitations, hardware compatibility notes, debugging guide.

**Exit:** ClassiCube runs playably at 640×480×16 on the G450 and G400, with the frame rate measured on the PII reference machine and published honestly against the G4 target — including if it falls short.

### Post-1.0

- v1.1: WARP microcode path (subject to D1 revisit); native CRTC modesetting; the G400's 11-level mip addressing.
- **First post-v1 feature: G400/G450 dual texturing** (`GL_SGIS_multitexture`, `GL_ARB_multitexture`) when the target moves on to GLQuake and Quake 2, whose two-pass lightmap fallback doubles host-side setup. Built in the shared HAL; G100/G200 keep the two-pass path.
- Later: 32-bit colour, additional consumers.

---

## 14. Risks

| ID | Risk | Severity | Mitigation |
|---|---|---|---|
| **R1** | Host-side triangle setup cannot sustain 30 FPS on a PII (§10.2). | **High** | Measure at M3, before texture work. Setup isolated behind one interface (FR-TS-4) so the WARP path can replace it wholesale. Fallback positions: assembly optimisation, write combining, reduced view distance, or accepting the blob. |
| R2 | Undocumented or erroneous register behaviour. | High | Cross-reference the Matrox specification against X.org and Mesa/DRI source. Write trace and serial log make divergence diagnosable. |
| R3 | Iteration on real hardware needs a person present. | Medium | Loop A removes the person from most iterations; Loop B's mTCP delivery, serial and video capture remove them from everything except reset-after-hang. A reset relay (Q12) closes that last gap. |
| R10 | **False green from 86Box.** The emulator accepts command streams that overrun a real FIFO, and is lax on alignment; code can pass every Loop A test and hang the G450. | High | Nothing counts as done until it passes in Loop B. FIFO pacing (FR-HAL-6) is written against the specification's stated depths, not tuned in the emulator, and is validated on silicon first at M2. |
| R11 | The emulated G400/G450 differ from silicon in places (modelled on the G200 engine plus the G400 specification), so Loop A can validate the wrong bit layout. | Medium | Chip-capability table with per-generation register encodings, taken from the specification and the X.org/DRI chip-type branches; the emulated cards' documentation lists what is modelled and what is guessed; any encoding that differs between generations is verified on the G450 explicitly. |
| R4 | Hardware failure — period-correct PSUs and motherboards. | Low | Three cards on hand (G450, G400, G200) across multiple machines. The PII reference machine is the only single point of failure, and it is needed for performance only. |
| R5 | VBE limits us to BIOS-advertised modes and BIOS page flipping (D8). | Medium | Modesetting behind a vtable (FR-HAL-3) so a native CRTC backend drops in without disturbing the layers above. |
| R6 | Scope creep from Tier 2 and general GL 1.1 completeness. | Medium | Tier 1 is the definition of done. Tier 2 slips to v1.1 without debate. |
| R7 | Solo part-time cadence stalls mid-bring-up. | Medium | Small milestones with demonstrable exit criteria; each one leaves something that visibly works. |
| R8 | 16-bit colour banding is visually unacceptable in ClassiCube. | Low | Per-texture format selection (§8.3). Optional ordered dithering if needed. |
| R9 | AGP-only G400 limits testable host machines. | Closed | The G450 PCI shares the G400's device ID and 3D core and fits any PCI host. |

---

## 15. Licensing and Provenance

- **DOS-GL is MIT/X11.**
- Register definitions and initialisation sequences may be derived from the MIT-licensed X.org `mga` DDX and Mesa/DRI `mga` driver, and from Matrox's published G200/G400 specification documents.
- Every file containing derived material carries the upstream copyright notice. `THIRD_PARTY.md` records each source, what was taken, and its licence.
- **No binary blobs in v1.0.** Should D1 be revisited in favour of WARP, the microcode would be a separate, clearly-labelled optional component under Matrox's redistribution terms — never silently bundled into `libGL.a`.
- Consumers may link `libGL.a` under any licence.

---

## 16. Repository Layout

```
DOSGL/
  PRD.md
  LICENSE                  MIT
  THIRD_PARTY.md           attribution for derived material
  Makefile
  include/GL/
    gl.h                   standard GL 1.1 subset declarations
    glext.h                minimal, for GL_BGRA_EXT
    dosgl.h                DGLConfig, dglInit, dglSwapBuffers, ...
  src/
    hal/                   PCI, DPMI mapping, modeset, registers, FIFO
    setup/                 host-side triangle setup
    gl/                    state engine, matrices, arrays, lists, textures
    debug/                 serial, write trace, exception handler
  third_party/
    classicube/            pinned submodule (M5)
  tests/
    unit/                  host-native unit tests (Linux)
    conform/               the 13 visual tests (plain GL; DOS and host OSMesa builds), generated references
  tools/
    conform/               DOS run + OSMesa reference + comparison
    classicube/            build script and the one ClassiCube patch
  examples/
    probe/                 M1 hardware diagnostic
    triangle/              M2/M3 demos
    cube/                  M3/M4 textured spinning cube
  docs/
    porting.md  hardware.md  debugging.md  registers.md
```

---

## 17. Open Questions

| # | Question | Needed by |
|---|---|---|
| Q1 | Exact revision IDs distinguishing G400 / G400 MAX / G450, all of which report `102B:0525`. Partly answered: G450 is `0x80` and up; the G400 BIOS singles out revision 2. Bench cards recorded at M1. | M1 |
| Q2 | Is write combining achievable under CWSDPMI, and does it require `CWSDPR0.EXE` (ring 0)? (§10.3) | M2 |
| Q3 | Confirmed maximum texture dimensions for G400 and G200. | M4 |
| Q4 | What is ClassiCube's actual per-frame triangle count at default view distance, 640×480? Determines whether R1 bites. | M3 |
| Q5 | Does the G400 3D engine impose constraints on framebuffer/Z-buffer placement or tiling beyond pitch alignment? | M2 |
| Q6 | Is VBE page flipping (function `4F07`) reliable across G400 BIOS revisions, or is native CRTC needed sooner than planned? | M2 |
| Q7 | Reference test machine specification — exact CPU, chipset, RAM, and which card it carries (presumed G400 AGP) — against which G4 is judged. | M3 |
| Q8 | What 16bpp linear-framebuffer modes does the G400 BIOS actually advertise, what pitch does it choose for each, and how many survive the FR-HAL-8 alignment filter? **Answered in emulation** (Matrox BIOSes 897-21 and 935-20: 555/565 at 640×480 to 1280×1024, pitch = width); silicon at M1. | M1 |
| Q9 | Does the G400/G450 BIOS report VBE 3.0, and is `4F02h` refresh-rate control usable? **VBE 3.0 in emulation** for both BIOSes; refresh control and silicon at M1. | M1 |
| Q10 | **Answered:** mTCP `HTGET` polls `LATEST.TXT` on the build host's HTTP server and fetches a host-generated `FETCH.BAT`; results go back by mTCP `FTP`. `HTGET` exits 21 on "200 OK". | — |
| Q11 | Capture chain is G450 DVI → passive DVI-D→HDMI → HDMI capture device → V4L2 on the build host. Verify: the G450 BIOS drives TMDS on the primary head under DOS; 720×400 @ 70 Hz text mode syncs; 640×480 @ 60 Hz syncs; and which of the VBE modes from Q8 also sync, since that bounds how much of §8.1 is visually verifiable. Fallback if TMDS is dark in DOS: DVI-I analogue pins → VGA → HDMI scaler. | M0 |
| Q12 | Remote reset for the DOS PC after a hard hang. A USB relay across the motherboard reset header is the cheap option; no spare smart plug exists. Until this exists, hangs need a person. (The bench tooling runs a per-PC reset command when one is configured.) | M0–M2 |
| Q13 | **Answered:** under 86Box's SLiRP networking the guest reaches the host at `10.0.2.2`. | — |
