/* gl_draw.h - the vertex stream between assembly (vertex.c) and its sinks. */
#ifndef DGL_GL_DRAW_H
#define DGL_GL_DRAW_H
#include "gl_state.h"

/* An input vertex: object position (x, y, z, w), colour 0..1, texcoord. */
typedef struct { float pos[4], col[4], tex[2], tex1[2]; } dgl_vin;   /* tex1: texture unit 1 */

/* vertex.c's cache of transformed vertices: a triangle sink gets pointers
 * into dgl_vslot when it can (no two of the triangle's vertices share a
 * slot), so it may cache per-slot results, in dgl_vslot_sink, until a slot
 * is refilled: that sets the slot's byte to 0. Within one dgl_assemble a
 * slot holds one vertex until it is refilled. */
#define DGL_VSLOTS 64
extern dgl_cvtx dgl_vslot[DGL_VSLOTS];
extern unsigned char dgl_vslot_sink[DGL_VSLOTS];

typedef struct {
    int  (*begin)(void);        /* validate state; 0 = draw nothing */
    void (*triangle)(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *provoking);
    void (*line)(const dgl_cvtx *a, const dgl_cvtx *b);
    void (*point)(const dgl_cvtx *a);
    void (*end)(void);
    /* A whole draw from the arrays the sink can do its own fast way
     * (emit.c's fast path): 1 when it did. */
    int  (*fast)(GLenum mode, GLsizei count, const GLint *idx, GLint first);
    /* Display lists (lists.c): return 1 when the call was captured. */
    int  (*record)(GLenum mode, GLsizei count, const GLint *idx, GLint first);
    int  (*record_imm)(GLenum mode, GLsizei count, const dgl_vin *v);
} dgl_sinks;

extern dgl_sinks dgl_sink;

void dgl_transform(const dgl_vin *in, dgl_cvtx *out);
/* The arrays resolved for the current draw (vertex.c, once a draw): each
 * one's first element, stride, type and size, and whether it is on. */
typedef struct { const unsigned char *p; size_t stride; GLenum type; int size, on; } dgl_aplan;
extern dgl_aplan dgl_pl_v, dgl_pl_c, dgl_pl_t, dgl_pl_t1;
/* One triangle of the current array draw the general way (vertex.c's cache,
 * then the triangle sink): for the fast path's triangles that need clipping. */
void dgl_tri_general(GLint a, GLint b, GLint c, GLint provoking);
/* Assemble a primitive from the current arrays (imm = NULL; idx or first)
 * or from an immediate-mode buffer (imm). */
void dgl_assemble(GLenum mode, GLsizei count, const GLint *idx, GLint first, const dgl_vin *imm);

#endif
