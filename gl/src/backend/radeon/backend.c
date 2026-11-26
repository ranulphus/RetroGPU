/* backend.c - DOS-GL's drawing back end on the ATI Radeon (backend.h; RetroGPU's docs/radeon.md, RD5): GL's drawing
 * on the Radeon device (hal/src/rg/radeon.c: the R100 family) through the layer's 3D state and primitives
 * (hal/src/radeon/tri.c), by the command processor's ring when it runs, else by plain register writes. Made from the
 * Rage 128's back end (gl/src/backend/r128).
 *
 * The chip sets triangles up itself from window-space vertices (x, y, z in [0, 1], 1/w, colours, a fog factor in
 * the specular's alpha, s and t for each unit), so the work here is GL's: clip-space vertices clipped (the shared
 * clipper, a guard band), projected, culled and offset, then sent; GL's state turned into the chip's registers (the
 * texture environments on its combiner, A x B + C a unit with complements, add signed, subtract, blend and a scale
 * of 2 or 4, after Mesa's radeon driver's radeonUpdateTextureEnv: every GL_COMBINE_ARB function has its own form;
 * blending, the alpha, depth and stencil tests, fog, the colour mask, the scissor); textures converted to the chip's
 * formats and sent through the command stream into the device's heap (the Matrox back end's allocator, vram.c),
 * the levels as the chip wants them (each after the last, 32-byte aligned, a narrow level's rows padded to 32
 * bytes: docs/radeon.md, DTRI); clears by the 2D engine.
 *
 * What the chip cannot do is approximated and said once (DGL-WARN): colour-index textures (expanded through the
 * palette), per-pixel fog (a factor per vertex, as Mesa's driver does), a stencil clear without the depth's. Lines
 * and points are drawn as triangles. */
#include "../backend.h"
#include "../../dgl/dgl.h"
#include "../../gl/gl_draw.h"
#include "../mga/vram.h"
#include "rg/radeon_dev.h"
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "radeon/regs3d_radeon.h"
#include "mga/prof.h"
#include "mga/fp.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GUARD 2.0f                      /* NDC units either side: the clipper's planes beyond the viewport */

/* The chip's texture formats used (PP_TXFORMAT's codes). */
enum { TX_1555 = 3, TX_565 = 4, TX_4444 = 5, TX_8888 = 6 };

/* The texture families GL's environments treat differently. */
enum { FAM_RGBA, FAM_RGB, FAM_ALPHA, FAM_INTENSITY };

/* A texture's copy in VRAM. */
typedef struct {
    uint32_t block;                     /* the heap's block */
    uint32_t lvoff[DGL_MAX_LEVELS];     /* each level's offset, the base first (32-byte aligned) */
    int      levels;                    /* the levels stored (a chain while they halve) */
    int      dt;                        /* TX_ format */
    int      cpp, family, lw, lh;
    unsigned stamp;
    unsigned palgen;                    /* colour-index levels: the palette's generation expanded through */
    const dgl_palette *pal;
    int      cube;                      /* a cube map: one level, lvoff[0..5] its faces (+X .. -Z) */
} rtex;

static unsigned stamp;
static uint32_t tris, lines_, points_;
static void trace_ring(const char *what, unsigned long a, unsigned long b, unsigned long c, unsigned long d);
static int warned[12];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        DGL_WARN("DGL-WARN radeon: %s", what);
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
        return TX_1555;
    case DGL_IF_RGB:
    case DGL_IF_LUMINANCE:
        *family = FAM_RGB;
        if (l->ifmt == GL_RGB8 || l->ifmt == GL_RGB10 || l->ifmt == GL_RGB12 || l->ifmt == GL_RGB16) {
            *cpp = 4;
            return TX_8888;
        }
        return TX_565;
    case DGL_IF_RGBA:
        *family = FAM_RGBA;
        if (l->ifmt == GL_RGBA4 || l->ifmt == GL_RGBA2)
            return TX_4444;
        *cpp = 4;
        return TX_8888;
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
    return TX_8888;
}

/* RGBA8 texels to the chip's format, n of them, into VRAM. */
static void put_texels(volatile uint8_t *d, const uint8_t *s, int n, int dt)
{
    int i;
    switch (dt) {
    case TX_8888: {
        volatile uint32_t *q = (volatile uint32_t *)d;
        for (i = 0; i < n; i++, s += 4)
            q[i] = (uint32_t)s[3] << 24 | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
        break;
    }
    case TX_565: {
        volatile uint16_t *q = (volatile uint16_t *)d;
        for (i = 0; i < n; i++, s += 4)
            q[i] = (uint16_t)((s[0] >> 3) << 11 | (s[1] >> 2) << 5 | s[2] >> 3);
        break;
    }
    case TX_1555: {
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

/* A level's bytes in video memory: its rows padded to 32 bytes (a level narrower than that), 32-byte aligned. */
static uint32_t row_bytes(const dgl_level *l, int cpp) { return ((uint32_t)(l->w * cpp) + 31u) & ~31u; }
static uint32_t level_bytes(const dgl_level *l, int cpp) { return row_bytes(l, cpp) * (uint32_t)l->h; }

/* Rows y0 to y1 of a level, converted, to its place at off: through the command stream when the command processor
 * runs (radeon_upload: ordered with the drawing, no sync), else written by the CPU with the engine idle (the
 * caller flushes the chip's caches after the last). */
static void send_rows(const dgl_texture *t, const dgl_level *l, const dgl_palette *pal, uint32_t off, int y0, int y1,
                      int dt, int cpp)
{
    static uint32_t up[16384];          /* 64 KB */
    uint32_t stride = row_bytes(l, cpp);
    int per = (int)(sizeof up / stride), y, k, stream = radeon_cp_running();
    (void)t;
    if (!stream)
        radeon_sync(1000000);
    for (y = y0; y < y1; y += per) {
        int rows = y1 - y < per ? y1 - y : per, j;
        if ((uint32_t)(l->w * cpp) != stride)
            memset(up, 0, stride * (uint32_t)rows);
        for (j = 0; j < rows; j++) {
            volatile uint8_t *d = (volatile uint8_t *)up + (uint32_t)j * stride;
            if (pal) {
                uint8_t row[2048 * 4];
                for (k = 0; k < l->w; k++)
                    memcpy(row + 4 * k, dgl_palette_texel(pal, l->idx[(y + j) * l->w + k]), 4);
                put_texels(d, row, l->w, dt);
            } else
                put_texels(d, l->rgba + (y + j) * l->w * 4, l->w, dt);
        }
        trace_ring("upload", (unsigned long)(off + (uint32_t)y * stride), (unsigned long)(stride * (uint32_t)rows),
                   (unsigned long)stream, 0);
        if (!stream || radeon_upload(off + (uint32_t)y * stride, up, stride * (uint32_t)rows) != 0) {
            volatile uint32_t *v = (volatile uint32_t *)(radeon.fb + off + (uint32_t)y * stride);
            uint32_t n = stride * (uint32_t)rows / 4u, i;
            if (stream)
                radeon_sync(1000000);
            for (i = 0; i < n; i++)
                v[i] = up[i];
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
            radeon_sync(1000000);
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
            radeon_sync(1000000);
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
MGA_HOT static rtex *prepare(dgl_texture *t)
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
    if ((1 << lw) != l0->w || (1 << lh) != l0->h || lw > 11 || lh > 11) {
        warn_once(2, "a texture larger than 2048 or not a power of two: drawn untextured");
        return NULL;
    }
    if (pal) {                          /* (colour indices: through the palette, as RGBA, ARGB8888) */
        cpp = 4, dt = TX_8888, family = pal->opaque ? FAM_RGB : FAM_RGBA;
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
    if (n > 12)
        n = 12;
    if (t->target == GL_TEXTURE_CUBE_MAP_ARB) {
        /* A cube map: its six faces' base levels, one after the other (Mesa's radeonSetTexImages: the R100 family
         * can't mipmap a cube map). */
        n = 1;
        size = 6u * ((level_bytes(l0, cpp) + 31u) & ~31u);
    } else
        for (i = 0; i < n; i++)
            size += level_bytes(&t->level[i], cpp);
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
                radeon_sync(1000000);
                dgl_vram_free(oldblock);
            }
            warn_once(1, "out of texture memory: a texture drawn untextured");
            return NULL;
        }
        if (old && dgl_vram_retire(oldblock) != 0) {
            radeon_sync(1000000);
            dgl_vram_sync_done();
            dgl_vram_free(oldblock);
        }
    }
    o = (x->block + 31u) & ~31u;
    if (t->target == GL_TEXTURE_CUBE_MAP_ARB) {
        for (i = 0; i < 6; i++) {
            const dgl_level *f = DGL_FACE_LEVEL(t, i, 0);
            x->lvoff[i] = o;
            send_rows(t, f, NULL, o, 0, f->h, dt, cpp);
            o += (level_bytes(f, cpp) + 31u) & ~31u;
        }
        x->cube = 1;
    } else
        for (i = 0; i < n; i++) {
            x->lvoff[i] = o;
            send_rows(t, &t->level[i], pal, o, 0, t->level[i].h, dt, cpp);
            o += level_bytes(&t->level[i], cpp);
        }
    if (!radeon_cp_running())
        radeon_vram_written(o - 4u);
    radeon_3d_tex_written();
    x->levels = n;
    x->dt = dt, x->cpp = cpp, x->family = family, x->lw = lw, x->lh = lh;
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

/* GL's comparison functions (GL_NEVER..GL_ALWAYS in order) to the chip's (never, <, <=, =, >=, >, !=, always). */
static uint32_t cmp_of(GLenum f)
{
    static const uint8_t m[8] = { 0, 1, 3, 2, 5, 6, 4, 7 };
    unsigned i = (unsigned)(f - GL_NEVER);
    return i < 8 ? m[i] : 7u;
}

/* A blend factor's code (RB3D_BLENDCNTL's GL_ numbering: 32 on), for the source or destination field. */
static uint32_t bfactor(GLenum f)
{
    switch (f) {
    case GL_ZERO: return 32;
    case GL_ONE: return 33;
    case GL_SRC_COLOR: return 34;
    case GL_ONE_MINUS_SRC_COLOR: return 35;
    case GL_DST_COLOR: return 36;
    case GL_ONE_MINUS_DST_COLOR: return 37;
    case GL_SRC_ALPHA: return 38;
    case GL_ONE_MINUS_SRC_ALPHA: return 39;
    case GL_DST_ALPHA: return 40;
    case GL_ONE_MINUS_DST_ALPHA: return 41;
    case GL_SRC_ALPHA_SATURATE: return 42;
    default: return 33;
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

static int swap_st;                     /* only GL's unit 1 has a texture: drawn by the chip's unit 0 with its coordinates */
static int fogging;                     /* the fog factor goes in the vertices' specular alpha */
static int valid_ok;                    /* radeon_3d holds GL's state now (until a dirty bit or a texture change) */
static const dgl_texture *valid_tex[2];
static int two;                         /* the second unit on: s2, t2 sent */
static int shift_s[2], shift_t[2];      /* a repeating texture: coordinates shifted by whole periods per triangle */
static int cube_unit[2];                /* GL's unit draws a cube map: its r goes in q's place */
static int any_cube;                    /* (a chip unit draws one: project() writes q; once a validate) */
static float vx_s, vx_o, vy_s, vy_o, vz_s, vz_o;    /* the viewport's scale and offset, from the last validate */

/* 0..1 to a byte without control-word switches (the TNT2 back end's: one add, an integer clamp). */
MGA_HOT static uint32_t ub(float v)
{
    union { float f; int32_t i; } u;
    int32_t i;
    u.f = v * 255.0f + 12582912.0f;
    i = u.i - 0x4B400000;
    return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

/* A vertex's colour packed, the last one's kept (GLQuake's world: one colour a polygon, or none). */
static uint32_t col_key[4], col_val, col_set;

MGA_HOT static uint32_t colour_of(const dgl_cvtx *c)
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
    c[0] = 0, c[1] = 0, c[2] = rg_radeon.width, c[3] = rg_radeon.height;
    if (dgl_gl.scissor_test)
        cut(c, dgl_gl.scissor[0], h - (dgl_gl.scissor[1] + dgl_gl.scissor[3]), dgl_gl.scissor[0] + dgl_gl.scissor[2],
            h - dgl_gl.scissor[1]);
    if (draw)
        cut(c, dgl_gl.viewport[0], h - (dgl_gl.viewport[1] + dgl_gl.viewport[3]),
            dgl_gl.viewport[0] + dgl_gl.viewport[2], h - dgl_gl.viewport[1]);
    if (c[2] < c[0]) c[2] = c[0];
    if (c[3] < c[1]) c[3] = c[1];
}

/* ---- The texture environments on the combiner ---------------------------------------------------------------- */

/* A unit computes A x B + C (or with half taken off, or A x B - C, or the blend B x (1 - C) + A x C), each argument
 * one of the sources or its complement, the result scaled by 1, 2 or 4. An argument here: a source and GL's
 * operand (0 the colour, 1 its complement, 2 the alpha, 3 its complement; for the alpha's combine 0 and 1). */
enum { S_ZERO, S_ONE, S_TEX, S_CONST, S_PRIMARY, S_PREVIOUS };
typedef struct { int src, op, unit; } carg;
typedef struct { GLenum fn; carg a[3]; float scale; } comb;

/* An argument's field value for the colour's combine (slot A's numbering) with its complement bit at 15. */
static uint32_t color_arg(const carg *g, int hw_unit, int first)
{
    static const uint32_t tex[3] = { 10, 12, 14 };
    uint32_t v, alpha = g->op >= 2, inv = g->op & 1;
    switch (g->src) {
    case S_TEX: v = tex[g->unit] + alpha; break;
    case S_CONST: v = 8u + alpha; break;
    case S_PRIMARY: v = 4u + alpha; break;
    case S_PREVIOUS: v = first ? 4u + alpha : 2u + alpha; break;    /* (the first unit's previous is the vertex's) */
    case S_ONE: v = 0, inv = !inv; break;
    default: v = 0; break;
    }
    (void)hw_unit;
    return v | inv << 15;
}

static uint32_t alpha_arg(const carg *g, int hw_unit, int first)
{
    static const uint32_t tex[3] = { 5, 6, 7 };
    uint32_t v, inv = g->op & 1;
    switch (g->src) {
    case S_TEX: v = tex[g->unit]; break;
    case S_CONST: v = 4; break;
    case S_PRIMARY: v = 2; break;
    case S_PREVIOUS: v = first ? 2 : 1; break;
    case S_ONE: v = 0, inv = !inv; break;
    default: v = 0; break;
    }
    (void)hw_unit;
    return v | inv << 15;
}

/* The register word for a combine: Mesa's forms (radeonUpdateTextureEnv's step 2). arg() gives slot A's value and
 * the complement at bit 15; shifts of 0, 5, 10 (colour) or 0, 4, 8 (alpha) and 15, 16, 17 place them. */
MGA_HOT static uint32_t combine_word(const comb *c, int alpha, int hw_unit, int first)
{
    const int sh[3] = { 0, alpha ? 4 : 5, alpha ? 8 : 10 };
    uint32_t g[3], w = RADEON_CLAMP_TX, one = 1u << 15;     /* (ZERO complemented) */
    int i;
#define PUT(k, slot) (w |= (g[k] & 0x1Fu) << sh[slot] | (g[k] >> 15) << (15 + (slot)))
    for (i = 0; i < 3; i++)
        g[i] = alpha ? alpha_arg(&c->a[i], hw_unit, first) : color_arg(&c->a[i], hw_unit, first);
    switch (c->fn) {
    case GL_REPLACE:                    /* 0 x 0 + arg0 */
        w |= RADEON_BLEND_CTL_ADD;
        PUT(0, 2);
        break;
    case GL_ADD:                        /* arg0 x 1 + arg1 */
        w |= RADEON_BLEND_CTL_ADD | (one >> 15) << 16;
        PUT(0, 0), PUT(1, 2);
        break;
    case GL_ADD_SIGNED_ARB:
        w |= RADEON_BLEND_CTL_ADDSIGNED | (one >> 15) << 16;
        PUT(0, 0), PUT(1, 2);
        break;
    case GL_SUBTRACT_ARB:
        w |= RADEON_BLEND_CTL_SUBTRACT | (one >> 15) << 16;
        PUT(0, 0), PUT(1, 2);
        break;
    case GL_INTERPOLATE_ARB:            /* arg0 x arg2 + arg1 x (1 - arg2) */
        w |= RADEON_BLEND_CTL_BLEND;
        PUT(0, 1), PUT(1, 0), PUT(2, 2);
        break;
    case GL_DOT3_RGB_ARB:               /* (the colour's only; Mesa: the chip's dot3 at its x4 is GL's at x1) */
    case GL_DOT3_RGBA_ARB:
        w |= RADEON_BLEND_CTL_DOT3 | RADEON_SCALE_4X;
        PUT(0, 0), PUT(1, 1);
        if (c->scale != 1.0f)
            warn_once(6, "dot3 with a scale above 1: drawn at 1 (the chip's dot3 has its own x4 only)");
        return w;
    default:                            /* GL_MODULATE: arg0 x arg1 + 0 */
        w |= RADEON_BLEND_CTL_ADD;
        PUT(0, 0), PUT(1, 1);
        break;
    }
#undef PUT
    w |= c->scale >= 3.0f ? RADEON_SCALE_4X : c->scale >= 1.5f ? RADEON_SCALE_2X : RADEON_SCALE_1X;
    return w;
}

static int src_of(GLenum s, int unit, int *tex_unit)
{
    *tex_unit = unit;
    switch (s) {
    case GL_TEXTURE: return S_TEX;
    case GL_TEXTURE0_ARB: *tex_unit = 0; return S_TEX;
    case GL_TEXTURE1_ARB: *tex_unit = 1; return S_TEX;
    case GL_CONSTANT_ARB: return S_CONST;
    case GL_PRIMARY_COLOR_ARB: return S_PRIMARY;
    default: return S_PREVIOUS;
    }
}

/* GL's environment for a texture of a family as the two combines (OpenGL 1.3's table 3.24; GL_COMBINE_ARB's own
 * arguments as given). hw: the chip's unit holding GL's unit `unit`'s texture (xlat: GL's unit to the chip's). */
MGA_HOT static void env_combines(int unit, const dgl_texenv *e, int family, comb *rgb, comb *al, const int *xlat)
{
    const carg T = { S_TEX, 0, xlat[unit] }, TA = { S_TEX, 2, xlat[unit] }, P = { S_PREVIOUS, 0, 0 },
               K = { S_CONST, 0, 0 }, Z = { S_ZERO, 0, 0 };
    int i;
    memset(rgb, 0, sizeof *rgb), memset(al, 0, sizeof *al);
    rgb->scale = al->scale = 1.0f;
#define SET(c, f, x, y, z) ((c)->fn = (f), (c)->a[0] = (x), (c)->a[1] = (y), (c)->a[2] = (z))
    if (e->mode == GL_COMBINE_ARB) {
        rgb->fn = e->combine_rgb, al->fn = e->combine_alpha;
        rgb->scale = e->rgb_scale, al->scale = e->alpha_scale;
        for (i = 0; i < 3; i++) {
            int u;
            rgb->a[i].src = src_of(e->src_rgb[i], unit, &u);
            rgb->a[i].unit = xlat[u];
            rgb->a[i].op = e->op_rgb[i] == GL_ONE_MINUS_SRC_COLOR ? 1 : e->op_rgb[i] == GL_SRC_ALPHA ? 2
                         : e->op_rgb[i] == GL_ONE_MINUS_SRC_ALPHA ? 3 : 0;
            al->a[i].src = src_of(e->src_alpha[i], unit, &u);
            al->a[i].unit = xlat[u];
            al->a[i].op = e->op_alpha[i] == GL_ONE_MINUS_SRC_ALPHA ? 1 : 0;
        }
        return;
    }
    /* (the alpha: the previous one for a texture without alpha; the texel's for REPLACE; else the product, but
     * for an intensity texture under GL_ADD the sum and under GL_BLEND the blend) */
    if (family == FAM_RGB)
        SET(al, GL_REPLACE, P, Z, Z);
    else if (e->mode == GL_REPLACE)
        SET(al, GL_REPLACE, T, Z, Z);
    else if (e->mode == GL_DECAL)
        SET(al, GL_REPLACE, P, Z, Z);
    else if (family == FAM_INTENSITY && e->mode == GL_ADD)
        SET(al, GL_ADD, P, T, Z);
    else if (family == FAM_INTENSITY && e->mode == GL_BLEND)
        SET(al, GL_INTERPOLATE_ARB, K, P, T);
    else
        SET(al, GL_MODULATE, P, T, Z);
    /* (the colour: an alpha texture leaves it) */
    if (family == FAM_ALPHA)
        SET(rgb, GL_REPLACE, P, Z, Z);
    else
        switch (e->mode) {
        case GL_REPLACE:
            SET(rgb, GL_REPLACE, T, Z, Z);
            break;
        case GL_DECAL:
            if (family == FAM_RGBA)
                SET(rgb, GL_INTERPOLATE_ARB, T, P, TA);
            else
                SET(rgb, GL_REPLACE, T, Z, Z);
            break;
        case GL_ADD:
            SET(rgb, GL_ADD, P, T, Z);
            break;
        case GL_BLEND:
            SET(rgb, GL_INTERPOLATE_ARB, K, P, T);
            break;
        default:
            SET(rgb, GL_MODULATE, P, T, Z);
            break;
        }
#undef SET
}

/* A chip unit's texture words: its filters and wrap, its format and size, its offset; NULL for no texture. */
/* (unit_words' rarer parts out of line: GLQuake validates on every bind, and the P3's instruction cache holds the
 * plain path better without them) */
static __attribute__((noinline)) uint32_t quality_min(dgl_texture *t, int gl_unit, int mip, uint32_t min)
{
    /* Anisotropic filtering: the chip's own minifying filters (Mesa's radeonSetTexFilter: they keep the mipmap
     * choice, not the bilinear one) and its ratio, the next one up (2, 4, 8, 16). */
    GLfloat a = dgl_tex_aniso_on(t) ? dgl_tex_aniso(t) : 1.0f;
    if (a > 1.0f) {
        int linear = t->min_filter == GL_LINEAR || t->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                     t->min_filter == GL_LINEAR_MIPMAP_LINEAR;
        if (!mip)
            min = linear ? RADEON_MIN_FILTER_ANISO_LINEAR : RADEON_MIN_FILTER_ANISO_NEAREST;
        else if (t->min_filter == GL_NEAREST_MIPMAP_LINEAR || t->min_filter == GL_LINEAR_MIPMAP_LINEAR)
            min = RADEON_MIN_FILTER_ANISO_NEAREST_MIP_LINEAR;
        else
            min = RADEON_MIN_FILTER_ANISO_NEAREST_MIP_NEAREST;
        min |= a <= 2.0f ? RADEON_MAX_ANISO_2_TO_1 : a <= 4.0f ? RADEON_MAX_ANISO_4_TO_1
             : a <= 8.0f ? RADEON_MAX_ANISO_8_TO_1 : RADEON_MAX_ANISO_16_TO_1;
    }
    /* The level-of-detail bias: signed 8 bits, a 128th of a level each, -1 to +127/128 (measured on the P3's 7500
     * by t36 and raw values: 64 gave +0.51, 96 +0.78, 127 +0.99; Mesa's radeonTexEnv takes the positive half as a
     * 32nd, which draws a quarter of the bias asked for). */
    if (dgl_tex_lod_bias_on(gl_unit)) {
        GLfloat b = dgl_tex_lod_bias(gl_unit);
        int f = (int)(b * 128.0f + (b >= 0 ? 0.5f : -0.5f));
        f = f > 127 ? 127 : f < -128 ? -128 : f;
        min |= (uint32_t)(uint8_t)f << RADEON_LOD_BIAS_SHIFT;
    }
    return min;
}

/* A cube map (Mesa's radeonSetTexImages and its state emit): faces 0 to 4 at the CUBIC_OFFSETs, face 5 at TXOFFSET;
 * faces 1 to 4's sizes in CUBIC_FACES, face 0's and face 5's in TXFORMAT (F5: without it the P3's 7500 drew face 5
 * as 1 x 1); s, t and r as directions (no shift by whole periods). */
static __attribute__((noinline)) void cube_words(const rtex *x, int hw)
{
    int f;
    for (f = 0; f < 5; f++)
        radeon_3d.tex[hw].cubic_off[f] = radeon.fb_location + x->lvoff[f];
    radeon_3d.tex[hw].txoffset = radeon.fb_location + x->lvoff[5];
    radeon_3d.tex[hw].txformat |= RADEON_TXFORMAT_CUBIC_MAP_ENABLE | (uint32_t)x->lw << RADEON_TXFORMAT_F5_WIDTH_SHIFT |
                                  (uint32_t)x->lh << RADEON_TXFORMAT_F5_HEIGHT_SHIFT;
    radeon_3d.tex[hw].cubic_faces =
        (uint32_t)x->lw << RADEON_FACE_WIDTH_1_SHIFT | (uint32_t)x->lh << RADEON_FACE_HEIGHT_1_SHIFT |
        (uint32_t)x->lw << RADEON_FACE_WIDTH_2_SHIFT | (uint32_t)x->lh << RADEON_FACE_HEIGHT_2_SHIFT |
        (uint32_t)x->lw << RADEON_FACE_WIDTH_3_SHIFT | (uint32_t)x->lh << RADEON_FACE_HEIGHT_3_SHIFT |
        (uint32_t)x->lw << RADEON_FACE_WIDTH_4_SHIFT | (uint32_t)x->lh << RADEON_FACE_HEIGHT_4_SHIFT;
}

/* (validate's dot3 case out of line too) */
static __attribute__((noinline)) uint32_t dot3_alpha(GLenum fn, uint32_t txablend)
{
    if (fn == GL_DOT3_RGB_ARB)                  /* (Mesa: the dot product goes to the alpha too unless told not) */
        return txablend | RADEON_DOT_ALPHA_DONT_REPLICATE;
    /* (DOT3_RGBA: the P3's 7500 didn't take the product into the alpha with Mesa's words or with the alpha's own
     * dot3: the alpha's combine draws it) */
    warn_once(7, "DOT3_RGBA: the alpha drawn by the alpha's combine, not the dot product");
    return txablend;
}

MGA_HOT static rtex *unit_words(dgl_texture *t, int hw, int gl_unit)
{
    rtex *x = t ? prepare(t) : NULL;
    int mip, n;
    uint32_t min, mag;
    shift_s[gl_unit] = shift_t[gl_unit] = 0;
    cube_unit[gl_unit] = 0;
    if (!x)
        return NULL;
    mip = t->min_filter != GL_NEAREST && t->min_filter != GL_LINEAR && x->levels > 1;
    mag = t->mag_filter == GL_LINEAR ? RADEON_MAG_FILTER_LINEAR : RADEON_MAG_FILTER_NEAREST;
    switch (t->min_filter) {
    case GL_LINEAR: min = RADEON_MIN_FILTER_LINEAR; break;
    case GL_NEAREST_MIPMAP_NEAREST: min = mip ? RADEON_MIN_FILTER_NEAREST_MIP_NEAREST : RADEON_MIN_FILTER_NEAREST; break;
    case GL_NEAREST_MIPMAP_LINEAR: min = mip ? RADEON_MIN_FILTER_NEAREST_MIP_LINEAR : RADEON_MIN_FILTER_NEAREST; break;
    case GL_LINEAR_MIPMAP_NEAREST: min = mip ? RADEON_MIN_FILTER_LINEAR_MIP_NEAREST : RADEON_MIN_FILTER_LINEAR; break;
    case GL_LINEAR_MIPMAP_LINEAR: min = mip ? RADEON_MIN_FILTER_LINEAR_MIP_LINEAR : RADEON_MIN_FILTER_LINEAR; break;
    default: min = RADEON_MIN_FILTER_NEAREST; break;
    }
    n = mip ? x->levels : 1;
    if (__builtin_expect(dgl_tex_aniso_on(t) | dgl_tex_lod_bias_on(gl_unit), 0))
        min = quality_min(t, gl_unit, mip, min);
    radeon_3d.tex[hw].txfilter = mag | min | (uint32_t)(n - 1) << RADEON_MAX_MIP_LEVEL_SHIFT |
        (t->wrap_s == GL_REPEAT ? RADEON_CLAMP_S_WRAP : RADEON_CLAMP_S_CLAMP_LAST) |   /* (GL_CLAMP as clamp to the edge) */
        (t->wrap_t == GL_REPEAT ? RADEON_CLAMP_T_WRAP : RADEON_CLAMP_T_CLAMP_LAST);
    radeon_3d.tex[hw].txformat = (uint32_t)x->dt | (x->dt == TX_565 ? 0u : RADEON_TXFORMAT_ALPHA_IN_MAP) |
        (uint32_t)x->lw << RADEON_TXFORMAT_WIDTH_SHIFT | (uint32_t)x->lh << RADEON_TXFORMAT_HEIGHT_SHIFT |
        (uint32_t)hw << 24 | RADEON_TXFORMAT_PERSPECTIVE_ENABLE;       /* (its own coordinates: ST_ROUTE_STQn) */
    radeon_3d.tex[hw].txoffset = radeon.fb_location + x->lvoff[0];
    radeon_3d.tex[hw].cubic_faces = 0;
    if (__builtin_expect(x->cube, 0)) {
        cube_words(x, hw);
        cube_unit[gl_unit] = 1;
        return x;
    }
    shift_s[gl_unit] = t->wrap_s == GL_REPEAT, shift_t[gl_unit] = t->wrap_t == GL_REPEAT;
    return x;
}

MGA_HOT static void validate(void)
{
    dgl_texture *t0 = dgl_gl.texture_cube[0] ? dgl_unit_cube(0) : dgl_gl.texture_2d ? dgl_unit_texture(0) : NULL;
    dgl_texture *t1 = dgl_gl.texture_cube[1] ? dgl_unit_cube(1) : dgl_gl.texture_2d1 ? dgl_unit_texture(1) : NULL;
    radeon_ctx3d *c = &radeon_3d;
    rtex *x0, *x1;
    comb rgb, al;
    int k[4], xlat[2] = { 0, 1 }, i;
    uint32_t pp, rb, zs;
    /* Nothing GL draws with changed since: the words still hold. */
    if (valid_ok && t0 == valid_tex[0] && t1 == valid_tex[1] && !(t0 && (t0->dirty || t0->level[0].idx)) &&
        !(t1 && (t1->dirty || t1->level[0].idx)) &&
        !(dgl_gl.dirty & (DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP)))
        return;
    vx_s = dgl_gl.viewport[2] * 0.5f, vx_o = dgl_gl.viewport[0] + dgl_gl.viewport[2] * 0.5f;
    vy_s = -dgl_gl.viewport[3] * 0.5f, vy_o = dgl_ctx.height - (dgl_gl.viewport[1] + dgl_gl.viewport[3] * 0.5f);
    vz_s = (float)((dgl_gl.depth_far - dgl_gl.depth_near) * 0.5);
    vz_o = (float)(dgl_gl.depth_near + (dgl_gl.depth_far - dgl_gl.depth_near) * 0.5);
    c->rb3d_coloroffset = (radeon.fb_location + rg_radeon_color_off(dgl_ctx.draw_front)) & RADEON_COLOROFFSET_MASK;
    scissor(1, k);
    if (k[2] <= k[0] || k[3] <= k[1])       /* (nothing: a one-pixel box in a corner, behind a scissor of nothing) */
        k[0] = k[1] = 0, k[2] = k[3] = 1, vx_o = vy_o = -4096.0f;
    c->re_top_left = (uint32_t)k[1] << 16 | (uint32_t)k[0];
    c->re_width_height = (uint32_t)(k[2] - 1) << RADEON_RE_WIDTH_SHIFT | (uint32_t)(k[3] - 1) << RADEON_RE_HEIGHT_SHIFT;

    /* Textures: GL's units on the chip's first two. Only GL's second with a texture: the chip's first draws it,
     * with the second's coordinates (project() sends them first). */
    swap_st = 0;
    x0 = unit_words(t0, 0, 0);
    x1 = NULL;
    if (x0)
        x1 = unit_words(t1, 1, 1);
    else if (t1 && (x0 = unit_words(t1, 0, 1)) != NULL) {
        swap_st = 1, xlat[1] = 0;
        shift_s[0] = shift_s[1], shift_t[0] = shift_t[1];
    }
    cur_tx[0] = x0, cur_tx[1] = x1;
    two = x1 != NULL;
    any_cube = (x0 && cube_unit[swap_st]) || (two && cube_unit[1]);
    pp = RADEON_SCISSOR_ENABLE | RADEON_ANTI_ALIAS_NONE | RADEON_TEX_BLEND_0_ENABLE;
    for (i = 0; i < 2; i++) {
        rtex *x = i ? x1 : x0;
        int glu = swap_st ? 1 : i;
        const dgl_texenv *e = dgl_tex_env(glu);
        if (!x) {
            /* (a unit without a texture passes its input on: the vertex's colour at the first) */
            c->tex[i].txcblend = RADEON_COLOR_ARG_A_ZERO | RADEON_COLOR_ARG_B_ZERO | RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X |
                                 RADEON_CLAMP_TX | (i ? RADEON_COLOR_ARG_C_CURRENT_COLOR : RADEON_COLOR_ARG_C_DIFFUSE_COLOR);
            c->tex[i].txablend = RADEON_ALPHA_ARG_A_ZERO | RADEON_ALPHA_ARG_B_ZERO | RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X |
                                 RADEON_CLAMP_TX | (i ? RADEON_ALPHA_ARG_C_CURRENT_ALPHA : RADEON_ALPHA_ARG_C_DIFFUSE_ALPHA);
            continue;
        }
        env_combines(glu, e, x->family, &rgb, &al, xlat);
        c->tex[i].txcblend = combine_word(&rgb, 0, i, i == 0);
        c->tex[i].txablend = combine_word(&al, 1, i, i == 0);
        if (__builtin_expect(rgb.fn == GL_DOT3_RGB_ARB || rgb.fn == GL_DOT3_RGBA_ARB, 0))
            c->tex[i].txablend = dot3_alpha(rgb.fn, c->tex[i].txablend);
        c->tex[i].tfactor = ub(e->color[3]) << 24 | ub(e->color[0]) << 16 | ub(e->color[1]) << 8 | ub(e->color[2]);
        pp |= i ? RADEON_TEX_1_ENABLE | RADEON_TEX_BLEND_1_ENABLE : RADEON_TEX_0_ENABLE;
    }

    /* The enables, the tests, the blend. */
    rb = RADEON_PLANE_MASK_ENABLE | (rg_radeon.bpp == 32 ? RADEON_COLOR_FORMAT_ARGB8888 : RADEON_COLOR_FORMAT_RGB565) |
         RADEON_ZBLOCK16;
    fogging = dgl_gl.fog;
    if (fogging)
        pp |= RADEON_FOG_ENABLE;
    if (dgl_gl.dither && rg_radeon.bpp != 32)
        rb |= RADEON_DITHER_ENABLE;
    if (dgl_gl.blend) {
        rb |= RADEON_ALPHA_BLEND_ENABLE;
        c->rb3d_blendcntl = RADEON_COMB_FCN_ADD_CLAMP | bfactor(dgl_gl.blend_src) << 16 | bfactor(dgl_gl.blend_dst) << 24;
    } else
        c->rb3d_blendcntl = RADEON_COMB_FCN_ADD_CLAMP | RADEON_SRC_BLEND_GL_ONE | RADEON_DST_BLEND_GL_ZERO;
    c->pp_misc &= ~(RADEON_ALPHA_TEST_OP_MASK | RADEON_REF_ALPHA_MASK);
    if (dgl_gl.alpha_test) {
        float rf = dgl_gl.alpha_ref * 255.0f + 0.5f;
        int ref = rf < 0.0f ? 0 : rf > 255.0f ? 255 : mga_ifloor(rf);
        pp |= RADEON_ALPHA_TEST_ENABLE;
        c->pp_misc |= cmp_of(dgl_gl.alpha_func) << 8 | (uint32_t)ref;
    } else
        c->pp_misc |= RADEON_ALPHA_TEST_PASS;
    zs = c->rb3d_zstencilcntl & 0xFu;           /* (the depth's format) */
    if (dgl_gl.depth_test && rg_radeon.z_off) {
        rb |= RADEON_Z_ENABLE;
        zs |= cmp_of(dgl_gl.depth_func) << 4 | (dgl_gl.depth_mask ? RADEON_Z_WRITE_ENABLE : 0u);
    } else
        zs |= RADEON_Z_TEST_ALWAYS;
    if (dgl_gl.stencil_test && rg_radeon.depth_bits == 24) {
        rb |= RADEON_STENCIL_ENABLE;
        zs |= cmp_of(dgl_gl.stencil_func) << 12 | sop(dgl_gl.stencil_fail) << 16 | sop(dgl_gl.stencil_zpass) << 20 |
              sop(dgl_gl.stencil_zfail) << 24;
        c->rb3d_stencilrefmask = ((uint32_t)dgl_gl.stencil_ref & 255u) << RADEON_STENCIL_REF_SHIFT |
                                 (dgl_gl.stencil_mask & 255u) << RADEON_STENCIL_MASK_SHIFT |
                                 (dgl_gl.stencil_writemask & 255u) << RADEON_STENCIL_WRITEMASK_SHIFT;
    } else {
        zs |= RADEON_STENCIL_TEST_ALWAYS;
        if (dgl_gl.stencil_test)
            warn_once(5, "the stencil test needs a 32-bit context (Z24S8): not drawn at 16 bits");
    }
    c->pp_cntl = pp;
    c->rb3d_cntl = rb;
    c->rb3d_zstencilcntl = zs;
    c->pp_fog_color = (ub(dgl_gl.fog_color[0]) << 16 | ub(dgl_gl.fog_color[1]) << 8 | ub(dgl_gl.fog_color[2])) |
                      RADEON_FOG_VERTEX | RADEON_FOG_USE_SPEC_ALPHA;
    if (rg_radeon.bpp == 32)
        c->rb3d_planemask = (dgl_gl.color_mask[3] ? 0xFF000000u : 0) | (dgl_gl.color_mask[0] ? 0xFF0000u : 0) |
                            (dgl_gl.color_mask[1] ? 0xFF00u : 0) | (dgl_gl.color_mask[2] ? 0xFFu : 0);
    else
        c->rb3d_planemask = (dgl_gl.color_mask[0] ? 0xF800u : 0) | (dgl_gl.color_mask[1] ? 0x07E0u : 0) |
                            (dgl_gl.color_mask[2] ? 0x001Fu : 0);
    /* (the vertex's words: x, y, z, 1/w and the colour always; the fog factor only fogging, s and t only
     * textured) */
    radeon_3d_format(RADEON_VTX_RHW | RADEON_VTX_ARGB | (fogging ? RADEON_VTX_SPEC : 0) | (x0 ? RADEON_VTX_ST : 0) |
                     (two ? RADEON_VTX_ST2 : 0) | (x0 && cube_unit[swap_st ? 1 : 0] ? RADEON_VTX_Q : 0) |
                     (two && cube_unit[1] ? RADEON_VTX_Q2 : 0));
    radeon_3d_dirty = 1;                        /* (the registers that changed written at the next primitive) */
    dgl_gl.dirty &= ~(DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP);
    valid_ok = 1;
    valid_tex[0] = t0, valid_tex[1] = t1;
}

/* ---- Drawing ---------------------------------------------------------------- */

MGA_HOT static int rb_begin(void)
{
    if (!dgl_ctx.active || !rg_radeon.active)
        return 0;
    validate();
    return 1;
}

MGA_HOT static void rb_end(void) { }

MGA_HOT static float fog_factor(float d)
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

MGA_HOT static void project(const dgl_cvtx *c, radeon_vertex *v, const dgl_cvtx *colour)
{
    float iw = 1.0f / c->w;
    v->x = c->x * iw * vx_s + vx_o;
    v->y = c->y * iw * vy_s + vy_o;
    v->z = c->z * iw * vz_s + vz_o;
    v->rhw = iw;
    v->argb = colour_of(colour);
    v->spec = (fogging ? ub(fog_factor(c->eye_d)) : 255u) << 24;
    if (swap_st)
        v->s = c->s1, v->t = c->t1;
    else
        v->s = c->s, v->t = c->t;
    v->s2 = c->s1, v->t2 = c->t1;
    if (any_cube)                               /* (a cube map's r, in q's place) */
        v->q = swap_st ? c->tr1 : c->tr, v->q2 = c->tr1;
}

static double offset_for(const radeon_vertex *p0, const radeon_vertex *p1, const radeon_vertex *p2)
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
    return dgl_gl.offset_factor * m + dgl_gl.offset_units / (rg_radeon.depth_bits == 24 ? 16777215.0 : 65535.0);
}

/* By whole periods so that the smallest coordinate is in [0, 1): the chip's interpolation is a float's, so huge
 * coordinates (conformance t14: 30,000 periods along) lose their fraction. */
static void wrap_shift(radeon_vertex *v, int n)
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
static int finish(radeon_vertex *v, int n)
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

MGA_HOT static void draw_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    PROF_SCOPE(PROF_D_PROJ);
    dgl_cvtx in[3], out[9];
    radeon_vertex v[9];
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
        radeon_tri(&v[0], &v[1], &v[2]);
    else
        radeon_poly(v, n);
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

MGA_HOT static void rb_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    if (fogging)
        fog_split(a, b, c, prov, 0);
    else
        draw_triangle(a, b, c, prov);
}

/* A polygon wholly inside the guard band, unfogged, of up to 16 vertices: each vertex projected once and the whole
 * sent as one triangle fan (radeon_poly); else triangle by triangle. */
MGA_HOT static void rb_polygon(const dgl_cvtx *v, int n, const dgl_cvtx *prov)
{
    PROF_SCOPE(PROF_D_PROJ);
    radeon_vertex w[16];
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
    radeon_poly(w, n);
    tris += (unsigned)(n - 2);
    dgl_prims.seqs++;
}

/* A line: a quad half the width either side, across its minor axis (OpenGL's wide lines' shape), b's colour when
 * flat. */
static void rb_line(const dgl_cvtx *a, const dgl_cvtx *b)
{
    dgl_cvtx in[3], out[9];
    radeon_vertex p[2], q[4];
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
    radeon_poly(q, 4);
    lines_++;
}

/* A point: a square of the point size about its centre; none when the centre is outside the view volume. */
static void rb_point(const dgl_cvtx *a)
{
    radeon_vertex p, q[4];
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
    radeon_poly(q, 4);
    points_++;
}

/* GL_POLYGON whole to rb_polygon (each vertex projected and sent once), as the TNT2 back end's. */
MGA_HOT static int rb_fast(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    GLint ids[DGL_POLY_MAX];
    GLsizei i;
    if (mode != GL_POLYGON || count < 4 || count > DGL_POLY_MAX)
        return 0;
    for (i = 0; i < count; i++)
        ids[i] = idx ? idx[i] : first + i;
    return dgl_poly_general(ids, (int)count, ids[0]);
}

/* ---- Hardware transform (gl/docs/dx7.md, X3) -------------------------------------------------------------------
 * On a chip with the engine (the 7500, the 7200) and the command processor running, a draw goes through the engine
 * when its state is one it does (so far: lighting one-sided and fog (X3b), but no texture-coordinate generation, clip
 * planes, projective texture matrix, cube map or polygon offset; filled triangles, strips, fans, quads and polygons): the draw's vertices in
 * object coordinates (x, y, z, w, the colour, each unit's s and t) written into a vertex area in video memory, then
 * the engine's indexed draw over them (tcl.c). Everything else takes the CPU's path as before. DGL_TNL=0 keeps every
 * draw on the CPU. */
#define VB_BYTES (1u << 20)
static int tnl_mode = -1;               /* DGL_TNL: 0 off, 1 on (where the chip has the engine) */
static uint32_t vb_block, vb_used;      /* the vertex area (0: none yet) and how much of it this frame used */
/* The command processor's vertex area in system memory (radeon_cp_varea, through the GART: the CPU's writes cached,
 * not across the bus one by one as into video memory), when there is one: two halves, a fence after each half's last
 * draw, so a half is written again only once the chip has fetched what was in it. */
static uint32_t va_used, va_fence[2];
static int va_half;
static float tnl_mvp[16], tnl_vp[6];
static uint32_t tnl_tris;               /* triangles the engine transformed (DGL-STAT's tnl/s) */
static int tnl_mvp_ok;
static uint16_t *tnl_idx;
static int tnl_idx_cap;

int dgl_radeon_tnl_active(void)
{
    if (tnl_mode < 0) {
        const char *e = getenv("DGL_TNL");
        /* (on wherever the chip has the engine, DGL_TNL=0 turns it off: the owner's decision of 2026-11-26, once the
         * waits cured the 7500's hangs and video memory for its vertices made it faster than the P3's CPU in Echoes
         * (+11-18%) and Fifth Wheel (+11-15%), level in GLQuake, Quake 2 and PrBoom-plus: gl/docs/dx7.md) */
        tnl_mode = !(e && *e == '0');
    }
    return tnl_mode && (radeon.flags & RADEON_HAS_TCL) && radeon_cp_running();
}

/* A unit's texture matrix as the engine path takes it: the identity, or affine (no projective row: s and t made while
 * the vertices are copied, below; Fifth Wheel's HIGH scales texel coordinates by 1/512 this way). NULL: the identity. */
static const float *tex_matrix(int unit)
{
    return unit ? (dgl_gl.tex1_identity ? NULL : dgl_gl.tex1.stack[dgl_gl.tex1.depth - 1].m)
                : (dgl_gl.tex_identity ? NULL : dgl_gl.tex.stack[dgl_gl.tex.depth - 1].m);
}

/* s and t through an affine texture matrix (GL's column order; q divided out at glTexCoord, so the matrix's own
 * scale m[15] divides) */
static void tex_affine(const float *m, float st[2], float r)
{
    float s = st[0], t = st[1], iq = 1.0f / m[15];
    st[0] = (m[0] * s + m[4] * t + m[8] * r + m[12]) * iq;
    st[1] = (m[1] * s + m[5] * t + m[9] * r + m[13]) * iq;
}

static int tex_matrix_ok(int unit)
{
    const float *m = tex_matrix(unit);
    return !m || (m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] != 0.0f);
}

static int tnl_light_ok(void);
static int tnl_fog_mode = -1;           /* DGL_TNL_FOG: 0 keeps fogged draws on the CPU (X3b) */

static int tnl_state_ok(GLenum mode)
{
    if (mode == GL_POINTS || mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
        return 0;                       /* (lines and points: the CPU's path, as their widths and ends are drawn) */
    if ((dgl_gl.clip_enabled | dgl_gl.texgen_on[0] | dgl_gl.texgen_on[1] | dgl_gl.texture_cube[0] |
         dgl_gl.texture_cube[1]) || dgl_gl.offset_fill || dgl_gl.polygon_mode != GL_FILL)
        return 0;
    if (dgl_gl.lighting && !tnl_light_ok())
        return 0;                       /* (X3b: two-sided lighting, or DGL_TNL_LIGHT=0: the CPU's path) */
    if (dgl_gl.fog) {
        if (tnl_fog_mode < 0) {
            const char *e = getenv("DGL_TNL_FOG");
            /* (off by default: on the P3 the engine's factor came out one value per draw, linear fogged solid,
             * exp and exp2 not at all, conformance t10 and t13 with fogged draws forced onto it, 2026-11-26; the
             * CPU's path fogs them right. DGL_TNL_FOG=1 tries it) */
            tnl_fog_mode = e && *e == '1';
        }
        if (!tnl_fog_mode)
            return 0;
    }
    if (!tex_matrix_ok(0) || !tex_matrix_ok(1))
        return 0;                       /* (a projective texture matrix: the CPU's path divides per vertex there) */
    return 1;
}

static uint16_t *idx_room(int n)
{
    if (n > tnl_idx_cap) {
        uint16_t *p = (uint16_t *)realloc(tnl_idx, (size_t)n * sizeof *p);
        if (!p)
            return NULL;
        tnl_idx = p, tnl_idx_cap = n;
    }
    return tnl_idx;
}

static int rb_tnl_draw(GLenum mode, GLsizei count, const GLint *idx, GLint first);

/* DGL_TNL=1's diagnostics while the P3's hang (gl/docs/dx7.md) is open: the last 32 draws sent to the engine, and
 * how many other primitives came between them; printed once, with the chip's state and the ring around the command
 * processor's read index (the packet it stopped at), when a fence is missed. */
typedef struct {
    uint32_t mode, count, nv, ni, pairs, dwords, vfmt, prim, addr, between, fence, fmt, sel;
    uint32_t wp0, wp1, se_cntl, ucp;    /* (the ring's span of its packets; the engine's words for it) */
    float mvp[16], vp[6];
} tnl_rec;
static tnl_rec tnl_recs[32];
static unsigned tnl_nrec;
static unsigned long tnl_begins;

/* DGL_TNL_TRACE=from,to[:from2,to2]: every engine draw, texture upload and depth clear of those frames (swaps counted
 * from 0) as DGL-TNLTRACE lines, with the ring's indices (Echoes' fly scene hangs the P3 at frame 111 or 373,
 * whichever the build: its draws there). */
static long trace_r[4] = { -2, -1, -1, -1 };
static int tracing(void)
{
    long f = (long)dgl_frame_no;
    if (trace_r[0] == -2) {
        const char *e = getenv("DGL_TNL_TRACE"), *c;
        trace_r[0] = -1;
        if (e && *e) {
            trace_r[0] = atol(e);
            c = strchr(e, ',');
            trace_r[1] = c ? atol(c + 1) : trace_r[0];
            if ((c = strchr(e, ':')) != NULL) {
                trace_r[2] = atol(c + 1);
                c = strchr(c, ',');
                trace_r[3] = c ? atol(c + 1) : trace_r[2];
            }
        }
    }
    return (trace_r[0] >= 0 && f >= trace_r[0] && f <= trace_r[1]) || (trace_r[2] >= 0 && f >= trace_r[2] && f <= trace_r[3]);
}

static void trace_ring(const char *what, unsigned long a, unsigned long b, unsigned long c, unsigned long d)
{
    uint32_t rp = 0, wp = 0, dw = 0;
    const volatile uint32_t *ring = NULL;
    if (!tracing())
        return;
    radeon_cp_peek(&rp, &wp, &ring, &dw);
    DGL_WARN("DGL-TNLTRACE f%lu %s %lu %lu %lu %lx rp %lu wp %lu begins %lu", dgl_frame_no, what, a, b, c, d,
             (unsigned long)rp, (unsigned long)wp, dgl_prims.begins);
}

static void tnl_note(GLenum mode, GLsizei count, int nv, int ni, int pairs, int dwords, uint32_t vfmt, uint32_t prim,
                     uint32_t addr, uint32_t wp0)
{
    tnl_rec *r = &tnl_recs[tnl_nrec++ & 31u];
    trace_ring(mode == GL_TRIANGLES ? "draw-tris" : "draw", (unsigned long)count, (unsigned long)nv,
               (unsigned long)(pairs * 16 + dwords), (unsigned long)addr);
    r->mode = mode, r->count = (uint32_t)count, r->nv = (uint32_t)nv, r->ni = (uint32_t)ni;
    r->pairs = (uint32_t)pairs, r->dwords = (uint32_t)dwords, r->vfmt = vfmt, r->prim = prim, r->addr = addr;
    r->between = (uint32_t)(dgl_prims.begins - tnl_begins - 1u);
    r->fence = va_fence[0] > va_fence[1] ? va_fence[0] : va_fence[1];
    r->fmt = radeon_tcl.output_vtx_fmt, r->sel = radeon_tcl.output_vtx_sel;
    r->wp0 = wp0, r->wp1 = radeon_cp_wptr(), r->se_cntl = radeon_3d.se_cntl, r->ucp = radeon_tcl.ucp_vert_blend_ctl;
    memcpy(r->mvp, tnl_mvp, sizeof r->mvp);
    memcpy(r->vp, tnl_vp, sizeof r->vp);
    tnl_begins = dgl_prims.begins;
}

/* (a float neither infinite nor NaN, by its bits: no FPU compare in the copy loop) */
static int finite_f(float f)
{
    union { float f; uint32_t u; } c;
    c.f = f;
    return (c.u & 0x7F800000u) != 0x7F800000u;
}

/* A position or the matrix not finite: the engine isn't given it (the CPU's path draws such a draw as it always
 * did); said once, counted for the diagnostics (suspected in the P3's hang: the chip's clipper may not survive
 * them). */
static unsigned long tnl_nonfinite;
static void tnl_refuse_nonfinite(void)
{
    if (!tnl_nonfinite++)
        DGL_WARN("DGL-TNLDIAG a draw with a non-finite position or matrix: the CPU's path draws it");
}

static void install(void);

/* (one draw's record for the dump: what, its ring span, state, matrix, its packets (up to 640 dwords: a 945-index
 * draw's four pieces are 489) and up to 512 of its vertices with their clip-space values) */
static void tnl_dump_rec(const tnl_rec *r, const char *what, uint32_t rp, const volatile uint32_t *ring, uint32_t dw)
{
    uint32_t len = (r->wp1 - r->wp0 + dw) % dw, n, j, k;
    const volatile uint32_t *v = NULL;
    DGL_WARN("DGL-TNLDIAG %s draw: ring %lu..%lu, read index %lu; count %lu nv %lu dwords %lu addr %08lx; se_cntl %08lx "
             "ucp %08lx vp %g %g %g %g %g %g", what, (unsigned long)r->wp0, (unsigned long)r->wp1, (unsigned long)rp,
             (unsigned long)r->count, (unsigned long)r->nv, (unsigned long)r->dwords, (unsigned long)r->addr,
             (unsigned long)r->se_cntl, (unsigned long)r->ucp, r->vp[0], r->vp[1], r->vp[2], r->vp[3], r->vp[4],
             r->vp[5]);
    for (k = 0; k < 4; k++)
        DGL_WARN("DGL-TNLDIAG %s mvp col %lu: %.9g %.9g %.9g %.9g", what, (unsigned long)k, r->mvp[k * 4],
                 r->mvp[k * 4 + 1], r->mvp[k * 4 + 2], r->mvp[k * 4 + 3]);
    for (k = 0; k < len && k < 640; k += 8) {
        uint32_t b = (r->wp0 + k) % dw;
        DGL_WARN("DGL-TNLDIAG %s ring[%lu..] %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", what, (unsigned long)b,
                 (unsigned long)ring[b], (unsigned long)ring[(b + 1) % dw], (unsigned long)ring[(b + 2) % dw],
                 (unsigned long)ring[(b + 3) % dw], (unsigned long)ring[(b + 4) % dw], (unsigned long)ring[(b + 5) % dw],
                 (unsigned long)ring[(b + 6) % dw], (unsigned long)ring[(b + 7) % dw]);
    }
    if (radeon_cp_varea && r->addr >= radeon_cp_varea_gart && r->addr < radeon_cp_varea_gart + radeon_cp_varea_bytes)
        v = (const volatile uint32_t *)(radeon_cp_varea + (r->addr - radeon_cp_varea_gart));
    else if (!radeon_cp_varea && r->addr >= radeon.fb_location)
        v = (const volatile uint32_t *)(radeon.fb + (r->addr - radeon.fb_location));
    n = r->nv < 512u ? r->nv : 512u;
    for (j = 0; v && j < n; j++, v += r->dwords) {
        union { uint32_t u; float f; } p[4];
        float c[4];
        int q;
        for (q = 0; q < 4; q++)
            p[q].u = v[q];
        for (q = 0; q < 4; q++)         /* (GL's column-major matrix times the vertex) */
            c[q] = r->mvp[q] * p[0].f + r->mvp[4 + q] * p[1].f + r->mvp[8 + q] * p[2].f + r->mvp[12 + q] * p[3].f;
        DGL_WARN("DGL-TNLDIAG %s v%lu: %.9g %.9g %.9g %.9g col %08lx clip %.6g %.6g %.6g %.6g%s", what,
                 (unsigned long)j, p[0].f, p[1].f, p[2].f, p[3].f, (unsigned long)v[4], c[0], c[1], c[2], c[3],
                 r->dwords > 5 ? " (st follow)" : "");
    }
}

static void tnl_dump(const char *why)
{
    static int done;
    uint32_t rp = 0, wp = 0, dw = 0, k;
    const volatile uint32_t *ring = NULL;
    unsigned i;
    if (done)
        return;
    done = 1;
    radeon_cp_peek(&rp, &wp, &ring, &dw);
    DGL_WARN("DGL-TNLDIAG %s: RBBM_STATUS %08lx CP_CSQ_STAT %08lx SCRATCH1 %lu RPTR %lu WPTR %lu (ring %lu dwords) "
             "draws %u non-finite %lu", why, (unsigned long)radeon_rd(RADEON_RBBM_STATUS),
             (unsigned long)radeon_rd(RADEON_CP_CSQ_STAT), (unsigned long)radeon_rd(0x15E4), (unsigned long)rp,
             (unsigned long)wp, (unsigned long)dw, tnl_nrec, tnl_nonfinite);
    if (ring && dw)                     /* (120 dwords before the read index: the packets the engine took last) */
        for (k = 0; k < 144; k += 8) {
            uint32_t b = (rp + dw - 120u + k) % dw;
            DGL_WARN("DGL-TNLDIAG ring[%lu..] %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", (unsigned long)b,
                     (unsigned long)ring[b], (unsigned long)ring[(b + 1) % dw], (unsigned long)ring[(b + 2) % dw],
                     (unsigned long)ring[(b + 3) % dw], (unsigned long)ring[(b + 4) % dw],
                     (unsigned long)ring[(b + 5) % dw], (unsigned long)ring[(b + 6) % dw],
                     (unsigned long)ring[(b + 7) % dw]);
        }
    for (i = 0; i < 32; i++) {
        const tnl_rec *r = &tnl_recs[(tnl_nrec + i) & 31u];
        if (!r->count)
            continue;
        DGL_WARN("DGL-TNLDIAG draw mode %lx count %lu nv %lu ni %lu pairs %lu dwords %lu vfmt %08lx prim %08lx "
                 "addr %08lx between %lu fence %lu out %08lx/%08lx", (unsigned long)r->mode, (unsigned long)r->count,
                 (unsigned long)r->nv, (unsigned long)r->ni, (unsigned long)r->pairs, (unsigned long)r->dwords,
                 (unsigned long)r->vfmt, (unsigned long)r->prim, (unsigned long)r->addr, (unsigned long)r->between,
                 (unsigned long)r->fence, (unsigned long)r->fmt, (unsigned long)r->sel);
    }
    /* The draw whose packets hold the read index, and the engine's draw that began before it (the command processor
     * fetches some way past the primitive the engine is stuck on: a draw by the CPU's path, or the next engine draw,
     * held the index in both hangs of Echoes' frame 371): their packets, state and vertices as the engine fetches
     * them (the vertex area isn't written again before its fence passes), with what their matrix makes of them. */
    if (ring && dw) {
        /* (and the two before it: the command processor's queue holds up to 128 dwords, so the draw the engine is
         * stuck on can be one or two back; CP_CSQ_STAT's second field is where the microcode reads, mod 128) */
        const tnl_rec *in = NULL, *before = NULL, *before2 = NULL;
        uint32_t best = 0xFFFFFFFFu, best2 = 0xFFFFFFFFu;
        for (i = 32; i-- > 0;) {         /* (the newest first: an older lap of the ring holds the index too) */
            const tnl_rec *r = &tnl_recs[(tnl_nrec + i) & 31u];
            uint32_t len = (r->wp1 - r->wp0 + dw) % dw, at = (rp - r->wp0 + dw) % dw;
            if (!r->count)
                continue;
            if (at < len && !in)
                in = r;
            else if (at >= len && r != in) {
                if (at < best)
                    best2 = best, before2 = before, best = at, before = r;
                else if (at < best2)
                    best2 = at, before2 = r;
            }
        }
        if (in)
            tnl_dump_rec(in, "stuck", rp, ring, dw);
        if (before)
            tnl_dump_rec(before, "before", rp, ring, dw);
        if (before2)
            tnl_dump_rec(before2, "before2", rp, ring, dw);
    }
}

/* The engine stayed stuck through radeon_engine_recover, or a FIFO wait in the register writes ran out: nothing more
 * can be drawn, and the layer writes nothing more to it (radeon_dead), so the program ends here with the PC alive,
 * rather than hold the PCI bus at its next write. */
static void dead_end(const char *why)
{
    DGL_ERR("DGL-ERR radeon: %s; the engine stayed stuck through a full reset (RBBM_STATUS %08lx): the program ends "
            "here. Switch the PC off and on to clear the card.", why, (unsigned long)radeon_recover_status);
    exit(3);
}

/* A fence or a sync not reached in a second: the engine isn't getting through its commands. No more of them from
 * here: the CPU's path for the rest of the run, after the recovery reset (radeon_cp_abandon), drawing by register
 * writes; or, the engine still stuck, the program's end (dead_end). */
static void tnl_hung(const char *why, const char *warn)
{
    tnl_dump(why);
    tnl_mode = 0;
    dgl_sink.tnl = NULL;
    radeon_tcl_forget();
    if (radeon_cp_abandon() != 0)
        dead_end(why);
    DGL_WARN("DGL-TNLDIAG the engine came back after the recovery reset (RBBM_STATUS %08lx, bus mastering %s)",
             (unsigned long)radeon_recover_status, radeon_recover_waited ? "back on" : "still off");
    install();
    warn_once(9, warn);
}

/* The distance in dwords between a draw's vertices of dwords each: packed (DGL_TNL_STRIDE=n, up to 16: n dwords apart,
 * through LOAD_VBPNTR's stride field; an experiment of the 7500's hang hunt, kept for tests) */
static int tnl_stride(int dwords)
{
    static int want = -1;
    if (want < 0) {
        const char *e = getenv("DGL_TNL_STRIDE");
        want = e && *e ? atoi(e) : 0;
    }
    return want > dwords && want <= 16 ? want : dwords;
}

/* A sparse draw's vertices compacted: only those its indices name are written to the vertex area, in first-use order,
 * the indices renumbered. Echoes' and Fifth Wheel's draws often name a few of a chunk's vertices (6 indices over 145,
 * 9 over 298): writing the whole range cost the CPU several times the vertices the CPU's own path transforms
 * (Fifth Wheel's FW1 ran 17% slower with T&L). tnl_stamp marks a vertex met in this draw (tnl_gen). */
static uint16_t tnl_remap[65536], tnl_order[65536], tnl_stamp[65536];
static uint16_t tnl_gen;
static int tnl_box_on;                  /* (the window box gathered only while DGL_TNL_MINPX is on) */
static int tnl_box_on_first(void)       /* (the first draw: DGL_TNL_MINPX read once, so the loop knows) */
{
    static int read;
    if (!read) {
        const char *e = getenv("DGL_TNL_MINPX");
        read = 1;
        tnl_box_on = e && atof(e) > 0;
    }
    return 0;
}

/* A draw whose window extent (its object-space box's corners through the matrix) is under DGL_TNL_MINPX pixels each
 * way (off by default): the CPU's path draws it. (An experiment of the 7500's hang hunt: a sub-pixel mesh of Echoes'
 * was the first suspect; it was only queued behind the draw the engine was stuck in.) A corner at or behind the eye:
 * not small. */
static unsigned long tnl_small;
static int tnl_too_small(const float lo[3], const float hi[3])
{
    static float minpx = -1;
    const float *m = dgl_mvp()->m;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    int c;
    if (minpx < 0) {
        const char *e = getenv("DGL_TNL_MINPX");
        minpx = e && *e ? (float)atof(e) : 0.0f;
    }
    if (minpx <= 0)
        return 0;
    tnl_box_on = 1;
    for (c = 0; c < 8; c++) {
        float p[3], cx, cy, cw;
        p[0] = c & 1 ? hi[0] : lo[0], p[1] = c & 2 ? hi[1] : lo[1], p[2] = c & 4 ? hi[2] : lo[2];
        cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
        cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
        cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        if (!(cw > 1e-6f))
            return 0;
        cx = cx / cw * vx_s + vx_o, cy = cy / cw * vy_s + vy_o;
        if (cx < x0) x0 = cx;
        if (cx > x1) x1 = cx;
        if (cy < y0) y0 = cy;
        if (cy > y1) y1 = cy;
    }
    return x1 - x0 < minpx && y1 - y0 < minpx;
}

/* A draw's vertices into the vertex area at dst, stride dwords apart: x, y, z, w, the colour, each pair's s and t
 * (through the affine texture matrices); 0 for one the engine shouldn't take (coordinates past 1,024, not finite). */
/* ---- X3b: GL's lighting on the engine ---------------------------------------------------------------------------
 * A lit draw goes through the engine as Mesa 7.0.3's radeon driver sends one (radeon_state.c, radeon_state_init.c):
 * eye-space lighting (LIGHT_IN_MODELSPACE off), the modelview in matrix slot 1 and its inverse, as the rows, in slot 2
 * for the normals; each vertex's normal after its position (radeon_maos_vbtmp.h's order); the front material in
 * registers, each light in the vector and scalar memories, the light model's ambient and the eye vector in vectors
 * 122 and 124; colour material as the vertex's colour for the components it names (LM_SOURCE_VERTEX_DIFFUSE). The
 * specular added into the colour (GL 1.1's single colour). Two-sided lighting stays on the CPU's path. DGL_TNL_LIGHT=0
 * keeps every lit draw on the CPU. */
static int tnl_light_mode = -1;
static struct {
    float mtl[17], glt[4], eye[4], mv[16], it[16], vec[8][24], scl[8][6];
    uint32_t lmc, plc[4], on;
} lt, lt_sent;
static int lt_known;
/* (the modelview in slot 1, for lighting and for fog alike: the engine's eye depth comes from it; a fogged unlit
 * draw without it fogged as at depth 0, conformance t10 on the P3, 2026-11-26) */
static float mv_sent[16];
static int mv_known;
/* (X3b) GL's fog on the engine (Mesa's radeonFogfv): its factor computed into the specular alpha, as the CPU's path
 * puts it there for the pixel pipeline's vertex fog; the mode in UCP_VERT_BLEND_CTL, c and d in vector 123 (linear:
 * end / (end - start) and -1 / (end - start); exp and exp2: 0 and minus the density, or its square). The specular is
 * computed with the lighting on and no light, for fog alone ("Bizarre: have to leave lighting enabled to get fog").
 * NOT YET RIGHT on the P3 (one factor a draw): DGL_TNL_FOG=1 only, fogged draws on the CPU by default. */
static float fog_sent[4];
static int fog_known;

static int tnl_light_ok(void)
{
    if (tnl_light_mode < 0) {
        const char *e = getenv("DGL_TNL_LIGHT");
        tnl_light_mode = !(e && *e == '0');
    }
    return tnl_light_mode && !dgl_gl.light_two_side;
}

static void norm3(const float a[3], float r[3])
{
    float d = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    d = d > 0 ? 1.0f / d : 0.0f;
    r[0] = a[0] * d, r[1] = a[1] * d, r[2] = a[2] * d;
}

/* GL's lighting state as the engine's words and floats (lt). */
static void tnl_light_build(void)
{
    const dgl_material *m = &dgl_gl.material[0];
    const dgl_mat4 *mv = &dgl_gl.mv.stack[dgl_gl.mv.depth - 1];
    dgl_mat4 inv;
    uint32_t src[4] = { RADEON_LM_SOURCE_STATE_MULT, RADEON_LM_SOURCE_STATE_MULT, RADEON_LM_SOURCE_STATE_MULT,
                        RADEON_LM_SOURCE_STATE_MULT };     /* (emission, ambient, diffuse, specular) */
    float f;
    int i, k;
    memset(&lt, 0, sizeof lt);
    for (k = 0; k < 4; k++) {
        lt.mtl[k] = m->emission[k], lt.mtl[4 + k] = m->ambient[k];
        lt.mtl[8 + k] = m->diffuse[k], lt.mtl[12 + k] = m->specular[k];
        lt.glt[k] = dgl_gl.light_model_ambient[k];  /* (every source MULT: the engine multiplies the material in) */
    }
    lt.mtl[16] = m->shininess;
    if (dgl_gl.color_material && dgl_gl.color_material_face != GL_BACK)
        switch (dgl_gl.color_material_mode) {
        case GL_EMISSION: src[0] = RADEON_LM_SOURCE_VERTEX_DIFFUSE; break;
        case GL_AMBIENT: src[1] = RADEON_LM_SOURCE_VERTEX_DIFFUSE; break;
        case GL_DIFFUSE: src[2] = RADEON_LM_SOURCE_VERTEX_DIFFUSE; break;
        case GL_SPECULAR: src[3] = RADEON_LM_SOURCE_VERTEX_DIFFUSE; break;
        default: src[1] = src[2] = RADEON_LM_SOURCE_VERTEX_DIFFUSE; break;  /* (GL_AMBIENT_AND_DIFFUSE) */
        }
    lt.lmc = RADEON_LIGHTING_ENABLE | RADEON_SPECULAR_LIGHTS | RADEON_DIFFUSE_SPECULAR_COMBINE |
             RADEON_LOCAL_LIGHT_VEC_GL | src[0] << RADEON_EMISSIVE_SOURCE_SHIFT | src[1] << RADEON_AMBIENT_SOURCE_SHIFT |
             src[2] << RADEON_DIFFUSE_SOURCE_SHIFT | src[3] << RADEON_SPECULAR_SOURCE_SHIFT;
    if (dgl_gl.light_local_viewer)
        lt.lmc |= RADEON_LOCAL_VIEWER;
    if (dgl_gl.normalize)
        lt.lmc |= RADEON_NORMALIZE_NORMALS;
    else if (dgl_gl.rescale_normal)
        lt.lmc |= RADEON_RESCALE_NORMALS;
    memcpy(lt.mv, mv->m, sizeof lt.mv);
    if (dgl_mat_inverse(&inv, mv) != 0)
        memcpy(lt.it, inv.m, sizeof lt.it);
    else
        for (k = 0; k < 16; k++)
            lt.it[k] = (k % 5) == 0 ? 1.0f : 0.0f;
    /* (the eye vector, and the normals' rescale: Mesa's _ModelViewInvScale, from the inverse's third row) */
    f = lt.it[2] * lt.it[2] + lt.it[6] * lt.it[6] + lt.it[10] * lt.it[10];
    lt.eye[2] = 1.0f, lt.eye[3] = f < 1e-12f ? 1.0f : 1.0f / sqrtf(f);
    for (i = 0; i < 8; i++) {
        const dgl_light *l = &dgl_gl.light[i];
        float *v = lt.vec[i], *c = lt.scl[i];
        uint32_t w = 1u | 2u | 4u;      /* (LIGHT_n_ENABLE, its ambient, its specular) */
        if (!l->enabled)
            continue;
        lt.on |= 1u << i;
        for (k = 0; k < 4; k++)
            v[k] = l->ambient[k], v[4 + k] = l->diffuse[k], v[8 + k] = l->specular[k];
        if (l->position[3] == 0) {      /* (a direction: towards the light, and the half vector for the far viewer) */
            float h[3];
            norm3(l->position, v + 12);
            h[0] = v[12], h[1] = v[13], h[2] = v[14] + 1.0f;
            norm3(h, v + 16);
        } else {
            float iw = 1.0f / l->position[3], d[3];
            v[12] = l->position[0] * iw, v[13] = l->position[1] * iw, v[14] = l->position[2] * iw, v[15] = 1.0f;
            norm3(l->spot_dir, d);
            v[16] = -d[0], v[17] = -d[1], v[18] = -d[2];
            w |= 8u;                    /* (IS_LOCAL) */
            if (l->spot_cutoff != 180.0f)
                w |= 16u;               /* (IS_SPOT) */
            if (!((l->att[0] == 0.0f || l->att[0] == 1.0f) && l->att[1] == 0.0f && l->att[2] == 0.0f))
                w |= l->att[1] == 0.0f && l->att[2] == 0.0f ? 64u | 128u : 64u;    /* (RANGE_ATTEN, CONSTANT_) */
        }
        v[20] = l->att[2], v[21] = l->att[1], v[22] = l->att[0];
        c[1] = l->spot_exp;
        c[2] = cosf(l->spot_cutoff * 3.14159265358979f / 180.0f);
        c[4] = FLT_MAX;
        c[5] = l->att[0] != 0.0f ? 1.0f / l->att[0] : FLT_MAX;
        lt.plc[i >> 1] |= w << ((i & 1) * 16);
    }
}

/* What changed of it into the engine (the held draw out first, with the state it was made in). */
static void tnl_light_send(void)
{
    int i;
    if (!lt_known || memcmp(lt.mtl, lt_sent.mtl, sizeof lt.mtl))
        radeon_tcl_material(lt.mtl);
    if (!lt_known || memcmp(lt.glt, lt_sent.glt, sizeof lt.glt))
        radeon_tcl_vectors(RADEON_TCL_VEC_GLOBAL_AMBIENT, 1, lt.glt, 4);
    if (!lt_known || memcmp(lt.eye, lt_sent.eye, sizeof lt.eye))
        radeon_tcl_vectors(RADEON_TCL_VEC_EYE, 1, lt.eye, 4);
    if (!lt_known || memcmp(lt.it, lt_sent.it, sizeof lt.it))
        radeon_tcl_matrix_t(2, lt.it);
    for (i = 0; i < 8; i++)
        if ((lt.on >> i & 1u) && (!lt_known || !(lt_sent.on >> i & 1u) || memcmp(lt.vec[i], lt_sent.vec[i], sizeof lt.vec[i]) ||
                                  memcmp(lt.scl[i], lt_sent.scl[i], sizeof lt.scl[i])))
            radeon_tcl_light(i, lt.vec[i], lt.scl[i]);
    lt_sent = lt;
    lt_known = 1;
}

/* (not MGA_HOT: the engine's path only; in .text.hot it parted the CPU path's hot code by 1.3 KB and GLQuake, which
 * never calls it, lost 3.9% on the P3: the instruction cache, as X1/X2's loss was) */
static int tnl_pack(volatile uint32_t *dst, GLint vmin, const uint16_t *map, int nv, int stride, int pairs,
                            const float *m0, const float *m1, float lo[3], float hi[3], int *w1, int lit)
{
    dgl_vin v;
    int i, k;
    for (i = 0; i < nv; i++, dst += stride) {
        union { float f; uint32_t u; } c;
        dgl_vertex_of(vmin + (map ? tnl_order[i] : i), &v);
        if (m0 && pairs)
            tex_affine(m0, v.tex, dgl_gl.texr_used ? v.texr[0] : 0.0f);
        if (m1 && pairs > 1)
            tex_affine(m1, v.tex1, dgl_gl.texr_used ? v.texr[1] : 0.0f);
        if (pairs && (v.tex[0] > 1024.0f || v.tex[0] < -1024.0f || v.tex[1] > 1024.0f || v.tex[1] < -1024.0f ||
                      (pairs > 1 && (v.tex1[0] > 1024.0f || v.tex1[0] < -1024.0f || v.tex1[1] > 1024.0f ||
                                     v.tex1[1] < -1024.0f))))
            return 0;                   /* (coordinates past 1,024 lose their fraction in the chip's floats: the CPU's
                                         * path takes whole periods off per triangle, conformance t14) */
        if (!finite_f(v.pos[0]) || !finite_f(v.pos[1]) || !finite_f(v.pos[2]) || !finite_f(v.pos[3])) {
            tnl_refuse_nonfinite();
            return 0;
        }
        c.f = v.pos[0], dst[0] = c.u;
        c.f = v.pos[1], dst[1] = c.u;
        c.f = v.pos[2], dst[2] = c.u;
        c.f = v.pos[3], dst[3] = c.u;
        if (tnl_box_on) {
            for (k = 0; k < 3; k++) {
                if (v.pos[k] < lo[k]) lo[k] = v.pos[k];
                if (v.pos[k] > hi[k]) hi[k] = v.pos[k];
            }
            *w1 &= v.pos[3] == 1.0f;
        }
        if (lit) {                      /* (the normal after the position: radeon_maos_vbtmp.h's order) */
            c.f = v.nrm[0], dst[4] = c.u;
            c.f = v.nrm[1], dst[5] = c.u;
            c.f = v.nrm[2], dst[6] = c.u;
        }
        dst[4 + lit] = ub(v.col[3]) << 24 | ub(v.col[0]) << 16 | ub(v.col[1]) << 8 | ub(v.col[2]);
        if (pairs) {
            c.f = v.tex[0], dst[5 + lit] = c.u;
            c.f = v.tex[1], dst[6 + lit] = c.u;
        }
        if (pairs > 1) {
            c.f = v.tex1[0], dst[7 + lit] = c.u;
            c.f = v.tex1[1], dst[8 + lit] = c.u;
        }
    }
    if (nv)
        (void)dst[-1];                  /* (the CPU's writes out before the chip reads them) */
    return 1;
}

/* ---- X3b: buffer objects' vertices kept in video memory ---------------------------------------------------------
 * A draw from the arrays whose every enabled array is in a buffer object made GL_STATIC_DRAW: its vertices, from 0 to
 * its highest index, converted once into video memory as the engine takes them, and drawn from there (indices only)
 * while nothing they came from changed: the buffers' generations, the arrays' layout, the current values that stand
 * for disabled arrays, the texture matrices applied while converting, the vertex's format. Echoes' settled terrain
 * and any GL 1.5 game's world; a buffer changed (glBufferData, glBufferSubData, a write through glMapBuffer) is
 * converted again at its next draw. DGL_TNL_VBO=0 converts every draw as before. */
#define VC_SLOTS 64
#define VC_BYTES (4u << 20)
typedef struct {
    struct { const void *b, *ptr; unsigned serial, gen; GLsizei stride; GLenum type; GLint size, en; } a[5];
    float cur[13], m0[16], m1[16];
    int dwords, stride, pairs, lit, swap, has_m0, has_m1, texr_used;
} vkey;
static struct { vkey k; uint32_t off, nv; int live; } vc[VC_SLOTS];
static uint32_t vc_block, vc_used;
static int vc_mode = -1, vc_next;
static unsigned long vc_hits, vc_made;

static int vc_arr(vkey *k, int n, const dgl_array *a, int needed)
{
    if (!a->enabled || !needed)
        return 1;
    if (!a->buf || a->buf->usage != GL_STATIC_DRAW_ARB || a->buf->mapped)
        return 0;
    k->a[n].b = a->buf, k->a[n].ptr = a->ptr, k->a[n].serial = a->buf->serial, k->a[n].gen = a->buf->gen;
    k->a[n].stride = a->stride, k->a[n].type = a->type, k->a[n].size = a->size, k->a[n].en = 1;
    return 1;
}

/* The draw's key, or 0: not one to keep (immediate mode, a client array, a buffer not static). */
static int vc_key(vkey *k, int dwords, int stride, int pairs, int lit, const float *m0, const float *m1)
{
    if (vc_mode < 0) {
        const char *e = getenv("DGL_TNL_VBO");
        vc_mode = !(e && *e == '0');
    }
    if (!vc_mode || !dgl_draw_from_arrays() || !dgl_gl.va.enabled || !dgl_gl.va.buf)
        return 0;
    memset(k, 0, sizeof *k);
    if (!vc_arr(k, 0, &dgl_gl.va, 1) || !vc_arr(k, 1, &dgl_gl.ca, 1) || !vc_arr(k, 2, &dgl_gl.ta, pairs > 0) ||
        !vc_arr(k, 3, &dgl_gl.ta1, pairs > 0) || !vc_arr(k, 4, &dgl_gl.na, lit))
        return 0;
    memcpy(k->cur, dgl_gl.cur_color, 4 * sizeof(float));
    memcpy(k->cur + 4, dgl_gl.cur_tex, 2 * sizeof(float));
    memcpy(k->cur + 6, dgl_gl.cur_tex1, 2 * sizeof(float));
    memcpy(k->cur + 8, dgl_gl.cur_normal, 3 * sizeof(float));
    memcpy(k->cur + 11, dgl_gl.cur_texr, 2 * sizeof(float));
    if (m0)
        memcpy(k->m0, m0, sizeof k->m0), k->has_m0 = 1;
    if (m1)
        memcpy(k->m1, m1, sizeof k->m1), k->has_m1 = 1;
    k->dwords = dwords, k->stride = stride, k->pairs = pairs, k->lit = lit, k->swap = swap_st;
    k->texr_used = dgl_gl.texr_used;
    return 1;
}

/* The video memory address (the chip's space) of the draw's vertices 0..vmax, made if they aren't there yet; 0 if
 * they can't be (no room, a vertex the engine can't take). */
static uint32_t vc_get(const vkey *k, GLint vmax, int stride, int pairs, int lit, const float *m0, const float *m1)
{
    uint32_t nv = (uint32_t)vmax + 1u, bytes = nv * (uint32_t)stride * 4u, off;
    float lo[3], hi[3];
    int i, w1 = 1, slot = -1;
    for (i = 0; i < VC_SLOTS; i++)
        if (vc[i].live && vc[i].nv >= nv && !memcmp(&vc[i].k, k, sizeof *k)) {
            vc_hits++;
            return radeon.fb_location + vc[i].off;
        }
    if (bytes > VC_BYTES / 4u)
        return 0;
    if (!vc_block && room(VC_BYTES + 32u, &vc_block, NULL) != 0) {
        vc_block = 0, vc_mode = 0;
        return 0;
    }
    for (i = 0; i < VC_SLOTS && slot < 0; i++)  /* (the same arrays kept for fewer vertices: replaced) */
        if (vc[i].live && !memcmp(&vc[i].k, k, sizeof *k))
            slot = i;
    vc_used = (vc_used + 31u) & ~31u;
    if (vc_used + bytes > VC_BYTES || (slot < 0 && vc[vc_next].live && vc_next == 0 && vc_used)) {
        /* (full: everything kept goes, once the engine has drawn from it) */
        if (radeon_sync(1000000) != 0)
            return 0;
        memset(vc, 0, sizeof vc);
        vc_used = 0, vc_next = 0, slot = -1;
    }
    if (slot < 0)
        slot = vc_next, vc_next = (vc_next + 1) % VC_SLOTS;
    off = ((vc_block + 31u) & ~31u) + vc_used;
    lo[0] = lo[1] = lo[2] = 1e30f, hi[0] = hi[1] = hi[2] = -1e30f;
    if (!tnl_pack((volatile uint32_t *)(radeon.fb + off), 0, NULL, (int)nv, stride, pairs, m0, m1, lo, hi, &w1, lit)) {
        vc[slot].live = 0;
        return 0;
    }
    vc_used += bytes;
    vc[slot].k = *k, vc[slot].off = off, vc[slot].nv = nv, vc[slot].live = 1;
    vc_made++;
    return radeon.fb_location + off;
}

static int rb_tnl(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    if (!tnl_mode || !(radeon.flags & RADEON_HAS_TCL))
        return 0;                       /* (said first: GLQuake draws thousands a second this way) */
    return rb_tnl_draw(mode, count, idx, first);
}

static int rb_tnl_draw(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    PROF_SCOPE(PROF_D_SETUP);
    /* (the coordinates: GL's unit 0's s and t, then unit 1's where the chip's second unit draws or the first draws GL's
     * unit 1, swap_st; the engine's outputs take the chip's units from them) */
    int units = cur_tx[1] ? 2 : cur_tx[0] ? 1 : 0, pairs = units == 2 || (units && swap_st) ? 2 : units;
    int lit = dgl_gl.lighting ? 3 : 0;  /* (X3b: a lit draw's normal, three words a vertex) */
    int dwords = 5 + 2 * pairs + lit, stride, flat = dgl_gl.shade_model == GL_FLAT;
    GLint vmin, vmax, i;
    int nv, ni = 0, k;
    uint32_t prim = RADEON_PRIM_TRI_LIST, vfmt, bytes, addr, s, took, fmt, sel, ucp, lmc = 0;
    vkey vk;
    int cached;
    volatile uint32_t *dst = NULL;
    const float *mvp;
    float vp[6];
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    const uint16_t *map;
    int w1 = 1;                         /* (every vertex's w 1: the box below applies) */
    uint16_t *ix;
    const float *m0, *m1;
    if (!dgl_radeon_tnl_active() || count < 3 || !tnl_state_ok(mode))
        return 0;
    {
        const float *mm = dgl_mvp()->m;
        for (i = 0; i < 16; i++)
            if (!finite_f(mm[i])) {
                tnl_refuse_nonfinite();
                return 0;
            }
    }
    m0 = tex_matrix(0), m1 = tex_matrix(1);
    /* The vertices the draw uses: a range of at most 65,536 (16-bit indices). */
    if (idx) {
        vmin = vmax = idx[0];
        for (i = 1; i < count; i++) {
            if (idx[i] < vmin) vmin = idx[i];
            if (idx[i] > vmax) vmax = idx[i];
        }
    } else
        vmin = first, vmax = first + count - 1;
    nv = vmax - vmin + 1;
    if (nv > 65536)
        return 0;
    map = NULL;
    cached = vmax < 65536 && vc_key(&vk, dwords, tnl_stride(dwords), pairs, lit, m0, m1);
    if (cached)                         /* (kept in video memory: the indices as they are, from vertex 0) */
        vmin = 0, nv = vmax + 1;
    else if (idx && nv > count) {            /* (fewer indices than vertices in the range: write only those named) */
        int nu = 0;
        if (!++tnl_gen) {
            memset(tnl_stamp, 0, sizeof tnl_stamp);
            tnl_gen = 1;
        }
        for (i = 0; i < count; i++) {
            int r = idx[i] - vmin;
            if (tnl_stamp[r] != tnl_gen)
                tnl_stamp[r] = tnl_gen, tnl_remap[r] = (uint16_t)nu, tnl_order[nu++] = (uint16_t)r;
        }
        map = tnl_remap;
        nv = nu;
    }
    {   /* (a draw of fewer than DGL_TNL_MINV vertices, 8 by default, through the CPU's path: the engine's cost a draw
         * (its state, the packet) outweighs what it saves on a few: PrBoom-plus's wall and floor quads ran 5.8%
         * slower on the 7500's engine than on the P3's CPU, level with this; 0: every draw to the engine) */
        static int minv = -1;
        if (minv < 0) {
            const char *e = getenv("DGL_TNL_MINV");
            minv = e && *e ? atoi(e) : 8;
        }
        if (nv < minv)
            return 0;
    }
    /* The indices, as the chip's primitive (provoking vertex last, a polygon's first: SE_CNTL below). */
#define IX(k) (map ? map[idx[k] - vmin] : (uint16_t)((idx ? idx[k] : first + (k)) - vmin))
    switch (mode) {
    case GL_TRIANGLES:
        if (!(ix = idx_room(count)))
            return 0;
        for (i = 0; i + 2 < count; i += 3)
            ix[ni++] = IX(i), ix[ni++] = IX(i + 1), ix[ni++] = IX(i + 2);
        break;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        if (count <= radeon_tcl_piece) {   /* (one packet: a strip or fan isn't split) */
            if (!(ix = idx_room(count)))
                return 0;
            for (i = 0; i < count; i++)
                ix[ni++] = IX(i);
            prim = mode == GL_TRIANGLE_STRIP ? RADEON_PRIM_TRI_STRIP : RADEON_PRIM_TRI_FAN;
        } else {                        /* (as a list, the windings and provoking vertices kept) */
            if (!(ix = idx_room(3 * count)))
                return 0;
            for (i = 0; i + 2 < count; i++)
                if (mode == GL_TRIANGLE_STRIP)
                    ix[ni++] = IX(i + (i & 1)), ix[ni++] = IX(i + 1 - (i & 1)), ix[ni++] = IX(i + 2);
                else if (mode == GL_POLYGON && flat)    /* (the first vertex last, the winding kept) */
                    ix[ni++] = IX(i + 1), ix[ni++] = IX(i + 2), ix[ni++] = IX(0);
                else
                    ix[ni++] = IX(0), ix[ni++] = IX(i + 1), ix[ni++] = IX(i + 2);
            if (mode == GL_POLYGON && flat)
                flat = 2;               /* (the list's last vertex already the polygon's first) */
        }
        break;
    case GL_QUADS:
    case GL_QUAD_STRIP:
        if (!(ix = idx_room(3 * count)))
            return 0;
        /* (the general path's diagonal, 0-2; flat: each triangle ending at the quad's last vertex) */
        if (mode == GL_QUADS)
            for (i = 0; i + 3 < count; i += 4) {
                if (flat)
                    ix[ni++] = IX(i), ix[ni++] = IX(i + 1), ix[ni++] = IX(i + 3),
                    ix[ni++] = IX(i + 1), ix[ni++] = IX(i + 2), ix[ni++] = IX(i + 3);
                else
                    ix[ni++] = IX(i), ix[ni++] = IX(i + 1), ix[ni++] = IX(i + 2),
                    ix[ni++] = IX(i), ix[ni++] = IX(i + 2), ix[ni++] = IX(i + 3);
            }
        else
            for (i = 0; i + 3 < count; i += 2)
                ix[ni++] = IX(i), ix[ni++] = IX(i + 1), ix[ni++] = IX(i + 3),
                ix[ni++] = IX(i + 2), ix[ni++] = IX(i), ix[ni++] = IX(i + 3);
        break;
    default:
        return 0;
    }
#undef IX
    if (!ni)
        return 1;
    stride = tnl_stride(dwords);
    bytes = (uint32_t)nv * (uint32_t)stride * 4u;
    /* The engine's state for the draw, in locals first: the setup word (shading, culling), its outputs, the cull
     * words, the format, the matrix and the viewport. */
    fmt = RADEON_TCL_VTX_Z0 | RADEON_TCL_VTX_W0 | RADEON_TCL_VTX_PK_DIFFUSE | (units ? RADEON_TCL_VTX_ST0 : 0u) |
          (units > 1 ? RADEON_TCL_VTX_ST1 : 0u) | (dgl_gl.fog ? RADEON_TCL_VTX_PK_SPEC : 0u);
    sel = RADEON_TCL_COMPUTE_XYZW | (swap_st && units == 1 ? 1u : 0u) << RADEON_TCL_TEX_0_OUTPUT_SHIFT |
          1u << RADEON_TCL_TEX_1_OUTPUT_SHIFT | (lit ? RADEON_TCL_COMPUTE_DIFFUSE : 0u) |
          (dgl_gl.fog ? RADEON_TCL_COMPUTE_SPECULAR : 0u);
    s = radeon_tcl_window_cntl() & ~(RADEON_FFACE_CULL_DIR_MASK | RADEON_BFACE_SOLID | RADEON_FFACE_SOLID | 3u << 6 |
                           RADEON_DIFFUSE_SHADE_MASK | RADEON_ALPHA_SHADE_MASK | RADEON_SPECULAR_SHADE_MASK);
    s |= RADEON_VPORT_XY_XFORM_ENABLE | RADEON_VPORT_Z_XFORM_ENABLE;
    s |= flat ? RADEON_DIFFUSE_SHADE_FLAT | RADEON_ALPHA_SHADE_FLAT | RADEON_SPECULAR_SHADE_FLAT
              : RADEON_DIFFUSE_SHADE_GOURAUD | RADEON_ALPHA_SHADE_GOURAUD | RADEON_SPECULAR_SHADE_GOURAUD;
    s |= mode == GL_POLYGON && flat == 1 ? RADEON_FLAT_SHADE_VTX_0 : RADEON_FLAT_SHADE_VTX_LAST;
    {   /* (experiment, DGL_TNL_ROUND=8: Mesa's rounding for the engine's vertices, truncated to eighths of a pixel,
         * against the sixteenths, rounded, that the CPU's path uses; a sub-pixel mesh of Echoes' hung the P3's) */
        static int round8 = -1;
        if (round8 < 0) {
            const char *e = getenv("DGL_TNL_ROUND");
            round8 = e && *e == '8';
        }
        if (round8)
            s = (s & ~(3u << 28 | 3u << 30)) | RADEON_ROUND_MODE_TRUNC | RADEON_ROUND_PREC_8TH_PIX;
    }
    ucp = RADEON_UCP_IN_CLIP_SPACE;
    if (dgl_gl.fog)
        ucp |= dgl_gl.fog_mode == GL_LINEAR ? RADEON_TCL_FOG_LINEAR : dgl_gl.fog_mode == GL_EXP ? RADEON_TCL_FOG_EXP
                                                                                              : RADEON_TCL_FOG_EXP2;
    if (dgl_gl.front_face == GL_CCW)
        s |= RADEON_FFACE_CULL_CCW, ucp |= RADEON_CULL_FRONT_IS_CCW;
    s |= RADEON_FFACE_SOLID | RADEON_BFACE_SOLID;
    if (dgl_gl.cull_face) {
        if (dgl_gl.cull_mode == GL_FRONT || dgl_gl.cull_mode == GL_FRONT_AND_BACK)
            s &= ~RADEON_FFACE_SOLID, ucp |= RADEON_CULL_FRONT;
        if (dgl_gl.cull_mode == GL_BACK || dgl_gl.cull_mode == GL_FRONT_AND_BACK)
            s &= ~RADEON_BFACE_SOLID, ucp |= RADEON_CULL_BACK;
    }
    vfmt = RADEON_CP_VC_FRMT_Z | RADEON_CP_VC_FRMT_W0 | RADEON_CP_VC_FRMT_PKCOLOR |
           (pairs ? RADEON_CP_VC_FRMT_ST0 : 0u) | (pairs > 1 ? RADEON_CP_VC_FRMT_ST1 : 0u) |
           (lit ? RADEON_CP_VC_FRMT_N0 : 0u);
    mvp = dgl_mvp()->m;
    vp[0] = vx_s, vp[1] = vx_o, vp[2] = vy_s, vp[3] = vy_o, vp[4] = vz_s, vp[5] = vz_o;
    (void)tnl_box_on_first();
    if (lit) {                          /* (the lights and the material where they changed: the held draw first) */
        tnl_light_build();
        if (!lt_known || memcmp(&lt, &lt_sent, sizeof lt))
            tnl_light_send();
        lmc = lt.lmc;
    } else if (dgl_gl.fog)
        lmc = RADEON_LIGHTING_ENABLE | RADEON_SPECULAR_LIGHTS | RADEON_DIFFUSE_SPECULAR_COMBINE |
              RADEON_LOCAL_LIGHT_VEC_GL | RADEON_LM_SOURCE_STATE_MULT << RADEON_EMISSIVE_SOURCE_SHIFT |
              RADEON_LM_SOURCE_STATE_MULT << RADEON_AMBIENT_SOURCE_SHIFT |
              RADEON_LM_SOURCE_STATE_MULT << RADEON_DIFFUSE_SOURCE_SHIFT |
              RADEON_LM_SOURCE_STATE_MULT << RADEON_SPECULAR_SOURCE_SHIFT;    /* (no light on: the fog's specular) */
    if (dgl_gl.fog) {
        float f[4] = { 0, 0, 0, 0 };
        if (dgl_gl.fog_mode == GL_LINEAR) {
            if (dgl_gl.fog_end == dgl_gl.fog_start)
                f[0] = f[1] = 1.0f;
            else
                f[0] = dgl_gl.fog_end / (dgl_gl.fog_end - dgl_gl.fog_start),
                f[1] = -1.0f / (dgl_gl.fog_end - dgl_gl.fog_start);
        } else
            f[1] = dgl_gl.fog_mode == GL_EXP ? -dgl_gl.fog_density : -(dgl_gl.fog_density * dgl_gl.fog_density);
        if (!fog_known || memcmp(f, fog_sent, sizeof f)) {
            radeon_tcl_vectors(RADEON_TCL_VEC_FOG_PARAM, 1, f, 4);
            memcpy(fog_sent, f, sizeof f);
            fog_known = 1;
        }
    }
    if (lit || dgl_gl.fog) {
        const float *mvm = dgl_gl.mv.stack[dgl_gl.mv.depth - 1].m;
        if (!mv_known || memcmp(mvm, mv_sent, sizeof mv_sent)) {
            radeon_tcl_matrix(1, mvm);
            memcpy(mv_sent, mvm, sizeof mv_sent);
            mv_known = 1;
        }
    }
    /* (a draw of buffer objects: its vertices in video memory already, or put there now, indices only) */
    if (!cached || !(addr = vc_get(&vk, vmax, stride, pairs, lit, m0, m1))) {
        if (radeon_cp_varea) {
            /* Room in the system-memory area: this half, else the other once its fence has passed. */
            uint32_t half = radeon_cp_varea_bytes / 2u;
            if (bytes > half)
                return 0;
            va_used = (va_used + 31u) & ~31u;
            if (va_used + bytes > half) {
                va_fence[va_half] = radeon_cp_fence();
                trace_ring("fence-half", (unsigned long)va_fence[va_half], (unsigned long)va_half,
                           (unsigned long)va_fence[va_half ^ 1], (unsigned long)radeon_rd(0x15E4));
                va_half ^= 1;
                va_used = 0;
                static long fake = -1;          /* (DGL_TNL_FAKEMISS=n: the n-th wait taken as missed, to test what follows) */
                if (fake < 0) {
                    const char *e = getenv("DGL_TNL_FAKEMISS");
                    fake = e && *e ? atol(e) : 0;
                }
                if (va_fence[va_half] && (radeon_cp_fence_wait(va_fence[va_half], 1000000u) != 0 || (fake > 0 && !--fake))) {
                    /* (the chip isn't getting through its commands: no more of them from here, the CPU's path for the
                     * rest of the run, rather than writing over vertices it may still be fetching) */
                    tnl_hung("a fence not passed in a second",
                             "the transform engine's vertex area: a fence not passed in a second; the engine reset and "
                             "off for the rest of the run, drawing by register writes");
                    return 0;
                }
            }
            addr = radeon_cp_varea_gart + (uint32_t)va_half * half + va_used;     /* (in the chip's space already) */
            dst = (volatile uint32_t *)(radeon_cp_varea + (uint32_t)va_half * half + va_used);
            va_used += bytes;               /* (exact: a draw that joins it follows straight on; rounded at the next start) */
        } else {
            /* Room in the vertex area in video memory (made at the first draw; used again from its start after a sync
             * once full). */
            if (bytes > VB_BYTES)
                return 0;
            if (!vb_block && room(VB_BYTES + 32u, &vb_block, NULL) != 0) {
                vb_block = 0;
                tnl_mode = 0;
                warn_once(8, "no video memory for the transform engine's vertices: transformed by the CPU");
                return 0;
            }
            vb_used = (vb_used + 31u) & ~31u;
            if (vb_used + bytes > VB_BYTES) {
                trace_ring("vram-wrap", (unsigned long)vb_used, (unsigned long)bytes, 0, 0);
                if (radeon_sync(1000000) != 0) {
                    /* (the chip not through its commands in a second: as at a missed fence, the engine reset) */
                    tnl_hung("the vertex area's sync not done in a second",
                             "the transform engine's vertex area: a sync not done in a second; the engine reset and off "
                             "for the rest of the run, drawing by register writes");
                    return 0;
                }
                vb_used = 0;
            }
            addr = ((vb_block + 31u) & ~31u) + vb_used;
            vb_used += bytes;
            dst = (volatile uint32_t *)(radeon.fb + addr);
            addr += radeon.fb_location;             /* (the chip's space: video memory starts at its frame buffer's base) */
        }
        took = bytes;
        if (!tnl_pack(dst, vmin, map, nv, stride, pairs, m0, m1, lo, hi, &w1, lit))
            return 0;
        if ((tnl_box_on || tnl_box_on_first()) && w1 && tnl_too_small(lo, hi)) {
            /* (the whole draw within a pixel or two: the CPU's path for it, the area's room given back) */
            if (radeon_cp_varea)
                va_used -= took;
            else
                vb_used -= took;
            tnl_small++;
            trace_ring("small", (unsigned long)count, (unsigned long)nv, 0, 0);
            return 0;
        }
    }
    /* The engine's state: on, the matrix and viewport where they changed, the outputs, shading and culling. */
    radeon_tcl_on(1);
    if (!tnl_mvp_ok || memcmp(tnl_mvp, mvp, sizeof tnl_mvp)) {
        memcpy(tnl_mvp, mvp, sizeof tnl_mvp);
        radeon_tcl_matrix(0, tnl_mvp);
        if (!tnl_mvp_ok)
            radeon_tcl_guard_band();
        tnl_mvp_ok = 1;
    }
    if (memcmp(vp, tnl_vp, sizeof vp)) {
        memcpy(tnl_vp, vp, sizeof vp);
        radeon_tcl_viewport(vp);
    }
    radeon_tcl.output_vtx_fmt = fmt;
    radeon_tcl.output_vtx_sel = sel;
    radeon_tcl.ucp_vert_blend_ctl = ucp;
    radeon_tcl.light_model_ctl = lmc;
    if (lit)
        memcpy(radeon_tcl.per_light_ctl, lt.plc, sizeof lt.plc);
    else                                /* (fog alone: no light's specular in its factor's colour) */
        memset(radeon_tcl.per_light_ctl, 0, sizeof radeon_tcl.per_light_ctl);
    if (radeon_3d.se_cntl != s)
        radeon_3d.se_cntl = s, radeon_3d_dirty = 1;
    if (!tnl_tris)
        DGL_WARN("DGL-RADEON the transform engine draws (DGL_TNL=0: the CPU transforms instead)");   /* (said once) */
    {
        uint32_t w0 = radeon_cp_wptr();
        radeon_tcl_draw_indexed_s(prim, vfmt, addr, dwords, stride, ix, ni);
        tnl_note(mode, count, nv, ni, pairs, stride, vfmt, prim, addr, w0);
    }
    {   /* (experiment, DGL_TNL_LAZY=1: the ring's pointer to the chip only once 1,024 dwords are waiting) */
        static int lazy = -1;
        if (lazy < 0) {
            const char *e = getenv("DGL_TNL_LAZY");
            lazy = e && *e == '1';
        }
        if (lazy)
            radeon_cp_flush_some(1024u);
        else
            radeon_cp_flush();
    }
    tris += (uint32_t)(prim == RADEON_PRIM_TRI_LIST ? ni / 3 : ni - 2);
    tnl_tris += (uint32_t)(prim == RADEON_PRIM_TRI_LIST ? ni / 3 : ni - 2);
    dgl_prims.tris_in += prim == RADEON_PRIM_TRI_LIST ? (unsigned long)ni / 3 : (unsigned long)ni - 2;
    (void)k;
    return 1;
}

static void install(void)
{
    valid_ok = 0;
    col_set = 0;
    {   /* (DGL_TNL_PKT=new: the engine's draws by LOAD_VBPNTR and DRAW_INDX, to compare; else Mesa's packet) */
        const char *e = getenv("DGL_TNL_PKT");
        radeon_tcl_old_packets = !(e && !strcmp(e, "new"));
        /* (the 3D engine idle between pieces and before new state, by default (6); DGL_TNL_SYNC=0 none, 1-5 other
         * waits for tests: see radeon_tcl_sync) */
        e = getenv("DGL_TNL_SYNC");
        radeon_tcl_sync = e && *e >= '0' && *e <= '8' ? *e - '0' : 6;
        e = getenv("DGL_TEST_STICK");   /* (tests only: the first window-space draw sticks the engine, to prove the
                                         * recovery's end and the next program's refusal leave the PC alive) */
        if (e && *e == '1')
            radeon_test_stick = 1;
        e = getenv("DGL_TNL_PIECE");    /* (tests: the indices a list's packet takes, a multiple of 6 up to 300) */
        radeon_tcl_piece = e && atoi(e) >= 6 && atoi(e) <= RADEON_TCL_PIECE ? atoi(e) / 6 * 6 : RADEON_TCL_PIECE;
    }
    radeon_3d_init(rg_radeon_color_off(0), rg_radeon.pitch_bytes, rg_radeon.bpp, rg_radeon.z_off,
                   rg_radeon.z_pitch_bytes ? rg_radeon.z_pitch_bytes : rg_radeon.pitch_bytes, rg_radeon.depth_bits,
                   rg_radeon.width, rg_radeon.height);
    dgl_sink.begin = rb_begin;
    dgl_sink.triangle = rb_triangle;
    dgl_sink.fast = rb_fast;
    /* (the hook only where the engine will draw: every draw's call into it cost GLQuake instruction-cache lines on the
     * P3, with DGL_TNL=0 or on a 7000 too) */
    dgl_sink.tnl = dgl_radeon_tnl_active() ? rb_tnl : NULL;
    dgl_sink.polygon = rb_polygon;
    dgl_sink.line = rb_line;
    dgl_sink.point = rb_point;
    dgl_sink.end = rb_end;
}

/* Clears by the 2D engine, in the scissor: colour, and the depth buffer (with the stencil's byte in Z24S8). */
static void clear(GLbitfield mask)
{
    int c[4];
    if (!dgl_ctx.active || !rg_radeon.active)
        return;
    if (radeon_dead)                    /* (a FIFO wait ran out in the frame before: the engine stuck) */
        dead_end("a register write found the engine's FIFO full for 200 ms");
    if (radeon_stalled) {               /* (a wait for the engine ran out in the frame before: the same recovery) */
        radeon_stalled = 0;
        tnl_hung("a wait of half a second or more for the engine ran out",
                 "the engine stopped (a wait for it ran out); the engine reset, the transform engine off for the rest "
                 "of the run, drawing by register writes");
        if (radeon_dead)
            dead_end("a wait of half a second or more for the engine ran out");
    }
    scissor(0, c);
    if (mask & GL_COLOR_BUFFER_BIT) {
        const GLfloat *k = dgl_gl.clear_color;
        uint32_t v = rg_radeon.bpp == 32
                   ? ub(k[3]) << 24 | ub(k[0]) << 16 | ub(k[1]) << 8 | ub(k[2])
                   : (ub(k[0]) >> 3) << 11 | (ub(k[1]) >> 2) << 5 | (ub(k[2]) >> 3);
        if (!dgl_gl.color_mask[0] || !dgl_gl.color_mask[1] || !dgl_gl.color_mask[2])
            warn_once(3, "a colour clear with a colour mask clears every channel");
        radeon_set_target(rg_radeon_color_off(dgl_ctx.draw_front), rg_radeon.pitch_bytes, rg_radeon.bpp);
        radeon_set_clip(c[0], c[1], c[2], c[3]);
        radeon_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
    }
    if (rg_radeon.z_off && (((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask) || (mask & GL_STENCIL_BUFFER_BIT))) {
        double d = dgl_gl.clear_depth < 0 ? 0 : dgl_gl.clear_depth > 1 ? 1 : dgl_gl.clear_depth;
        int zw = (mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask;
        if (radeon.flags & RADEON_TILED_Z) {
            /* (a tiled depth buffer: by the 3D engine, depth and stencil each as asked) */
            uint32_t z = rg_radeon.depth_bits == 24 ? (uint32_t)(d * 16777215.0 + 0.5) : (uint32_t)(d * 65535.0 + 0.5);
            trace_ring("depth-clear", (unsigned long)(c[2] - c[0]), (unsigned long)(c[3] - c[1]), (unsigned long)zw,
                       (unsigned long)radeon_tcl_live);
            radeon_3d_depth_clear(c[0], c[1], c[2] - c[0], c[3] - c[1], z, zw,
                                  (mask & GL_STENCIL_BUFFER_BIT) && rg_radeon.depth_bits == 24 ? dgl_gl.clear_stencil & 255 : -1);
        } else {
            uint32_t v = rg_radeon.depth_bits == 24
                       ? (uint32_t)(d * 16777215.0 + 0.5) | ((unsigned)dgl_gl.clear_stencil & 255u) << 24
                       : (uint32_t)(d * 65535.0 + 0.5);
            if (!zw)
                warn_once(4, "a stencil clear clears the depth buffer too");
            radeon_set_target(rg_radeon.z_off, rg_radeon.z_pitch_bytes, rg_radeon.depth_bits == 24 ? 32 : 16);
            radeon_set_clip(c[0], c[1], c[2], c[3]);
            radeon_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
        }
    }
    radeon_cp_flush();
}

static void caps(dgl_caps *c)
{
    memset(c, 0, sizeof *c);
    c->renderer = radeon.name ? radeon.name : "ATI Radeon";
    c->texture_units = 2;
    c->sub_narrow = 1;                          /* (tex_sub sends whole rows of the rectangle's) */
    c->max_texture_size = 2048;
    c->max_anisotropy = 16;
    c->max_lod_bias = 1;                        /* (+1 drawn as +127/128: unit_words) */
    c->dot3 = 1;
    c->cube = 1;
    c->hw_tnl = dgl_radeon_tnl_active();
}

static void synced(void) { dgl_vram_sync_done(); }   /* (the blocks retired before it are free) */

/* A rectangle of a single-level texture (GLQuake's lightmaps): its rows, whole, sent again in place (through the
 * command stream: no sync; GLQuake's rectangles are whole rows anyway); anything else is made again whole at its
 * next use (t->dirty). */
static int tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    rtex *tx = (rtex *)t->be;
    const dgl_level *l = &t->level[level];
    if (!tx || level != 0 || tx->levels != 1 || tx->pal || !l->rgba || t->dirty || l->w != (1 << tx->lw) ||
        l->h != (1 << tx->lh) || x < 0 || y < 0 || x + w > l->w || y + h > l->h)
        return 0;
    if (!radeon_cp_running()) {
        int o = PROF_SWITCH(PROF_D_DRAIN), r = radeon_sync(1000000);
        PROF_BACK(o);
        if (r != 0)
            return 0;
    }
    send_rows(t, l, NULL, tx->lvoff[0], y, y + h, tx->dt, tx->cpp);
    if (radeon_cp_running())
        dgl_texc.sub_iload++;
    else {
        radeon_vram_written(tx->lvoff[0] + row_bytes(l, tx->cpp) * (uint32_t)(y + h) - 4u);
        dgl_texc.sub_sync++;
    }
    radeon_3d_tex_written();
    dgl_texc.sub_fast++;
    return 1;
}

static void tex_free(dgl_texture *t)
{
    tex_drop(t);
}

static int tex_info(const dgl_texture *t, int rgba[4])
{
    const rtex *x = (const rtex *)t->be;
    if (!x)
        return 0;
    if (x->dt == TX_565) rgba[0] = 5, rgba[1] = 6, rgba[2] = 5, rgba[3] = 0;
    else if (x->dt == TX_1555) rgba[0] = rgba[1] = rgba[2] = 5, rgba[3] = 1;
    else if (x->dt == TX_4444) rgba[0] = rgba[1] = rgba[2] = rgba[3] = 4;
    else rgba[0] = rgba[1] = rgba[2] = rgba[3] = 8;
    return 1;
}

static void tex_reset(uint32_t start, uint32_t end)
{
    int i, n = dgl_texture_count();
    dgl_texture *t;
    valid_ok = 0;
    vb_block = 0, vb_used = 0, tnl_mvp_ok = 0, lt_known = 0, fog_known = 0, mv_known = 0;
    vc_block = 0, vc_used = 0, vc_next = 0;
    memset(vc, 0, sizeof vc);   /* (a new context: the vertex area and the engine's state anew) */
    va_used = 0, va_half = 0, va_fence[0] = va_fence[1] = 0;
    memset(tnl_vp, 0, sizeof tnl_vp);
    radeon_tcl_forget();
    radeon_tcl_defaults();
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
    s->tris_tnl = tnl_tris;
    s->mem_used = dgl_vram_used();
}

const dgl_backend dgl_radeon_backend = {
    .name = "radeon",
    .dev = &rg_radeon_device,
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
