/* backend.c - DOS-GL's drawing back end on the ATI Rage 128 (backend.h; RetroGPU's docs/rage128.md, R4): GL's
 * drawing on the Rage 128 device (hal/src/rg/r128.c) through the layer's 3D context and 3D_RNDR_GEN_PRIM packets
 * (hal/src/r128/tri.c), the CCE fed by the CPU in PIO mode.
 *
 * The chip sets triangles up itself from window-space vertices (x, y, z in [0, 1], 1/w, colours, a fog factor in
 * the specular's alpha, s and t), so the work here is GL's, as on the TNT2 (gl/src/backend/tnt2): clip-space vertices
 * clipped (the shared clipper, a guard band), projected, culled and offset, then sent; GL's state turned into the
 * context's words (the texture environments after Mesa's r128 driver's r128UpdateTextureEnv, blending, the alpha,
 * depth and stencil tests, fog, the colour mask, the scissor); textures converted to the chip's formats and written
 * by the CPU into the device's heap (the Matrox back end's allocator, vram.c), the levels as Mesa lays them out
 * (each after the last, 32-byte aligned; the offset slots from the smallest level: docs/rage128.md, RMIP); clears by
 * the 2D engine.
 *
 * What the chip cannot do is approximated and said once (DGL-WARN): GL_COMBINE_ARB's functions beyond the chip's
 * (its combiner has one function a unit: modulate, add, add signed, blends by the texel's or the vertex's alpha),
 * colour-index textures (expanded through the palette), per-pixel fog (a factor per vertex, as Mesa's driver does),
 * a stencil clear without the depth's. Lines and points are drawn as triangles. */
#include "../backend.h"
#include "../../dgl/dgl.h"
#include "../../gl/gl_draw.h"
#include "../mga/vram.h"
#include "rg/r128_dev.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/prof.h"
#include "mga/fp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GUARD 2.0f                      /* NDC units either side: the clipper's planes beyond the viewport */

/* The texture families GL's environments treat differently (Mesa's r128UpdateTextureEnv's cases). */
enum { FAM_RGBA, FAM_RGB, FAM_ALPHA, FAM_INTENSITY };

/* A texture's copy in VRAM. */
typedef struct {
    uint32_t block;                     /* the heap's block */
    uint32_t lvoff[DGL_MAX_LEVELS];     /* each level's offset, the base first (32-byte aligned) */
    int      levels;                    /* the levels stored (a chain while they halve) */
    int      dt;                        /* R128_TEX_ datatype */
    int      cpp, family, lw, lh;
    unsigned stamp;
    unsigned palgen;                    /* colour-index levels: the palette's generation expanded through */
    const dgl_palette *pal;
} rtex;

static unsigned stamp;
static uint32_t tris, lines_, points_;
static int warned[12];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        DGL_WARN("DGL-WARN r128: %s", what);
    }
}

/* ---- Textures ------------------------------------------------------------- */

static int log2_of(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

/* The chip's format for GL's internal format class (and its ifmt's size, where GL asked for few bits). */
static int chip_format(const dgl_level *l, int *cpp, int *family)
{
    *cpp = 2;
    switch (l->ifc) {
    case DGL_IF_RGB5_A1:
        *family = FAM_RGBA;
        return R128_TEX_ARGB1555;
    case DGL_IF_RGB:
    case DGL_IF_LUMINANCE:
        *family = FAM_RGB;
        if (l->ifmt == GL_RGB8 || l->ifmt == GL_RGB10 || l->ifmt == GL_RGB12 || l->ifmt == GL_RGB16) {
            *cpp = 4;
            return R128_TEX_ARGB8888;
        }
        return R128_TEX_RGB565;
    case DGL_IF_RGBA:
        *family = FAM_RGBA;
        if (l->ifmt == GL_RGBA4 || l->ifmt == GL_RGBA2)
            return R128_TEX_ARGB4444;
        *cpp = 4;
        return R128_TEX_ARGB8888;
    case DGL_IF_ALPHA:
        *family = FAM_ALPHA;
        break;
    case DGL_IF_INTENSITY:
        *family = FAM_INTENSITY;
        break;
    default:                            /* (luminance-alpha: as RGBA) */
        *family = FAM_RGBA;
        break;
    }
    *cpp = 4;
    return R128_TEX_ARGB8888;
}

/* RGBA8 texels to the chip's format, n of them, into VRAM. */
static void put_texels(volatile uint8_t *d, const uint8_t *s, int n, int dt)
{
    int i;
    switch (dt) {
    case R128_TEX_ARGB8888: {
        volatile uint32_t *q = (volatile uint32_t *)d;
        for (i = 0; i < n; i++, s += 4)
            q[i] = (uint32_t)s[3] << 24 | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
        break;
    }
    case R128_TEX_RGB565: {
        volatile uint16_t *q = (volatile uint16_t *)d;
        for (i = 0; i < n; i++, s += 4)
            q[i] = (uint16_t)((s[0] >> 3) << 11 | (s[1] >> 2) << 5 | s[2] >> 3);
        break;
    }
    case R128_TEX_ARGB1555: {
        volatile uint16_t *q = (volatile uint16_t *)d;
        for (i = 0; i < n; i++, s += 4)
            q[i] = (uint16_t)((s[3] >= 128 ? 0x8000u : 0u) | (s[0] >> 3) << 10 | (s[1] >> 3) << 5 | s[2] >> 3);
        break;
    }
    default: {                          /* ARGB4444 */
        volatile uint16_t *q = (volatile uint16_t *)d;
        for (i = 0; i < n; i++, s += 4)
            q[i] = (uint16_t)((s[3] >> 4) << 12 | (s[0] >> 4) << 8 | (s[1] >> 4) << 4 | s[2] >> 4);
        break;
    }
    }
}

static const rtex *cur_tx[2];           /* the copies the state sent draws with */

static void tex_drop(dgl_texture *t)
{
    rtex *x = (rtex *)t->be;
    if (x == cur_tx[0]) cur_tx[0] = NULL;
    if (x == cur_tx[1]) cur_tx[1] = NULL;
    if (x) {
        if (dgl_vram_retire(x->block) != 0) {   /* (drawing queued may still read it: free after the next sync) */
            r128_sync(1000000);
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
            r128_sync(1000000);
            dgl_vram_sync_done();
            continue;
        }
        for (i = 0; i < n; i++)
            if ((u = dgl_texture_nth(i)) && u->be && u != keep &&
                (!old || ((rtex *)u->be)->stamp < ((rtex *)old->be)->stamp))
                old = u;
        if (!old)
            return -1;
        tex_drop(old);
        dgl_texc.evictions++;
    }
    return 0;
}

/* The texture's copy, made or made again when its levels changed. NULL: draw untextured. */
static rtex *prepare(dgl_texture *t)
{
    PROF_SCOPE(PROF_D_TEX);
    rtex *x = (rtex *)t->be;
    const dgl_level *l0 = &t->level[0];
    uint32_t size = 0, o;
    int cpp, family, dt, n, i, lw, lh;
    const dgl_palette *pal = l0->idx ? dgl_palette_for(t) : NULL;
    if (x && !t->dirty && (!pal || (x->pal == pal && x->palgen == pal->gen))) {
        x->stamp = ++stamp;
        return x;
    }
    if (!l0->rgba && !l0->idx)
        return NULL;
    lw = log2_of(l0->w), lh = log2_of(l0->h);
    if ((1 << lw) != l0->w || (1 << lh) != l0->h || lw > 10 || lh > 10) {
        warn_once(2, "a texture larger than 1024 or not a power of two: drawn untextured");
        return NULL;
    }
    if (pal) {                          /* (colour indices: through the palette, as RGBA, ARGB8888) */
        cpp = 4, dt = R128_TEX_ARGB8888, family = pal->opaque ? FAM_RGB : FAM_RGBA;
    } else
        dt = chip_format(l0, &cpp, &family);
    /* The levels after the base, as long as they halve. */
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
    if (n > 11)
        n = 11;
    for (i = 0; i < n; i++)
        size += ((uint32_t)(t->level[i].w * t->level[i].h * cpp) + 31u) & ~31u;
    {
        /* Made again (its levels or palette changed): into a new block, the old one retired until the next sync,
         * so the chip need not drain first. */
        rtex *old = (rtex *)t->be;
        uint32_t oldblock = old ? old->block : 0;
        t->be = NULL;
        if (old == cur_tx[0]) cur_tx[0] = NULL;
        if (old == cur_tx[1]) cur_tx[1] = NULL;
        free(old);
        x = (rtex *)calloc(1, sizeof *x);
        if (!x || room(size + 32u, &x->block, t) != 0) {
            free(x);
            if (old) {
                r128_sync(1000000);
                dgl_vram_free(oldblock);
            }
            warn_once(1, "out of texture memory: a texture drawn untextured");
            return NULL;
        }
        if (old && dgl_vram_retire(oldblock) != 0) {
            r128_sync(1000000);
            dgl_vram_sync_done();
            dgl_vram_free(oldblock);
        }
    }
    o = (x->block + 31u) & ~31u;
    /* The engine idle before the CPU writes VRAM (docs/rage128.md, R4): written while it was busy (a clear's fill),
     * a texture's last 32-byte line was still old when the triangles after it read it (conformance t14), though the
     * CPU read it back right. */
    r128_sync(1000000);
    for (i = 0; i < n; i++) {
        const dgl_level *l = &t->level[i];
        x->lvoff[i] = o;
        if (pal) {
            uint8_t row[1024 * 4];
            int y, k;
            for (y = 0; y < l->h; y++) {
                for (k = 0; k < l->w; k++)
                    memcpy(row + 4 * k, dgl_palette_texel(pal, l->idx[y * l->w + k]), 4);
                put_texels(r128.fb + o + (uint32_t)(y * l->w * cpp), row, l->w, dt);
            }
        } else
            put_texels(r128.fb + o, l->rgba, l->w * l->h, dt);
        o += ((uint32_t)(l->w * l->h * cpp) + 31u) & ~31u;
    }
    r128_vram_written(o - 4u);
    x->levels = n;
    x->dt = dt, x->cpp = cpp, x->family = family, x->lw = lw, x->lh = lh;
    x->stamp = ++stamp;
    x->pal = pal;
    x->palgen = pal ? pal->gen : 0;
    t->be = x;
    t->dirty = 0;
    r128_3d_stale = 1;                  /* (the context sent again: its TEX_CACHE_FLUSH) */
    dgl_texc.uploads++;
    dgl_texc.upload_bytes += size;
    return x;
}

/* ---- State ------------------------------------------------------------------ */

/* GL's comparison functions (GL_NEVER..GL_ALWAYS in order) to the chip's (never, <, <=, =, >=, >, !=, always). */
static uint32_t cmp_of(GLenum f)
{
    static const uint8_t m[8] = { 0, 1, 3, 2, 5, 6, 4, 7 };
    unsigned i = (unsigned)(f - GL_NEVER);
    return i < 8 ? m[i] : 7u;
}

static uint32_t bfactor(GLenum f)
{
    switch (f) {
    case GL_ZERO: return R128_BLEND_ZERO;
    case GL_ONE: return R128_BLEND_ONE;
    case GL_SRC_COLOR: return R128_BLEND_SRCCOLOR;
    case GL_ONE_MINUS_SRC_COLOR: return R128_BLEND_INVSRCCOLOR;
    case GL_SRC_ALPHA: return R128_BLEND_SRCALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return R128_BLEND_INVSRCALPHA;
    case GL_DST_ALPHA: return R128_BLEND_DSTALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return R128_BLEND_INVDSTALPHA;
    case GL_DST_COLOR: return R128_BLEND_DSTCOLOR;
    case GL_ONE_MINUS_DST_COLOR: return R128_BLEND_INVDSTCOLOR;
    case GL_SRC_ALPHA_SATURATE: return R128_BLEND_SAT;
    default: return R128_BLEND_ONE;
    }
}

static uint32_t sop(GLenum o)
{
    switch (o) {
    case GL_ZERO: return 1;
    case GL_REPLACE: return 2;
    case GL_INCR: return 3;
    case GL_DECR: return 4;
    case GL_INVERT: return 5;
    default: return 0;                  /* GL_KEEP */
    }
}

static int fogging;                     /* the fog factor goes in the vertices' specular alpha */
static int valid_ok;                    /* the words in r128_3d are GL's state now (until a dirty bit or a texture change) */
static const dgl_texture *valid_tex[2];
static int two;                         /* the second unit on: s2, t2 sent */
static int shift_s[2], shift_t[2];      /* a repeating texture: coordinates shifted by whole periods per triangle */
static float vx_s, vx_o, vy_s, vy_o, vz_s, vz_o;    /* the viewport's scale and offset, from the last validate */

/* 0..1 to a byte without control-word switches (the TNT2 back end's: one add, an integer clamp). */
static uint32_t ub(float v)
{
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

/* A vertex's two outcodes at once: the view volume's (returned) and the guard band's (*guard), by the signs of the
 * planes' distances read as integers (the TNT2 back end's: no x87 compares). */
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
    c[0] = 0, c[1] = 0, c[2] = rg_r128.width, c[3] = rg_r128.height;
    if (dgl_gl.scissor_test)
        cut(c, dgl_gl.scissor[0], h - (dgl_gl.scissor[1] + dgl_gl.scissor[3]), dgl_gl.scissor[0] + dgl_gl.scissor[2],
            h - dgl_gl.scissor[1]);
    if (draw)
        cut(c, dgl_gl.viewport[0], h - (dgl_gl.viewport[1] + dgl_gl.viewport[3]),
            dgl_gl.viewport[0] + dgl_gl.viewport[2], h - dgl_gl.viewport[1]);
    if (c[2] < c[0]) c[2] = c[0];
    if (c[3] < c[1]) c[3] = c[1];
}

/* The combiner's words (regs_r128.h): a function, the colour factor, the input; the same for alpha. */
#define CC(fn, factor) ((uint32_t)(fn) | (uint32_t)(factor) << R128_COLOR_FACTOR_SHIFT)
#define AC(fn, factor) ((uint32_t)(fn) << R128_COMB_ALPHA_SHIFT | (uint32_t)(factor) << R128_ALPHA_FACTOR_SHIFT)
enum { CF_CONST = 0, CF_NCONST = 1, CF_TEX = 4, CF_NTEX = 5, CF_TEXA = 6, CF_NTEXA = 7 };
enum { AF_TEXA = 6, AF_NTEXA = 7 };
#define C_DIS      CC(R128_COMB_DIS, CF_TEX)                /* C = Ct */
#define C_COPY     CC(R128_COMB_COPY_INP, CF_TEX)           /* C = Cf */
#define C_MOD      CC(R128_COMB_MODULATE, CF_TEX)           /* C = Cf Ct */
#define C_ADD      CC(R128_COMB_ADD, CF_TEX)                /* C = Cf + Ct */
#define C_DECAL    CC(R128_COMB_BLEND_TEXTURE, CF_TEX)      /* C = Cf (1 - At) + Ct At */
#define C_BLEND    (CC(R128_COMB_MODULATE2X, CF_CONST) | R128_COMB_FCN_MSB)   /* C = Cf (1 - Ct) + Cc Ct (Pro) */
#define A_DIS      AC(R128_COMB_DIS, AF_TEXA)               /* A = At */
#define A_COPY     AC(R128_COMB_COPY_INP, AF_TEXA)          /* A = Af */
#define A_MOD      AC(R128_COMB_MODULATE, AF_TEXA)          /* A = Af At */
#define A_ADD      AC(R128_COMB_ADD, AF_TEXA)               /* A = Af + At */

/* GL_COMBINE_ARB's function to one of the chip's (an argument order of texture, then the previous or primary
 * colour; ONE_MINUS operands on the texture by the inverse factor), or a near one, said once. */
static uint32_t combine_fn(GLenum fn, const GLenum *src, const GLenum *op, float scale, int alpha)
{
    int tex0 = src[0] == GL_TEXTURE || src[0] == GL_TEXTURE0_ARB || src[0] == GL_TEXTURE1_ARB;
    int inv = alpha ? op[tex0 ? 0 : 1] == GL_ONE_MINUS_SRC_ALPHA
                    : op[tex0 ? 0 : 1] == GL_ONE_MINUS_SRC_COLOR || op[tex0 ? 0 : 1] == GL_ONE_MINUS_SRC_ALPHA;
    int texa = !alpha && (op[tex0 ? 0 : 1] == GL_SRC_ALPHA || op[tex0 ? 0 : 1] == GL_ONE_MINUS_SRC_ALPHA);
    uint32_t factor = alpha ? (inv ? AF_NTEXA : AF_TEXA) : texa ? (inv ? CF_NTEXA : CF_TEXA) : (inv ? CF_NTEX : CF_TEX);
    uint32_t f;
    switch (fn) {
    case GL_REPLACE:
        f = tex0 ? R128_COMB_DIS : R128_COMB_COPY_INP;
        break;
    case GL_MODULATE:
        f = scale >= 3.0f ? R128_COMB_MODULATE4X : scale >= 1.5f ? R128_COMB_MODULATE2X : R128_COMB_MODULATE;
        break;
    case GL_ADD:
        f = R128_COMB_ADD;
        break;
    case GL_ADD_SIGNED_ARB:
        f = scale >= 1.5f ? R128_COMB_ADD_SIGNED2X : R128_COMB_ADD_SIGNED;
        break;
    case GL_INTERPOLATE_ARB:            /* (by the texel's alpha: DECAL's form; else the vertex's) */
        f = (src[2] == GL_TEXTURE || src[2] == GL_TEXTURE0_ARB || src[2] == GL_TEXTURE1_ARB)
            ? R128_COMB_BLEND_TEXTURE : R128_COMB_BLEND_VERTEX;
        break;
    default:
        warn_once(6, "a GL_COMBINE_ARB function the Rage 128's combiner lacks: drawn as GL_MODULATE");
        f = R128_COMB_MODULATE;
        break;
    }
    if (fn != GL_REPLACE && fn != GL_INTERPOLATE_ARB && !tex0 && !(src[1] == GL_TEXTURE ||
                                                                   src[1] == GL_TEXTURE0_ARB || src[1] == GL_TEXTURE1_ARB))
        warn_once(7, "a GL_COMBINE_ARB function without the texture: drawn with it");
    if (f == R128_COMB_ADD_SIGNED2X && alpha)
        f = R128_COMB_ADD_SIGNED;       /* (no 2x for alpha's add signed in the field's four bits: 14 fits) */
    return alpha ? AC(f, factor) : CC(f, factor);
}

/* A unit's combine word (Mesa's r128UpdateTextureEnv, by the texture's family), with its input: the vertex's
 * colour for unit 0, the first unit's result for unit 1. */
static uint32_t env_words(int unit, const dgl_texenv *e, int family)
{
    uint32_t w = unit ? (8u << R128_INPUT_FACTOR_SHIFT | 4u << R128_INP_FACTOR_A_SHIFT)
                      : (4u << R128_INPUT_FACTOR_SHIFT | 2u << R128_INP_FACTOR_A_SHIFT);
    switch (e->mode) {
    case GL_REPLACE:
        w |= family == FAM_RGB ? C_DIS | A_COPY : family == FAM_ALPHA ? C_COPY | A_DIS : C_DIS | A_DIS;
        break;
    case GL_DECAL:
        w |= family == FAM_RGBA ? C_DECAL | A_COPY : family == FAM_RGB ? C_DIS | A_COPY : C_COPY | A_COPY;
        break;
    case GL_ADD:
        w |= family == FAM_RGB ? C_ADD | A_COPY : family == FAM_ALPHA ? C_COPY | A_MOD
           : family == FAM_INTENSITY ? C_ADD | A_ADD : C_ADD | A_MOD;
        break;
    case GL_BLEND:
        if (family == FAM_INTENSITY)
            warn_once(8, "GL_BLEND with an intensity texture: its alpha modulated, not blended");
        w |= family == FAM_RGB ? C_BLEND | A_COPY : family == FAM_ALPHA ? C_COPY | A_MOD : C_BLEND | A_MOD;
        break;
    case GL_COMBINE_ARB:
        w |= combine_fn(e->combine_rgb, e->src_rgb, e->op_rgb, e->rgb_scale, 0) |
             combine_fn(e->combine_alpha, e->src_alpha, e->op_alpha, e->alpha_scale, 1);
        break;
    default:                            /* GL_MODULATE */
        w |= family == FAM_RGB ? C_MOD | A_COPY : family == FAM_ALPHA ? C_COPY | A_MOD : C_MOD | A_MOD;
        break;
    }
    return w;
}

/* A unit's texture words: its control (format, filters, wrap) and size, its offset slots (the smallest level in
 * slot 0); NULL for no texture. */
static rtex *unit_words(dgl_texture *t, uint32_t *cntl, uint32_t *size_pitch, uint32_t *slots, int unit)
{
    rtex *x = t ? prepare(t) : NULL;
    int mip, i, n, small;
    uint32_t min, mag;
    shift_s[unit] = shift_t[unit] = 0;
    if (!x)
        return NULL;
    mip = t->min_filter != GL_NEAREST && t->min_filter != GL_LINEAR && x->levels > 1;
    mag = t->mag_filter == GL_LINEAR ? 1u : 0u;
    switch (t->min_filter) {
    case GL_LINEAR: min = 1; break;
    case GL_NEAREST_MIPMAP_NEAREST: min = mip ? 2u : 0u; break;
    case GL_NEAREST_MIPMAP_LINEAR: min = mip ? 3u : 0u; break;
    case GL_LINEAR_MIPMAP_NEAREST: min = mip ? 4u : 1u; break;
    case GL_LINEAR_MIPMAP_LINEAR: min = mip ? 5u : 1u; break;
    default: min = 0; break;
    }
    n = mip ? x->levels : 1;
    *cntl = (uint32_t)x->dt << R128_TEX_DATATYPE_SHIFT | min << R128_MIN_BLEND_SHIFT | mag << R128_MAG_BLEND_SHIFT |
            (mip ? 0u : R128_MIP_MAP_DISABLE) |
            (t->wrap_s == GL_REPEAT ? 0u : 2u) << R128_TEX_CLAMP_S_SHIFT |    /* (GL_CLAMP as clamp to the edge) */
            (t->wrap_t == GL_REPEAT ? 0u : 2u) << R128_TEX_CLAMP_T_SHIFT;
    {
        int big = x->lw > x->lh ? x->lw : x->lh, lastw = x->lw - (n - 1), lasth = x->lh - (n - 1);
        small = (lastw > lasth ? lastw : lasth);
        if (small < 0)
            small = 0;
        *size_pitch = (uint32_t)x->lw | (uint32_t)big << 4 | (uint32_t)x->lh << 8 | (uint32_t)small << 12;
    }
    for (i = 0; i < 11; i++)
        slots[i] = mip && i < n ? x->lvoff[n - 1 - i] : x->lvoff[0];
    shift_s[unit] = t->wrap_s == GL_REPEAT, shift_t[unit] = t->wrap_t == GL_REPEAT;
    return x;
}

static void validate(void)
{
    dgl_texture *t0 = dgl_gl.texture_2d ? dgl_unit_texture(0) : NULL;
    dgl_texture *t1 = dgl_gl.texture_2d1 ? dgl_unit_texture(1) : NULL;
    r128_ctx3d *c = &r128_3d;
    rtex *x0, *x1;
    uint32_t sp0 = 0, sp1 = 0, cntl, tc, misc, zs;
    int k[4], bytes = rg_r128.bpp == 32 ? 4 : 2;
    /* Nothing GL draws with changed since: the words still hold (the layer sends them again itself after 2D). */
    if (valid_ok && t0 == valid_tex[0] && t1 == valid_tex[1] && !(t0 && (t0->dirty || t0->level[0].idx)) &&
        !(t1 && (t1->dirty || t1->level[0].idx)) &&
        !(dgl_gl.dirty & (DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP)))
        return;
    vx_s = dgl_gl.viewport[2] * 0.5f, vx_o = dgl_gl.viewport[0] + dgl_gl.viewport[2] * 0.5f;
    vy_s = -dgl_gl.viewport[3] * 0.5f, vy_o = dgl_ctx.height - (dgl_gl.viewport[1] + dgl_gl.viewport[3] * 0.5f);
    vz_s = (float)((dgl_gl.depth_far - dgl_gl.depth_near) * 0.5);
    vz_o = (float)(dgl_gl.depth_near + (dgl_gl.depth_far - dgl_gl.depth_near) * 0.5);
    c->dst_pitch_offset_c = (uint32_t)(rg_r128.pitch_bytes / bytes / 8) << 21 |
                            rg_r128_color_off(dgl_ctx.draw_front) >> 5;
    scissor(1, k);
    if (k[2] <= k[0] || k[3] <= k[1])       /* (nothing: a one-pixel box off the screen) */
        k[0] = k[2] = 0x3FFF, k[1] = k[3] = 0x3FFF;
    c->sc_top_left_c = (uint32_t)k[1] << 16 | (uint32_t)k[0];
    c->sc_bottom_right_c = (uint32_t)(k[3] - 1) << 16 | (uint32_t)(k[2] - 1);

    /* Textures: unit 0 (and 1); a unit without a texture passes its input on. */
    x0 = unit_words(t0, &cntl, &sp0, c->prim_tex_offset_c, 0);
    if (x0) {
        c->prim_tex_cntl_c = cntl;
        c->prim_texture_combine_cntl_c = env_words(0, dgl_tex_env(0), x0->family);
    }
    x1 = unit_words(t1, &cntl, &sp1, c->sec_tex_offset_c, 1);
    if (x1) {
        c->sec_tex_cntl_c = cntl | R128_SEC_SELECT_SEC_ST;
        c->sec_tex_combine_cntl_c = env_words(1, dgl_tex_env(1), x1->family);
        if (!x0) {                      /* (only the second: it alone, on the vertex's colour, from unit 0's slot) */
            c->prim_tex_cntl_c = cntl;
            memcpy(c->prim_tex_offset_c, c->sec_tex_offset_c, sizeof c->prim_tex_offset_c);
            c->prim_texture_combine_cntl_c = env_words(0, dgl_tex_env(1), x1->family);
            sp0 = sp1, x0 = x1, x1 = NULL;
            shift_s[0] = shift_s[1], shift_t[0] = shift_t[1];
            warn_once(9, "texture unit 1 alone: drawn with its coordinates as unit 0's");
        }
    }
    cur_tx[0] = x0, cur_tx[1] = x1;
    two = x1 != NULL;
    c->tex_size_pitch_c = sp0 | sp1 << 16;
    {
        /* (one constant colour for both units: the one that reads it; unit 0's when both do) */
        const dgl_texenv *e = dgl_tex_env(x1 && (dgl_tex_env(1)->mode == GL_BLEND || dgl_tex_env(1)->mode == GL_COMBINE_ARB) &&
                                          !(x0 && (dgl_tex_env(0)->mode == GL_BLEND || dgl_tex_env(0)->mode == GL_COMBINE_ARB)) ? 1 : 0);
        c->constant_color_c = ub(e->color[3]) << 24 | ub(e->color[0]) << 16 | ub(e->color[1]) << 8 | ub(e->color[2]);
    }
    if (two)
        c->scale_3d_cntl |= R128_SCALE_TEX_CACHE_SPLIT;
    else
        c->scale_3d_cntl &= ~R128_SCALE_TEX_CACHE_SPLIT;

    /* The enables, the tests, the blend. */
    tc = R128_TEX_CACHE_FLUSH | 0x3Fu << R128_LOD_BIAS_SHIFT;
    if (x0)
        tc |= R128_TEXMAP_ENABLE;
    if (two)
        tc |= R128_SEC_TEXMAP_ENABLE;
    fogging = dgl_gl.fog;
    if (fogging)
        tc |= R128_FOG_ENABLE;
    if (dgl_gl.dither)
        tc |= R128_DITHER_ENABLE;
    misc = R128_MISC_SCALE_3D_TEXMAP_SHADE | R128_MISC_SCALE_PIX_REPLICATE;
    if (dgl_gl.blend) {
        tc |= R128_ALPHA_ENABLE;
        misc |= bfactor(dgl_gl.blend_src) << R128_ALPHA_BLEND_SRC_SHIFT | bfactor(dgl_gl.blend_dst) << R128_ALPHA_BLEND_DST_SHIFT;
    } else
        misc |= (uint32_t)R128_BLEND_ONE << R128_ALPHA_BLEND_SRC_SHIFT | (uint32_t)R128_BLEND_ZERO << R128_ALPHA_BLEND_DST_SHIFT;
    if (dgl_gl.alpha_test) {
        float rf = dgl_gl.alpha_ref * 255.0f + 0.5f;
        int ref = rf < 0.0f ? 0 : rf > 255.0f ? 255 : mga_ifloor(rf);
        tc |= R128_ALPHA_TEST_ENABLE;
        misc |= cmp_of(dgl_gl.alpha_func) << R128_ALPHA_TEST_SHIFT | (uint32_t)ref;
    } else
        misc |= 7u << R128_ALPHA_TEST_SHIFT;
    zs = c->z_sten_cntl_c & 6u;                 /* (the depth's width) */
    if (dgl_gl.depth_test && rg_r128.z_off) {
        tc |= R128_Z_ENABLE | (dgl_gl.depth_mask ? R128_Z_WRITE_ENABLE : 0u);
        zs |= cmp_of(dgl_gl.depth_func) << R128_Z_TEST_SHIFT;
    } else
        zs |= 7u << R128_Z_TEST_SHIFT;
    if (dgl_gl.stencil_test && rg_r128.depth_bits == 24) {
        tc |= R128_STENCIL_ENABLE;
        zs |= cmp_of(dgl_gl.stencil_func) << R128_STENCIL_TEST_SHIFT | sop(dgl_gl.stencil_fail) << R128_STENCIL_S_FAIL_SHIFT |
              sop(dgl_gl.stencil_zpass) << R128_STENCIL_ZPASS_SHIFT | sop(dgl_gl.stencil_zfail) << R128_STENCIL_ZFAIL_SHIFT;
        c->sten_ref_mask_c = ((uint32_t)dgl_gl.stencil_ref & 255u) | (dgl_gl.stencil_mask & 255u) << R128_STEN_MASK_SHIFT |
                             (dgl_gl.stencil_writemask & 255u) << R128_STEN_WRITE_MASK_SHIFT;
    } else {
        zs |= 7u << R128_STENCIL_TEST_SHIFT;
        if (dgl_gl.stencil_test)
            warn_once(5, "the stencil test needs a 32-bit context (Z24S8): not drawn at 16 bits");
    }
    c->tex_cntl_c = tc;
    c->misc_3d_state_cntl_reg = misc;
    c->z_sten_cntl_c = zs;
    c->fog_color_c = ub(dgl_gl.fog_color[0]) << 16 | ub(dgl_gl.fog_color[1]) << 8 | ub(dgl_gl.fog_color[2]);
    if (rg_r128.bpp == 32)
        c->plane_3d_mask_c = (dgl_gl.color_mask[3] ? 0xFF000000u : 0) | (dgl_gl.color_mask[0] ? 0xFF0000u : 0) |
                             (dgl_gl.color_mask[1] ? 0xFF00u : 0) | (dgl_gl.color_mask[2] ? 0xFFu : 0);
    else
        c->plane_3d_mask_c = (dgl_gl.color_mask[0] ? 0xF800u : 0) | (dgl_gl.color_mask[1] ? 0x07E0u : 0) |
                             (dgl_gl.color_mask[2] ? 0x001Fu : 0);
    r128_3d_format(R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB | R128_VC_FRMT_SPEC_FRGB | R128_VC_FRMT_S_T |
                   (two ? R128_VC_FRMT_S2_T2 : 0));
    r128_3d_stale = 1;                          /* (sent at the next triangle) */
    dgl_gl.dirty &= ~(DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP);
    valid_ok = 1;
    valid_tex[0] = t0, valid_tex[1] = t1;
}

/* ---- Drawing ---------------------------------------------------------------- */

static int rb_begin(void)
{
    if (!dgl_ctx.active || !rg_r128.active)
        return 0;
    validate();
    return 1;
}

static void rb_end(void) { }

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

static void project(const dgl_cvtx *c, r128_vertex *v, const dgl_cvtx *colour)
{
    float iw = 1.0f / c->w;
    v->x = c->x * iw * vx_s + vx_o;
    v->y = c->y * iw * vy_s + vy_o;
    v->z = c->z * iw * vz_s + vz_o;
    v->rhw = iw;
    v->argb = colour_of(colour);
    v->spec = (fogging ? ub(fog_factor(c->eye_d)) : 255u) << 24;
    v->s = c->s, v->t = c->t;
    v->s2 = c->s1, v->t2 = c->t1;
}

static double offset_for(const r128_vertex *p0, const r128_vertex *p1, const r128_vertex *p2)
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
    return dgl_gl.offset_factor * m + dgl_gl.offset_units / (rg_r128.depth_bits == 24 ? 16777215.0 : 65535.0);
}

/* By whole periods so that the smallest coordinate is in [0, 1): the chip's interpolation is a float's, so huge
 * coordinates (conformance t14: 30,000 periods along) lose their fraction. */
static void wrap_shift(r128_vertex *v, int n)
{
    int i;
    if (shift_s[0] || shift_t[0]) {
        float ms = v[0].s, mt = v[0].t;
        for (i = 1; i < n; i++) {
            if (v[i].s < ms) ms = v[i].s;
            if (v[i].t < mt) mt = v[i].t;
        }
        ms = shift_s[0] && ms > -1e9f && ms < 1e9f ? (float)mga_ifloor(ms) : 0;
        mt = shift_t[0] && mt > -1e9f && mt < 1e9f ? (float)mga_ifloor(mt) : 0;
        if (ms != 0 || mt != 0)
            for (i = 0; i < n; i++)
                v[i].s -= ms, v[i].t -= mt;
    }
    if (two && (shift_s[1] || shift_t[1])) {
        float ms = v[0].s2, mt = v[0].t2;
        for (i = 1; i < n; i++) {
            if (v[i].s2 < ms) ms = v[i].s2;
            if (v[i].t2 < mt) mt = v[i].t2;
        }
        ms = shift_s[1] && ms > -1e9f && ms < 1e9f ? (float)mga_ifloor(ms) : 0;
        mt = shift_t[1] && mt > -1e9f && mt < 1e9f ? (float)mga_ifloor(mt) : 0;
        if (ms != 0 || mt != 0)
            for (i = 0; i < n; i++)
                v[i].s2 -= ms, v[i].t2 -= mt;
    }
}

/* Culling and the polygon offset on the window vertices: 0 when culled or of no area. */
static int finish(r128_vertex *v, int n)
{
    double area2 = 0;
    int i;
    for (i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        area2 += (double)v[i].x * v[j].y - (double)v[j].x * v[i].y;
    }
    if (area2 == 0) {
        dgl_prims.zero_area++;
        return 0;
    }
    if (dgl_gl.cull_face) {
        int front = (dgl_gl.front_face == GL_CCW) ? area2 < 0 : area2 > 0;
        if (dgl_gl.cull_mode == GL_FRONT_AND_BACK || (dgl_gl.cull_mode == GL_BACK) != front) {
            dgl_prims.culled++;
            return 0;
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
    if (shift_s[0] || shift_t[0] || (two && (shift_s[1] || shift_t[1])))
        wrap_shift(v, n);
    return 1;
}

static void draw_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    PROF_SCOPE(PROF_D_PROJ);
    dgl_cvtx in[3], out[9];
    r128_vertex v[9];
    int n, i, flat = dgl_gl.shade_model == GL_FLAT;
    unsigned ga, gb, gc;
    dgl_prims.tris_in++;
    if (outcodes(a, &ga) & outcodes(b, &gb) & outcodes(c, &gc)) {
        dgl_prims.clipped++;
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
    if (!finish(v, n))
        return;
    PROF_SWITCH(PROF_D_SETUP);
    if (n == 3)
        r128_tri(&v[0], &v[1], &v[2]);
    else
        r128_poly(v, n);
    tris += (unsigned)(n - 2);
}

static void mid(dgl_cvtx *o, const dgl_cvtx *a, const dgl_cvtx *b)
{
#define M(f) o->f = (a->f + b->f) * 0.5f
    M(x); M(y); M(z); M(w); M(r); M(g); M(b); M(a); M(s); M(t); M(s1); M(t1); M(eye_d);
#undef M
}

/* Fog: the chip interpolates a factor per vertex; a triangle whose factor changes by more than a tenth across it
 * is split in four in clip space (up to three times), as the TNT2 back end does (conformance t10). */
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

static void rb_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    if (fogging)
        fog_split(a, b, c, prov, 0);
    else
        draw_triangle(a, b, c, prov);
}

/* A polygon wholly inside the guard band, unfogged, of up to 16 vertices: each vertex projected once and the whole
 * sent as one triangle fan (r128_poly); else triangle by triangle. */
static void rb_polygon(const dgl_cvtx *v, int n, const dgl_cvtx *prov)
{
    PROF_SCOPE(PROF_D_PROJ);
    r128_vertex w[16];
    unsigned oc = 0, out = ~0u;
    int i, flat = dgl_gl.shade_model == GL_FLAT;
    if (n < 3)
        return;
    for (i = 0; i < n; i++) {
        unsigned g;
        out &= outcodes(&v[i], &g);
        oc |= g;
    }
    if (out) {
        dgl_prims.clipped++;
        return;
    }
    if (fogging || n > 16 || oc) {
        for (i = 1; i + 1 < n; i++)
            rb_triangle(&v[0], &v[i], &v[i + 1], prov);
        return;
    }
    dgl_prims.tris_in += (unsigned)(n - 2);
    for (i = 0; i < n; i++)
        project(&v[i], &w[i], flat ? prov : &v[i]);
    if (!finish(w, n))
        return;
    PROF_SWITCH(PROF_D_SETUP);
    r128_poly(w, n);
    tris += (unsigned)(n - 2);
    dgl_prims.seqs++;
}

/* A line: a quad half the width either side, across its minor axis (OpenGL's wide lines' shape), b's colour when
 * flat. */
static void rb_line(const dgl_cvtx *a, const dgl_cvtx *b)
{
    dgl_cvtx in[3], out[9];
    r128_vertex p[2], q[4];
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
    if (fabsf(p[1].x - p[0].x) >= fabsf(p[1].y - p[0].y)) {    /* x-major: widen in y */
        q[0].y -= hw, q[1].y += hw, q[2].y += hw, q[3].y -= hw;
    } else {
        q[0].x -= hw, q[1].x += hw, q[2].x += hw, q[3].x -= hw;
    }
    r128_poly(q, 4);
    lines_++;
}

/* A point: a square of the point size about its centre; none when the centre is outside the view volume. */
static void rb_point(const dgl_cvtx *a)
{
    r128_vertex p, q[4];
    float h = (dgl_gl.point_size > 1.0f ? dgl_gl.point_size : 1.0f) * 0.5f;
    int i;
    if (dgl_outcode(a, 1.0f, 1.0f))
        return;
    project(a, &p, a);
    for (i = 0; i < 4; i++)
        q[i] = p;
    q[0].x -= h, q[0].y -= h;
    q[1].x += h, q[1].y -= h;
    q[2].x += h, q[2].y += h;
    q[3].x -= h, q[3].y += h;
    r128_poly(q, 4);
    points_++;
}

/* GL_POLYGON whole to rb_polygon (each vertex projected and sent once), as the TNT2 back end's. */
static int rb_fast(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    GLint ids[DGL_POLY_MAX];
    GLsizei i;
    if (mode != GL_POLYGON || count < 4 || count > DGL_POLY_MAX)
        return 0;
    for (i = 0; i < count; i++)
        ids[i] = idx ? idx[i] : first + i;
    return dgl_poly_general(ids, (int)count, ids[0]);
}

static void install(void)
{
    valid_ok = 0;
    col_set = 0;
    r128_3d_init(rg_r128_color_off(0), rg_r128.pitch_bytes, rg_r128.bpp, rg_r128.z_off,
                 rg_r128.z_pitch_bytes ? rg_r128.z_pitch_bytes : rg_r128.pitch_bytes,
                 rg_r128.depth_bits ? rg_r128.depth_bits : 16);
    dgl_sink.begin = rb_begin;
    dgl_sink.triangle = rb_triangle;
    dgl_sink.fast = rb_fast;
    dgl_sink.polygon = rb_polygon;
    dgl_sink.line = rb_line;
    dgl_sink.point = rb_point;
    dgl_sink.end = rb_end;
}

/* Clears by the 2D engine, in the scissor: colour, and the depth buffer (with the stencil's byte in Z24S8). */
static void clear(GLbitfield mask)
{
    int c[4];
    if (!dgl_ctx.active || !rg_r128.active)
        return;
    scissor(0, c);
    if (mask & GL_COLOR_BUFFER_BIT) {
        const GLfloat *k = dgl_gl.clear_color;
        uint32_t v = rg_r128.bpp == 32
                   ? ub(k[3]) << 24 | ub(k[0]) << 16 | ub(k[1]) << 8 | ub(k[2])
                   : (ub(k[0]) >> 3) << 11 | (ub(k[1]) >> 2) << 5 | (ub(k[2]) >> 3);
        if (!dgl_gl.color_mask[0] || !dgl_gl.color_mask[1] || !dgl_gl.color_mask[2])
            warn_once(3, "a colour clear with a colour mask clears every channel");
        r128_set_target(rg_r128_color_off(dgl_ctx.draw_front), rg_r128.pitch_bytes, rg_r128.bpp);
        r128_set_clip(c[0], c[1], c[2], c[3]);
        r128_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
    }
    if (rg_r128.z_off && (((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask) || (mask & GL_STENCIL_BUFFER_BIT))) {
        double d = dgl_gl.clear_depth < 0 ? 0 : dgl_gl.clear_depth > 1 ? 1 : dgl_gl.clear_depth;
        uint32_t v = rg_r128.depth_bits == 24
                   ? (uint32_t)(d * 16777215.0 + 0.5) | ((unsigned)dgl_gl.clear_stencil & 255u) << 24
                   : (uint32_t)(d * 65535.0 + 0.5);
        if (!((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask))
            warn_once(4, "a stencil clear clears the depth buffer too");
        r128_set_target(rg_r128.z_off, rg_r128.z_pitch_bytes, rg_r128.depth_bits == 24 ? 32 : 16);
        r128_set_clip(c[0], c[1], c[2], c[3]);
        r128_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
    }
    r128_cce_flush();
}

static void caps(dgl_caps *c)
{
    memset(c, 0, sizeof *c);
    c->renderer = r128.device_id >= 0x5040 && r128.device_id <= 0x507F ? "ATI Rage 128 Pro" : "ATI Rage 128";
    c->texture_units = 2;
    c->sub_narrow = 1;                          /* (tex_sub writes texels into place by the CPU) */
    c->max_texture_size = 1024;
}

static void synced(void) { dgl_vram_sync_done(); }   /* (the blocks retired before it are free) */

/* A rectangle of a single-level texture (GLQuake's lightmaps) written in place once the engine is idle (in PIO
 * mode the chip is never more than its FIFO behind: a short wait); anything else is made again whole at its next
 * use (t->dirty). */
static int tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    rtex *tx = (rtex *)t->be;
    const dgl_level *l = &t->level[level];
    int j;
    if (!tx || level != 0 || tx->levels != 1 || tx->pal || !l->rgba || t->dirty || l->w != (1 << tx->lw) ||
        l->h != (1 << tx->lh) || x < 0 || y < 0 || x + w > l->w || y + h > l->h)
        return 0;
    {
        int o = PROF_SWITCH(PROF_D_DRAIN), r = r128_sync(1000000);
        PROF_BACK(o);
        if (r != 0)
            return 0;
    }
    for (j = 0; j < h; j++)
        put_texels(r128.fb + tx->lvoff[0] + (uint32_t)(((y + j) * l->w + x) * tx->cpp),
                   l->rgba + ((y + j) * l->w + x) * 4, w, tx->dt);
    r128_vram_written(tx->lvoff[0] + (uint32_t)(((y + h - 1) * l->w + x + w - 1) * tx->cpp));
    r128_3d_stale = 1;                          /* (the texture cache flushed with the context) */
    dgl_texc.sub_fast++;
    dgl_texc.sub_sync++;
    return 1;
}

static void tex_free(dgl_texture *t) { tex_drop(t); }

static int tex_info(const dgl_texture *t, int rgba[4])
{
    const rtex *x = (const rtex *)t->be;
    if (!x)
        return 0;
    if (x->dt == R128_TEX_RGB565) rgba[0] = 5, rgba[1] = 6, rgba[2] = 5, rgba[3] = 0;
    else if (x->dt == R128_TEX_ARGB1555) rgba[0] = rgba[1] = rgba[2] = 5, rgba[3] = 1;
    else if (x->dt == R128_TEX_ARGB4444) rgba[0] = rgba[1] = rgba[2] = rgba[3] = 4;
    else rgba[0] = rgba[1] = rgba[2] = rgba[3] = 8;
    return 1;
}

static void tex_reset(uint32_t start, uint32_t end)
{
    int i, n = dgl_texture_count();
    dgl_texture *t;
    valid_ok = 0;
    cur_tx[0] = cur_tx[1] = NULL;
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

const dgl_backend dgl_r128_backend = {
    .name = "r128",
    .dev = &rg_r128_device,
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
