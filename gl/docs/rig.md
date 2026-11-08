# Loop C: DOS-GL on a real card from Linux

86Box's speed says little about a real Pentium II with a Matrox card, and
the bench PCs are not set up yet. A Linux machine with a Matrox chip can
still run DOS-GL's own code on silicon: `make rig` builds DOS-GL for 32-bit
Linux, with x87 maths as DJGPP's code uses, on the HAL's Linux port, which
reaches the card through its sysfs resource files (MGA-Glide's Loop C,
`docs/loop-c-results.md` there, drives a G200eR2 the same way).

## What the rig build changes (`DGL_RIG`)

- No BIOS: VRAM is `DGL_RIG_VRAM_KB` (default 8192), and the modes are the
  usual 16-bit sizes, drawn into VRAM and never shown.
- Nothing on the display is touched: no mode set, no DAC ramp, no display
  start, no text mode at the end. The buffers start at
  `DGL_RIG_VRAM_BASE` (KB), so they can sit above the console of a host
  driver that is still bound.
- Everything else (the GL pipeline, the HAL's setup and engine code) is the
  DOS build's.

## Programs

| Program | What |
|---|---|
| `build/rig/conform/tNN` | the conformance tests (`tests/conform/ct_rig.c`); frames to `$CT_OUT/<name>.ppm` |
| `build/rig/rigbench` | batches of triangles (16-1024 px; flat, Gouraud, textured, textured and blended; with and without Z) timed until `glFinish`, and the raw register write rate: on a fast host CPU the time is the chip's and the bus's. Each case reports the triangles the HAL set up (`set_up=`), and `HX-TEST drawn` fails if a case set up fewer than it drew: before 2026-10-02 every batch was at eye z +0.5, outside `glOrtho(..., 0, 1)`, so the cases timed rejection |

All are static; they need root (the resource files) and `RIG_BDF`.

## Running: `tools/rig/run.py`

```
tools/rig/run.py info --host retro@devserver --bdf 0000:09:00.0        # read-only
tools/rig/run.py smoke conform bench --host ... --bdf ... --unbind     # writes: agreed window only
```

Stages stop at the first anomaly. `smoke` runs MGA-Glide's `mgarig` (read
registers, DWGSYNC, one fill, one triangle) before DOS-GL touches the card.
`--unbind` detaches the host's driver for the session and binds it again at
the end, whatever happens; without it, pass `--vram-base-kb` above the
console's framebuffer.

Results go to `out/rig/<host>/`. Frames are compared with Mesa's the same
way as `make conform` (RGB565 quantisation, `tests/conform/manifest.json`).

## devserver

`retro@devserver` has a G200eR2 (`0000:09:00.0`) that is also its console
(`mgag200`, 1920x1200x32 from VRAM offset 0), beside GPUs running
production jobs. Only read-only stages run without a window agreed with
its owner.
