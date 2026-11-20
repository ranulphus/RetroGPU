/* share.h - drawing helpers for the back ends that draw from Glide's vertices on the CPU's set-up rather than the
 * Matrox's (the ViRGE's, glide/src/backend/virge; the TNT2 back end has its own copies until the TNT2 is back on a
 * bench to check a switch: the plan of 2026-11-20). */
#ifndef BACKEND_SHARE_H
#define BACKEND_SHARE_H
#include "glide/mg.h"

/* The Voodoo's truncation of a window coordinate to 1/16 pixel toward zero. */
int32_t mg_snap16(float v);

/* A vertex's texture coordinates, in units of the largest level's 256: s/w over w (TMU0's 1/w under the STW hint). */
void mg_st_of(const GrVertex *v, double *s, double *t);

/* Whether the colour or alpha combine reads the texture. */
int mg_uses_texture(void);

/* The Voodoo's level (the Matrox back end's model): lambda = log2 of the larger of the texel coordinates' gradients
 * along x and along y at the largest level, in quarter levels with the LOD bias, floor(lambda) clamped to the chain. */
typedef struct { double s0, sx, sy, t0, tx, ty, q0, qx, qy, x0, y0; int valid; } mg_lod_planes;
void    mg_lod_setup(mg_lod_planes *L, const GrVertex *a, const GrVertex *b, const GrVertex *c);
GrLOD_t mg_level_at(const mg_lod_planes *L, double px, double py);
/* Whether the texture is minified there (lambda with the bias above zero: the Voodoo's minification filter), for
 * chips that filter a triangle one way. */
int     mg_minified_at(const mg_lod_planes *L, double px, double py);

/* What the Voodoo works out per pixel and these chips per vertex or per triangle: table fog's factor and the level.
 * A triangle whose vertices' fog factors differ by more than a sixteenth (fogged), or that wants different levels
 * (mip), is split in four in screen space, up to five deep; each piece is drawn by draw with its centroid's level. */
typedef void (*mg_tri_level_fn)(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod);
/* In mip: also cut where the core's colour or alpha combine (worked out at the vertices: combine/eval.c) at an edge's
 * midpoint differs from the average at its ends by more than 8 levels: products of iterated values (a lerp by
 * iterated alpha) iterated linearly in pieces small enough. */
#define MG_SPLIT_COMBINE 0x100
void mg_split(const GrVertex *a, const GrVertex *b, const GrVertex *c, int fogged, int mip, mg_tri_level_fn draw);

#endif
