/* tex.h - texture state for TEXTURE_TRAP (shared by MGA-Glide and DOS-GL).
 *
 * Texture coordinates reach setup_triangle as s, t normalised to the bound
 * level (1.0 = one texture width) and multiplied by q = 1/w; the setup
 * scales them and writes TEXWIDTH/TEXHEIGHT. tex_emit() programs the rest
 * of the sampler for one level or a mip window. */
#ifndef MGA_TEX_H
#define MGA_TEX_H
#include "mga/setup.h"

typedef struct {
    uint32_t org;               /* VRAM offset of the first level */
    uint32_t mip_org[5];        /* the window's levels; mip_org[0] == org */
    int      mip_n;             /* levels in the window (G200+); 0 or 1 = one level */
    int      w_log2, h_log2;    /* hardware size of the first level (>= 3) */
    int      pitch;             /* texels */
    uint32_t hwfmt;             /* TEXCTL_TW16, TEXCTL_TW15, TEXCTL_TW12, ... */
    int      clamp_u, clamp_v;  /* clamp instead of wrap */
    int      modulate;          /* texel times iterated colour (TEXCTL.tmodulate) */
    int      bilinear;          /* linear magnification and minification */
    int      trilinear;         /* mip window: blend two levels (G200+) */
    int      key_alpha0;        /* texels with alpha 0 are transparent (STRANS|TAMASK) */
    uint32_t texctl2;           /* G200+: TEXCTL2 (specular, decal blend) */
} mga_texstate;

/* Program TEXORG (and TEXORG1..4 for a window), TEXCTL, TEXCTL2, TEXFILTER
 * and TEXTRANS. ALPHACTRL stays with the caller. */
void tex_emit(const mga_texstate *t);

/* tex_emit when the registers hold prev's state (a tex_emit of it): only
 * the registers whose values differ (TEXORG and TEXCTL between two levels
 * of a texture; all of them for a window or another filter or TEXCTL2). */
void tex_emit_change(const mga_texstate *t, const mga_texstate *prev);

/* G400 dual texturing (docs/g400-dual-texture.md): program t0 into both
 * maps (TEXCTL2 gets TEXCTL2_DUALTEX), then map 1's sampler from t1 alone;
 * TEXWIDTH/TEXHEIGHT and the TMRs come per triangle from setup_triangle with
 * MGA_S_TEX2. tex_emit() with dualtex clear returns both maps to single
 * texturing. */
void tex_emit_dual(const mga_texstate *t0, const mga_texstate *t1);
/* Map 1's sampler alone, when the maps hold a tex_emit_dual's state and only
 * map 1's changes (tex_emit_dual's second half). */
void tex_emit_map1(const mga_texstate *t1);
/* The G400 combiner: TDUALSTAGE0 and TDUALSTAGE1 (single texturing uses
 * stage 0 and wants stage 1 equal to it; zero passes the texel through). */
void tex_emit_combiner(uint32_t stage0, uint32_t stage1);

/* Keep the engine on well-behaved inputs: in wrap mode shift each axis by
 * whole texture periods so the smallest coordinate lies in [0, 1); with
 * bilinear filtering move the sample point by half a texel so the filter
 * centres on texels. */
void tex_adjust_coords(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t);
/* The same for map 1's coordinates (s1, t1). */
void tex_adjust_coords1(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t);

#endif
