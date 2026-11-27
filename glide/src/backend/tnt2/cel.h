/* cel.h - the TNT2 back end's Celsius half (celsius.c): Glide's triangles on the NV1x's own 3D class (0x96), whose
 * D3D5/D3D6 classes draw nothing (docs/nv10.md). The TNT2 back end (tnt2.c) keeps the window, the clears, the swaps,
 * the LFB, the level per triangle and the fog splitting; on an NV1x it hands each triangle here. */
#ifndef GLIDE_BACKEND_TNT2_CEL_H
#define GLIDE_BACKEND_TNT2_CEL_H

#include "glide/mg.h"
#include "tnt2/tnt2.h"

/* From tnt2.c. */
int  tnt2be_uses_texture(void);
void tnt2be_st_of(const GrVertex *v, double *s, double *t);

/* Celsius set up after the window opened; the vertex area taken from the end of the heap: the heap's new end. */
uint32_t cel_open(uint32_t heap_off, uint32_t heap_end);
void cel_close(void);
void cel_target(void);                  /* the render target and Glide's clip window (MG_DIRTY_TARGET, _CLIP) */
void cel_flush(void);                   /* the triangles batched so far to the chip */
void cel_tex_written(void);             /* a texture written in place: its words sent again before the next draw */
/* One triangle: Glide's vertices, the TNT2 back end's screen-space ones (snapped, depth, colour, fog), its level. */
void cel_draw(const GrVertex *a, const GrVertex *b, const GrVertex *c, const tnt2_vertex v[3], GrLOD_t lod,
              int wmode, int depth, int fog);
unsigned cel_draws(void);               /* (MGL-WINCLOSE) */

#endif
