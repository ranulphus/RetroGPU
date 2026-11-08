# ABI facts

What a drop-in `GLIDE2X.OVL` must match, with where each fact comes from.
Implementation follows this file and public documentation only (PRD D12);
`tools/ci/no_3dfx.py` keeps 3dfx material out of the repository.

## Module

| Fact | Source |
|---|---|
| DOS/4G LE DLL, module name `glide2x` (lowercase), module flags `0x8200` | the retail OVL's header (`tools/abi/ledump.py`) |
| Exports are `__stdcall`, named `_<NAME>@<argbytes>` in upper case | the games' import tables (`tools/abi/extract_imports.py`) |
| GTA imports 131 names, Screamer Rally 129; GTA also imports `guFbReadRegion` / `guFbWriteRegion` | same (`abi/games/*.names`) |
| Every fixup in the retail OVL is internal; it imports nothing | `ledump.py` (so a small LE loader can run it: `tests/shim/leload.c`) |
| DOS/4GW calls the DLL's init once, at load | spike S2 (`docs/loader.md`) |

## Types

| Fact | Source |
|---|---|
| `GrVertex` is 60 bytes: x, y, z, r, g, b, ooz, a, oow, then two TMU blocks of sow, tow, oow | Glide 2.x SDK documentation; checked by value ranges in the games' vertices (traces) |
| `GrState` is 312 bytes, opaque to the game | SDK documentation; `include/glide/layout.h` asserts it |
| `GrHwConfiguration` is 148 bytes | same |

## Behaviour measured from the retail runtime

Each is covered by a conformance test (see `docs/reference-gaps.md` for how
the emulated reference card is handled).

| Behaviour | Test |
|---|---|
| Vertex x, y are truncated to 1/16 pixel | t04 |
| Texture state after `grSstWinOpen`: clamp, point sampling; s,t are not iterated until a texture combine is set | t25 |
| A download into the texture being sourced takes effect without a new `grTexSource` (the TMU reads its memory); whole-chain, level and partial downloads | t26 |
| After `grSstWinClose` and a `grSstWinOpen` with another buffer layout, textures downloaded again are intact through a depth clear (MGA-Glide once put them inside the new depth buffer) | t27 |
| Texture memory: level sizes and offsets (short side at least 2 texels, totals rounded to 8 bytes) | `tests/unit/data/texmem.txt` |
| LOD: `floor(log2 of the larger texel-space gradient)` plus the bias in quarter steps | t12 |
| Bilinear filtering samples at texel centres (half-texel offset) | t11 |
| Default gamma is 1.7; the ramp is applied by the video DAC, not in the framebuffer | t22 |
| `grConstantColorValue4` sets the colour of the DELTA0 presets, black until set | t13 |
| DIFF_SPEC_A is texel x alpha + iterated; DIFF_SPEC_B is texel x iterated + alpha | t13 |
| Iterated-Z fog draws unfogged on Voodoo Graphics | t17 |
| `guFogGenerate*` compute in single precision | t17 |
| Pixel-pipeline LFB writes skip the colour combine but are blended, keyed and depth-tested | t18 |
| W-buffer depth values are a 16-bit float code of 1/w (4-bit exponent, inverted 12-bit mantissa) | t07, `src/glide/depth.c` |
