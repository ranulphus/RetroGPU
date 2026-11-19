/* backend.c - DOS-GL's drawing back end on the Riva TNT2 (backend.h; RetroGPU's docs/tnt2.md, T4): GL's drawing
 * on the TNT2 device (hal/src/rg/tnt2.c) through the TNT2 layer's textured-triangle object (class 0x54).
 *
 * The chip sets triangles up itself from window-space vertices (Direct3D's TLVERTEX), so the work here is GL's:
 * clip-space vertices clipped (the shared clipper, a guard band), projected to window space, culled and offset as
 * the software back end does, then handed over with their colours, 1/w, texture coordinates and a fog factor; GL's
 * state turned into the object's words (Direct3D 5's codes: the texture environment, blending, the alpha and depth
 * tests, fog); textures converted to the chip's formats and stored swizzled in the device's heap (the Matrox back
 * end's allocator, vram.c); clears by the 2D engine.
 *
 * What the 0x54 object cannot do is approximated and said once (DGL-WARN): GL_BLEND's texture environment, colour
 * masks, stencil (the 0x55 object has them: later), a second texture unit, colour-index textures (drawn untextured),
 * per-pixel fog (the chip fogs by a factor per vertex, as Mesa's driver does). Lines and points are drawn as
 * triangles (the chip has neither). */
#include "../backend.h"
#include "../../dgl/dgl.h"
#include "../../gl/gl_draw.h"
#include "../mga/vram.h"
#include "rg/tnt2_dev.h"
#include "tnt2/tnt2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GUARD 2.0f                      /* NDC units either side: the clipper's planes beyond the viewport */

/* The chip's words (docs/tnt2.md). */
#define F_DMA_A      0x00000001u
#define F_ORIGIN     0x000000A0u         /* zero- and first-order-hold origins at the texel's corner (Mesa's) */
#define F_A1R5G5B5   0x00000200u
#define F_A4R4G4B4   0x00000400u
#define F_R5G6B5     0x00000500u
#define F_A8R8G8B8   0x00000600u
#define B_MSB        0x00000020u
#define B_GOURAUD    0x00000080u
#define B_PERSP      0x00000100u
#define B_FOG        0x00010000u
#define B_BLEND      0x00100000u
#define C_ALPHA_ON   0x00001000u
#define C_CORNER     0x00002000u
#define C_Z_ON       0x00004000u
#define C_CULL_NONE  0x00100000u
#define C_DITHER     0x00400000u
#define C_Z_WRITE    0x01000000u
#define C_Z_FIXED    0x40000000u
enum { TB_DECAL = 1, TB_MODULATE = 2, TB_DECALALPHA = 3, TB_MODULATEALPHA = 4, TB_ADD = 8 };

/* A texture's copy in VRAM. */
typedef struct {
    uint32_t block, off;                /* the heap's block, and the base level in it (128-byte aligned) */
    uint32_t format;                    /* FORMAT without the filter-dependent level count */
    int      levels;                    /* the levels stored (a full chain when the source has one) */
    int      has_alpha;                 /* the format keeps the texel's alpha (DECAL then takes it) */
    unsigned stamp;
    unsigned palgen;                    /* colour-index levels: the palette's generation expanded through */
    const dgl_palette *pal;
} ttex;

static unsigned stamp;
static uint32_t tris, lines_, points_;
static int warned[8];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        DGL_WARN("DGL-WARN tnt2: %s", what);
    }
}

/* ---- Textures ------------------------------------------------------------- */

static int log2_of(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

/* The chip's format for GL's internal format class (and its ifmt's size, where GL asked for few bits). */
static uint32_t chip_format(const dgl_level *l, int *cpp, int *has_alpha)
{
    *cpp = 2, *has_alpha = 1;
    switch (l->ifc) {
    case DGL_IF_RGB5_A1:
        return F_A1R5G5B5;
    case DGL_IF_RGB:
    case DGL_IF_LUMINANCE:
        *has_alpha = 0;
        if (l->ifmt == GL_RGB8 || l->ifmt == GL_RGB10 || l->ifmt == GL_RGB12 || l->ifmt == GL_RGB16) {
            *cpp = 4;
            return F_A8R8G8B8;
        }
        return F_R5G6B5;
    case DGL_IF_RGBA:
        if (l->ifmt == GL_RGBA4 || l->ifmt == GL_RGBA2) return F_A4R4G4B4;
        *cpp = 4;
        return F_A8R8G8B8;
    default:                            /* (luminance-alpha, alpha, intensity: their RGBA8 copies as they are) */
        *cpp = 4;
        return F_A8R8G8B8;
    }
}

static void pack(uint32_t fmt, const unsigned char *rgba, unsigned char *out, int n)
{
    int i;
    for (i = 0; i < n; i++, rgba += 4) {
        unsigned r = rgba[0], g = rgba[1], b = rgba[2], a = rgba[3];
        if (fmt == F_A8R8G8B8) {
            uint32_t v = a << 24 | r << 16 | g << 8 | b;
            memcpy(out + 4 * i, &v, 4);
        } else {
            uint16_t v = fmt == F_R5G6B5 ? (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3))
                       : fmt == F_A1R5G5B5 ? (uint16_t)((a >= 128) << 15 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3))
                       : (uint16_t)((a >> 4) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4));
            memcpy(out + 2 * i, &v, 2);
        }
    }
}

static void tex_drop(dgl_texture *t)
{
    ttex *x = (ttex *)t->be;
    if (x) {
        tnt2_sync(1000000);             /* (drawing queued may still read it) */
        dgl_vram_free(x->block);
        free(x);
        t->be = NULL;
    }
}

/* Room for size bytes: the least recently drawn textures given up until it fits. */
static int room(uint32_t size, uint32_t *block, const dgl_texture *keep)
{
    while (dgl_vram_alloc(size, block) != 0) {
        dgl_texture *old = NULL, *u;
        int i, n = dgl_texture_count();
        for (i = 0; i < n; i++)
            if ((u = dgl_texture_nth(i)) && u->be && u != keep &&
                (!old || ((ttex *)u->be)->stamp < ((ttex *)old->be)->stamp))
                old = u;
        if (!old)
            return -1;
        tex_drop(old);
        dgl_texc.evictions++;
    }
    return 0;
}

/* The texture's copy, made or made again when its levels changed. NULL: draw untextured. */
static ttex *prepare(dgl_texture *t)
{
    ttex *x = (ttex *)t->be;
    const dgl_level *l0 = &t->level[0];
    unsigned char *buf;
    const void *lv[DGL_MAX_LEVELS];
    unsigned char *lvbuf[DGL_MAX_LEVELS];
    uint32_t fmt, size = 0;
    int cpp, alpha, n, i, lw, lh;
    const dgl_palette *pal = l0->idx ? dgl_palette_for(t) : NULL;
    unsigned char *expanded[DGL_MAX_LEVELS];
    if (x && !t->dirty && (!pal || (x->pal == pal && x->palgen == pal->gen))) {
        x->stamp = ++stamp;
        return x;
    }
    if (!l0->rgba && !l0->idx)
        return NULL;
    lw = log2_of(l0->w), lh = log2_of(l0->h);
    if ((1 << lw) != l0->w || (1 << lh) != l0->h || lw > 11 || lh > 11)
        return NULL;
    if (pal) {                          /* (colour indices: through the palette, as RGBA, A8R8G8B8) */
        cpp = 4, alpha = 1, fmt = F_A8R8G8B8;
    } else
        fmt = chip_format(l0, &cpp, &alpha);
    /* The levels after the base, as long as they halve (the chip reads a chain from its size and count). */
    for (n = 1; n < DGL_MAX_LEVELS && n <= t->max_level; n++) {
        const dgl_level *l = &t->level[n];
        int w = l0->w >> n ? l0->w >> n : 1, h = l0->h >> n ? l0->h >> n : 1;
        if (!(pal ? l->idx != NULL : l->rgba != NULL) || l->w != w || l->h != h)
            break;
        if (w == 1 && h == 1) {
            n++;
            break;
        }
    }
    for (i = 0; i < n; i++)
        size += (uint32_t)(t->level[i].w * t->level[i].h * cpp);
    tex_drop(t);
    x = (ttex *)calloc(1, sizeof *x);
    if (!x || room(size + 128u, &x->block, t) != 0) {
        free(x);
        warn_once(1, "out of texture memory: a texture drawn untextured");
        return NULL;
    }
    x->off = (x->block + 127u) & ~127u;
    buf = (unsigned char *)malloc(size);
    if (!buf) {
        dgl_vram_free(x->block);
        free(x);
        return NULL;
    }
    {
        uint32_t o = 0;
        for (i = 0; i < n; i++) {
            const dgl_level *l = &t->level[i];
            const unsigned char *src = l->rgba;
            expanded[i] = NULL;
            if (pal) {
                int k;
                if (!(expanded[i] = (unsigned char *)malloc((size_t)l->w * l->h * 4)))
                    break;
                for (k = 0; k < l->w * l->h; k++)
                    memcpy(expanded[i] + 4 * k, dgl_palette_texel(pal, l->idx[k]), 4);
                src = expanded[i];
            }
            lvbuf[i] = buf + o;
            pack(fmt, src, lvbuf[i], l->w * l->h);
            free(expanded[i]);
            lv[i] = lvbuf[i];
            o += (uint32_t)(l->w * l->h * cpp);
        }
    }
    tnt2_sync(1000000);                 /* (the block may have been a texture still drawn from) */
    tnt2_tex_load(x->off, cpp, lw, lh, n, lv);
    free(buf);
    x->format = F_DMA_A | F_ORIGIN | fmt | (uint32_t)lh << 20 | (uint32_t)lw << 16;
    x->levels = n;
    x->has_alpha = alpha;
    x->stamp = ++stamp;
    x->pal = pal;
    x->palgen = pal ? pal->gen : 0;
    t->be = x;
    t->dirty = 0;
    dgl_texc.uploads++;
    dgl_texc.upload_bytes += size;
    return x;
}

/* ---- State ------------------------------------------------------------------ */

static uint32_t wrap_of(GLenum w) { return w == GL_REPEAT ? 1u : 3u; }    /* (GL_CLAMP as CLAMP_TO_EDGE) */
static uint32_t cmp_of(GLenum f) { return (uint32_t)(f - GL_NEVER) + 1u; }

static uint32_t bfactor(GLenum f)
{
    switch (f) {
    case GL_ZERO: return 1;
    case GL_ONE: return 2;
    case GL_SRC_COLOR: return 3;
    case GL_ONE_MINUS_SRC_COLOR: return 4;
    case GL_SRC_ALPHA: return 5;
    case GL_ONE_MINUS_SRC_ALPHA: return 6;
    case GL_DST_ALPHA: return 7;
    case GL_ONE_MINUS_DST_ALPHA: return 8;
    case GL_DST_COLOR: return 9;
    case GL_ONE_MINUS_DST_COLOR: return 10;
    case GL_SRC_ALPHA_SATURATE: return 11;
    default: return 2;
    }
}

static int fogging;                     /* the fog factor goes in the vertices' specular alpha */
static int shift_s, shift_t;            /* a repeating texture: coordinates shifted by whole periods per triangle */

static void cut(int *c, int x0, int y0, int x1, int y1)
{
    if (x0 > c[0]) c[0] = x0;
    if (y0 > c[1]) c[1] = y0;
    if (x1 < c[2]) c[2] = x1;
    if (y1 < c[3]) c[3] = y1;
}

/* The scissor cut (draw: also to the viewport, as the clipper keeps a guard band), in window rows (down). */
static void scissor(int draw, int *c)
{
    int h = dgl_ctx.height;
    c[0] = 0, c[1] = 0, c[2] = rg_tnt2.width, c[3] = rg_tnt2.height;
    if (dgl_gl.scissor_test)
        cut(c, dgl_gl.scissor[0], h - (dgl_gl.scissor[1] + dgl_gl.scissor[3]), dgl_gl.scissor[0] + dgl_gl.scissor[2],
            h - dgl_gl.scissor[1]);
    if (draw)
        cut(c, dgl_gl.viewport[0], h - (dgl_gl.viewport[1] + dgl_gl.viewport[3]),
            dgl_gl.viewport[0] + dgl_gl.viewport[2], h - dgl_gl.viewport[1]);
    if (c[2] < c[0]) c[2] = c[0];
    if (c[3] < c[1]) c[3] = c[1];
}

static void validate(void)
{
    dgl_texture *t = dgl_gl.texture_2d ? dgl_unit_texture(0) : NULL;
    ttex *x = t ? prepare(t) : NULL;
    uint32_t off, format, filter, blend, control, fogc;
    int c[4];
    tnt2_3d_target(rg_tnt2_color_off(dgl_ctx.draw_front), rg_tnt2.z_off, rg_tnt2.pitch_bytes, rg_tnt2.bpp,
                   rg_tnt2.width, rg_tnt2.height);
    scissor(1, c);
    tnt2_3d_clip(c[0], c[1], c[2], c[3]);
    blend = B_MSB | B_PERSP | B_GOURAUD;        /* (flat shading: every vertex the provoking one's colour) */
    if (x) {
        const dgl_texenv *e = dgl_tex_env(0);
        int mip = t->min_filter != GL_NEAREST && t->min_filter != GL_LINEAR;
        uint32_t mag = t->mag_filter == GL_LINEAR ? 2u : 1u, min;
        switch (t->min_filter) {
        case GL_LINEAR: min = 2; break;
        case GL_NEAREST_MIPMAP_NEAREST: min = 3; break;
        case GL_LINEAR_MIPMAP_NEAREST: min = 4; break;
        case GL_NEAREST_MIPMAP_LINEAR: min = 5; break;
        case GL_LINEAR_MIPMAP_LINEAR: min = 6; break;
        default: min = 1; break;
        }
        if (mip && x->levels < 2)
            min = min == 3 || min == 5 ? 1u : 2u;
        off = x->off;
        shift_s = t->wrap_s == GL_REPEAT, shift_t = t->wrap_t == GL_REPEAT;
        format = x->format | wrap_of(t->wrap_s) << 24 | wrap_of(t->wrap_t) << 28 |
                 (uint32_t)(mip ? x->levels : 1) << 12;
        /* The level by footprint sums, rounded down after the bias: +4/8 gives OpenGL's nearest (docs/tnt2.md). */
        filter = mag << 28 | min << 24 | (mip ? 4u << 16 : 0u) | 0x1010u;
        switch (e->mode) {
        case GL_MODULATE: blend |= TB_MODULATEALPHA; break;
        case GL_REPLACE: blend |= x->has_alpha ? TB_DECAL : TB_DECALALPHA; break;
        case GL_DECAL: blend |= TB_DECALALPHA; break;
        case GL_ADD: blend |= TB_ADD; break;
        default:
            warn_once(2, "GL_BLEND and combine texture environments drawn as GL_MODULATE");
            blend |= TB_MODULATEALPHA;
            break;
        }
    } else {
        shift_s = shift_t = 0;
        off = tnt2_tex_dummy_off();
        format = 0x110016A1u;           /* (the white texel: A8R8G8B8, one level, repeat) */
        filter = 0x11001010u;
        blend |= TB_MODULATEALPHA;
    }
    if (dgl_gl.texture_2d1)
        warn_once(3, "the second texture unit is not drawn yet");
    fogging = dgl_gl.fog;
    if (fogging)
        blend |= B_FOG;
    if (dgl_gl.blend)
        blend |= B_BLEND | bfactor(dgl_gl.blend_src) << 24 | bfactor(dgl_gl.blend_dst) << 28;
    else
        blend |= 2u << 24 | 1u << 28;   /* (ONE, ZERO) */
    control = C_Z_FIXED | C_CORNER | C_CULL_NONE;  /* (culling here, on the window vertices) */
    if (dgl_gl.alpha_test) {
        int ref = (int)floorf(dgl_gl.alpha_ref * 255.0f + 0.5f);
        control |= C_ALPHA_ON | cmp_of(dgl_gl.alpha_func) << 8 | (uint32_t)(ref < 0 ? 0 : ref > 255 ? 255 : ref);
    } else
        control |= 8u << 8;
    if (dgl_gl.depth_test && rg_tnt2.z_off) {
        control |= C_Z_ON | cmp_of(dgl_gl.depth_func) << 16;
        if (dgl_gl.depth_mask)
            control |= C_Z_WRITE;
    } else
        control |= 8u << 16;
    if (dgl_gl.dither)
        control |= C_DITHER;
    if (!dgl_gl.color_mask[0] || !dgl_gl.color_mask[1] || !dgl_gl.color_mask[2] || !dgl_gl.color_mask[3])
        warn_once(4, "colour masks are not drawn yet (all channels written)");
    if (dgl_gl.stencil_test)
        warn_once(5, "the stencil test is not drawn yet");
    fogc = 0xFF000000u | (uint32_t)(dgl_gl.fog_color[0] * 255.0f + 0.5f) << 16 |
           (uint32_t)(dgl_gl.fog_color[1] * 255.0f + 0.5f) << 8 | (uint32_t)(dgl_gl.fog_color[2] * 255.0f + 0.5f);
    tnt2_3d_state(off, format, filter, blend, control, fogc);
}

/* ---- Drawing ---------------------------------------------------------------- */

static int tb_begin(void)
{
    if (!dgl_ctx.active || !rg_tnt2.active)
        return 0;
    validate();
    return 1;
}

static void tb_end(void) { tnt2_kick(); }

static uint32_t ub(float v)
{
    int i = (int)floorf(v * 255.0f + 0.5f);
    return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static float fog_factor(float d)
{
    float f;
    d = fabsf(d);
    if (dgl_gl.fog_mode == GL_LINEAR)
        f = dgl_gl.fog_end != dgl_gl.fog_start ? (dgl_gl.fog_end - d) / (dgl_gl.fog_end - dgl_gl.fog_start) : 1.0f;
    else if (dgl_gl.fog_mode == GL_EXP)
        f = expf(-dgl_gl.fog_density * d);
    else
        f = expf(-(dgl_gl.fog_density * d) * (dgl_gl.fog_density * d));
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static void project(const dgl_cvtx *c, tnt2_vertex *v, const dgl_cvtx *colour)
{
    double iw = 1.0 / c->w;
    v->sx = (float)(dgl_gl.viewport[0] + (c->x * iw + 1.0) * 0.5 * dgl_gl.viewport[2]);
    v->sy = (float)(dgl_ctx.height - (dgl_gl.viewport[1] + (c->y * iw + 1.0) * 0.5 * dgl_gl.viewport[3]));
    v->sz = (float)(dgl_gl.depth_near + (c->z * iw + 1.0) * 0.5 * (dgl_gl.depth_far - dgl_gl.depth_near));
    v->rhw = (float)iw;
    v->color = ub(colour->a) << 24 | ub(colour->r) << 16 | ub(colour->g) << 8 | ub(colour->b);
    v->specular = (fogging ? ub(fog_factor(c->eye_d)) : 255u) << 24;
    v->tu = c->s, v->tv = c->t;
}

static double offset_for(const tnt2_vertex *p0, const tnt2_vertex *p1, const tnt2_vertex *p2)
{
    double x1, y1, z1, x2, y2, z2, det, m = 0;
    if (!dgl_gl.offset_fill || (dgl_gl.offset_factor == 0.0f && dgl_gl.offset_units == 0.0f))
        return 0;
    x1 = p1->sx - p0->sx, y1 = p1->sy - p0->sy, z1 = p1->sz - p0->sz;
    x2 = p2->sx - p0->sx, y2 = p2->sy - p0->sy, z2 = p2->sz - p0->sz;
    det = x1 * y2 - x2 * y1;
    if (det != 0) {
        double dzdx = fabs((z1 * y2 - z2 * y1) / det), dzdy = fabs((x1 * z2 - x2 * z1) / det);
        m = dzdx > dzdy ? dzdx : dzdy;
    }
    return dgl_gl.offset_factor * m + dgl_gl.offset_units / (rg_tnt2.depth_bits == 24 ? 16777215.0 : 65535.0);
}

/* By whole periods so that the smallest coordinate is in [0, 1): the chip's interpolation is a float's, so huge
 * coordinates (conformance t14: 30,000 periods along) lose their fraction. */
static void wrap_shift(tnt2_vertex *v, int n)
{
    float ms = v[0].tu, mt = v[0].tv;
    int i;
    for (i = 1; i < n; i++) {
        if (v[i].tu < ms) ms = v[i].tu;
        if (v[i].tv < mt) mt = v[i].tv;
    }
    ms = shift_s && ms > -1e9f && ms < 1e9f ? floorf(ms) : 0;
    mt = shift_t && mt > -1e9f && mt < 1e9f ? floorf(mt) : 0;
    if (ms != 0 || mt != 0)
        for (i = 0; i < n; i++)
            v[i].tu -= ms, v[i].tv -= mt;
}

static void draw_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    dgl_cvtx in[3], out[9];
    tnt2_vertex v[9];
    double area2;
    int n, i, flat = dgl_gl.shade_model == GL_FLAT;
    dgl_prims.tris_in++;
    if (!(dgl_outcode(a, GUARD, GUARD) | dgl_outcode(b, GUARD, GUARD) | dgl_outcode(c, GUARD, GUARD))) {
        n = 3;
        project(a, &v[0], flat ? prov : a), project(b, &v[1], flat ? prov : b), project(c, &v[2], flat ? prov : c);
    } else {
        in[0] = *a; in[1] = *b; in[2] = *c;
        if (flat)
            for (i = 0; i < 3; i++) {
                in[i].r = prov->r; in[i].g = prov->g; in[i].b = prov->b; in[i].a = prov->a;
            }
        n = dgl_clip_polygon(in, 3, out, GUARD, GUARD);
        if (n < 3) {
            dgl_prims.clipped++;
            return;
        }
        for (i = 0; i < n; i++)
            project(&out[i], &v[i], &out[i]);
    }
    /* The polygon's own area (a clipped one's first three vertices can lie on one line: conformance t35's quad). */
    for (area2 = 0, i = 0; i < n; i++)
        area2 += (double)v[i].sx * v[(i + 1) % n].sy - (double)v[(i + 1) % n].sx * v[i].sy;
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
                double z = v[i].sz + off;
                v[i].sz = (float)(z < 0 ? 0 : z > 1 ? 1 : z);
            }
    }
    if (shift_s || shift_t)
        wrap_shift(v, n);
    for (i = 1; i + 1 < n; i++) {
        tnt2_tri(&v[0], &v[i], &v[i + 1]);
        tris++;
    }
}

static void mid(dgl_cvtx *o, const dgl_cvtx *a, const dgl_cvtx *b)
{
#define M(f) o->f = (a->f + b->f) * 0.5f
    M(x); M(y); M(z); M(w); M(r); M(g); M(b); M(a); M(s); M(t); M(s1); M(t1); M(eye_d);
#undef M
}

/* Fog: the chip interpolates a factor per vertex; a triangle whose factor changes by more than a tenth across it
 * is split in four in clip space (up to three times), so the factor follows the fog's curve as OpenGL's per-pixel
 * fog does (conformance t10). */
static void fog_split(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov, int depth)
{
    float fa, fb, fc, lo, hi;
    dgl_cvtx ab, bc, ca;
    if (depth >= 3) {
        draw_triangle(a, b, c, prov);
        return;
    }
    fa = fog_factor(a->eye_d), fb = fog_factor(b->eye_d), fc = fog_factor(c->eye_d);
    lo = fa < fb ? fa : fb, lo = fc < lo ? fc : lo;
    hi = fa > fb ? fa : fb, hi = fc > hi ? fc : hi;
    if (hi - lo <= 0.1f) {
        draw_triangle(a, b, c, prov);
        return;
    }
    mid(&ab, a, b), mid(&bc, b, c), mid(&ca, c, a);
    fog_split(a, &ab, &ca, prov, depth + 1);
    fog_split(&ab, b, &bc, prov, depth + 1);
    fog_split(&ca, &bc, c, prov, depth + 1);
    fog_split(&ab, &bc, &ca, prov, depth + 1);
}

static void tb_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    if (fogging)
        fog_split(a, b, c, prov, 0);
    else
        draw_triangle(a, b, c, prov);
}

/* A line: a quad half the width either side, across its minor axis (OpenGL's wide lines' shape), b's colour when
 * flat. */
static void tb_line(const dgl_cvtx *a, const dgl_cvtx *b)
{
    dgl_cvtx in[3], out[9];
    tnt2_vertex p[2], q[4];
    float hw = (dgl_gl.line_width > 1.0f ? dgl_gl.line_width : 1.0f) * 0.5f;
    int i;
    in[0] = *a; in[1] = *b; in[2] = *b;
    if (dgl_gl.shade_model == GL_FLAT) {
        in[0].r = b->r; in[0].g = b->g; in[0].b = b->b; in[0].a = b->a;
    }
    if (dgl_clip_polygon(in, 3, out, GUARD, GUARD) < 2)
        return;
    project(&out[0], &p[0], &out[0]);
    project(&out[1], &p[1], &out[1]);
    for (i = 0; i < 4; i++)
        q[i] = p[i < 2 ? 0 : 1];
    if (fabsf(p[1].sx - p[0].sx) >= fabsf(p[1].sy - p[0].sy)) {    /* x-major: widen in y */
        q[0].sy -= hw, q[1].sy += hw, q[2].sy += hw, q[3].sy -= hw;
    } else {
        q[0].sx -= hw, q[1].sx += hw, q[2].sx += hw, q[3].sx -= hw;
    }
    tnt2_tri(&q[0], &q[1], &q[2]);
    tnt2_tri(&q[0], &q[2], &q[3]);
    lines_++;
}

/* A point: a square of the point size about its centre; none when the centre is outside the view volume. */
static void tb_point(const dgl_cvtx *a)
{
    tnt2_vertex p, q[4];
    float h = (dgl_gl.point_size > 1.0f ? dgl_gl.point_size : 1.0f) * 0.5f;
    int i;
    if (dgl_outcode(a, 1.0f, 1.0f))
        return;
    project(a, &p, a);
    for (i = 0; i < 4; i++)
        q[i] = p;
    q[0].sx -= h, q[0].sy -= h;
    q[1].sx += h, q[1].sy -= h;
    q[2].sx += h, q[2].sy += h;
    q[3].sx -= h, q[3].sy += h;
    tnt2_tri(&q[0], &q[1], &q[2]);
    tnt2_tri(&q[0], &q[2], &q[3]);
    points_++;
}

static void install(void)
{
    dgl_sink.begin = tb_begin;
    dgl_sink.triangle = tb_triangle;
    dgl_sink.fast = NULL;                       /* (the general path only) */
    dgl_sink.polygon = NULL;
    dgl_sink.line = tb_line;
    dgl_sink.point = tb_point;
    dgl_sink.end = tb_end;
}

/* Clears by the 2D engine, in the scissor: colour, and the depth buffer (with stencil's byte in Z24S8). */
static void clear(GLbitfield mask)
{
    int c[4];
    rg_surface s;
    if (!dgl_ctx.active || !rg_tnt2.active)
        return;
    scissor(0, c);
    s.pitch_bytes = rg_tnt2.pitch_bytes, s.bpp = rg_tnt2.bpp, s.width = rg_tnt2.width, s.height = rg_tnt2.height;
    if (mask & GL_COLOR_BUFFER_BIT) {
        const GLfloat *k = dgl_gl.clear_color;
        uint32_t v = rg_tnt2.bpp == 32
                   ? ub(k[3]) << 24 | ub(k[0]) << 16 | ub(k[1]) << 8 | ub(k[2])
                   : (ub(k[0]) >> 3) << 11 | (ub(k[1]) >> 2) << 5 | (ub(k[2]) >> 3);
        if (!dgl_gl.color_mask[0] || !dgl_gl.color_mask[1] || !dgl_gl.color_mask[2])
            warn_once(6, "a colour clear with a colour mask clears every channel");
        tnt2_set_target(rg_tnt2_color_off(dgl_ctx.draw_front), s.pitch_bytes, s.bpp);
        tnt2_set_clip(c[0], c[1], c[2], c[3]);
        tnt2_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
    }
    if (rg_tnt2.z_off && (((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask) || (mask & GL_STENCIL_BUFFER_BIT))) {
        double d = dgl_gl.clear_depth < 0 ? 0 : dgl_gl.clear_depth > 1 ? 1 : dgl_gl.clear_depth;
        uint32_t v = rg_tnt2.depth_bits == 24
                   ? (uint32_t)(d * 16777215.0 + 0.5) << 8 | ((unsigned)dgl_gl.clear_stencil & 255u)
                   : (uint32_t)(d * 65535.0 + 0.5);
        if (!((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask))
            warn_once(7, "a stencil clear clears the depth buffer too");
        tnt2_set_target(rg_tnt2.z_off, s.pitch_bytes, s.bpp);
        tnt2_set_clip(c[0], c[1], c[2], c[3]);
        tnt2_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
    }
    tnt2_kick();
}

static void caps(dgl_caps *c)
{
    static char name[48];
    memset(c, 0, sizeof *c);
    strcpy(name, tnt2.name ? tnt2.name : "Riva TNT2");
    c->renderer = name;
    c->texture_units = 1;
    c->max_texture_size = 2048;
}

static void synced(void) { }

static int tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    (void)t, (void)level, (void)x, (void)y, (void)w, (void)h;
    return 0;                                   /* (made again whole at its next use: t->dirty) */
}

static void tex_free(dgl_texture *t) { tex_drop(t); }

static int tex_info(const dgl_texture *t, int rgba[4])
{
    const ttex *x = (const ttex *)t->be;
    uint32_t f = x ? x->format & 0xF00u : 0;
    if (!x)
        return 0;
    if (f == F_R5G6B5) rgba[0] = 5, rgba[1] = 6, rgba[2] = 5, rgba[3] = 0;
    else if (f == F_A1R5G5B5) rgba[0] = rgba[1] = rgba[2] = 5, rgba[3] = 1;
    else if (f == F_A4R4G4B4) rgba[0] = rgba[1] = rgba[2] = rgba[3] = 4;
    else rgba[0] = rgba[1] = rgba[2] = rgba[3] = 8;
    return 1;
}

static void tex_reset(uint32_t start, uint32_t end)
{
    int i, n = dgl_texture_count();
    dgl_texture *t;
    for (i = 0; i < n; i++)
        if ((t = dgl_texture_nth(i)) && t->be) {
            free(t->be);
            t->be = NULL;
        }
    dgl_vram_init(start, end);
}

static void stats(dgl_be_stats *s)
{
    memset(s, 0, sizeof *s);
    s->tris_offloaded = tris;                   /* (every triangle: the chip sets them up) */
    s->mem_used = dgl_vram_used();
}

const dgl_backend dgl_tnt2_backend = {
    .name = "tnt2",
    .dev = &rg_tnt2_device,
    .caps = caps,
    .install = install,
    .clear = clear,
    .synced = synced,
    .tex_sub = tex_sub,
    .tex_free = tex_free,
    .tex_info = tex_info,
    .tex_reset = tex_reset,
    .tex_change = NULL,
    .stats = stats,
};
