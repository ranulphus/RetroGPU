/* backend.c - GL's software drawing back end (backend.h; docs/b2.md): GL's
 * state turned into the software core's (hal/src/soft), clip-space vertices
 * clipped (the shared clipper, a guard band of 8: the core clips its spans
 * to the scissor), projected to window space, culled, offset; drawn into the
 * buffers in system memory a device gives (rg/softbuf.h: the memory device
 * now, B3's software device next). */
#include "../backend.h"
#include "../../dgl/dgl.h"
#include "../../gl/gl_draw.h"
#include "rg/softbuf.h"
#include "soft/soft.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GUARD 8.0f                      /* NDC units either side: the clipper's planes beyond the viewport */

static soft_target tg;
static soft_state st;

static int format_of(int bpp) { return bpp == 32 ? SOFT_XRGB8888 : bpp == 8 ? SOFT_RGB332 : SOFT_RGB565; }

/* The buffers drawing goes into now. */
static void target_now(void)
{
    memset(&tg, 0, sizeof tg);
    tg.color = rg_sb_color(dgl_ctx.draw_front);
    tg.color_pitch = rg_sb.pitch;
    tg.format = format_of(rg_sb.bpp);
    tg.depth = rg_sb.depth;
    tg.depth_pitch = rg_sb.depth_pitch;
    tg.depth_bits = rg_sb.depth_bits;
    tg.stencil = rg_sb.stencil;
    tg.stencil_pitch = rg_sb.stencil_pitch;
    tg.width = rg_sb.width;
    tg.height = rg_sb.height;
    soft_target_set(&tg);
}

/* GL's raster state as the core's. */
static void validate(void)
{
    int h = dgl_ctx.height;
    memset(&st, 0, sizeof st);
    st.scissor[0] = 0, st.scissor[1] = 0, st.scissor[2] = tg.width, st.scissor[3] = tg.height;
    if (dgl_gl.scissor_test) {
        int x0 = dgl_gl.scissor[0], x1 = dgl_gl.scissor[0] + dgl_gl.scissor[2];
        int y0 = h - (dgl_gl.scissor[1] + dgl_gl.scissor[3]), y1 = h - dgl_gl.scissor[1];   /* (GL's rows go up) */
        if (x0 > st.scissor[0]) st.scissor[0] = x0;
        if (y0 > st.scissor[1]) st.scissor[1] = y0;
        if (x1 < st.scissor[2]) st.scissor[2] = x1;
        if (y1 < st.scissor[3]) st.scissor[3] = y1;
    }
    st.color_mask = (dgl_gl.color_mask[0] ? 1u : 0) | (dgl_gl.color_mask[1] ? 2u : 0) | (dgl_gl.color_mask[2] ? 4u : 0) |
                    (dgl_gl.color_mask[3] ? 8u : 0);
    st.smooth = dgl_gl.shade_model != GL_FLAT;
    st.depth_test = dgl_gl.depth_test && tg.depth;
    st.depth_func = (int)(dgl_gl.depth_func - GL_NEVER);
    st.depth_write = dgl_gl.depth_mask;
    soft_state_set(&st);
}

static int sb_begin(void)
{
    if (!dgl_ctx.active)
        return 0;
    target_now();
    validate();
    return 1;
}

static void sb_end(void) { }

static void project(const dgl_cvtx *c, soft_vertex *v)
{
    double iw = 1.0 / c->w;
    v->x = (float)(dgl_gl.viewport[0] + (c->x * iw + 1.0) * 0.5 * dgl_gl.viewport[2]);
    v->y = (float)(dgl_ctx.height - (dgl_gl.viewport[1] + (c->y * iw + 1.0) * 0.5 * dgl_gl.viewport[3]));
    v->z = (float)(dgl_gl.depth_near + (c->z * iw + 1.0) * 0.5 * (dgl_gl.depth_far - dgl_gl.depth_near));
    v->oow = (float)iw;
    v->r = c->r, v->g = c->g, v->b = c->b, v->a = c->a;
    v->tex[0][0] = c->s, v->tex[0][1] = c->t, v->tex[0][2] = 1.0f;
    v->tex[1][0] = c->s1, v->tex[1][1] = c->t1, v->tex[1][2] = 1.0f;
    v->fog = 1.0f;
}

/* Polygon offset (GL 1.1): the factor times the triangle's largest depth
 * slope, plus units of the depth buffer's resolution. */
static double offset_for(const soft_vertex *p0, const soft_vertex *p1, const soft_vertex *p2)
{
    double x1, y1, z1, x2, y2, z2, det, m = 0;
    if (!dgl_gl.offset_fill || (dgl_gl.offset_factor == 0.0f && dgl_gl.offset_units == 0.0f))
        return 0;
    x1 = p1->x - p0->x, y1 = p1->y - p0->y, z1 = p1->z - p0->z;
    x2 = p2->x - p0->x, y2 = p2->y - p0->y, z2 = p2->z - p0->z;
    det = x1 * y2 - x2 * y1;
    if (det != 0) {
        double dzdx = fabs((z1 * y2 - z2 * y1) / det), dzdy = fabs((x1 * z2 - x2 * z1) / det);
        m = dzdx > dzdy ? dzdx : dzdy;
    }
    return dgl_gl.offset_factor * m + dgl_gl.offset_units / (rg_sb.depth_bits == 32 ? 4294967295.0 : 65535.0);
}

static void sb_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    dgl_cvtx in[3], out[9];
    soft_vertex v[9];
    double area2;
    int n, i;
    dgl_prims.tris_in++;
    in[0] = *a; in[1] = *b; in[2] = *c;
    if (dgl_gl.shade_model == GL_FLAT)
        for (i = 0; i < 3; i++) {
            in[i].r = prov->r; in[i].g = prov->g; in[i].b = prov->b; in[i].a = prov->a;
        }
    n = dgl_clip_polygon(in, 3, out, GUARD, GUARD);
    if (n < 3) {
        dgl_prims.clipped++;
        return;
    }
    for (i = 0; i < n; i++)
        project(&out[i], &v[i]);
    /* Cull on the first three window vertices' winding; y runs down on the
     * screen, so GL's counter-clockwise is area < 0. */
    area2 = (double)(v[1].x - v[0].x) * (v[2].y - v[0].y) - (double)(v[2].x - v[0].x) * (v[1].y - v[0].y);
    if (area2 == 0) {
        dgl_prims.zero_area++;
        return;
    }
    if (dgl_gl.cull_face) {
        int front = (dgl_gl.front_face == GL_CCW) ? area2 < 0 : area2 > 0;
        if (dgl_gl.cull_mode == GL_FRONT_AND_BACK || (dgl_gl.cull_mode == GL_BACK) != front) {
            dgl_prims.culled++;
            return;
        }
    }
    {
        double off = offset_for(&v[0], &v[1], &v[2]);
        if (off != 0)
            for (i = 0; i < n; i++) {
                double z = v[i].z + off;
                v[i].z = (float)(z < 0 ? 0 : z > 1 ? 1 : z);
            }
    }
    for (i = 1; i + 1 < n; i++)
        soft_triangle(&v[0], &v[i], &v[i + 1]);
}

static void install(void)
{
    dgl_sink.begin = sb_begin;
    dgl_sink.triangle = sb_triangle;
    dgl_sink.fast = NULL;                       /* (the general path only) */
    dgl_sink.line = NULL;                       /* (B2.5) */
    dgl_sink.point = NULL;
    dgl_sink.end = sb_end;
}

static void clear(GLbitfield mask)
{
    unsigned what = 0;
    target_now();
    validate();
    if (mask & GL_COLOR_BUFFER_BIT)
        what |= SOFT_CLEAR_COLOR;
    if ((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask)
        what |= SOFT_CLEAR_DEPTH;
    if (mask & GL_STENCIL_BUFFER_BIT)
        what |= SOFT_CLEAR_STENCIL;
    soft_clear(what, dgl_gl.clear_color, dgl_gl.clear_depth, (unsigned)dgl_gl.clear_stencil & dgl_gl.stencil_writemask);
    dgl_gl.dirty |= DGL_DIRTY_RASTER;
}

static void caps(dgl_caps *c)
{
    memset(c, 0, sizeof *c);
    c->renderer = "Software";
    c->texture_units = 2;
    c->max_texture_size = 1024;
}

static void synced(void) { }

/* Textures (B2.3): no copy of the software back end's own yet. */
static int tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    (void)t, (void)level, (void)x, (void)y, (void)w, (void)h;
    return 0;
}

static void tex_free(dgl_texture *t)
{
    free(t->be);
    t->be = NULL;
}

static int tex_info(const dgl_texture *t, int rgba[4])
{
    (void)t;
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 8;
    return 1;
}

static void tex_reset(uint32_t start, uint32_t end) { (void)start, (void)end; }

static void stats(dgl_be_stats *s)
{
    memset(s, 0, sizeof *s);
    s->tris = soft_count.tris;
}

const dgl_backend dgl_soft_backend = {
    .name = "soft",
    .dev = &rg_mem_device,
    .caps = caps,
    .install = install,
    .clear = clear,
    .synced = synced,
    .tex_sub = tex_sub,
    .tex_free = tex_free,
    .tex_info = tex_info,
    .tex_reset = tex_reset,
    .stats = stats,
};
