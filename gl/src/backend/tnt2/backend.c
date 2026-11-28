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
#include "mga/prof.h"
#include "mga/fp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GUARD 4.0f                      /* NDC units either side: the clipper's planes beyond the viewport (x and y
                                           stay within +-2048 at 640 wide) */

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
    int      code, lw, lh;              /* its TNT2_TEX_ format and size, for rectangles written in place */
    uint32_t mark;                      /* the command stream after the last draw with it (tnt2_mark) */
} ttex;

static ttex *cur_tt0, *cur_tt1;          /* the copies the state sent draws with (marked at each draw's end) */

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

/* Plain RGB and luminance at eight bits a component: asked for by size, or unsized on a 32-bit screen (GL leaves an
 * unsized format's precision to us: the screen's, as the era's drivers did; the owner, 2026-11-27); else RGB565. */
static int at_8bits(GLint f)
{
    switch (f) {
    case GL_RGB8: case GL_RGB10: case GL_RGB12: case GL_RGB16:
        return 1;
    case 3: case GL_RGB: case 1: case GL_LUMINANCE: case GL_COMPRESSED_RGB_ARB: case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        return rg_tnt2.bpp == 32;
    default:
        return 0;                       /* (few bits asked for) */
    }
}

/* GL's copy of a plain RGB level held as the chip will draw it (texstore.c): RGB565 rounded as the layer's
 * conversion rounds (tnt2_tex_load_rgba8), or three bytes. */
static int tex_store(int ifc, GLint ifmt)
{
    (void)ifc;
    return at_8bits(ifmt) ? DGL_ST_RGB8 : DGL_ST_RGB565R;
}

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
        if (at_8bits(l->ifmt)) {
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

static void tex_drop(dgl_texture *t)
{
    ttex *x = (ttex *)t->be;
    if (x == cur_tt0) cur_tt0 = NULL;
    if (x == cur_tt1) cur_tt1 = NULL;
    if (x) {
        if (dgl_vram_retire(x->block) != 0) {   /* (drawing queued may still read it: free after the next sync) */
            tnt2_sync(1000000);
            dgl_vram_sync_done();
            dgl_vram_free(x->block);
        }
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
        if (dgl_vram_retired()) {               /* (blocks waiting for a sync first: then they are free) */
            tnt2_sync(1000000);
            dgl_vram_sync_done();
            continue;
        }
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
    PROF_SCOPE(PROF_D_TEX);
    ttex *x = (ttex *)t->be;
    const dgl_level *l0 = &t->level[0];
    uint32_t fmt, size = 0;
    int cpp, alpha, n, i, lw, lh;
    const dgl_palette *pal = l0->idx ? dgl_palette_for(t) : NULL;
    unsigned char *expanded[DGL_MAX_LEVELS];
    if (x && !t->dirty && (!pal || (x->pal == pal && x->palgen == pal->gen))) {
        x->stamp = ++stamp;
        return x;
    }
    if (!DGL_LEVEL_DEFINED(l0))
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
        if (!(pal ? l->idx != NULL : DGL_LEVEL_TEXELS(l)) || l->w != w || l->h != h)
            break;
        if (w == 1 && h == 1) {
            n++;
            break;
        }
    }
    for (i = 0; i < n; i++)
        size += (uint32_t)(t->level[i].w * t->level[i].h * cpp);
    {
        /* Made again (its levels or palette changed: GLQuake's lightmaps every frame): into a new block, the old
         * one retired until the next sync, so the chip need not drain first. */
        ttex *old = (ttex *)t->be;
        uint32_t oldblock = old ? old->block : 0;
        t->be = NULL;
        if (old == cur_tt0) cur_tt0 = NULL;
        if (old == cur_tt1) cur_tt1 = NULL;
        free(old);
        x = (ttex *)calloc(1, sizeof *x);
        if (!x || room(size + 128u, &x->block, t) != 0) {
            free(x);
            if (old) {
                tnt2_sync(1000000);         /* (queued drawing may still read it) */
                dgl_vram_free(oldblock);
            }
            warn_once(1, "out of texture memory: a texture drawn untextured");
            return NULL;
        }
        if (old && dgl_vram_retire(oldblock) != 0) {
            tnt2_sync(1000000);
            dgl_vram_sync_done();
            dgl_vram_free(oldblock);
        }
    }
    x->off = (x->block + 127u) & ~127u;
    {
        /* GL's RGBA8 levels (colour indices through the palette first) converted and swizzled in one pass. */
        const uint8_t *src[DGL_MAX_LEVELS];
        int code = fmt == F_A8R8G8B8 ? TNT2_TEX_A8R8G8B8 : fmt == F_R5G6B5 ? TNT2_TEX_R5G6B5
                 : fmt == F_A1R5G5B5 ? TNT2_TEX_A1R5G5B5 : TNT2_TEX_A4R4G4B4;
        for (i = 0; i < n; i++) {
            const dgl_level *l = &t->level[i];
            expanded[i] = NULL;
            src[i] = dgl_level_rgba(l, i);  /* (its copy, or expanded from its smaller form: texstore.c) */
            if (pal) {
                int k;
                if (!(expanded[i] = (unsigned char *)malloc((size_t)l->w * l->h * 4)))
                    src[i] = NULL;
                else {
                    for (k = 0; k < l->w * l->h; k++)
                        memcpy(expanded[i] + 4 * k, dgl_palette_texel(pal, l->idx[k]), 4);
                    src[i] = expanded[i];
                }
            }
        }
        for (i = 0; i < n && src[i]; i++)
            ;
        if (i == n)
            tnt2_tex_load_rgba8(x->off, code, lw, lh, n, src);   /* (a free block: nothing queued reads it) */
        x->code = code, x->lw = lw, x->lh = lh;
        for (i = 0; i < n; i++)
            free(expanded[i]);
        dgl_level_rgba_done();
    }
    x->format = F_DMA_A | F_ORIGIN | fmt | (uint32_t)lh << 20 | (uint32_t)lw << 16;
    x->levels = n;
    x->has_alpha = alpha;
    x->stamp = ++stamp;
    x->mark = tnt2_mark();
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
static int valid_ok;                    /* the words sent are GL's state now (until a dirty bit or a texture change) */
static const dgl_texture *valid_tex, *valid_tex1;
static int mt_on;                       /* drawing with the multitexture object (0x55) */
static int shift_s1, shift_t1;          /* its second texture's whole-period shift */
static float vx_s, vx_o, vy_s, vy_o, vz_s, vz_o;    /* the viewport's scale and offset, from the last validate */
static int shift_s, shift_t;            /* a repeating texture: coordinates shifted by whole periods per triangle */

/* 0..1 to a byte: one FISTP in the current (nearest) rounding, no control-word switches (mga/fp.h: a cast or
 * floorf costs two FLDCWs each, which stall a P6; four a vertex were a third of the P3's projection time). */
static uint32_t ub(float v)
{
    /* v * 255 rounded to an integer by adding 1.5 * 2^23 (the float's last place is then 1), read back as an
     * integer, then clamped as an integer: no x87 compares (each an FNSTSW) and no FISTP. Values far out of range
     * land outside 0-255 and clamp the right way. */
    union { float f; int32_t i; } u;
    int32_t i;
    u.f = v * 255.0f + 12582912.0f;
    i = u.i - 0x4B400000;
    return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

/* A vertex's colour packed, the last one's kept (GLQuake's world: one colour a polygon, or none). */
static uint32_t col_key[4], col_val, col_set;

static uint32_t colour_of(const dgl_cvtx *c)
{
    uint32_t k[4];
    memcpy(k, &c->r, sizeof k);         /* (r, g, b, a: one after another in dgl_cvtx) */
    if (col_set && k[0] == col_key[0] && k[1] == col_key[1] && k[2] == col_key[2] && k[3] == col_key[3])
        return col_val;
    memcpy(col_key, k, sizeof k);
    col_set = 1;
    return col_val = ub(c->a) << 24 | ub(c->r) << 16 | ub(c->g) << 8 | ub(c->b);
}

/* A vertex's two outcodes at once (bit set: outside that plane): the view volume's (returned) and the guard
 * band's (*guard), from the signs of the planes' distances stored as floats and read as integers, so no x87
 * compares (an FNSTSW each with i586 code; the shared dgl_outcode's twelve a vertex were a fifth of the P3's
 * projection). A distance of -0 counts as outside: harmless both ways (a trivially rejected triangle is wholly
 * on a plane, a guard-band vertex is clipped). */
static unsigned outcodes(const dgl_cvtx *v, unsigned *guard)
{
    union { float f; uint32_t u; } d[10];
    float gw = GUARD * v->w;
    d[0].f = v->z + v->w, d[1].f = v->w - v->z;
    d[2].f = v->x + v->w, d[3].f = v->w - v->x, d[4].f = v->y + v->w, d[5].f = v->w - v->y;
    d[6].f = v->x + gw, d[7].f = gw - v->x, d[8].f = v->y + gw, d[9].f = gw - v->y;
    *guard = (d[0].u >> 31) | (d[1].u >> 31) << 1 | (d[6].u >> 31) << 2 | (d[7].u >> 31) << 3 |
             (d[8].u >> 31) << 4 | (d[9].u >> 31) << 5;
    return (d[0].u >> 31) | (d[1].u >> 31) << 1 | (d[2].u >> 31) << 2 | (d[3].u >> 31) << 3 |
           (d[4].u >> 31) << 4 | (d[5].u >> 31) << 5;
}

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

/* A texture unit's words for either object: its copy's offset, FORMAT and FILTER (the white texel when none). */
static ttex *tex_words(dgl_texture *t, uint32_t *off, uint32_t *format, uint32_t *filter, int *ss, int *st)
{
    ttex *x = t ? prepare(t) : NULL;
    *ss = *st = 0;
    if (!x) {
        *off = tnt2_tex_dummy_off(), *format = 0x110016A1u, *filter = 0x11001010u;
        return NULL;
    }
    {
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
        *off = x->off;
        *ss = t->wrap_s == GL_REPEAT, *st = t->wrap_t == GL_REPEAT;
        *format = x->format | wrap_of(t->wrap_s) << 24 | wrap_of(t->wrap_t) << 28 | (uint32_t)(mip ? x->levels : 1) << 12;
        /* The level by footprint sums, rounded down after the bias: +4/8 gives OpenGL's nearest (docs/tnt2.md). */
        *filter = mag << 28 | min << 24 | (mip ? 4u << 16 : 0u) | 0x1010u;
    }
    return x;
}

/* The multitexture object's combiners (docs/tnt2.md): each stage out = in0*in1 + in2*in3, an input a byte of
 * (source << 2 | alpha << 1 | inverse): 1 zero, 2 the constant, 3 the vertex's colour, 4 the stage before, 5 and 6
 * the textures. */
enum { CS_ZERO = 1, CS_CONST = 2, CS_PRIM = 3, CS_PREV = 4, CS_TEX0 = 5 };
#define CIN(src, alpha, inv) ((uint32_t)(src) << 2 | ((alpha) ? 2u : 0u) | ((inv) ? 1u : 0u))
#define CW(a, b, c, d) ((a) | (b) << 8 | (c) << 16 | (d) << 24 | 1u << 29)   /* (MAP: identity) */
#define C_ONE  CIN(CS_ZERO, 0, 1)
#define C_NONE CIN(CS_ZERO, 0, 0)

/* GL_COMBINE_ARB (texture_env_combine): a function's arguments as inputs. The colour combiner takes a source's
 * alpha by the ALPHA bit; ONE_MINUS operands flip INVERSE. */
static uint32_t comb_in(int unit, GLenum src, GLenum op, int colour)
{
    uint32_t s;
    switch (src) {
    case GL_TEXTURE: s = CS_TEX0 + (uint32_t)unit; break;
    case GL_TEXTURE0_ARB: case GL_TEXTURE1_ARB: s = CS_TEX0 + (uint32_t)(src - GL_TEXTURE0_ARB); break;
    case GL_CONSTANT_ARB: s = CS_CONST; break;
    case GL_PRIMARY_COLOR_ARB: s = CS_PRIM; break;
    default: s = unit ? CS_PREV : CS_PRIM; break;      /* GL_PREVIOUS_ARB (unit 0's: the vertex colour) */
    }
    if (colour)
        return CIN(s, op == GL_SRC_ALPHA || op == GL_ONE_MINUS_SRC_ALPHA,
                   op == GL_ONE_MINUS_SRC_COLOR || op == GL_ONE_MINUS_SRC_ALPHA);
    return CIN(s, 0, op == GL_ONE_MINUS_SRC_ALPHA);
}

/* A function to one word: REPLACE a0*1, MODULATE a0*a1, ADD and ADD_SIGNED a0*1 + a1*1 (the latter less a half,
 * by the map), INTERPOLATE a0*a2 + a1*(1-a2); the output map scales by 1, 2 or 4 (Mesa's driver never used 4).
 * SUBTRACT has no form here: drawn as MODULATE, said once. */
static uint32_t comb_fn(GLenum fn, const uint32_t *a, float scale)
{
    uint32_t map = scale >= 3.0f ? 3u : scale >= 1.5f ? 2u : 1u, in0, in1, in2, in3;
    switch (fn) {
    case GL_REPLACE: in0 = a[0], in1 = C_ONE, in2 = C_NONE, in3 = C_NONE; break;
    case GL_ADD: case GL_ADD_SIGNED_ARB:
        in0 = a[0], in1 = C_ONE, in2 = a[1], in3 = C_ONE;
        if (fn == GL_ADD_SIGNED_ARB)
            map = map == 1 ? 4u : 7u;           /* (BIAS, BIAS_SCALE2: no bias with four) */
        break;
    case GL_INTERPOLATE_ARB: in0 = a[0], in1 = a[2], in2 = a[1], in3 = a[2] ^ 1u; break;
    default:
        if (fn != GL_MODULATE)
            warn_once(6, "GL_SUBTRACT_ARB drawn as GL_MODULATE (the TNT2's combiner adds)");
        in0 = a[0], in1 = a[1], in2 = C_NONE, in3 = C_NONE;
        break;
    }
    return in0 | in1 << 8 | in2 << 16 | in3 << 24 | map << 29;
}

static void combine_words(int unit, const dgl_texenv *e, uint32_t *aw, uint32_t *cw)
{
    uint32_t c[3], a[3];
    int i;
    for (i = 0; i < 3; i++) {
        c[i] = comb_in(unit, e->src_rgb[i], e->op_rgb[i], 1);
        a[i] = comb_in(unit, e->src_alpha[i], e->op_alpha[i], 0);
    }
    *cw = comb_fn(e->combine_rgb, c, e->rgb_scale);
    *aw = comb_fn(e->combine_alpha, a, e->alpha_scale);
}

/* Does a unit's environment read the constant colour (GL_BLEND, or a combine argument)? */
static int uses_const(const dgl_texenv *e)
{
    int i;
    if (e->mode == GL_BLEND)
        return 1;
    if (e->mode != GL_COMBINE_ARB)
        return 0;
    for (i = 0; i < 3; i++)
        if (e->src_rgb[i] == GL_CONSTANT_ARB || e->src_alpha[i] == GL_CONSTANT_ARB)
            return 1;
    return 0;
}

static void stage_words(int unit, int on, const dgl_texenv *e, int has_alpha, uint32_t *aw, uint32_t *cw)
{
    uint32_t T = CS_TEX0 + (uint32_t)unit, P = unit ? CS_PREV : CS_PRIM;
    GLenum env = e->mode;
    if (!on) {
        *aw = *cw = CW(CIN(P, 0, 0), C_ONE, C_NONE, C_NONE);
        return;
    }
    if (env == GL_COMBINE_ARB) {
        combine_words(unit, e, aw, cw);
        return;
    }
    switch (env) {
    case GL_REPLACE:
        *cw = CW(CIN(T, 0, 0), C_ONE, C_NONE, C_NONE);
        *aw = has_alpha ? *cw : CW(CIN(P, 0, 0), C_ONE, C_NONE, C_NONE);
        break;
    case GL_DECAL:                      /* C = Cp (1 - At) + Ct At, A = Ap */
        *cw = CW(CIN(T, 0, 0), CIN(T, 1, 0), CIN(P, 0, 0), CIN(T, 1, 1));
        *aw = CW(CIN(P, 0, 0), C_ONE, C_NONE, C_NONE);
        break;
    case GL_ADD:                        /* C = Cp + Ct, A = Ap At */
        *cw = CW(CIN(T, 0, 0), C_ONE, CIN(P, 0, 0), C_ONE);
        *aw = CW(CIN(T, 0, 0), CIN(P, 0, 0), C_NONE, C_NONE);
        break;
    case GL_BLEND:                      /* C = Cp (1 - Ct) + Cc Ct, A = Ap At */
        *cw = CW(CIN(P, 0, 0), CIN(T, 0, 1), CIN(CS_CONST, 0, 0), CIN(T, 0, 0));
        *aw = CW(CIN(T, 0, 0), CIN(P, 0, 0), C_NONE, C_NONE);
        break;
    default:                            /* GL_MODULATE (and what this can't do) */
        *cw = CW(CIN(T, 0, 0), CIN(P, 0, 0), C_NONE, C_NONE);
        *aw = *cw;
        break;
    }
}

static uint32_t sop(GLenum o)
{
    switch (o) {
    case GL_ZERO: return 2;
    case GL_REPLACE: return 3;
    case GL_INCR: return 4;
    case GL_DECR: return 5;
    case GL_INVERT: return 6;
    default: return 1;                  /* GL_KEEP */
    }
}

static void validate(void)
{
    dgl_texture *t = dgl_gl.texture_2d ? dgl_unit_texture(0) : NULL;
    dgl_texture *t1 = dgl_gl.texture_2d1 ? dgl_unit_texture(1) : NULL;
    int masked = !dgl_gl.color_mask[0] || !dgl_gl.color_mask[1] || !dgl_gl.color_mask[2] || !dgl_gl.color_mask[3];
    int stencil = dgl_gl.stencil_test && rg_tnt2.depth_bits == 24;
    int need_mt = t1 != NULL || masked || stencil ||
                  (t && (dgl_tex_env(0)->mode == GL_BLEND || dgl_tex_env(0)->mode == GL_COMBINE_ARB));
    ttex *x;
    uint32_t off, format, filter, blend, control, fogc;
    int c[4];
    /* Nothing GL draws with changed since (GLQuake: a thousand begins a frame, most with the state as it was): the
     * words sent still hold (the layer sends them again itself after 2D). */
    if (valid_ok && t == valid_tex && t1 == valid_tex1 && need_mt == mt_on && !(t && (t->dirty || t->level[0].idx)) &&
        !(t1 && (t1->dirty || t1->level[0].idx)) &&
        !(dgl_gl.dirty & (DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP)))
        return;
    x = t ? prepare(t) : NULL;
    cur_tt0 = x, cur_tt1 = NULL;
    vx_s = dgl_gl.viewport[2] * 0.5f, vx_o = dgl_gl.viewport[0] + dgl_gl.viewport[2] * 0.5f;
    vy_s = -dgl_gl.viewport[3] * 0.5f, vy_o = dgl_ctx.height - (dgl_gl.viewport[1] + dgl_gl.viewport[3] * 0.5f);
    vz_s = (float)((dgl_gl.depth_far - dgl_gl.depth_near) * 0.5);
    vz_o = (float)(dgl_gl.depth_near + (dgl_gl.depth_far - dgl_gl.depth_near) * 0.5);
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
        default:                        /* (GL_BLEND goes to the multitexture object's combiners) */
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
    fogging = dgl_gl.fog;
    if (fogging)
        blend |= B_FOG;
    if (dgl_gl.blend)
        blend |= B_BLEND | bfactor(dgl_gl.blend_src) << 24 | bfactor(dgl_gl.blend_dst) << 28;
    else
        blend |= 2u << 24 | 1u << 28;   /* (ONE, ZERO) */
    control = C_Z_FIXED | C_CORNER | C_CULL_NONE;  /* (culling here, on the window vertices) */
    if (dgl_gl.alpha_test) {
        float rf = dgl_gl.alpha_ref * 255.0f + 0.5f;  /* (mga_ifloor: exact, without floorf's FLDCWs) */
        int ref = rf < 0.0f ? 0 : rf > 255.0f ? 255 : mga_ifloor(rf);
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
    if (dgl_gl.stencil_test && rg_tnt2.depth_bits != 24)
        warn_once(5, "the stencil test needs a 32-bit context (Z24S8): not drawn at 16 bits");
    fogc = 0xFF000000u | ub(dgl_gl.fog_color[0]) << 16 | ub(dgl_gl.fog_color[1]) << 8 | ub(dgl_gl.fog_color[2]);
    mt_on = need_mt;
    if (!need_mt)
        tnt2_3d_state(off, format, filter, blend, control, fogc);
    else {
        /* The multitexture object: both units, the combiners, colour writes, stencil. */
        uint32_t w[16], off1, format1, filter1;
        const dgl_texenv *e0 = dgl_tex_env(0), *e1 = dgl_tex_env(1);
        ttex *x0, *x1;
        int ss, st;
        x0 = tex_words(t, &w[0], &w[2], &w[4], &ss, &st);
        shift_s = ss, shift_t = st;
        x1 = tex_words(t1, &off1, &format1, &filter1, &ss, &st);
        shift_s1 = ss, shift_t1 = st;
        cur_tt0 = x0, cur_tt1 = x1;
        w[1] = off1, w[3] = format1, w[5] = filter1;
        stage_words(0, x0 != NULL, e0, x0 ? x0->has_alpha : 1, &w[6], &w[7]);
        stage_words(1, x1 != NULL, e1, x1 ? x1->has_alpha : 1, &w[8], &w[9]);
        {
            /* (one constant for both stages: the unit that reads it; unit 0's when both do) */
            const dgl_texenv *ce = x1 && uses_const(e1) && !(x0 && uses_const(e0)) ? e1 : e0;
            if (x0 && x1 && uses_const(e0) && uses_const(e1) && memcmp(e0->color, e1->color, sizeof e0->color))
                warn_once(7, "two different texture environment colours: unit 0's for both");
            w[10] = ub(ce->color[3]) << 24 | ub(ce->color[0]) << 16 | ub(ce->color[1]) << 8 | ub(ce->color[2]);
        }
        w[11] = blend & ~0xFu;              /* (no texture map field in 0x55's BLEND) */
        w[12] = control | (dgl_gl.color_mask[3] ? 0x04000000u : 0) | (dgl_gl.color_mask[0] ? 0x08000000u : 0) |
                (dgl_gl.color_mask[1] ? 0x10000000u : 0) | (dgl_gl.color_mask[2] ? 0x20000000u : 0);
        w[13] = 0, w[14] = 0x111;
        if (stencil) {
            w[12] |= (dgl_gl.stencil_writemask & 255u) ? 0x02000000u : 0;
            w[13] = 1u | cmp_of(dgl_gl.stencil_func) << 4 | ((uint32_t)dgl_gl.stencil_ref & 255u) << 8 |
                    (dgl_gl.stencil_mask & 255u) << 16 | (dgl_gl.stencil_writemask & 255u) << 24;
            w[14] = sop(dgl_gl.stencil_fail) | sop(dgl_gl.stencil_zfail) << 4 | sop(dgl_gl.stencil_zpass) << 8;
        }
        w[15] = fogc;
        tnt2_3d_mt_state(w);
    }
    dgl_gl.dirty &= ~(DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP);
    valid_ok = 1;
    valid_tex = t;
    valid_tex1 = t1;
}

/* ---- Drawing ---------------------------------------------------------------- */

static int tb_begin(void)
{
    if (!dgl_ctx.active || !rg_tnt2.active)
        return 0;
    validate();
    return 1;
}

/* The textures drawn with marked: a rectangle written into one waits for the chip to pass it (tex_sub). */
static void tb_end(void)
{
    uint32_t m = tnt2_mark();
    if (cur_tt0) cur_tt0->mark = m;
    if (cur_tt1) cur_tt1->mark = m;
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
    float iw = 1.0f / c->w;             /* (single precision: the chip takes x and y in 1/16 pixel) */
    v->sx = c->x * iw * vx_s + vx_o;
    v->sy = c->y * iw * vy_s + vy_o;
    v->sz = c->z * iw * vz_s + vz_o;
    v->rhw = iw;
    v->color = colour_of(colour);
    v->specular = (fogging ? ub(fog_factor(c->eye_d)) : 255u) << 24;
    v->tu = c->s, v->tv = c->t;
    v->tu1 = c->s1, v->tv1 = c->t1;
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
    if (mt_on && (shift_s1 || shift_t1)) {
        float m1 = v[0].tu1, n1 = v[0].tv1;
        for (i = 1; i < n; i++) {
            if (v[i].tu1 < m1) m1 = v[i].tu1;
            if (v[i].tv1 < n1) n1 = v[i].tv1;
        }
        m1 = shift_s1 && m1 > -1e9f && m1 < 1e9f ? (float)mga_ifloor(m1) : 0;
        n1 = shift_t1 && n1 > -1e9f && n1 < 1e9f ? (float)mga_ifloor(n1) : 0;
        for (i = 0; i < n; i++)
            v[i].tu1 -= m1, v[i].tv1 -= n1;
    }
    for (i = 1; i < n; i++) {
        if (v[i].tu < ms) ms = v[i].tu;
        if (v[i].tv < mt) mt = v[i].tv;
    }
    ms = shift_s && ms > -1e9f && ms < 1e9f ? (float)mga_ifloor(ms) : 0;
    mt = shift_t && mt > -1e9f && mt < 1e9f ? (float)mga_ifloor(mt) : 0;
    if (ms != 0 || mt != 0)
        for (i = 0; i < n; i++)
            v[i].tu -= ms, v[i].tv -= mt;
}

static void draw_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    PROF_SCOPE(PROF_D_PROJ);
    dgl_cvtx in[3], out[9];
    tnt2_vertex v[9];
    double area2;
    int n, i, flat = dgl_gl.shade_model == GL_FLAT;
    unsigned ga, gb, gc;
    dgl_prims.tris_in++;
    if (outcodes(a, &ga) & outcodes(b, &gb) & outcodes(c, &gc)) {
        dgl_prims.clipped++;            /* (wholly outside one of the view volume's planes: no clipping needed) */
        return;
    }
    if (!(ga | gb | gc)) {
        n = 3;
        project(a, &v[0], flat ? prov : a), project(b, &v[1], flat ? prov : b), project(c, &v[2], flat ? prov : c);
    } else {
        in[0] = *a; in[1] = *b; in[2] = *c;
        if (flat)
            for (i = 0; i < 3; i++) {
                in[i].r = prov->r; in[i].g = prov->g; in[i].b = prov->b; in[i].a = prov->a;
            }
        {
            int o = PROF_SWITCH(PROF_D_CLIP);
            n = dgl_clip_polygon(in, 3, out, GUARD, GUARD);
            PROF_BACK(o);
        }
        if (n < 3) {
            dgl_prims.clipped++;
            return;
        }
        for (i = 0; i < n; i++)
            project(&out[i], &v[i], &out[i]);
    }
    /* The polygon's own area (a clipped one's first three vertices can lie on one line: conformance t35's quad). */
    for (area2 = 0, i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;         /* (not %: an IDIV) */
        area2 += (double)v[i].sx * v[j].sy - (double)v[j].sx * v[i].sy;
    }
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
    if (shift_s || shift_t || (mt_on && (shift_s1 || shift_t1)))
        wrap_shift(v, n);
    PROF_SWITCH(PROF_D_SETUP);
    for (i = 1; i + 1 < n; i++) {
        if (mt_on)
            tnt2_tri_mt(&v[0], &v[i], &v[i + 1]);
        else
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

/* A polygon (GL_POLYGON, a quad, a fan) wholly inside the guard band, unfogged, of up to 16 vertices: each vertex
 * projected once and sent once (tnt2_poly: the chip's vertex slots and index words); else triangle by triangle. */
static void tb_polygon(const dgl_cvtx *v, int n, const dgl_cvtx *prov)
{
    PROF_SCOPE(PROF_D_PROJ);
    tnt2_vertex w[16];
    unsigned oc = 0, out = ~0u;
    double area2 = 0;
    int i, flat = dgl_gl.shade_model == GL_FLAT;
    if (n < 3)
        return;
    for (i = 0; i < n; i++) {
        unsigned g;
        out &= outcodes(&v[i], &g);
        oc |= g;
    }
    if (out) {
        dgl_prims.clipped++;            /* (wholly outside one of the view volume's planes) */
        return;
    }
    if (fogging || n > (mt_on ? 8 : 16) || oc) {
        for (i = 1; i + 1 < n; i++)
            tb_triangle(&v[0], &v[i], &v[i + 1], prov);
        return;
    }
    dgl_prims.tris_in += (unsigned)(n - 2);
    for (i = 0; i < n; i++)
        project(&v[i], &w[i], flat ? prov : &v[i]);
    for (i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        area2 += (double)w[i].sx * w[j].sy - (double)w[j].sx * w[i].sy;
    }
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
        double off = offset_for(&w[0], &w[1], &w[2]);
        if (off != 0)
            for (i = 0; i < n; i++) {
                double z = w[i].sz + off;
                w[i].sz = (float)(z < 0 ? 0 : z > 1 ? 1 : z);
            }
    }
    if (shift_s || shift_t || (mt_on && (shift_s1 || shift_t1)))
        wrap_shift(w, n);
    PROF_SWITCH(PROF_D_SETUP);
    if (mt_on)
        tnt2_poly_mt(w, n);
    else
        tnt2_poly(w, n);
    tris += (unsigned)(n - 2);
    dgl_prims.seqs++;                   /* (polygons sent whole) */
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
    if (mt_on) {
        tnt2_tri_mt(&q[0], &q[1], &q[2]);
        tnt2_tri_mt(&q[0], &q[2], &q[3]);
    } else {
        tnt2_tri(&q[0], &q[1], &q[2]);
        tnt2_tri(&q[0], &q[2], &q[3]);
    }
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
    if (mt_on) {
        tnt2_tri_mt(&q[0], &q[1], &q[2]);
        tnt2_tri_mt(&q[0], &q[2], &q[3]);
    } else {
        tnt2_tri(&q[0], &q[1], &q[2]);
        tnt2_tri(&q[0], &q[2], &q[3]);
    }
    points_++;
}

/* GL_POLYGON whole to tb_polygon (each vertex projected and sent once): the GL core would send it as a fan of
 * triangles, each projecting all three of its vertices (GLQuake's world: six vertices a polygon, projected twelve
 * times). Polygons only: a fan's or strip's triangles are culled one by one. */
static int tb_fast(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    GLint ids[DGL_POLY_MAX];
    GLsizei i;
    if (mode != GL_POLYGON || count < 4 || count > DGL_POLY_MAX)
        return 0;
    for (i = 0; i < count; i++)
        ids[i] = idx ? idx[i] : first + i;
    return dgl_poly_general(ids, (int)count, ids[0]);    /* (a polygon's provoking vertex: its first) */
}

static void install(void)
{
    valid_ok = 0;
    dgl_sink.begin = tb_begin;
    dgl_sink.triangle = tb_triangle;
    dgl_sink.fast = tb_fast;                    /* (polygons whole; the rest the general path) */
    dgl_sink.polygon = tb_polygon;
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
    c->texture_units = 2;                       /* (the multitexture object's two) */
    c->sub_narrow = 1;                          /* (tex_sub writes texels into place by the CPU) */
    c->max_texture_size = 2048;
}

static void synced(void) { dgl_vram_sync_done(); }   /* (the blocks retired before it are free) */

/* A rectangle of a single-level texture (GLQuake's lightmaps, before each surface with -mtex: ~900 a second)
 * written in place once the chip has done every draw queued with it (the layer's fence); anything else is made
 * again whole at its next use (t->dirty). */
static int tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    ttex *tx = (ttex *)t->be;
    const dgl_level *l = &t->level[level];
    const unsigned char *rgba;
    if (!tx || level != 0 || tx->levels != 1 || tx->pal || !DGL_LEVEL_TEXELS(l) || t->dirty || l->w != (1 << tx->lw) ||
        l->h != (1 << tx->lh) || x < 0 || y < 0 || x + w > l->w || y + h > l->h)
        return 0;
    {
        int o = PROF_SWITCH(PROF_D_DRAIN), r = tnt2_mark_wait(tx->mark, 1000000);
        PROF_BACK(o);
        if (r != 0)
            return 0;
    }
    if (!(rgba = dgl_level_rgba_rect(l, 0, x, y, w, h)))
        return 0;
    tnt2_tex_sub_rgba8(tx->off, tx->code, tx->lw, tx->lh, x, y, w, h, rgba, l->w);
    dgl_level_rgba_done();
    tnt2_3d_tex_written();
    dgl_texc.sub_fast++;
    return 1;
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
    valid_ok = 0;
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
    .tex_store = tex_store,
    .tex_change = NULL,
    .stats = stats,
};
