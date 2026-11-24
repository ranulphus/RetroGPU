# DOS-GL on DirectX 7-class cards (X1-X4)

The owner's programme of 2026-11-24: better pictures and higher presets for programs on the cards of OpenGL 1.3's
generation, in four steps. The plan, with what Echoes of the Void and Fifth Wheel asked for, is the session's plan
file; this file records what is built and what the cards showed.

| Step | What | State |
|---|---|---|
| X1 | Picture-quality features, per card, and settings that apply them to programs that don't ask | under way |
| X2 | GL 1.1's lighting, normals, texture-coordinate generation and clip planes on the CPU (accepted and ignored until now) | done |
| X3 | Hardware transform and lighting on the Radeon 7500 (RV200) and 7200 (R100): `GL_DGL_hw_tnl`, `DGL_TNL=0/1` | transform on (X3a); lighting, fog, texgen to come |
| X4 | SSE in the software transform (needs GLOS to enable it) | to do |

## X1: what each renderer offers

`GL_EXTENSIONS` is built from the back end's caps (`dgl_get_caps`), so a program sees only what its card draws.

| | Software renderer | Radeon (7000, 7500, 7200) | Matrox, TNT2, Rage 128 |
|---|---|---|---|
| `GL_EXT_texture_filter_anisotropic` | 16:1 (reference span) | 16:1 | not yet |
| `GL_EXT_texture_lod_bias` | +-15 | -1 to +127/128 (reported 1) | not yet (the Rage 128 has a field) |
| `GL_ARB_texture_env_dot3` | yes | yes (DOT3_RGBA's alpha: see below) | no |
| `GL_ARB_texture_compression`, `GL_EXT_texture_compression_s3tc` | yes | yes | yes |
| `GL_ARB_texture_cube_map` | yes | yes (base level only: the chip can't mipmap one) | no |

Compressed textures (DXT1, DXT3, DXT5) are decoded by the CPU into the level's RGBA8 copy (`src/gl/texcomp.c`),
so every card takes them; the blocks are kept for `glGetCompressedTexImageARB`. Compressed internal formats given
uncompressed texels stay uncompressed (`GL_TEXTURE_COMPRESSED_ARB` false), as the extension allows. The Radeons
could take the blocks as they are (less memory, less bus); not done yet.

The settings `DGL_TRILINEAR`, `DGL_ANISO` and `DGL_LODBIAS` (`docs/porting.md`) apply these to every texture, for
programs (GLQuake) that never ask.

## X1: what the cards showed

- **Radeon 7500, anisotropic filtering** (P3, 2026-11-24): the chip's own anisotropic minifying filters with the
  ratio field (Mesa's `radeonSetTexFilter`) matched Mesa's llvmpipe on t37 to within 0.01% of pixels; the far
  rows' stripe contrast went from 1.7 (isotropic) to 82.6 (16:1).
- **Radeon 7500, LOD bias**: the field (`PP_TXFILTER` bits 15:8) is signed, a 128th of a level each on both sides:
  raw 64 drew +0.51, 96 +0.78, 127 +0.99 (t36's trilinear squares). Mesa 7.0.3's `radeonTexEnv` scales the positive
  half as a 32nd (a stated range of -1 to +4), which draws a quarter of the bias asked for. So the range is -1 to
  +127/128, and DOS-GL reports `GL_MAX_TEXTURE_LOD_BIAS_EXT` as 1.
- **Radeon 7500, dot3**: DOT3_RGB as Mesa forms it (`BLEND_CTL_DOT3` at the chip's x4) matches Mesa. DOT3_RGBA's
  product did not reach the alpha with Mesa's words (alpha combine as set, x4 or x1), nor with the alpha's own
  operation set to dot3, so the alpha is the alpha combine's (said once); t38's DOT3_RGBA quad is excused for the
  Radeon in the manifest. Bump mapping uses DOT3_RGB.
- **Radeon 7500, the whole suite** (P3, 2026-11-24 15:30): 38 of 38 with these changes, then t39.

## Tests

- t36 (LOD bias), t37 (anisotropic filtering), t38 (dot3), t39 (S3TC: every block form, a sub-image, the
  blocks read back). Each skips itself where its extension isn't advertised
  (`ct_has`, `CT-SKIP`; `SKIPPED` in `tools/conform/run.py`), as on the Matrox cards in Loop A.
- t37 judges the card's anisotropic frame by its own contrast check (`ct_check`), since cards filter
  anisotropically each in their own way.

## X2: lighting on the CPU

`src/gl/light.c`: OpenGL 1.1's section 2.13 as written: eight lights (directional, positional with attenuation,
spot), the light model (ambient, local viewer, two-sided), front and back materials, colour material, normals
from `glNormal*` and `glNormalPointer` (bytes, shorts, ints, floats, doubles), `GL_NORMALIZE` and
`GL_RESCALE_NORMAL_EXT` (both normalise). A lit vertex's colour is computed in `dgl_transform`; two-sided lighting
keeps the back's colour too and the assembly picks by the triangle's facing (the sign of the homogeneous
determinant of x, y and w, which holds for the part a clip leaves). While lighting is on, draws take the general
vertex path: no back end's fast path transforms them (`DGL_VTX_EXTRA`). t40 matches Mesa to the pixel on the
software renderer at 8, 16 and 32 bits. Display lists still execute state calls at compile time (FR-DL-3), so a
`glMaterial` inside a list takes effect then, not at its call.

## X2: clip planes and texture-coordinate generation

`src/gl/texgen.c`. A clip plane is taken into eye coordinates at `glClipPlane` (the modelview's inverse) and into
clip coordinates at each draw (the projection's inverse), where a point's distance is the same linear function;
the assembly cuts a triangle (fanned back out with its provoking vertex), a line or a point against the enabled
planes before the back end sees it, so no back end changed. Texture coordinates s and t of each unit are generated
object-linear, eye-linear (the plane into eye coordinates at `glTexGen`) or sphere-mapped, before the texture
matrix. Both take the general vertex path. t41 matches Mesa to the pixel on the software renderer (orthographic and
perspective frames).

## X2: the r coordinate and cube maps

Texture coordinates now carry r through `glTexCoord3/4`, `glMultiTexCoord3/4`, arrays of three or four, the texture
matrix and clipping; generation covers r and q too, with `GL_REFLECTION_MAP_ARB` and `GL_NORMAL_MAP_ARB`.
`GL_ARB_texture_cube_map` (where the back end says `cube`): cube-map objects with their own binding per unit and a
default object, six square faces through every upload and query (`glTexImage2D`, sub-images, copies, compressed
images, `glGetTexImage`, level parameters), `GL_TEXTURE_CUBE_MAP_ARB` above `GL_TEXTURE_2D` on a unit. The software
renderer samples them in its reference span (the face by the major axis, its level of detail from the face
coordinates a pixel along); its tiles don't keep cube-mapped drawing, which is drawn at once. t42 (the six faces
unfolded by explicit directions, an environment-mapped and a normal-mapped sphere, a cube map above a 2D texture,
the binding and a face read back) matches Mesa to the pixel. The Radeons can't mipmap a cube map (Mesa's driver
sends the base level only): their cube map is the six base levels in one block, faces 0 to 4 at the
`PP_CUBIC_OFFSET_Tn_m` registers and face 5 at `TXOFFSET`, faces 1 to 4's sizes in `PP_CUBIC_FACES_n` and face 5's
in `TXFORMAT`'s F5 fields (without those the P3's 7500 drew face 5 as one texel), r in the vertex's q slot. t42 on
the 7500 matches Mesa to the pixel.

## X3: the first silicon results (DTCL0, the P3's 7500, 2026-11-24)

`tests/radeon/dtcl0.c`: DTRI0's three triangles given in object coordinates, taken to the window by the chip. What
works, as Mesa's radeon driver does it:

- The state: `SE_CNTL_STATUS` without `TCL_BYPASS`; `SE_COORD_FMT` = `VTX_W0_IS_NOT_1_OVER_W0`; `SE_CNTL`'s viewport
  transforms on and `SE_VPORT_*` set (x: W/2, W/2; y: -H/2, H/2; z: 0.5, 0.5); `SE_TCL_OUTPUT_VTX_FMT` Z0, W0 and a
  packed diffuse; `SE_TCL_OUTPUT_VTX_SEL` computing x, y, z and w; the matrix slots as Mesa numbers them
  (model-projection 0, model 1, its inverse transpose 2, texture matrices 3 to 5); the matrix's rows written into
  the vector memory (`SE_TCL_STATE_FLUSH`, `SE_TCL_VECTOR_INDX_REG`, then `SE_TCL_VECTOR_DATA_REG` as a one-register
  table); the four guard-band scalars at 1.0.
- The vertices: an array in video memory (`3D_LOAD_VBPNTR`: one component, its size and stride in dwords, its
  address in the chip's space), then `3D_DRAW_VBUF` with `MAOS_ENABLE` and `TCL_ENABLE`.
- The result: coverage identical to the reference (0 pixels differ), colours within 9 levels, with the matrix
  turning the triangles 0, 30 or 117 degrees (the vertices given turned back).
- Vertices inline in the stream (`3D_DRAW_IMMD` with `TCL_ENABLE`) are not taken that way: the engine stays well
  but the triangles come out wrong (the whole screen drawn). Mesa's driver uses arrays alone for T&L; so will X3,
  dynamic vertices copied into an area the chip reads.
- The rate: a 64 x 64 grid of 4-pixel cells (7,938 triangles as an unindexed list, 23,814 vertices) drawn 50
  times: 7.68 million triangles a second, 23 million vertices (DOS-GL's CPU path on the P3: in the hundreds of
  thousands).

## X3a: DOS-GL's draws through the engine

The front end tries a back end's `tnl` sink hook before anything else for every draw (`src/gl/vertex.c`); the Radeon
back end takes it where its engine does the state (so far: no lighting, fog, texture-coordinate generation, clip
planes, texture matrix, cube map, polygon offset or line and point primitives; texture coordinates within 1,024, as
the chip's floats lose a fraction beyond, conformance t14). It writes the draw's vertices in object coordinates
(x, y, z, w, the colour, the units' s and t) into a 1 MB vertex area in video memory (made at the first such draw;
from its start again after a sync when full), makes indices for GL's primitive (strips and fans as the chip's below
4,092 vertices, else lists; quads and quad strips as lists with the general path's diagonal, or ending at the quad's
last vertex when flat; a flat polygon's first vertex as the provoking one), sets the engine's state (the
model-view-projection matrix and the viewport where they changed, outputs, shading, culling in both the setup engine
and the engine, as Mesa's radeonCullFace and radeonFrontFace) and sends the indexed draw (`hal/src/radeon/tcl.c`). A
window-space draw after it puts the window's words back (`tri.c`). `GL_DGL_hw_tnl` is in `GL_EXTENSIONS` while it is
on; `DGL_TNL=0` keeps every draw on the CPU; `DGL-STAT` counts `tnl/s`; the first such draw says
`DGL-RADEON the transform engine draws`.

On the P3's 7500 (2026-11-24): the whole suite passes with it, 32 of the 42 tests drawn through the engine (the rest:
fog, lighting, texture generation, cube maps, lines and points, a capped texture heap), against the same Mesa
references.
