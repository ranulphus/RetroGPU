# Reference-card gaps

Conformance references are rendered by the retail Voodoo Graphics runtime
on 86Box's emulated Voodoo (PRD §11.4). Where that emulation departs from
the real Voodoo, the reference is wrong, and MGA-Glide follows the Glide
specification instead. Each gap below is handled in the test or the
manifest, never by copying the gap into the runtime.

| Gap in 86Box's Voodoo | Effect on references | Handling |
|---|---|---|
| LFB writes and reads ignore the lfbMode Y-origin bit | Lower-left-origin locks land upside down | t18: regions excluded with manifest `ignore` rectangles |
| LFB writes ignore the lfbMode RGBA lane order | ABGR/RGBA/BGRA 8888 writes show as ARGB | t18: region excluded |
| The decoded-texture cache is keyed on the XOR of the 256 palette entries | Two palettes with equal XOR (any per-channel permutation of 0..255 gives 0) reuse the stale texture | t19: the test palettes have distinct XORs. Game references with palette changes may show stale colours; treat such frame differences with suspicion |

## Behaviour measured from the retail runtime

These are properties of the retail runtime, found with the conformance
programs, which MGA-Glide reproduces:

- Default gamma is 1.7 (t22, frame `t22_d`).
- Pixel-pipeline LFB writes skip the colour combine but go through chroma
  key, alpha test, depth test and blending (t18).
- Iterated-Z fog draws unfogged, with or without a depth buffer and
  whatever the fog table holds (t17).
- Fog tables from the gu generators are computed in single precision
  (t17 logs the tables; they match byte for byte).
- Texture coordinates are not iterated until a texture combine function
  has been set, and the defaults are clamp and point sampling (t25).
