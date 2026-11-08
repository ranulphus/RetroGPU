# Configuration

MGA-Glide reads options from `MGAGLIDE.CFG` in the game's directory and then
from the `MGAGLIDE` environment variable (which wins), as `key=value` pairs
separated by spaces or new lines:

```
SET MGAGLIDE=res=1024x768 bpp=32 trilinear=1
```

Everything defaults to fidelity: with no options set, MGA-Glide aims to
draw what a Voodoo Graphics board would.

## Enhancements (off by default)

| Option | Values | Effect |
|---|---|---|
| `res` | `WxH`, e.g. `1024x768` | Render at W x H while the game keeps its own resolution. Textures use finer mip levels; LFB access is scaled |
| `bpp` | `16` (default), `32` | Render in 32-bit colour (no dithering); falls back to 16 when no 32-bit mode fits |
| `hwmip` | `0`, `1` | G200 and later: let the chip pick the mip level per pixel (it rounds where the Voodoo floors, so textures are a little softer; no 8-row bands) |
| `trilinear` | `0`, `1` | G200 and later: blend between mip levels (implies `hwmip`) |
| `warp` | `1` (default), `0` | G200, G400 and G450: the chip's WARP engine sets triangles up from vertices sent by DMA; `0` keeps the CPU's setup, with the Voodoo's exact edge rule (docs/g400-warp.md, docs/g200-warp.md) |
| `fast` | `1` (default), `0` | With `warp`: a triangle drawn whole at one level goes by the fast path, its state's work done once a state change and its vertices written straight into the DMA stream; `0` sends every triangle the general way (the same pixels) |
| `agp` | `1` (default), `0` | With `warp`, under GLOS: the DMA rings write-combining and fetched by AGP cycles, about three times PCI's bandwidth; `0` keeps PCI. The G200 always keeps PCI (docs/g200-warp.md). (Not yet for Glide games under DOS/4GW, whose DPMI 0A00h doesn't reach GLOS) |
| `dma_ring`, `dma_list`, `dma_kick` | KB (`128`), N (`2`), KB (`6`) | With `warp`, tuning: the DMA rings' size, the list's share of it (1/N; Glide writes many registers a triangle), and the vertices between kicks (docs/g400-warp.md) |
| `bilinear` | `0`, `1` | Filter every texture bilinearly |
| `z32` | `-1` auto (default), `0`, `1` | 32-bit depth buffer. Auto uses it when VRAM allows (it makes W-buffering more precise than the Voodoo's) |
| `zoom` | `0` (default), `1` | Show 320x240, 400x300, 512x384 and 640x512 (the latter through `res=640x512`) in the BIOS mode twice the size with the chip's line and pixel doubling: the same monitor signal, no scaling cost. Off until the bench confirms it on each card |
| `scale_filter` | `nearest` (default), `bilinear` | How sizes the BIOS lacks are scaled into a BIOS mode (see Resolutions) |

## Resolutions

The Matrox BIOSes offer 16-bit 640x480, 800x600, 1024x768 and 1280x1024 (the
G100's also 1600x1200). A Glide resolution the BIOS has is shown as it is.
Any other is drawn at its own size into render buffers and scaled by the
drawing engine into a BIOS mode at every swap (`scale=1`, the default):

| Glide resolution | Shown in | How |
|---|---|---|
| 320x240, 400x300, 512x384 | 640x480, 800x600, 1024x768 | exactly 2x (with `zoom=1`: the chip's doubling instead) |
| 320x200, 640x200, 640x350, 640x400, 400x256, 512x256 | 640x480 | stretched to fill 4:3, as a CRT showed them |
| 960x720, 856x480 | 1024x768 | scaled evenly and centred (856x480 with black bars) |
| 640x480, 800x600, 1024x768, 1280x1024 | the same | native |
| 1600x1200 | 1600x1200 where the BIOS offers it (G100) | native; otherwise the window does not open |

`res=640x512` (with the game at 640x480, or any size) renders at 640x512,
shown 2x in 1280x1024. Scaled modes keep 32-bit colour off (the engine
scales 16-bit pictures), use two display buffers besides the render buffers
(1280x1024 needs about 7 MB of VRAM with 640x512 rendering), and include the
scaling in every frame's time. `scale=0` restores the old behaviour (the
smallest larger mode, drawn 1:1 in its top-left corner); `scale=force` takes
the scaled path even for native sizes (a test switch). `MGL-WINOPEN` reports
the display mode and how the picture is shown (`display=`, `fit=`).

## What the game is told

| Option | Default | Effect |
|---|---|---|
| `version` | `2.43` | Version string from `grGlideGetVersion` |
| `fb_mb`, `tmu_mb`, `tmus` | `2`, `2`, `1` | Reported frame-buffer and texture memory, TMU count |
| `voodoo2` | `0` | Report a Voodoo 2 (also enables iterated-Z fog) |

## Behaviour on the G100

| Option | Default | Effect |
|---|---|---|
| `g100_additive` | `0` | Additive blending, which the G100 cannot do: `0` draws it as 50% stipple, `1` skips it |
| `gamma` | `1` | `0` keeps a linear display ramp whatever the game asks |
| `strict` | `0` | `1` skips every approximated draw state (to find them; see `docs/combine-coverage.md`) |
| `hooks` | `1` | Fault and exit hooks that put the display back in text mode |

## Diagnostics

| Option | Effect |
|---|---|
| `log` | Log level 0 (errors) to 4 (trace); lines go to COM1 |
| `stats=N` | `MGL-STAT` line every N frames (`fast`, `fastbands`: triangles by the fast path, and of them those drawn in bands of levels; `vfallback`, `splits`: totals of Voodoo edges too flat for the chip's AR fields, drawn with the centre rule, and of triangles over 8191 pixels, drawn as four; `docs/loop-c-results.md`); with the profiling runtime (`make runtime-prof`, `build/ow-prof/GLIDE2X.OVL`) also `MGL-PROF`, the cycles of each stage (`docs/setup-perf.md`) |
| `census=1` | Log each distinct draw state once (`MGL-CENSUS`) |
| `exit_after=N` | End the run after N frames (the harness uses it) |
| `snap=a,b,...` | Mark frames for the harness to capture (`MGL-SNAP`) |
| `trace=1`, `trace_path`, `trace_from`, `trace_to` | Record a call trace (`docs/trace.md`) |
| `retail=PATH` | GLTRACE.OVL only: the retail runtime to forward to |
