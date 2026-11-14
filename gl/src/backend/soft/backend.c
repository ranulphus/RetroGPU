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

/* ---- Textures ------------------------------------------------------------ */

/* A texture's part here: the core's view of GL's own RGBA8 levels (no copy;
 * colour-index levels expanded through their palette). */
typedef struct {
    soft_texture st;
    uint8_t *expanded[SOFT_MAX_LEVELS];
    int expanded_bytes[SOFT_MAX_LEVELS];
    const dgl_palette *pal;
    unsigned pal_gen;
    int complete;
} stex;

static int is_mip(GLenum f) { return f >= GL_NEAREST_MIPMAP_NEAREST && f <= GL_LINEAR_MIPMAP_LINEAR; }

static int filter_of(GLenum f)
{
    switch (f) {
    case GL_NEAREST: return SOFT_NEAREST;
    case GL_LINEAR: return SOFT_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST: return SOFT_NEAREST_MIPMAP_NEAREST;
    case GL_LINEAR_MIPMAP_NEAREST: return SOFT_LINEAR_MIPMAP_NEAREST;
    case GL_NEAREST_MIPMAP_LINEAR: return SOFT_NEAREST_MIPMAP_LINEAR;
    default: return SOFT_LINEAR_MIPMAP_LINEAR;
    }
}

static int wrap_of(GLenum w) { return w == GL_CLAMP ? SOFT_CLAMP : w == GL_CLAMP_TO_EDGE ? SOFT_CLAMP_TO_EDGE : SOFT_REPEAT; }

static int log2_of(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

/* The texture as the core samples it: NULL when it is not complete (GL:
 * texturing is then as if disabled for its unit). */
static const soft_texture *prepare(dgl_texture *t)
{
    stex *x = (stex *)t->be;
    const dgl_palette *pal = t->level[0].idx ? dgl_palette_for(t) : NULL;
    int l, n = 0, maxl;
    if (!x) {
        if (!(x = (stex *)calloc(1, sizeof *x)))
            return NULL;
        t->be = x;
        t->dirty = 1;
    }
    if (t->dirty || (pal && (pal != x->pal || pal->gen != x->pal_gen))) {
        maxl = t->max_level < SOFT_MAX_LEVELS - 1 ? t->max_level : SOFT_MAX_LEVELS - 1;
        for (l = 0; l <= maxl; l++) {
            const dgl_level *L = &t->level[l];
            if (!DGL_LEVEL_DEFINED(L))
                break;
            if (l && (L->w != (t->level[l - 1].w > 1 ? t->level[l - 1].w / 2 : 1) ||
                      L->h != (t->level[l - 1].h > 1 ? t->level[l - 1].h / 2 : 1)))
                break;
            if (L->idx) {
                int bytes = L->w * L->h * 4, i;
                if (!pal)
                    break;
                if (x->expanded_bytes[l] < bytes) {
                    uint8_t *p = (uint8_t *)realloc(x->expanded[l], (size_t)bytes);
                    if (!p)
                        break;
                    x->expanded[l] = p, x->expanded_bytes[l] = bytes;
                }
                for (i = 0; i < L->w * L->h; i++)
                    memcpy(x->expanded[l] + i * 4, dgl_palette_texel(pal, L->idx[i]), 4);
                x->st.level[l].rgba = x->expanded[l];
            } else
                x->st.level[l].rgba = L->rgba;
            x->st.level[l].w = L->w;
            x->st.level[l].h = L->h;
            n++;
            if (L->w == 1 && L->h == 1)
                break;
        }
        x->st.levels = n;
        {
            int want = log2_of(t->level[0].w > t->level[0].h ? t->level[0].w : t->level[0].h);
            if (want > t->max_level)
                want = t->max_level;
            x->complete = n > 0 && (!is_mip(t->min_filter) || n - 1 >= want);
        }
        x->pal = pal;
        x->pal_gen = pal ? pal->gen : 0;
        t->dirty = 0;
    }
    x->st.wrap_s = wrap_of(t->wrap_s);
    x->st.wrap_t = wrap_of(t->wrap_t);
    x->st.min_filter = filter_of(t->min_filter);
    x->st.mag_filter = filter_of(t->mag_filter);
    if (!is_mip(t->min_filter) && x->st.levels > 1)
        x->st.levels = 1;                       /* (re-made from the levels when the filter goes back to a mipmap one) */
    return x->complete ? &x->st : NULL;
}

/* ---- Environments in combine terms --------------------------------------- */

static void comb(soft_combine *c, int f, int s0, int o0, int s1, int o1, int s2, int o2)
{
    c->func = f, c->src[0] = s0, c->op[0] = o0, c->src[1] = s1, c->op[1] = o1, c->src[2] = s2, c->op[2] = o2;
    c->scale = 1.0f;
}

static int src_of(GLenum s)
{
    return s == GL_TEXTURE ? SOFT_S_TEXTURE : s == GL_CONSTANT_ARB ? SOFT_S_CONSTANT
         : s == GL_PRIMARY_COLOR_ARB ? SOFT_S_PRIMARY : SOFT_S_PREVIOUS;
}

static int op_of(GLenum o)
{
    return o == GL_SRC_COLOR ? SOFT_O_COLOR : o == GL_ONE_MINUS_SRC_COLOR ? SOFT_O_ONE_MINUS_COLOR
         : o == GL_SRC_ALPHA ? SOFT_O_ALPHA : SOFT_O_ONE_MINUS_ALPHA;
}

static int func_of(GLenum f)
{
    switch (f) {
    case GL_REPLACE: return SOFT_C_REPLACE;
    case GL_MODULATE: return SOFT_C_MODULATE;
    case GL_ADD: return SOFT_C_ADD;
    case GL_ADD_SIGNED_ARB: return SOFT_C_ADD_SIGNED;
    case GL_INTERPOLATE_ARB: return SOFT_C_INTERPOLATE;
    default: return SOFT_C_SUBTRACT;
    }
}

/* GL 1.1's environments (with GL_ADD and GL_ARB_texture_env_combine) by the
 * texture's base format. GL keeps the levels reduced to their format as
 * RGBA8 (an ALPHA texture's colour white, a LUMINANCE one's alpha opaque),
 * so MODULATE and DECAL are RGBA's formulas for every format; REPLACE, BLEND
 * and ADD take the format's exceptions. */
static void env_stage(soft_stage *st, const dgl_texenv *e, int ifc)
{
    const int T = SOFT_S_TEXTURE, P = SOFT_S_PREVIOUS, K = SOFT_S_CONSTANT;
    const int C = SOFT_O_COLOR, A = SOFT_O_ALPHA;
    int i;
    for (i = 0; i < 4; i++)
        st->constant[i] = e->color[i];
    switch (e->mode) {
    case GL_COMBINE_ARB:
        comb(&st->rgb, func_of(e->combine_rgb), src_of(e->src_rgb[0]), op_of(e->op_rgb[0]), src_of(e->src_rgb[1]),
             op_of(e->op_rgb[1]), src_of(e->src_rgb[2]), op_of(e->op_rgb[2]));
        comb(&st->alpha, func_of(e->combine_alpha), src_of(e->src_alpha[0]), op_of(e->op_alpha[0]),
             src_of(e->src_alpha[1]), op_of(e->op_alpha[1]), src_of(e->src_alpha[2]), op_of(e->op_alpha[2]));
        st->rgb.scale = e->rgb_scale;
        st->alpha.scale = e->alpha_scale;
        return;
    case GL_REPLACE:
        comb(&st->rgb, SOFT_C_REPLACE, ifc == DGL_IF_ALPHA ? P : T, C, P, C, P, C);
        comb(&st->alpha, SOFT_C_REPLACE, ifc == DGL_IF_LUMINANCE || ifc == DGL_IF_RGB ? P : T, A, P, A, P, A);
        return;
    case GL_DECAL:
        if (ifc == DGL_IF_RGB || ifc == DGL_IF_RGBA || ifc == DGL_IF_RGB5_A1 || ifc == DGL_IF_INDEX)
            comb(&st->rgb, SOFT_C_INTERPOLATE, T, C, P, C, T, A);
        else
            comb(&st->rgb, SOFT_C_REPLACE, P, C, P, C, P, C);   /* (undefined in GL for other formats: unchanged) */
        comb(&st->alpha, SOFT_C_REPLACE, P, A, P, A, P, A);
        return;
    case GL_BLEND:
        if (ifc == DGL_IF_ALPHA)
            comb(&st->rgb, SOFT_C_REPLACE, P, C, P, C, P, C);
        else
            comb(&st->rgb, SOFT_C_INTERPOLATE, K, C, P, C, T, C);
        if (ifc == DGL_IF_INTENSITY)
            comb(&st->alpha, SOFT_C_INTERPOLATE, K, A, P, A, T, A);
        else
            comb(&st->alpha, SOFT_C_MODULATE, T, A, P, A, P, A);
        return;
    case GL_ADD:
        if (ifc == DGL_IF_ALPHA)
            comb(&st->rgb, SOFT_C_REPLACE, P, C, P, C, P, C);
        else
            comb(&st->rgb, SOFT_C_ADD, P, C, T, C, P, C);
        if (ifc == DGL_IF_INTENSITY)
            comb(&st->alpha, SOFT_C_ADD, P, A, T, A, P, A);
        else
            comb(&st->alpha, SOFT_C_MODULATE, T, A, P, A, P, A);
        return;
    default:                                    /* GL_MODULATE */
        comb(&st->rgb, SOFT_C_MODULATE, T, C, P, C, P, C);
        comb(&st->alpha, SOFT_C_MODULATE, T, A, P, A, P, A);
        return;
    }
}

static int bfactor(GLenum f)
{
    switch (f) {
    case GL_ZERO: return SOFT_B_ZERO;
    case GL_ONE: return SOFT_B_ONE;
    case GL_SRC_COLOR: return SOFT_B_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return SOFT_B_ONE_MINUS_SRC_COLOR;
    case GL_DST_COLOR: return SOFT_B_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return SOFT_B_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA: return SOFT_B_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return SOFT_B_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return SOFT_B_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return SOFT_B_ONE_MINUS_DST_ALPHA;
    default: return SOFT_B_SRC_ALPHA_SATURATE;
    }
}

static int sop(GLenum o)
{
    switch (o) {
    case GL_ZERO: return SOFT_SO_ZERO;
    case GL_REPLACE: return SOFT_SO_REPLACE;
    case GL_INCR: return SOFT_SO_INCR;
    case GL_DECR: return SOFT_SO_DECR;
    case GL_INVERT: return SOFT_SO_INVERT;
    default: return SOFT_SO_KEEP;
    }
}

static void stage_for(soft_stage *st, int unit, int on)
{
    dgl_texture *t = on ? dgl_unit_texture(unit) : NULL;
    const soft_texture *tx = t ? prepare(t) : NULL;
    memset(st, 0, sizeof *st);
    if (!tx)
        return;
    st->tex = tx;
    st->coord = unit;
    env_stage(st, dgl_tex_env(unit), t->level[0].ifc);
}

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
    st.fill_rule = SOFT_FILL_BOTTOM_LEFT;       /* (OpenGL's with y down, as the references draw) */
    st.dither = dgl_gl.dither;
    st.depth_test = dgl_gl.depth_test && tg.depth;
    st.depth_func = (int)(dgl_gl.depth_func - GL_NEVER);
    st.depth_write = dgl_gl.depth_mask;
    stage_for(&st.stage[0], 0, dgl_gl.texture_2d);
    stage_for(&st.stage[1], 1, dgl_gl.texture_2d1);
    st.fog = dgl_gl.fog;
    st.fog_mode = dgl_gl.fog_mode == GL_LINEAR ? SOFT_FOG_LINEAR : dgl_gl.fog_mode == GL_EXP ? SOFT_FOG_EXP : SOFT_FOG_EXP2;
    st.fog_per_vertex = 0;                      /* (per pixel: OSMesa's gallium drivers fog each fragment; t10) */
    st.fog_start = dgl_gl.fog_start, st.fog_end = dgl_gl.fog_end, st.fog_density = dgl_gl.fog_density;
    st.fog_color[0] = dgl_gl.fog_color[0], st.fog_color[1] = dgl_gl.fog_color[1], st.fog_color[2] = dgl_gl.fog_color[2];
    st.alpha_test = dgl_gl.alpha_test;
    st.alpha_func = (int)(dgl_gl.alpha_func - GL_NEVER);
    st.alpha_ref = dgl_gl.alpha_ref;
    st.stencil_test = dgl_gl.stencil_test && tg.stencil;
    st.stencil_func = (int)(dgl_gl.stencil_func - GL_NEVER);
    st.stencil_ref = (unsigned)dgl_gl.stencil_ref & 255u;
    st.stencil_mask = dgl_gl.stencil_mask & 255u;
    st.stencil_writemask = dgl_gl.stencil_writemask & 255u;
    st.stencil_fail = sop(dgl_gl.stencil_fail), st.stencil_zfail = sop(dgl_gl.stencil_zfail);
    st.stencil_zpass = sop(dgl_gl.stencil_zpass);
    st.blend = dgl_gl.blend;
    st.blend_src = bfactor(dgl_gl.blend_src), st.blend_dst = bfactor(dgl_gl.blend_dst);
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
    v->fog = c->eye_d;
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

/* A line: clipped as a degenerate triangle (its first two outputs are the
 * segment, in a's direction), flat shading with b's colour. */
static void sb_line(const dgl_cvtx *a, const dgl_cvtx *b)
{
    dgl_cvtx in[3], out[9];
    soft_vertex v[2];
    in[0] = *a; in[1] = *b; in[2] = *b;
    if (dgl_gl.shade_model == GL_FLAT) {
        in[0].r = b->r; in[0].g = b->g; in[0].b = b->b; in[0].a = b->a;
    }
    if (dgl_clip_polygon(in, 3, out, GUARD, GUARD) < 2)
        return;
    project(&out[0], &v[0]);
    project(&out[1], &v[1]);
    soft_line(&v[0], &v[1], dgl_gl.line_width);
}
/* A point: none when its centre is outside the view volume (OpenGL's rule). */
static void sb_point(const dgl_cvtx *a)
{
    soft_vertex v;
    if (dgl_outcode(a, 1.0f, 1.0f))
        return;
    project(a, &v);
    soft_point(&v, dgl_gl.point_size);
}

static void install(void)
{
    dgl_sink.begin = sb_begin;
    dgl_sink.triangle = sb_triangle;
    dgl_sink.fast = NULL;                       /* (the general path only) */
    dgl_sink.line = sb_line;
    dgl_sink.point = sb_point;
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

/* A rectangle of a level changed: GL's own levels are what the core reads,
 * so written already; a colour-index level's expansion is made again. */
static int tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    (void)level, (void)x, (void)y, (void)w, (void)h;
    return !t->level[0].idx;
}

static void tex_free(dgl_texture *t)
{
    stex *x = (stex *)t->be;
    int l;
    if (x)
        for (l = 0; l < SOFT_MAX_LEVELS; l++)
            free(x->expanded[l]);
    free(x);
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
