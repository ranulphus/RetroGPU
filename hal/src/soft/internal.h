/* internal.h - between the software core's own files (hal/src/soft): the
 * integer sampling the fast spans use (gl/docs/b2.md B2.8). Not for callers. */
#ifndef SOFT_INTERNAL_H
#define SOFT_INTERNAL_H
#include "soft/soft.h"

/* A texture's level(s) and filter for a level of detail, picked by the same
 * rules as soft_sample: n levels (1 or 2), the second's weight f8 (of 256),
 * LINEAR within a level or not; sh_s, sh_t: each level's size as a shift of
 * level 0's (coordinates in level-0 texels shift down to the level's). */
typedef struct {
    int n, lv[2], f8, linear, sh_s[2], sh_t[2];
} soft_pick;

void soft_pick_lod(const soft_texture *tx, double dsdx, double dsdy, double dtdx, double dtdy, soft_pick *p);

/* The texel at (U, V), level-0 texels in 16.16 (texel centres at halves),
 * as p picked; RGBA 0..255. GL_REPEAT (power-of-two sizes) and
 * GL_CLAMP_TO_EDGE only: GL_CLAMP's border is the reference span's. */
void soft_fetch8(const soft_texture *tx, const soft_pick *p, int32_t U, int32_t V, int out[4]);

/* a and b (0..255) mixed by f (0..256 for b), rounded: the filters' step. */
static inline int soft_lerp8(int a, int b, int f) { return (a * (256 - f) + b * f + 128) >> 8; }

#endif
