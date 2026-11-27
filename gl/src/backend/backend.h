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

/* What glGet and the extensions report. The caller zeroes the struct before caps (dgl_get_caps does), so a back
 * end leaves alone what its card can't do. */
typedef struct {
    const char *renderer;           /* GL_RENDERER's chip ("DOS-GL on ...") */
    int texture_units;              /* 1, or 2 */
    int max_texture_size;
    int sub_narrow;                 /* glTexSubImage2D tells tex_sub only the texels that changed (0: the rectangle
                                     * given) */
    float max_anisotropy;           /* GL_EXT_texture_filter_anisotropic's limit; 0 or 1: none (not advertised) */
    float max_lod_bias;             /* GL_EXT_texture_lod_bias: biases within +-this; 0: none (not advertised) */
    int dot3;                       /* GL_ARB_texture_env_dot3's combines (with two units or more) */
    int cube;                       /* GL_ARB_texture_cube_map (the back end draws dgl_unit_cube's) */
    int hw_tnl;                     /* draws go through the card's transform engine (GL_DGL_hw_tnl; DGL_TNL=0: no) */
} dgl_caps;
void dgl_get_caps(dgl_caps *c);     /* ext.c: the back end's caps, zeroed first and made consistent */

/* The back end's counters (DGL_STATS, DGL_FRAMES). */
typedef struct {
    uint32_t tris;                  /* triangles set up by the CPU */
    uint32_t tris_offloaded;        /* ... and by the card's own setup (the WARP) */
    uint32_t tris_tnl;              /* of those, transformed by the card too (the Radeons' engine, X3) */
    uint32_t mem_used;              /* texture memory in use (bytes) */
    /* (X3b, DGL-TNLSTAT: the engine's draws so far; those fogged; those given back to the CPU's path for fog or for
     * being small; a buffer object's vertices found in video memory, and converted there) */
    uint32_t tnl_draws, tnl_fogged, tnl_fog_cpu, tnl_small_cpu, tnl_vbo_hits, tnl_vbo_made;
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

extern const dgl_backend dgl_mga_backend, dgl_soft_backend, dgl_tnt2_backend, dgl_r128_backend, dgl_radeon_backend,
                         dgl_nv10_backend;
int dgl_nv10_wanted(void);           /* the TNT2 device found an NV1x: Celsius's back end (DGL_NV_CLASS=95: not) */
extern const dgl_backend *dgl_be;   /* the one in use (the Matrox one until a context opens on another device) */

#endif
