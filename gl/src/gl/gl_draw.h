/* gl_draw.h - the vertex stream between assembly (vertex.c) and its sinks. */
#ifndef DGL_GL_DRAW_H
#define DGL_GL_DRAW_H
#include "gl_state.h"

/* An input vertex: object position (x, y, z, w), colour 0..1, texcoord. */
typedef struct { float pos[4], col[4], tex[2], tex1[2], nrm[3], texr[2]; } dgl_vin;   /* tex1: unit 1; texr: r */
void dgl_light_vertex(const dgl_vin *in, dgl_cvtx *o);      /* light.c: o's colour (and back colour) lit */
void dgl_texgen_vertex(int unit, const dgl_vin *in, const float pe[4], const float ne[3], float tc[4]);   /* texgen.c */

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
    /* A convex polygon of n (3..DGL_POLY_MAX) vertices that needs clipping,
     * whole: clipped once, then its fan (NULL: triangle by triangle). */
    void (*polygon)(const dgl_cvtx *v, int n, const dgl_cvtx *provoking);
    void (*line)(const dgl_cvtx *a, const dgl_cvtx *b);
    void (*point)(const dgl_cvtx *a);
    void (*end)(void);
    /* A whole draw from the arrays the sink can do its own fast way
     * (emit.c's fast path): 1 when it did. */
    int  (*fast)(GLenum mode, GLsizei count, const GLint *idx, GLint first);
    /* A whole draw through the card's own transform engine (the Radeon's, X3): 1 when it did; tried before fast
     * and before the general path, whatever the state (the back end says no to what its engine can't do). The
     * draw's vertices: dgl_vertex_of. */
    int  (*tnl)(GLenum mode, GLsizei count, const GLint *idx, GLint first);
    /* Display lists (lists.c): return 1 when the call was captured. */
    int  (*record)(GLenum mode, GLsizei count, const GLint *idx, GLint first);
    int  (*record_imm)(GLenum mode, GLsizei count, const dgl_vin *v);
} dgl_sinks;

extern dgl_sinks dgl_sink;

void dgl_transform(const dgl_vin *in, dgl_cvtx *out);
void dgl_vertex_of(GLint i, dgl_vin *v);   /* the draw being assembled's vertex i (immediate or from the arrays) */
int dgl_draw_from_arrays(void);             /* (X3b) the draw being assembled comes from the arrays, not glBegin's */
unsigned dgl_clip_user_planes(float eq[6][4]);  /* (X3b) the enabled clip planes in clip coordinates: their bits */
/* The arrays resolved for the current draw (vertex.c, once a draw): each
 * one's first element, stride, type and size, and whether it is on. */
typedef struct { const unsigned char *p; size_t stride; GLenum type; int size, on, uniform; } dgl_aplan;
/* (uniform: immediate mode's colour plan when every vertex of the draw has
 * the colour of the first: the fast path converts it once, E2a.) */
extern dgl_aplan dgl_pl_v, dgl_pl_c, dgl_pl_t, dgl_pl_t1;
/* One triangle of the current array draw the general way (vertex.c's cache,
 * then the triangle sink): for the fast path's triangles that need clipping. */
void dgl_tri_general(GLint a, GLint b, GLint c, GLint provoking);
/* A polygon of the current array draw the general way, whole: its vertices
 * transformed once, then the polygon sink (g4path.md, E1). 0 when there is
 * no polygon sink or n is out of its range: the caller sends triangles. */
#define DGL_POLY_MAX 9
/* DGL_ABLATE (diagnosis, g4path.md; frames wrong on purpose): 1 nothing
 * sent to the card, 2 the fast path returns at once, 4 glEnd returns at
 * once, 8 glVertex and glEnd do nothing. */
extern int dgl_ablate;
int dgl_poly_general(const GLint *ids, int n, GLint provoking);
/* Assemble a primitive from the current arrays (imm = NULL; idx or first)
 * or from an immediate-mode buffer (imm). */
void dgl_assemble(GLenum mode, GLsizei count, const GLint *idx, GLint first, const dgl_vin *imm);

#endif
