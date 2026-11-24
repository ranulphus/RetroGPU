# DOS-GL on DirectX 7-class cards (X1-X4)

The owner's programme of 2026-11-24: better pictures and higher presets for programs on the cards of OpenGL 1.3's
generation, in four steps. The plan, with what Echoes of the Void and Fifth Wheel asked for, is the session's plan
file; this file records what is built and what the cards showed.

| Step | What | State |
|---|---|---|
| X1 | Picture-quality features, per card, and settings that apply them to programs that don't ask | under way |
| X2 | GL 1.1's lighting, normals, texture-coordinate generation and clip planes on the CPU (accepted and ignored until now) | to do |
| X3 | Hardware transform and lighting on the Radeon 7500 (RV200) and 7200 (R100): `GL_DGL_hw_tnl`, `DGL_TNL=0/1` | to do |
| X4 | SSE in the software transform (needs GLOS to enable it) | to do |

## X1: what each renderer offers

`GL_EXTENSIONS` is built from the back end's caps (`dgl_get_caps`), so a program sees only what its card draws.

| | Software renderer | Radeon (7000, 7500, 7200) | Matrox, TNT2, Rage 128 |
|---|---|---|---|
| `GL_EXT_texture_filter_anisotropic` | 16:1 (reference span) | 16:1 | not yet |
| `GL_EXT_texture_lod_bias` | +-15 | -1 to +127/128 (reported 1) | not yet (the Rage 128 has a field) |
| `GL_ARB_texture_env_dot3` | yes | yes (DOT3_RGBA's alpha: see below) | no |
| `GL_ARB_texture_compression`, `GL_EXT_texture_compression_s3tc` | yes | yes | yes |

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
