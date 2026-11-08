/* gl_state.h - the GL state DOS-GL tracks (PRD §5.3, FR-ST-1..3). */
#ifndef DGL_GL_STATE_H
#define DGL_GL_STATE_H
#define GL_GLEXT_PROTOTYPES 1          /* DOS-GL defines the extension entry points */
#include <GL/gl.h>
#include <GL/glext.h>
#include "mga/prof.h"

/* DOS-GL's profiling stages (make PROF=1; DGL-PROF lines with DGL_STATS=2),
 * after the HAL's (mga/prof.h): time outside the library is app. */
enum {
    PROF_D_XFORM = PROF_CLIENT,     /* dgl_assemble: vertex fetch, transform, copies */
    PROF_D_VALID,                   /* the sink's begin: state into registers */
    PROF_D_CLIP,                    /* dgl_clip_polygon */
    PROF_D_PROJ,                    /* projection, culling */
    PROF_D_SETUP,                   /* draw_projected before setup_triangle; lines, points */
    PROF_D_TEX,                     /* glTexImage2D, glTexSubImage2D */
    PROF_D_CLEAR,                   /* glClear */
    PROF_D_DRAIN,                   /* SwapBuffers waiting for the engine */
    PROF_D_VSYNC,                   /* SwapBuffers waiting for retrace */
    PROF_D_SWAP                     /* the rest of SwapBuffers (flip, statistics) */
};

/* Column-major 4x4, as GL stores matrices. */
typedef struct { GLfloat m[16]; } dgl_mat4;

enum { DGL_MV_DEPTH = 32, DGL_PROJ_DEPTH = 2, DGL_TEX_DEPTH = 2 };

typedef struct {
    dgl_mat4 *stack;
    int depth, max;
} dgl_mstack;

/* Dirty groups validated at draw time (FR-ST-1). */
enum {
    DGL_DIRTY_MVP     = 1u << 0,
    DGL_DIRTY_RASTER  = 1u << 1,     /* depth, blend, alpha test, fog, masks */
    DGL_DIRTY_TARGET  = 1u << 2,     /* scissor */
    DGL_DIRTY_TEXTURE = 1u << 3,
    DGL_DIRTY_FOG     = 1u << 4
};

/* A buffer object (GL_ARB_vertex_buffer_object, buffer.c): system memory. */
typedef struct dgl_buffer {
    GLuint name;
    GLsizeiptrARB size;
    void *data;
    GLenum usage, access;
    int mapped;
} dgl_buffer;

/* Client-side array (glVertexPointer etc.). With a buffer object (buf), ptr
 * is an offset into it (dgl_buffer_address). */
typedef struct {
    GLint size;
    GLenum type;
    GLsizei stride;
    const void *ptr;
    int enabled;
    const dgl_buffer *buf;
} dgl_array;

/* A vertex after the modelview-projection transform: clip coordinates plus
 * attributes (colour 0..1, texture coordinates, eye distance for fog). */
typedef struct {
    float x, y, z, w;
    float r, g, b, a;
    float s, t;
    float s1, t1;                      /* texture unit 1 (GL_ARB_multitexture) */
    float eye_d;
} dgl_cvtx;

typedef struct {
    /* Matrices */
    GLenum      matrix_mode;
    dgl_mstack  mv, proj, tex, tex1;
    dgl_mat4    mv_stack[DGL_MV_DEPTH], proj_stack[DGL_PROJ_DEPTH], tex_stack[DGL_TEX_DEPTH],
                tex1_stack[DGL_TEX_DEPTH];
    dgl_mat4    mvp;                   /* proj * mv, valid unless DGL_DIRTY_MVP */
    int         tex_identity, tex1_identity;   /* texture matrices are the identity */
    /* Enables */
    int depth_test, cull_face, blend, alpha_test, fog, scissor_test, texture_2d, dither;
    int texture_2d1;                   /* GL_TEXTURE_2D on texture unit 1 */
    int active_unit, client_unit;      /* GL_ARB_multitexture: 0 or 1 */
    /* Raster state */
    GLenum  depth_func, blend_src, blend_dst, alpha_func, cull_mode, front_face, shade_model;
    GLenum  fog_mode, polygon_mode;
    int     depth_mask;
    GLboolean color_mask[4];
    GLfloat alpha_ref;
    GLint   viewport[4], scissor[4];
    GLfloat clear_color[4];
    GLdouble clear_depth, depth_near, depth_far;
    GLfloat fog_density, fog_start, fog_end, fog_color[4];
    GLenum  hint_perspective, hint_fog;
    GLint   pack_align, unpack_align, unpack_row_length;
    /* Rasterisation extras (GL 1.1): point size and line width (screen-space
     * quads), polygon offset, and stencil state accepted with no stencil
     * buffer (0 bits: the test always passes, PRD §2.2). */
    GLfloat point_size, line_width, offset_factor, offset_units;
    int     offset_fill, stencil_test, shared_palette;
    GLenum  stencil_func, stencil_fail, stencil_zfail, stencil_zpass;
    GLint   stencil_ref, clear_stencil;
    GLuint  stencil_mask, stencil_writemask;
    GLenum  draw_buffer, read_buffer;
    /* Current vertex attributes and arrays */
    GLfloat cur_color[4], cur_tex[2], cur_tex1[2];
    dgl_array va, ca, ta, ta1;         /* ta1: unit 1's texture coordinates */
    /* Errors */
    GLenum      error;
    unsigned    dirty;
} dgl_gl_state;

extern dgl_gl_state dgl_gl;

void dgl_gl_reset(void);                                  /* GL defaults for a w x h window */
void dgl_gl_set_window(int w, int h);
void dgl_gl_error(GLenum e);                              /* record the first error */
extern void (*dgl_gl_error_hook)(GLenum e, void *caller); /* context.c logs the first few */

/* The x87 control word inside drawing entry points. Games may run the FPU
 * at 24-bit precision (Quake 2 does, for its software renderer), where
 * libm's lrint returns 0 for everything and the setup maths loses bits:
 * drawing switches to 53-bit, round-to-nearest, exceptions masked (the
 * HAL's FPU_ENTER) and restores the caller's word. Host builds use SSE. */
#ifdef MGA_DJGPP
#  include "mga/fp.h"
#  define DGL_FPU_ENTER() FPU_ENTER()
#  define DGL_FPU_LEAVE() FPU_LEAVE()
#else
#  define DGL_FPU_ENTER() ((void)0)
#  define DGL_FPU_LEAVE() ((void)0)
#endif

/* Where primitives go (DGL_STATS=2 prints them every second as DGL-PRIMS). */
typedef struct {
    unsigned long begins, skipped;              /* primitives; skipped by validation */
    unsigned long tris_in, clipped, zero_area, culled;
    unsigned long seqs;                         /* fans and strips sent as one sequence of the WARPs' */
} dgl_prim_counts;
extern dgl_prim_counts dgl_prims;

/* buffer.c */
const dgl_buffer *dgl_array_buffer(void);                 /* bound to GL_ARRAY_BUFFER_ARB, or NULL */
const dgl_buffer *dgl_element_buffer(void);               /* bound to GL_ELEMENT_ARRAY_BUFFER_ARB */
const void *dgl_buffer_address(const dgl_buffer *b, const void *p);
int dgl_buffer_query(GLenum p, double *v);                /* glGet* of the bindings */
void dgl_buffers_reset(void);
/* vertex.c: the arrays let go of a buffer being deleted */
void dgl_arrays_forget_buffer(const dgl_buffer *b);

/* matrix.c */
void dgl_mat_identity(dgl_mat4 *r);
void dgl_mat_mul(dgl_mat4 *r, const dgl_mat4 *a, const dgl_mat4 *b);     /* r = a * b */
const dgl_mat4 *dgl_mvp(void);                            /* recomputed when dirty */
void dgl_matrix_reset(void);

/* clip.c: clip a convex polygon (in clip coordinates) against the near and
 * far planes and a guard band of +-g in x and y (NDC units); returns the
 * output vertex count (0 = rejected). out must hold n + 6 vertices. */
int dgl_clip_polygon(const dgl_cvtx *in, int n, dgl_cvtx *out, float gx, float gy);
/* Outcode of one vertex against the same planes (0 = inside all). */
unsigned dgl_outcode(const dgl_cvtx *v, float gx, float gy);

/* i / 255.0f for each byte (vertex.c): the quotients, not their products
 * with 1/255, without a divide. (i / 255 repeats i's eight bits forever, so
 * it is never halfway between two floats: the table's constants are the
 * divides' results at any x87 precision.) */
extern const float dgl_ub_f[256];
#define DGL_UB(c) (dgl_ub_f[(unsigned char)(c)])

#endif
