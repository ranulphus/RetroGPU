/* backend.h - GL's drawing back end (docs/backend-interface.md, B0 v2;
 * docs/b1.md): one table of functions for GL's drawing, on a device
 * (rg/device.h). The Matrox one is src/backend/mga; the software one comes
 * in B2.
 *
 * Drawing is today's sink (gl_draw.h's dgl_sink, in clip space): install
 * puts the back end's begin, triangle, line, point, end and fast there, so
 * vertex.c calls them through the same pointers as before. A texture is
 * made drawable inside the back end's begin, so tex_ready is the back end's
 * own business; GL tells it when levels change (t->dirty, tex_sub) and when
 * a texture goes (tex_free). */
#ifndef DGL_BACKEND_H
#define DGL_BACKEND_H
#include "rg/device.h"
#include "../gl/gl_state.h"
#include "../gl/gl_tex.h"

/* What glGet and the extensions report. */
typedef struct {
    const char *renderer;           /* GL_RENDERER's chip ("DOS-GL on ...") */
    int texture_units;              /* 1, or 2 */
    int max_texture_size;
} dgl_caps;

/* The back end's counters (DGL_STATS, DGL_FRAMES). */
typedef struct {
    uint32_t tris;                  /* triangles set up by the CPU */
    uint32_t tris_offloaded;        /* ... and by the card's own setup (the WARP) */
    uint32_t mem_used;              /* texture memory in use (bytes) */
} dgl_be_stats;

typedef struct dgl_backend {
    const char *name;
    const rg_device *dev;           /* the device it draws on */
    void (*caps)(dgl_caps *c);

    /* Drawing */
    void (*install)(void);          /* its drawing into dgl_sink (after each open) */
    void (*clear)(GLbitfield mask); /* colour, depth: dgl_gl's values, scissor and masks */
    void (*synced)(void);           /* the device's sync completed: queued drawing has read everything */

    /* Textures */
    int  (*tex_sub)(dgl_texture *t, int level, int x, int y, int w, int h);   /* a rectangle of its levels
                                       changed: 1 written into its copy, 0 the copy is stale (t->dirty) */
    void (*tex_free)(dgl_texture *t);                               /* its back-end part gone */
    int  (*tex_info)(const dgl_texture *t, int rgba_bits[4]);       /* its copy: resident (1), bits a component */
    void (*tex_reset)(uint32_t mem_start, uint32_t mem_end);        /* every texture freed: a new context's memory */
    void (*tex_change)(dgl_texture *t);     /* GL's copy of its levels is about to change (glTexImage2D,
                                       glTexSubImage2D): drawing kept that samples it is done first (the
                                       software back end's tiles); NULL: nothing reads GL's copy later */

    /* Diagnostics */
    void (*stats)(dgl_be_stats *s);
    void (*prof_micro)(void);       /* DGL_STATS=3 in PROF=1 builds: single operations' costs (or NULL) */
} dgl_backend;

extern const dgl_backend dgl_mga_backend, dgl_soft_backend;
extern const dgl_backend *dgl_be;   /* the one in use (the Matrox one until a context opens on another device) */

#endif
