/* backend.h - GL's drawing back end (docs/backend-interface.md, B0 v2;
 * docs/b1.md): one table of functions for GL's drawing, on a device
 * (rg/device.h). The Matrox one is src/backend/mga; the software one comes
 * in B2.
 *
 * Drawing is today's sink (gl_draw.h's dgl_sink, in clip space): install
 * puts the back end's begin, triangle, line, point, end and fast there, so
 * vertex.c calls them through the same pointers as before. */
#ifndef DGL_BACKEND_H
#define DGL_BACKEND_H
#include "rg/device.h"
#include "../gl/gl_state.h"
#include "../gl/gl_tex.h"

/* What glGet and the extensions report. */
typedef struct {
    const char *renderer;           /* GL_RENDERER */
    int texture_units;              /* 1, or 2 */
    int max_texture_size, max_mip_levels;
    int paletted;                   /* GL_EXT_paletted_texture */
    int red_bits, green_bits, blue_bits, depth_bits, stencil_bits;
} dgl_caps;

typedef struct dgl_backend {
    const char *name;
    const rg_device *dev;           /* the device it draws on */
    void (*caps)(dgl_caps *c);

    /* Drawing */
    void (*install)(void);          /* its drawing into dgl_sink (after each open) */
    void (*clear)(GLbitfield mask); /* colour, depth: dgl_gl's values, scissor and masks */

    /* Textures */
    int  (*tex_ready)(dgl_texture *t);                              /* made drawable from its levels: 0 drawable */
    void (*tex_sub)(dgl_texture *t, int level, int x, int y, int w, int h);  /* a rectangle changed */
    void (*tex_free)(dgl_texture *t);                               /* its back-end part gone */
    void (*tex_reset)(void);                                        /* every texture's (a new context) */
} dgl_backend;

extern const dgl_backend dgl_mga_backend;
extern const dgl_backend *dgl_be;   /* the one in use (context.c), NULL before dglInit */

#endif
