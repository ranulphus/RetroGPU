/* raster.c - the software core's triangles and clears (soft/soft.h; gl/docs/
 * b2.md B2.2): scanline spans. Exactness first: edges walked in integers on
 * the 1/16-pixel grid (two divisions an edge a row), attributes as planes in
 * double precision through the snapped vertices, taken at pixel centres.
 * Tiles (B2.7): the same drawing, kept and binned, then done tile by tile.
 * The speed work (fixed-point steps, span routines per state, MMX) comes in
 * later steps against the same pixels. */
#include "soft/soft.h"
#include "internal.h"
#include "mga/fp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The x87 at 53 bits, rounding to nearest, inside the core's entry points:
 * GL's drawing calls already run so, but a flush can come from a swap or a
 * read under the program's own control word (Quake 2 runs at 24 bits). */
#define CORE_FPU_ENTER() FPU_ENTER()
#define CORE_FPU_LEAVE() FPU_LEAVE()

static soft_target T;
static soft_state S;                    /* the state drawing uses: the caller's, or a kept one's at a flush */
static soft_state U;                    /* the caller's */
static int state_new = 1;               /* U changed since it was last kept */
soft_stats soft_count;

static int tile_size;                   /* 0: draw at once */
static int kept_cmds;
static void keep_reset(void);
static void select_span(void);          /* the fast span for S and T, or the reference one */

void soft_target_set(const soft_target *t)
{
    if (kept_cmds && memcmp(&T, t, sizeof T))
        soft_flush();
    T = *t;
    select_span();
}

void soft_state_set(const soft_state *s)
{
    S = U = *s;
    state_new = 1;
    select_span();
}

static int64_t ceil_div(int64_t n, int64_t d)   /* d > 0 */
{
    return n >= 0 ? (n + d - 1) / d : -((-n) / d);
}

static int to8(double v)                        /* 0..255, rounded */
{
    return v <= 0 ? 0 : v >= 255 ? 255 : (int)(v + 0.5);
}

/* ---- Pixels ---------------------------------------------------------------- */

static uint32_t depth_max(void) { return T.depth_bits == 32 ? 0xFFFFFFFFu : 0xFFFFu; }

static int depth_pass(uint32_t z, uint32_t zb)
{
    switch (S.depth_func) {
    case SOFT_NEVER: return 0;
    case SOFT_LESS: return z < zb;
    case SOFT_EQUAL: return z == zb;
    case SOFT_LEQUAL: return z <= zb;
    case SOFT_GREATER: return z > zb;
    case SOFT_NOTEQUAL: return z != zb;
    case SOFT_GEQUAL: return z >= zb;
    default: return 1;
    }
}

static uint32_t depth_get(int x, int y)
{
    const uint8_t *p = T.depth + (size_t)y * T.depth_pitch;
    return T.depth_bits == 32 ? ((const uint32_t *)p)[x] : ((const uint16_t *)p)[x];
}

static void depth_put(int x, int y, uint32_t z)
{
    uint8_t *p = T.depth + (size_t)y * T.depth_pitch;
    if (T.depth_bits == 32)
        ((uint32_t *)p)[x] = z;
    else
        ((uint16_t *)p)[x] = (uint16_t)z;
}

/* 8 bits to n levels (top = 2^n - 1), with pixel (x, y)'s ordered-dither
 * threshold when dithering: floor(v * top / 255 + (B + 0.5) / 16), B the 4x4
 * Bayer matrix's entry, so a block's average is the colour; else rounded. */
static const uint8_t bayer4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

static unsigned levels(int v, unsigned top, int x, int y)
{
    unsigned q;
    if (!S.dither)
        return ((unsigned)v * top + 127) / 255;
    q = ((unsigned)v * top * 32 + (2u * bayer4[y & 3][x & 3] + 1) * 255) / 8160;
    return q > top ? top : q;
}

/* A pixel of 8-bit channels into the colour buffer, through the colour mask. */
static void color_put(int x, int y, int r, int g, int b, int a)
{
    uint8_t *row = T.color + (size_t)y * T.color_pitch;
    unsigned m = S.color_mask;
    (void)a;
    if (T.format == SOFT_RGB565) {
        uint16_t *p = (uint16_t *)row + x, v = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        if ((m & 7u) != 7u) {
            uint16_t keep = (uint16_t)(((m & 1u) ? 0 : 0xF800u) | ((m & 2u) ? 0 : 0x07E0u) | ((m & 4u) ? 0 : 0x001Fu));
            v = (uint16_t)((*p & keep) | (v & ~keep));
        }
        *p = v;
    } else if (T.format == SOFT_XRGB8888) {
        uint32_t *p = (uint32_t *)row + x, v = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b | (uint32_t)a << 24;
        if ((m & 15u) != 15u) {
            uint32_t keep = ((m & 1u) ? 0 : 0x00FF0000u) | ((m & 2u) ? 0 : 0x0000FF00u) | ((m & 4u) ? 0 : 0x000000FFu) |
                            ((m & 8u) ? 0 : 0xFF000000u);
            v = (*p & keep) | (v & ~keep);
        }
        *p = v;
    } else {
        uint8_t *p = row + x, v = (uint8_t)(levels(r, 7, x, y) << 5 | levels(g, 7, x, y) << 2 | levels(b, 3, x, y));
        if ((m & 7u) != 7u) {
            uint8_t keep = (uint8_t)(((m & 1u) ? 0 : 0xE0u) | ((m & 2u) ? 0 : 0x1Cu) | ((m & 4u) ? 0 : 0x03u));
            v = (uint8_t)((*p & keep) | (v & ~keep));
        }
        *p = v;
    }
}

/* ---- Clears -------------------------------------------------------------- */

static void draw_clear(unsigned what, const float rgba[4], double depth, unsigned stencil)
{
    int x, y, r = to8(rgba[0] * 255.0), g = to8(rgba[1] * 255.0), b = to8(rgba[2] * 255.0), a = to8(rgba[3] * 255.0);
    uint32_t z = (uint32_t)(depth <= 0 ? 0 : depth >= 1 ? depth_max() : depth * depth_max());
    for (y = S.scissor[1]; y < S.scissor[3]; y++)
        for (x = S.scissor[0]; x < S.scissor[2]; x++) {
            if ((what & SOFT_CLEAR_COLOR) && (S.color_mask & 15u))
                color_put(x, y, r, g, b, a);
            if ((what & SOFT_CLEAR_DEPTH) && T.depth)
                depth_put(x, y, z);
            if ((what & SOFT_CLEAR_STENCIL) && T.stencil)
                T.stencil[(size_t)y * T.stencil_pitch + x] = (uint8_t)stencil;
        }
}

/* ---- The per-pixel tests, fog and blending -------------------------------- */

static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static int compare(int func, unsigned a, unsigned b)    /* a FUNC b */
{
    switch (func) {
    case SOFT_NEVER: return 0;
    case SOFT_LESS: return a < b;
    case SOFT_EQUAL: return a == b;
    case SOFT_LEQUAL: return a <= b;
    case SOFT_GREATER: return a > b;
    case SOFT_NOTEQUAL: return a != b;
    case SOFT_GEQUAL: return a >= b;
    default: return 1;
    }
}

/* GL compares alpha and its reference as the colour buffer's fixed point would: here in 8 bits. */
static int alpha_pass(int a8) { return compare(S.alpha_func, (unsigned)a8, (unsigned)to8(S.alpha_ref * 255.0)); }

static int stencil_pass(unsigned sv) { return compare(S.stencil_func, S.stencil_ref & S.stencil_mask, sv & S.stencil_mask); }

static uint8_t stencil_op(int op, uint8_t sv)
{
    unsigned v = sv;
    switch (op) {
    case SOFT_SO_ZERO: v = 0; break;
    case SOFT_SO_REPLACE: v = S.stencil_ref; break;
    case SOFT_SO_INCR: v = sv < 255 ? sv + 1u : 255u; break;
    case SOFT_SO_DECR: v = sv > 0 ? sv - 1u : 0u; break;
    case SOFT_SO_INVERT: v = ~(unsigned)sv; break;
    default: return sv;                         /* KEEP */
    }
    return (uint8_t)((sv & ~S.stencil_writemask) | (v & S.stencil_writemask));
}

static double fog_factor(double d)
{
    double f;
    if (d < 0)
        d = -d;
    switch (S.fog_mode) {
    case SOFT_FOG_LINEAR: f = S.fog_end != S.fog_start ? (S.fog_end - d) / (S.fog_end - S.fog_start) : 1.0; break;
    case SOFT_FOG_EXP: f = exp(-S.fog_density * d); break;
    default: f = exp(-(S.fog_density * d) * (S.fog_density * d)); break;
    }
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

/* The colour buffer's pixel as 0..1 (alpha 1: no alpha bits). */
static void color_get(int x, int y, float d[4])
{
    const uint8_t *row = T.color + (size_t)y * T.color_pitch;
    if (T.format == SOFT_RGB565) {
        unsigned v = ((const uint16_t *)row)[x];
        d[0] = ((v >> 11) & 31) / 31.0f, d[1] = ((v >> 5) & 63) / 63.0f, d[2] = (v & 31) / 31.0f;
    } else if (T.format == SOFT_XRGB8888) {
        uint32_t v = ((const uint32_t *)row)[x];
        d[0] = ((v >> 16) & 255) / 255.0f, d[1] = ((v >> 8) & 255) / 255.0f, d[2] = (v & 255) / 255.0f;
    } else {
        unsigned v = row[x];
        d[0] = (v >> 5) / 7.0f, d[1] = ((v >> 2) & 7) / 7.0f, d[2] = (v & 3) / 3.0f;
    }
    d[3] = 1.0f;
}

static void factor(int f, const float *s, const float *d, float out[4])
{
    int k;
    for (k = 0; k < 4; k++)
        switch (f) {
        case SOFT_B_ZERO: out[k] = 0; break;
        case SOFT_B_ONE: out[k] = 1; break;
        case SOFT_B_SRC_COLOR: out[k] = s[k]; break;
        case SOFT_B_ONE_MINUS_SRC_COLOR: out[k] = 1 - s[k]; break;
        case SOFT_B_DST_COLOR: out[k] = d[k]; break;
        case SOFT_B_ONE_MINUS_DST_COLOR: out[k] = 1 - d[k]; break;
        case SOFT_B_SRC_ALPHA: out[k] = s[3]; break;
        case SOFT_B_ONE_MINUS_SRC_ALPHA: out[k] = 1 - s[3]; break;
        case SOFT_B_DST_ALPHA: out[k] = d[3]; break;
        case SOFT_B_ONE_MINUS_DST_ALPHA: out[k] = 1 - d[3]; break;
        default: out[k] = k == 3 ? 1.0f : (s[3] < 1 - d[3] ? s[3] : 1 - d[3]); break;   /* SRC_ALPHA_SATURATE */
        }
}

static void blend(int x, int y, float c[4])
{
    float d[4], fs[4], fd[4];
    int k;
    color_get(x, y, d);
    factor(S.blend_src, c, d, fs);
    factor(S.blend_dst, c, d, fd);
    for (k = 0; k < 4; k++)
        c[k] = clampf(c[k] * fs[k] + d[k] * fd[k]);
}

/* ---- Spans ----------------------------------------------------------------- */

/* An attribute's plane: its value at the origin, and its steps. */
typedef struct { double a0, dx, dy; } plane;

/* A primitive's attributes as planes about one origin (pixels). Colour in
 * 0..255, depth in the buffer's units; texture coordinates and the fog
 * coordinate over w, and 1/w (q), for perspective. */
typedef struct { int64_t v; int32_t dx, dy; } fixplane;    /* an integer plane: at the reference pixel, steps */

typedef struct {
    double ox, oy;
    plane r, g, b, a, z, q, f, s[2], t[2];
    /* The same as integer planes for the integer spans (B2.8), at the centre
     * of pixel (xref, yref), the origin's: colour in 16.16, depth in 16.14;
     * and, where a stage's coordinates are linear on the screen (q constant,
     * GL_REPEAT), s and t in level-0 texels, 16.16, modulo 2^32. */
    int xref, yref;
    fixplane fr, fg, fb, fa, fz;
    int affine[2];
    fixplane fu[2], fv[2];
} attrs;

/* Row j's pixels xl..xr-1 (inside the scissor): the fragment pipeline.
 * Each attribute at pixel i is the row's base (from j alone) plus i steps,
 * so a pixel's values do not depend on where its span starts: a span cut at
 * a tile's edge (B2.7) draws the same pixels as the whole one. */
typedef void fast_span_fn(int j, int xl, int xr, const attrs *A);
static fast_span_fn *fast_fn;           /* the fast span for S and T; NULL: the reference span below */

static void span(int j, int xl, int xr, const attrs *A)
{
    int i, k, texturing = S.stage[0].tex || S.stage[1].tex;
    if (fast_fn) {
        fast_fn(j, xl, xr, A);
        return;
    }
    int depth_on = T.depth && S.depth_test, stencil_on = T.stencil && S.stencil_test;
    uint32_t zmax = depth_max();
    double fx0 = 0.5 - A->ox, fy = (j + 0.5) - A->oy;
#define ROW(P) ((P).a0 + (P).dx * fx0 + (P).dy * fy)
    double r0 = ROW(A->r), g0 = ROW(A->g), b0 = ROW(A->b), a0 = ROW(A->a), z0 = ROW(A->z), q0 = ROW(A->q),
           f0 = ROW(A->f), s0r[2], t0r[2];
    for (k = 0; k < 2; k++)
        s0r[k] = ROW(A->s[k]), t0r[k] = ROW(A->t[k]);
#undef ROW
    for (i = xl; i < xr; i++) {
        double r = r0 + A->r.dx * i, g = g0 + A->g.dx * i, bl = b0 + A->b.dx * i, al = a0 + A->a.dx * i;
        double z = z0 + A->z.dx * i, q = q0 + A->q.dx * i, fv = f0 + A->f.dx * i;
        double sv[2], tv[2];
        float c[4];
        uint32_t zi = 0;
        uint8_t *sp = stencil_on ? T.stencil + (size_t)j * T.stencil_pitch + i : NULL;
        if (depth_on) {
            zi = (uint32_t)(z <= 0 ? 0 : z >= (double)zmax ? zmax : z);
            if (!S.alpha_test && !stencil_on && !depth_pass(zi, depth_get(i, j)))
                continue;                       /* (nothing before the depth test could discard it: not shaded) */
        }
        /* The fragment's colour: the primary colour through the texture stages, then fog. */
        c[0] = clampf((float)(r / 255.0)), c[1] = clampf((float)(g / 255.0)), c[2] = clampf((float)(bl / 255.0)),
        c[3] = clampf((float)(al / 255.0));
        if (texturing) {
            float prim[4], tex[4];
            prim[0] = c[0], prim[1] = c[1], prim[2] = c[2], prim[3] = c[3];
            for (k = 0; k < 2; k++)
                if (S.stage[k].tex) {
                    double s0, t0;
                    sv[k] = s0r[k] + A->s[k].dx * i, tv[k] = t0r[k] + A->t[k].dx * i;
                    s0 = sv[k] / q, t0 = tv[k] / q;
                    soft_sample(S.stage[k].tex, s0, t0, (A->s[k].dx - s0 * A->q.dx) / q, (A->s[k].dy - s0 * A->q.dy) / q,
                                (A->t[k].dx - t0 * A->q.dx) / q, (A->t[k].dy - t0 * A->q.dy) / q, tex);
                    soft_combine_apply(&S.stage[k], tex, prim, c, c);
                }
        }
        if (S.fog) {
            float f = (float)(S.fog_per_vertex ? fv / q : fog_factor(fv / q));
            f = clampf(f);
            for (k = 0; k < 3; k++)
                c[k] = f * c[k] + (1.0f - f) * S.fog_color[k];
        }
        /* The tests: alpha, stencil, depth. */
        if (S.alpha_test && !alpha_pass(to8(c[3] * 255.0)))
            continue;
        if (stencil_on && !stencil_pass(*sp)) {
            *sp = stencil_op(S.stencil_fail, *sp);
            continue;
        }
        if (depth_on) {
            if (!depth_pass(zi, depth_get(i, j))) {
                if (stencil_on)
                    *sp = stencil_op(S.stencil_zfail, *sp);
                continue;
            }
            if (S.depth_write)
                depth_put(i, j, zi);
        }
        if (stencil_on)
            *sp = stencil_op(S.stencil_zpass, *sp);
        if (S.blend)
            blend(i, j, c);
        soft_count.pixels++;
        color_put(i, j, to8(c[0] * 255.0), to8(c[1] * 255.0), to8(c[2] * 255.0), to8(c[3] * 255.0));
    }
}

/* ---- Fast spans (B2.8) ----------------------------------------------------- */

/* The same fragment pipeline in integers, one routine per class of state
 * (smooth or flat, depth or not, textured or not, blended or not; the alpha
 * test and the colour mask inside), chosen when S or T changes. Colour steps
 * in 16.16 and 16-bit depth in 16.14 from each row's value at a column fixed
 * for the primitive (its planes' origin), so a pixel's values depend on its
 * position alone; texture coordinates divided
 * exactly at every 16th column (x a multiple of 16) and stepped in 16.16
 * level-0 texels between, the level of detail per 16-pixel piece (from the
 * derivatives at its first column); colour arithmetic in 8 bits. Stencil,
 * fog, 32-bit depth and GL_CLAMP's border take the reference span, and so
 * does everything with soft_exact(1). */

static int exact_mode;                  /* soft_exact: 0 every fast path, 1 the reference span, 2 the generic
                                           integer spans only (the specialised runs checked against them) */
static int const8[2][4];                /* the stages' constants in 8 bits */
static uint8_t wide5[32], wide6[64], wide3[8];   /* a buffer's channel widened to 8 bits (its bits repeated) */
enum { BK_GENERIC, BK_ALPHA, BK_ADD, BK_MUL };
static int blend_kind;                  /* S's blending, where it is one of the common ones */
enum { SM_MOD, SM_TEX, SM_PRIM };
static int simple_rgb, simple_alpha;    /* the specialised textured runs' combine: modulate, texture, primary */

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static int mul255(int a, int b)         /* a * b / 255, rounded (0..255 each) */
{
    int x = a * b + 128;
    return (x + (x >> 8)) >> 8;
}

static void color_get8(int x, int y, int d[4])
{
    const uint8_t *row = T.color + (size_t)y * T.color_pitch;
    if (T.format == SOFT_RGB565) {
        unsigned v = ((const uint16_t *)row)[x];
        d[0] = wide5[v >> 11], d[1] = wide6[(v >> 5) & 63], d[2] = wide5[v & 31];
    } else if (T.format == SOFT_XRGB8888) {
        uint32_t v = ((const uint32_t *)row)[x];
        d[0] = (int)((v >> 16) & 255), d[1] = (int)((v >> 8) & 255), d[2] = (int)(v & 255);
    } else {
        unsigned v = row[x];
        d[0] = wide3[v >> 5], d[1] = wide3[(v >> 2) & 7], d[2] = (int)(v & 3) * 85;
    }
    d[3] = 255;                                 /* (no destination alpha) */
}

static int factor8(int f, const int *s, const int *d, int k)
{
    switch (f) {
    case SOFT_B_ZERO: return 0;
    case SOFT_B_ONE: return 255;
    case SOFT_B_SRC_COLOR: return s[k];
    case SOFT_B_ONE_MINUS_SRC_COLOR: return 255 - s[k];
    case SOFT_B_DST_COLOR: return d[k];
    case SOFT_B_ONE_MINUS_DST_COLOR: return 255 - d[k];
    case SOFT_B_SRC_ALPHA: return s[3];
    case SOFT_B_ONE_MINUS_SRC_ALPHA: return 255 - s[3];
    case SOFT_B_DST_ALPHA: return d[3];
    case SOFT_B_ONE_MINUS_DST_ALPHA: return 255 - d[3];
    default: return k == 3 ? 255 : (s[3] < 255 - d[3] ? s[3] : 255 - d[3]);  /* SRC_ALPHA_SATURATE */
    }
}

/* (The common kinds give what the general factors give, without them:
 * mul255(v, 255) is v, mul255(v, 0) is 0.) */
static void blend8(int x, int y, int c[4])
{
    int d[4], out[4], k;
    color_get8(x, y, d);
    switch (blend_kind) {
    case BK_ALPHA:                              /* SRC_ALPHA, ONE_MINUS_SRC_ALPHA */
        for (k = 0; k < 4; k++)
            out[k] = clamp255(mul255(c[k], c[3]) + mul255(d[k], 255 - c[3]));
        break;
    case BK_ADD:                                /* ONE, ONE */
        for (k = 0; k < 4; k++)
            out[k] = clamp255(c[k] + d[k]);
        break;
    case BK_MUL:                                /* DST_COLOR, ZERO or ZERO, SRC_COLOR */
        for (k = 0; k < 4; k++)
            out[k] = mul255(c[k], d[k]);
        break;
    default:
        for (k = 0; k < 4; k++)
            out[k] = clamp255(mul255(c[k], factor8(S.blend_src, c, d, k)) + mul255(d[k], factor8(S.blend_dst, c, d, k)));
        break;
    }
    c[0] = out[0], c[1] = out[1], c[2] = out[2], c[3] = out[3];
}

static int arg8(int unit, int src, int op, const int *tex, const int *prim, const int *prev, int k)
{
    const int *p = src == SOFT_S_TEXTURE ? tex : src == SOFT_S_CONSTANT ? const8[unit] : src == SOFT_S_PRIMARY ? prim : prev;
    int v = (op == SOFT_O_COLOR || op == SOFT_O_ONE_MINUS_COLOR) ? p[k] : p[3];
    return (op == SOFT_O_ONE_MINUS_COLOR || op == SOFT_O_ONE_MINUS_ALPHA) ? 255 - v : v;
}

static int func8(const soft_combine *cb, int a0, int a1, int a2)
{
    int v, x;
    switch (cb->func) {
    case SOFT_C_REPLACE: v = a0; break;
    case SOFT_C_MODULATE: v = mul255(a0, a1); break;
    case SOFT_C_ADD: v = a0 + a1; break;
    case SOFT_C_ADD_SIGNED: v = a0 + a1 - 128; break;
    case SOFT_C_INTERPOLATE: x = a0 * a2 + a1 * (255 - a2) + 128; v = (x + (x >> 8)) >> 8; break;
    default: v = a0 - a1; break;               /* SOFT_C_SUBTRACT */
    }
    return clamp255(cb->scale == 1.0f ? v : cb->scale == 2.0f ? 2 * v : 4 * v);
}

/* Stage `unit`'s combine of its texel, the primary colour and the previous stage's (c, replaced). */
static void comb8(int unit, const int *tex, const int *prim, int *c)
{
    const soft_stage *st = &S.stage[unit];
    int out[4], k;
    for (k = 0; k < 3; k++)
        out[k] = func8(&st->rgb, arg8(unit, st->rgb.src[0], st->rgb.op[0], tex, prim, c, k),
                       arg8(unit, st->rgb.src[1], st->rgb.op[1], tex, prim, c, k),
                       arg8(unit, st->rgb.src[2], st->rgb.op[2], tex, prim, c, k));
    out[3] = func8(&st->alpha, arg8(unit, st->alpha.src[0], st->alpha.op[0], tex, prim, c, 3),
                   arg8(unit, st->alpha.src[1], st->alpha.op[1], tex, prim, c, 3),
                   arg8(unit, st->alpha.src[2], st->alpha.op[2], tex, prim, c, 3));
    c[0] = out[0], c[1] = out[1], c[2] = out[2], c[3] = out[3];
}

/* A stage's texture coordinates on a row: s, t (over w) and q (1/w) at
 * column x's centre are b + dx * x; dy, their steps down (the level of detail). */
typedef struct {
    double sb, sdx, sdy, tb, tdx, tdy, qb, qdx, qdy;
} rowtex;

/* A 16-pixel piece of a row for one stage: the coordinates at its first
 * column and their steps (16.16 level-0 texels), and the level picked; or,
 * where q changes too much across it (or reaches 0), each pixel divided. */
typedef struct {
    int piece;                          /* x >> 4 (INT32_MIN: none yet) */
    int x0, per_pixel;
    int32_t U0, V0, dU, dV;
    soft_pick pick;
} texpiece;

/* u (level-0 texels) into 16.16: for GL_REPEAT modulo 2^32, a multiple of
 * every level's size in 16.16 (the masks wrap the rest); for
 * GL_CLAMP_TO_EDGE held within 8192 texels. */
static int32_t texfix(double u, int mode)
{
    double v = u * 65536.0;
    if (mode == SOFT_REPEAT)
        return v > -4e18 && v < 4e18 ? (int32_t)(uint32_t)(uint64_t)mga_ifloor64(v + 0.5) : 0;
    u = u < -8192 ? -8192 : u > 8192 ? 8192 : u;
    return mga_ifloor(u * 65536.0 + 0.5);
}

static int32_t stepfix(double du)       /* a step a pixel, within 1024 texels */
{
    du = du < -1024 ? -1024 : du > 1024 ? 1024 : du;
    return mga_ifloor(du * 65536.0 + 0.5);
}

static int32_t step_at(int32_t U0, int n, int32_t dU)  /* U0 + n dU, modulo 2^32 */
{
    return (int32_t)((uint32_t)U0 + (uint32_t)n * (uint32_t)dU);
}

/* s/q and t/q at column x's centre, and 1/q: a function of x alone (a
 * piece's far end is the next one's near end, the same values either way). */
typedef struct { int x; double s, t, iq, q; } texpt;

static void pt_at(const rowtex *R, int x, texpt *p)
{
    p->x = x;
    p->q = R->qb + R->qdx * x;
    p->iq = p->q != 0 ? 1.0 / p->q : 0.0;
    p->s = (R->sb + R->sdx * x) * p->iq, p->t = (R->tb + R->tdx * x) * p->iq;
}

/* The piece holding column x; *near: the near end's point when the last
 * piece computed it (its far end), else computed. */
static void piece_at(const soft_texture *tx, const rowtex *R, int x, texpiece *P, texpt *far)
{
    int w = tx->level[0].w, h = tx->level[0].h, x0 = x & ~15;
    texpt p0;
    if (far->x == x0)
        p0 = *far;
    else
        pt_at(R, x0, &p0);
    pt_at(R, x0 + 16, far);
    P->piece = x >> 4, P->x0 = x0;
    P->per_pixel = !(p0.q > 0 && far->q > 0 && far->q < 8 * p0.q && p0.q < 8 * far->q);
    if (!P->per_pixel) {
        if (tx->min_filter != tx->mag_filter || tx->levels > 1)
            soft_pick_lod(tx, (R->sdx - p0.s * R->qdx) * p0.iq, (R->sdy - p0.s * R->qdy) * p0.iq,
                          (R->tdx - p0.t * R->qdx) * p0.iq, (R->tdy - p0.t * R->qdy) * p0.iq, &P->pick);
        else
            soft_pick_lod(tx, 0, 0, 0, 0, &P->pick);   /* (one level, one filter: nothing to choose) */
        P->U0 = texfix(p0.s * w, tx->wrap_s), P->V0 = texfix(p0.t * h, tx->wrap_t);
        P->dU = stepfix((far->s - p0.s) * w / 16), P->dV = stepfix((far->t - p0.t) * h / 16);
    }
}

/* A stage on a span: its coordinates' row (perspective) and piece, or, linear
 * on the screen, U and V at column 0 of the row and their steps (modulo 2^32)
 * with the primitive's one level of detail. */
typedef struct {
    int affine;
    rowtex R;
    texpiece P;
    texpt far;                          /* the last piece's far end */
    int32_t U0, V0, dU, dV;
    soft_pick pick;
} stagecur;

static void stage_row(int k, const attrs *A, int j, stagecur *C)
{
    const soft_texture *tx = S.stage[k].tex;
    double fx0 = 0.5 - A->ox, fy = (j + 0.5) - A->oy;
    C->affine = A->affine[k];
    if (C->affine) {
        int64_t oy = j - A->yref, ox = -(int64_t)A->xref;
        double q = A->q.a0;
        C->U0 = (int32_t)(uint32_t)(uint64_t)(A->fu[k].v + ox * A->fu[k].dx + oy * A->fu[k].dy);
        C->V0 = (int32_t)(uint32_t)(uint64_t)(A->fv[k].v + ox * A->fv[k].dx + oy * A->fv[k].dy);
        C->dU = A->fu[k].dx, C->dV = A->fv[k].dx;
        soft_pick_lod(tx, A->s[k].dx / q, A->s[k].dy / q, A->t[k].dx / q, A->t[k].dy / q, &C->pick);
    } else {
        C->R.sb = A->s[k].a0 + A->s[k].dx * fx0 + A->s[k].dy * fy, C->R.sdx = A->s[k].dx, C->R.sdy = A->s[k].dy;
        C->R.tb = A->t[k].a0 + A->t[k].dx * fx0 + A->t[k].dy * fy, C->R.tdx = A->t[k].dx, C->R.tdy = A->t[k].dy;
        C->R.qb = A->q.a0 + A->q.dx * fx0 + A->q.dy * fy, C->R.qdx = A->q.dx, C->R.qdy = A->q.dy;
        C->P.piece = INT32_MIN;
        C->far.x = INT32_MIN;
    }
}

/* Stage k at the start of a chunk (from column x to the next multiple of
 * 16): U, V at x and their steps, and the pick; or *pp set, each pixel's
 * coordinates divided (pp_at). The same values a pixel as its piece gives it. */
static const soft_pick *chunk_start(int k, stagecur *C, int x, int32_t *U, int32_t *V, int32_t *dU, int32_t *dV,
                                    int *pp)
{
    *pp = 0;
    if (C->affine) {
        *U = step_at(C->U0, x, C->dU), *V = step_at(C->V0, x, C->dV), *dU = C->dU, *dV = C->dV;
        return &C->pick;
    }
    piece_at(S.stage[k].tex, &C->R, x, &C->P, &C->far);
    if (C->P.per_pixel) {
        *pp = 1, *U = *V = *dU = *dV = 0;
        return &C->P.pick;
    }
    *U = step_at(C->P.U0, x - C->P.x0, C->P.dU), *V = step_at(C->P.V0, x - C->P.x0, C->P.dV);
    *dU = C->P.dU, *dV = C->P.dV;
    return &C->P.pick;
}

/* A per-pixel piece's coordinates and level at column x (into C's pick). */
static const soft_pick *pp_at(int k, stagecur *C, int x, int32_t *U, int32_t *V)
{
    const soft_texture *tx = S.stage[k].tex;
    double q = C->R.qb + C->R.qdx * x, sv = C->R.sb + C->R.sdx * x, tv = C->R.tb + C->R.tdx * x, s0, t0;
    if (q == 0)
        q = 1e-30;
    s0 = sv / q, t0 = tv / q;
    soft_pick_lod(tx, (C->R.sdx - s0 * C->R.qdx) / q, (C->R.sdy - s0 * C->R.qdy) / q, (C->R.tdx - t0 * C->R.qdx) / q,
                  (C->R.tdy - t0 * C->R.qdy) / q, &C->P.pick);
    *U = texfix(s0 * tx->level[0].w, tx->wrap_s), *V = texfix(t0 * tx->level[0].h, tx->wrap_t);
    return &C->P.pick;
}

/* One level of a GL_REPEAT texture, fetched in the span (the same texels and
 * weights as soft_fetch8, the wrap by masks). */
typedef struct {
    const uint8_t *p;
    int w, wm, hm, sh_s, sh_t, linear;
} inlevel;

static void inlevel_of(const soft_texture *tx, const soft_pick *pk, inlevel *L)
{
    const soft_level *lv = &tx->level[pk->lv[0]];
    L->p = lv->rgba, L->w = lv->w, L->wm = lv->w - 1, L->hm = lv->h - 1;
    L->sh_s = pk->sh_s[0], L->sh_t = pk->sh_t[0], L->linear = pk->linear;
}

static __attribute__((always_inline)) inline void inl_fetch(const inlevel *L, int32_t U, int32_t V, int t[4])
{
    int32_t u = U >> L->sh_s, v = V >> L->sh_t;
    if (!L->linear) {
        const uint8_t *p = L->p + ((size_t)((v >> 16) & L->hm) * (size_t)L->w + (size_t)((u >> 16) & L->wm)) * 4;
        t[0] = p[0], t[1] = p[1], t[2] = p[2], t[3] = p[3];
    } else {
        int fu, fv, ia, ib, ja, jb, k;
        const uint8_t *r0, *r1;
        u -= 0x8000, v -= 0x8000;
        fu = (u >> 8) & 255, fv = (v >> 8) & 255;
        ia = (u >> 16) & L->wm, ib = (ia + 1) & L->wm, ja = (v >> 16) & L->hm, jb = (ja + 1) & L->hm;
        r0 = L->p + (size_t)ja * (size_t)L->w * 4, r1 = L->p + (size_t)jb * (size_t)L->w * 4;
        for (k = 0; k < 4; k++)
            t[k] = soft_lerp8(soft_lerp8(r0[ia * 4 + k], r0[ib * 4 + k], fu), soft_lerp8(r1[ia * 4 + k], r1[ib * 4 + k], fu), fv);
    }
}

static __attribute__((always_inline)) inline void fast_span(int j, int xl, int xr, const attrs *A, const int SMOOTH,
                                                            const int DEPTH, const int TEX, const int BLEND)
{
    int64_t ox = xl - A->xref, oy = j - A->yref;
    int i, k, atest = S.alpha_test;
    int32_t r, g, b, a, z = 0, dr = A->fr.dx, dg = A->fg.dx, db = A->fb.dx, da = A->fa.dx, dz = A->fz.dx;
    stagecur C[2];
    inlevel IL = { NULL, 0, 0, 0, 0, 0, 0 };    /* (TEX 2: the chunk's level, fetched here) */
    int inl = 0;
#define AT(F) ((int32_t)(uint32_t)(uint64_t)((F).v + ox * (F).dx + oy * (F).dy))
    r = AT(A->fr), g = AT(A->fg), b = AT(A->fb), a = AT(A->fa);
    if (DEPTH)
        z = AT(A->fz);
#undef AT
    if (TEX)
        for (k = 0; k < 2; k++)
            if (S.stage[k].tex)
                stage_row(k, A, j, &C[k]);
    for (i = xl; i < xr;) {
        /* A chunk: to the next multiple of 16 (the pieces' columns) with a texture, else the span. */
        int end = TEX ? ((i | 15) + 1 < xr ? (i | 15) + 1 : xr) : xr;
        int32_t U[2] = { 0, 0 }, V[2] = { 0, 0 }, dU[2] = { 0, 0 }, dV[2] = { 0, 0 };
        int pp[2] = { 0, 0 };
        const soft_pick *pk[2] = { NULL, NULL };
        if (TEX)
            for (k = 0; k < 2; k++)
                if (S.stage[k].tex)
                    pk[k] = chunk_start(k, &C[k], i, &U[k], &V[k], &dU[k], &dV[k], &pp[k]);
        if (TEX == 2 && (inl = !pp[0] && pk[0]->n == 1))
            inlevel_of(S.stage[0].tex, pk[0], &IL);
        for (; i < end; i++) {
            int c[4], prim[4];
            uint32_t zi = 0;
            if (DEPTH) {
                int32_t zz = z >> 14;
                zi = zz < 0 ? 0 : zz > 65535 ? 65535 : (uint32_t)zz;
                if (!atest && !depth_pass(zi, depth_get(i, j)))
                    goto next;
            }
            prim[0] = c[0] = clamp255((r + 0x8000) >> 16), prim[1] = c[1] = clamp255((g + 0x8000) >> 16);
            prim[2] = c[2] = clamp255((b + 0x8000) >> 16), prim[3] = c[3] = clamp255((a + 0x8000) >> 16);
            if (TEX == 1)
                for (k = 0; k < 2; k++)
                    if (S.stage[k].tex) {
                        int t[4];
                        if (pp[k]) {
                            int32_t u, v;
                            const soft_pick *p1 = pp_at(k, &C[k], i, &u, &v);
                            soft_fetch8(S.stage[k].tex, p1, u, v, t);
                        } else
                            soft_fetch8(S.stage[k].tex, pk[k], U[k], V[k], t);
                        comb8(k, t, prim, c);
                    }
            if (TEX == 2) {                     /* one GL_REPEAT texture, a simple combine (select_span) */
                int t[4];
                if (inl)
                    inl_fetch(&IL, U[0], V[0], t);
                else if (pp[0]) {
                    int32_t u, v;
                    const soft_pick *p1 = pp_at(0, &C[0], i, &u, &v);
                    soft_fetch8(S.stage[0].tex, p1, u, v, t);
                } else
                    soft_fetch8(S.stage[0].tex, pk[0], U[0], V[0], t);
                if (simple_rgb == SM_MOD)
                    c[0] = mul255(t[0], prim[0]), c[1] = mul255(t[1], prim[1]), c[2] = mul255(t[2], prim[2]);
                else if (simple_rgb == SM_TEX)
                    c[0] = t[0], c[1] = t[1], c[2] = t[2];
                c[3] = simple_alpha == SM_MOD ? mul255(t[3], prim[3]) : simple_alpha == SM_TEX ? t[3] : prim[3];
            }
            if (atest && !alpha_pass(c[3]))
                goto next;
            if (DEPTH) {
                if (atest && !depth_pass(zi, depth_get(i, j)))
                    goto next;
                if (S.depth_write)
                    depth_put(i, j, zi);
            }
            if (BLEND)
                blend8(i, j, c);
            soft_count.pixels++;
            color_put(i, j, c[0], c[1], c[2], c[3]);
        next:
            if (SMOOTH)                         /* (modulo 2^32, as the planes) */
                r = (int32_t)((uint32_t)r + (uint32_t)dr), g = (int32_t)((uint32_t)g + (uint32_t)dg),
                b = (int32_t)((uint32_t)b + (uint32_t)db), a = (int32_t)((uint32_t)a + (uint32_t)da);
            if (DEPTH)
                z = (int32_t)((uint32_t)z + (uint32_t)dz);
            if (TEX)
                for (k = 0; k < 2; k++)
                    U[k] = (int32_t)((uint32_t)U[k] + (uint32_t)dU[k]), V[k] = (int32_t)((uint32_t)V[k] + (uint32_t)dV[k]);
        }
    }
}

#define FAST(SM, DE, TE, BL) \
    static void fast_##SM##DE##TE##BL(int j, int xl, int xr, const attrs *A) { fast_span(j, xl, xr, A, SM, DE, TE, BL); }
#define FAST_TE(SM, DE, TE) FAST(SM, DE, TE, 0) FAST(SM, DE, TE, 1)
#define FAST_DE(SM, DE) FAST_TE(SM, DE, 0) FAST_TE(SM, DE, 1) FAST_TE(SM, DE, 2)
FAST_DE(0, 0) FAST_DE(0, 1) FAST_DE(1, 0) FAST_DE(1, 1)
#undef FAST_DE
#undef FAST_TE
#undef FAST
/* Indexed ((smooth * 2 + depth) * 3 + textured) * 2 + blended; textured 0 none, 1 general, 2 the simple runs. */
static fast_span_fn *const fast_tab[24] = {
    fast_0000, fast_0001, fast_0010, fast_0011, fast_0020, fast_0021, fast_0100, fast_0101, fast_0110, fast_0111,
    fast_0120, fast_0121, fast_1000, fast_1001, fast_1010, fast_1011, fast_1020, fast_1021, fast_1100, fast_1101,
    fast_1110, fast_1111, fast_1120, fast_1121,
};

/* A combine the simple runs do: SM_MOD (texture times primary), SM_TEX
 * (texture), SM_PRIM (primary); -1 otherwise. Stage 0's previous colour is
 * the primary; for alpha an operand's COLOR is its alpha too. */
static int simple_mode(const soft_combine *cb, int alpha)
{
    int ok0 = cb->op[0] == SOFT_O_COLOR || (alpha && cb->op[0] == SOFT_O_ALPHA);
    int ok1 = cb->op[1] == SOFT_O_COLOR || (alpha && cb->op[1] == SOFT_O_ALPHA);
    int tex0 = cb->src[0] == SOFT_S_TEXTURE, tex1 = cb->src[1] == SOFT_S_TEXTURE;
    int prim0 = cb->src[0] == SOFT_S_PRIMARY || cb->src[0] == SOFT_S_PREVIOUS;
    int prim1 = cb->src[1] == SOFT_S_PRIMARY || cb->src[1] == SOFT_S_PREVIOUS;
    if (cb->scale != 1.0f || !ok0)
        return -1;
    if (cb->func == SOFT_C_REPLACE)
        return tex0 ? SM_TEX : prim0 ? SM_PRIM : -1;
    if (cb->func == SOFT_C_MODULATE && ok1 && ((tex0 && prim1) || (prim0 && tex1)))
        return SM_MOD;
    return -1;
}

static void select_span(void)
{
    static int ready;
    int k, tex = 0, depth = T.depth && S.depth_test;
    fast_fn = NULL;
    if (!ready) {
        int v;
        for (v = 0; v < 32; v++) wide5[v] = (uint8_t)(v << 3 | v >> 2);
        for (v = 0; v < 64; v++) wide6[v] = (uint8_t)(v << 2 | v >> 4);
        for (v = 0; v < 8; v++) wide3[v] = (uint8_t)(v << 5 | v << 2 | v >> 1);
        ready = 1;
    }
    if (exact_mode == 1 || (S.stencil_test && T.stencil) || S.fog || (depth && T.depth_bits != 16))
        return;
    for (k = 0; k < 2; k++)
        if (S.stage[k].tex) {
            const soft_texture *tx = S.stage[k].tex;
            int c, w = tx->level[0].w, h = tx->level[0].h;
            if (tx->wrap_s == SOFT_CLAMP || tx->wrap_t == SOFT_CLAMP || w > 1024 || h > 1024 || (w & (w - 1)) ||
                (h & (h - 1)) || tx->levels < 1)
                return;
            for (c = 0; c < 4; c++)
                const8[k][c] = to8(S.stage[k].constant[c] * 255.0);
            tex = 1;
        }
    if (tex && exact_mode == 0 && S.stage[0].tex && !S.stage[1].tex && S.stage[0].tex->wrap_s == SOFT_REPEAT &&
        S.stage[0].tex->wrap_t == SOFT_REPEAT && (simple_rgb = simple_mode(&S.stage[0].rgb, 0)) >= 0 &&
        (simple_alpha = simple_mode(&S.stage[0].alpha, 1)) >= 0)
        tex = 2;
    blend_kind = BK_GENERIC;
    if (S.blend && exact_mode == 0) {
        int sf = S.blend_src, df = S.blend_dst;
        if (sf == SOFT_B_SRC_ALPHA && df == SOFT_B_ONE_MINUS_SRC_ALPHA)
            blend_kind = BK_ALPHA;
        else if (sf == SOFT_B_ONE && df == SOFT_B_ONE)
            blend_kind = BK_ADD;
        else if ((sf == SOFT_B_DST_COLOR && df == SOFT_B_ZERO) || (sf == SOFT_B_ZERO && df == SOFT_B_SRC_COLOR))
            blend_kind = BK_MUL;
    }
    fast_fn = fast_tab[((S.smooth != 0) * 2 + depth) * 6 + tex * 2 + (S.blend != 0)];
}

void soft_exact(int mode)
{
    soft_flush();
    exact_mode = mode;
    select_span();
}

/* ---- Coverage -------------------------------------------------------------- */

static int64_t floor_div(int64_t n, int64_t d)  /* d > 0 */
{
    return n >= 0 ? n / d : -((-n + d - 1) / d);
}

/* The first row whose centre is inside a top edge at y (1/16 pixels), by
 * the fill rule: at or below it, or below it. */
static int first_row(int64_t y)
{
    return S.fill_rule == SOFT_FILL_BOTTOM_LEFT ? (int)floor_div(y - 8, 16) + 1 : (int)ceil_div(y - 8, 16);
}

/* An edge Va->Vb (1/16 pixels; Yb > Ya) a row at a time: on the row whose
 * centre is yc, the first pixel whose centre is at or right of it is
 * q = ceil(N / D), N = (Xa - 8) dy + (yc - Ya)(Xb - Xa), D = 16 dy. N grows
 * by S = 16 (Xb - Xa) a row; q and the remainder q D - N step exactly, with
 * no division after the first row. */
typedef struct { int64_t q, rem, D, qs, rs; } edge;

static void edge_at(edge *e, int64_t Xa, int64_t Ya, int64_t Xb, int64_t Yb, int64_t yc)
{
    int64_t dy = Yb - Ya, N = (Xa - 8) * dy + (yc - Ya) * (Xb - Xa), S = 16 * (Xb - Xa);
    e->D = 16 * dy;
    e->q = ceil_div(N, e->D);
    e->rem = e->q * e->D - N;                   /* (0 <= rem < D) */
    e->qs = floor_div(S, e->D);
    e->rs = S - e->qs * e->D;                   /* (0 <= rs < D) */
}

static void edge_next(edge *e)
{
    e->q += e->qs;
    e->rem -= e->rs;
    if (e->rem < 0)
        e->q++, e->rem += e->D;
}

/* A triangle's pixels, its vertices on the 1/16 grid in any order: each
 * row's run between its left edge (centres on it in) and its right one
 * (centres on it out), the rows by the fill rule. */
static void cover(const int64_t *Xin, const int64_t *Yin, const attrs *A)
{
    int64_t X[3], Y[3], area2, t;
    int i, j, j0, j1, long_left, bottom = S.fill_rule == SOFT_FILL_BOTTOM_LEFT, upper = 0, lower = 0;
    edge el, eu, ew;                            /* the long edge, the upper and lower short ones */
    for (i = 0; i < 3; i++)
        X[i] = Xin[i], Y[i] = Yin[i];
    for (i = 0; i < 2; i++)                     /* (sorted by y, stably) */
        for (j = 0; j < 2 - i; j++)
            if (Y[j + 1] < Y[j]) {
                t = Y[j], Y[j] = Y[j + 1], Y[j + 1] = t;
                t = X[j], X[j] = X[j + 1], X[j + 1] = t;
            }
    area2 = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (area2 == 0 || Y[2] == Y[0])
        return;
    long_left = area2 > 0;                      /* (y down: the middle vertex right of the long edge) */
    j0 = first_row(Y[0]);
    j1 = first_row(Y[2]);
    if (j0 < S.scissor[1]) j0 = S.scissor[1];
    if (j1 > S.scissor[3]) j1 = S.scissor[3];
    if (j0 >= j1)
        return;
    edge_at(&el, X[0], Y[0], X[2], Y[2], 16 * (int64_t)j0 + 8);
    for (j = j0; j < j1; j++, edge_next(&el)) {
        int64_t yc = 16 * (int64_t)j + 8;
        int xlong = (int)el.q, xshort, xl, xr;
        if (bottom ? yc <= Y[1] : yc < Y[1]) {  /* (the upper short edge: rows from the top, one after another) */
            if (upper++)
                edge_next(&eu);
            else
                edge_at(&eu, X[0], Y[0], X[1], Y[1], yc);
            xshort = (int)eu.q;
        } else {                                /* (then the lower one) */
            if (lower++)
                edge_next(&ew);
            else
                edge_at(&ew, X[1], Y[1], X[2], Y[2], yc);
            xshort = (int)ew.q;
        }
        xl = long_left ? xlong : xshort;
        xr = long_left ? xshort : xlong;
        if (xl < S.scissor[0]) xl = S.scissor[0];
        if (xr > S.scissor[2]) xr = S.scissor[2];
        if (xl < xr)
            span(j, xl, xr, A);
    }
}

/* A plane as an integer one (fixplane) at pixel (xref, yref)'s centre, its
 * steps held to +-2^28 (a primitive steeper than that is under a pixel wide:
 * its spans are single pixels, which take no step). */
static int32_t fixstep28(double d)
{
    return d < -268435456.0 ? -268435456 : d > 268435456.0 ? 268435456 : mga_ifloor(d + 0.5);
}

static fixplane fixp(double a0, double pdx, double pdy, double fx, double fy, double sc)
{
    fixplane f;
    double v = (a0 + pdx * fx + pdy * fy) * sc;
    f.v = v > -4e18 && v < 4e18 ? mga_ifloor64(v + 0.5) : 0;
    f.dx = fixstep28(pdx * sc), f.dy = fixstep28(pdy * sc);
    return f;
}

/* A primitive's integer planes (attrs), under S (the caller's state). */
static void fix_planes(attrs *A)
{
    double fx, fy;
    int k;
    A->xref = A->ox > -1e9 && A->ox < 1e9 ? mga_ifloor(A->ox) : 0;
    A->yref = A->oy > -1e9 && A->oy < 1e9 ? mga_ifloor(A->oy) : 0;
    fx = A->xref + 0.5 - A->ox, fy = A->yref + 0.5 - A->oy;
    A->fr = fixp(A->r.a0, A->r.dx, A->r.dy, fx, fy, 65536.0);
    A->fg = fixp(A->g.a0, A->g.dx, A->g.dy, fx, fy, 65536.0);
    A->fb = fixp(A->b.a0, A->b.dx, A->b.dy, fx, fy, 65536.0);
    A->fa = fixp(A->a.a0, A->a.dx, A->a.dy, fx, fy, 65536.0);
    A->fz = fixp(A->z.a0, A->z.dx, A->z.dy, fx, fy, 16384.0);
    for (k = 0; k < 2; k++) {
        const soft_texture *tx = S.stage[k].tex;
        A->affine[k] = tx && A->q.dx == 0 && A->q.dy == 0 && A->q.a0 > 0 && tx->wrap_s == SOFT_REPEAT &&
                       tx->wrap_t == SOFT_REPEAT;
        if (A->affine[k]) {
            double su = tx->level[0].w / A->q.a0 * 65536.0, sv = tx->level[0].h / A->q.a0 * 65536.0;
            A->fu[k] = fixp(A->s[k].a0, A->s[k].dx, A->s[k].dy, fx, fy, su);
            A->fv[k] = fixp(A->t[k].a0, A->t[k].dx, A->t[k].dy, fx, fy, sv);
        }
    }
}

/* Drawing now, or kept for the tiles (below). */
static void emit_cover(const int64_t *X, const int64_t *Y, const attrs *A, const attrs **kept);
static void emit_rect(int x0, int y0, int x1, int y1, const attrs *A);

/* ---- Triangles ----------------------------------------------------------- */

static plane plane_of(double a0, double a1, double a2, const double *px, const double *py, double det)
{
    plane p;
    p.a0 = a0;
    p.dx = ((a1 - a0) * (py[2] - py[0]) - (a2 - a0) * (py[1] - py[0])) / det;
    p.dy = ((a2 - a0) * (px[1] - px[0]) - (a1 - a0) * (px[2] - px[0])) / det;
    return p;
}

static void triangle_now(const soft_vertex *a, const soft_vertex *b, const soft_vertex *c)
{
    const soft_vertex *v[3] = { a, b, c }, *t;
    int64_t X[3], Y[3], area2;
    double px[3], py[3], det;
    attrs A;
    int i, j, k;
    double zmax = (double)depth_max();

    /* Snapped to 1/16 of a pixel, sorted top (smallest y) first: the planes' origin the top vertex. */
    if (v[1]->y < v[0]->y) t = v[0], v[0] = v[1], v[1] = t;
    if (v[2]->y < v[1]->y) t = v[1], v[1] = v[2], v[2] = t;
    if (v[1]->y < v[0]->y) t = v[0], v[0] = v[1], v[1] = t;
    for (i = 0; i < 3; i++) {
        X[i] = mga_ifloor64((double)v[i]->x * 16.0 + 0.5);
        Y[i] = mga_ifloor64((double)v[i]->y * 16.0 + 0.5);
        px[i] = (double)X[i] / 16.0;
        py[i] = (double)Y[i] / 16.0;
    }
    if (Y[1] < Y[0] || Y[2] < Y[1]) {           /* (snapping can reorder equal-ish y: sort again on the grid) */
        for (i = 0; i < 2; i++)
            for (j = 0; j < 2 - i; j++)
                if (Y[j + 1] < Y[j]) {
                    int64_t ty = Y[j], tx = X[j];
                    double tpx = px[j], tpy = py[j];
                    Y[j] = Y[j + 1], X[j] = X[j + 1], px[j] = px[j + 1], py[j] = py[j + 1], t = v[j], v[j] = v[j + 1];
                    Y[j + 1] = ty, X[j + 1] = tx, px[j + 1] = tpx, py[j + 1] = tpy, v[j + 1] = t;
                }
    }
    area2 = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (area2 == 0 || Y[2] == Y[0])
        return;
    soft_count.tris++;
    memset(&A, 0, sizeof A);
    A.ox = px[0], A.oy = py[0];
    det = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
    if (S.smooth) {
        A.r = plane_of(v[0]->r * 255.0, v[1]->r * 255.0, v[2]->r * 255.0, px, py, det);
        A.g = plane_of(v[0]->g * 255.0, v[1]->g * 255.0, v[2]->g * 255.0, px, py, det);
        A.b = plane_of(v[0]->b * 255.0, v[1]->b * 255.0, v[2]->b * 255.0, px, py, det);
        A.a = plane_of(v[0]->a * 255.0, v[1]->a * 255.0, v[2]->a * 255.0, px, py, det);
    } else
        A.r.a0 = v[0]->r * 255.0, A.g.a0 = v[0]->g * 255.0, A.b.a0 = v[0]->b * 255.0, A.a.a0 = v[0]->a * 255.0;
    A.z = plane_of(v[0]->z * zmax, v[1]->z * zmax, v[2]->z * zmax, px, py, det);
    /* Texture coordinates over w, and 1/w: planes on the screen (perspective-correct per pixel). */
    A.q = plane_of(v[0]->oow, v[1]->oow, v[2]->oow, px, py, det);
    for (k = 0; k < 2; k++)
        if (S.stage[k].tex) {
            int cc = S.stage[k].coord;
            A.s[k] = plane_of(v[0]->tex[cc][0] * v[0]->oow, v[1]->tex[cc][0] * v[1]->oow, v[2]->tex[cc][0] * v[2]->oow,
                              px, py, det);
            A.t[k] = plane_of(v[0]->tex[cc][1] * v[0]->oow, v[1]->tex[cc][1] * v[1]->oow, v[2]->tex[cc][1] * v[2]->oow,
                              px, py, det);
        }
    /* Fog: the coordinate (or, per vertex, the factor) over w. */
    if (S.fog) {
        double f0 = v[0]->fog, f1 = v[1]->fog, f2 = v[2]->fog;
        if (S.fog_per_vertex)
            f0 = fog_factor(f0), f1 = fog_factor(f1), f2 = fog_factor(f2);
        A.f = plane_of(f0 * v[0]->oow, f1 * v[1]->oow, f2 * v[2]->oow, px, py, det);
    }
    fix_planes(&A);
    {
        const attrs *kept = NULL;
        emit_cover(X, Y, &A, &kept);
    }
}

/* ---- Lines and points ---------------------------------------------------- */

/* Rounded to the nearest whole number, halves to the even one (llvmpipe's
 * util_iround). */
static int64_t round_even(double v)
{
    double f = floor(v), d = v - f;
    if (d > 0.5 || (d == 0.5 && ((int64_t)f & 1)))
        f += 1;
    return (int64_t)f;
}

static int64_t snap16(float v) { return round_even(16.0 * (double)v); }
static int sgn(float v) { return v >= 0; }
static float ffloor(float v) { return (float)floor((double)v); }
static float ffabs(float v) { return v < 0 ? -v : v; }

/* A line's or a point's attributes, varying along x (axis 0), along y
 * (axis 1), or not at all (a's alone: axis -1). */
static void line_attrs(attrs *A, const soft_vertex *a, const soft_vertex *b, int axis)
{
    double d = axis == 0 ? (double)b->x - a->x : axis == 1 ? (double)b->y - a->y : 0, zmax = (double)depth_max();
    int k;
#define LINE_PLANE(P, VA, VB) \
    do { \
        double va_ = (VA), vb_ = (VB); \
        (P).a0 = va_; \
        if (axis == 0) (P).dx = (vb_ - va_) / d; \
        if (axis == 1) (P).dy = (vb_ - va_) / d; \
    } while (0)
    memset(A, 0, sizeof *A);
    A->ox = a->x, A->oy = a->y;
    if (S.smooth) {
        LINE_PLANE(A->r, a->r * 255.0, b->r * 255.0);
        LINE_PLANE(A->g, a->g * 255.0, b->g * 255.0);
        LINE_PLANE(A->b, a->b * 255.0, b->b * 255.0);
        LINE_PLANE(A->a, a->a * 255.0, b->a * 255.0);
    } else                                      /* (flat: the provoking vertex, b) */
        A->r.a0 = b->r * 255.0, A->g.a0 = b->g * 255.0, A->b.a0 = b->b * 255.0, A->a.a0 = b->a * 255.0;
    LINE_PLANE(A->z, a->z * zmax, b->z * zmax);
    LINE_PLANE(A->q, a->oow, b->oow);
    for (k = 0; k < 2; k++)
        if (S.stage[k].tex) {
            int c = S.stage[k].coord;
            LINE_PLANE(A->s[k], a->tex[c][0] * a->oow, b->tex[c][0] * b->oow);
            LINE_PLANE(A->t[k], a->tex[c][1] * a->oow, b->tex[c][1] * b->oow);
        }
    if (S.fog) {
        double fa = a->fog, fb = b->fog;
        if (S.fog_per_vertex)
            fa = fog_factor(fa), fb = fog_factor(fb);
        LINE_PLANE(A->f, fa * a->oow, fb * b->oow);
    }
#undef LINE_PLANE
}

/* A line: the parallelogram llvmpipe draws (lp_setup_line.c), its decisions
 * made in single precision as there. Each end's pixel is drawn or not by
 * the diamond-exit test; where the plain parallelogram would decide
 * otherwise, that end moves to the pixel's boundary along the line. The
 * width goes across the minor axis. */
static void line_now(const soft_vertex *a, const soft_vertex *b, float width)
{
    float dx = a->x - b->x, dy = a->y - b->y;
    float x1diff = a->x - ffloor(a->x) - 0.5f, y1diff = a->y - ffloor(a->y) - 0.5f;
    float x2diff = b->x - ffloor(b->x) - 0.5f, y2diff = b->y - ffloor(b->y) - 0.5f;
    float oax = 0, oay = 0, obx = 0, oby = 0;  /* the ends' moves */
    int draw_start, draw_end, will_start, will_end, xmajor, bottom = S.fill_rule == SOFT_FILL_BOTTOM_LEFT;
    int64_t X[4], Y[4], half = 8 * round_even(width < 1.0f ? 1.0 : (double)width);
    attrs A;
    if (dx == 0 && dy == 0)
        return;
    soft_count.lines++;
    xmajor = ffabs(dx) >= ffabs(dy);
    if (xmajor) {
        float dydx = dy / dx;
        if (y2diff == -0.5f && dy < 0.0f)
            y2diff = 0.5f;
        if (ffabs(x1diff) + ffabs(y1diff) < 0.5f)
            draw_start = 1;
        else if (sgn(x1diff) == sgn(-dx))
            draw_start = 0;
        else if (sgn(-y1diff) != sgn(dy))
            draw_start = 1;
        else {
            float yi = (a->y - ffloor(a->y)) + x1diff * dydx;
            draw_start = yi < 1.0f && yi > 0.0f;
        }
        if (ffabs(x2diff) + ffabs(y2diff) < 0.5f)
            draw_end = 0;
        else if (sgn(x2diff) != sgn(-dx))
            draw_end = 0;
        else if (sgn(-y2diff) == sgn(dy))
            draw_end = 1;
        else {
            float yi = (b->y - ffloor(b->y)) + x2diff * dydx;
            draw_end = yi < 1.0f && yi > 0.0f;
        }
        if (dx < 0.0f) {                        /* (a the left end) */
            will_start = x1diff <= 0.0f;
            if (will_start != draw_start)
                oax = -x1diff - 0.5f, oay = oax * dydx;
            will_end = x2diff > 0.0f;
            if (will_end != draw_end)
                obx = -x2diff - 0.5f, oby = obx * dydx;
        } else {
            will_start = x1diff > 0.0f;
            if (will_start != draw_start)
                oax = -x1diff + 0.5f, oay = oax * dydx;
            will_end = x2diff <= 0.0f;
            if (will_end != draw_end)
                obx = -x2diff + 0.5f, oby = obx * dydx;
        }
        X[0] = X[3] = snap16(a->x + oax);
        X[1] = X[2] = snap16(b->x + obx);
        Y[0] = snap16(a->y + oay) - half, Y[3] = Y[0] + 2 * half;
        Y[1] = snap16(b->y + oby) - half, Y[2] = Y[1] + 2 * half;
    } else {
        float dxdy = dx / dy;
        if (x2diff == -0.5f && dx < 0.0f)
            x2diff = 0.5f;
        if (ffabs(x1diff) + ffabs(y1diff) < 0.5f)
            draw_start = 1;
        else if (sgn(-y1diff) == sgn(dy))
            draw_start = 0;
        else if (sgn(x1diff) != sgn(-dx))
            draw_start = 1;
        else {
            float xi = (a->x - ffloor(a->x)) + y1diff * dxdy;
            draw_start = xi < 1.0f && xi > 0.0f;
        }
        if (ffabs(x2diff) + ffabs(y2diff) < 0.5f)
            draw_end = 0;
        else if (sgn(-y2diff) != sgn(dy))
            draw_end = 0;
        else if (sgn(x2diff) == sgn(-dx))
            draw_end = 1;
        else {
            float xi = (b->x - ffloor(b->x)) + y2diff * dxdy;
            draw_end = xi < 1.0f && xi >= 0.0f;
        }
        if (dy > 0.0f) {                        /* (a the bottom end) */
            will_start = bottom ? y1diff >= 0.0f : y1diff > 0.0f;
            will_end = bottom ? y2diff < 0.0f : y2diff <= 0.0f;
            if (will_start != draw_start)
                oay = -y1diff + 0.5f, oax = oay * dxdy;
            if (will_end != draw_end)
                oby = -y2diff + 0.5f, obx = oby * dxdy;
        } else {
            will_start = bottom ? y1diff < 0.0f : y1diff <= 0.0f;
            will_end = bottom ? y2diff >= 0.0f : y2diff > 0.0f;
            if (will_start != draw_start)
                oay = -y1diff - 0.5f, oax = oay * dxdy;
            if (will_end != draw_end)
                oby = -y2diff - 0.5f, obx = oby * dxdy;
        }
        Y[0] = Y[3] = snap16(a->y + oay);
        Y[1] = Y[2] = snap16(b->y + oby);
        X[0] = snap16(a->x + oax) - half, X[3] = X[0] + 2 * half;
        X[1] = snap16(b->x + obx) - half, X[2] = X[1] + 2 * half;
    }
    line_attrs(&A, a, b, xmajor ? 0 : 1);
    fix_planes(&A);
    {
        int64_t tx[3] = { X[0], X[1], X[2] }, ty[3] = { Y[0], Y[1], Y[2] };
        int64_t ux[3] = { X[0], X[2], X[3] }, uy[3] = { Y[0], Y[2], Y[3] };
        const attrs *kept = NULL;                /* (one copy of the attributes for both halves) */
        emit_cover(tx, ty, &A, &kept);
        emit_cover(ux, uy, &A, &kept);
    }
}

/* A point: OpenGL's square (llvmpipe's legacy points, lp_setup_point.c),
 * positions on a 1/256 grid there. A centre on a pixel's boundary goes, in y,
 * the way the fill rule's horizontal edges do. */
static void point_now(const soft_vertex *v, float size)
{
    int64_t X = round_even(256.0 * v->x), Y = round_even(256.0 * v->y) - (S.fill_rule == SOFT_FILL_BOTTOM_LEFT);
    int64_t w = (round_even(256.0 * size) + 127) >> 8, x0, y0;
    int j, xl, xr, j1;
    attrs A;
    if (w < 1)
        w = 1;
    if (w & 1)
        x0 = floor_div(X, 256) - (w - 1) / 2, y0 = floor_div(Y, 256) - (w - 1) / 2;
    else
        x0 = floor_div(X + 128, 256) - w / 2, y0 = floor_div(Y + 128, 256) - w / 2;
    soft_count.points++;
    line_attrs(&A, v, v, -1);
    fix_planes(&A);
    xl = x0 < -1 ? -1 : x0 > T.width ? T.width : (int)x0;      /* (within a pixel of the target: the rest is clipped) */
    j = y0 < -1 ? -1 : y0 > T.height ? T.height : (int)y0;
    xr = x0 + w < -1 ? -1 : x0 + w > T.width ? T.width : (int)(x0 + w);
    j1 = y0 + w < -1 ? -1 : y0 + w > T.height ? T.height : (int)(y0 + w);
    emit_rect(xl, j, xr, j1, &A);
}

/* A rectangle of pixels [x0, x1) x [y0, y1) within the scissor (a point's). */
static void draw_rect(int x0, int y0, int x1, int y1, const attrs *A)
{
    int j;
    if (x0 < S.scissor[0]) x0 = S.scissor[0];
    if (x1 > S.scissor[2]) x1 = S.scissor[2];
    if (y0 < S.scissor[1]) y0 = S.scissor[1];
    if (y1 > S.scissor[3]) y1 = S.scissor[3];
    if (x0 < x1)
        for (j = y0; j < y1; j++)
            span(j, x0, x1, A);
}

/* ---- Tiles (B2.7) ---------------------------------------------------------- */

/* The store: kept commands, their attributes and the states they were given
 * under, emptied at each flush. Each tile's bin lists the commands touching
 * it, in order. A command whose scissor is cut to the tile draws exactly the
 * tile's share of its pixels (rows and columns are clipped, and span's
 * values do not depend on where a span starts), so the tiles together draw
 * what the commands would have drawn at once. */
#define KEEP_BYTES (1u << 20)

enum { CMD_COVER, CMD_RECT, CMD_CLEAR };

typedef struct {
    soft_state st;
    soft_texture tex[2];                /* the stages' textures as they were (the texels by pointer) */
} kept_state;

typedef struct {
    int kind;
    const kept_state *ks;
    const attrs *A;
    int64_t X[3], Y[3];                 /* a triangle (1/16 pixels) */
    int x0, y0, x1, y1;                 /* a rectangle */
    unsigned what, stencil;             /* a clear */
    float rgba[4];
    double depth;
} kept_cmd;

typedef struct { uint32_t *at; int n, cap; } bin;

static uint8_t *store;
static size_t store_used;
static const kept_state *ks_now;        /* U as last kept (NULL: not since the flush) */
static bin *bins;
static int nbins, tiles_x, tiles_y;
static unsigned epoch_n;

static void *keep_bytes(size_t n)
{
    void *p;
    n = (n + 7) & ~(size_t)7;
    if (!store && !(store = (uint8_t *)malloc(KEEP_BYTES)))
        return NULL;
    if (store_used + n > KEEP_BYTES)
        return NULL;
    p = store + store_used;
    store_used += n;
    return p;
}

static void keep_reset(void)
{
    int i;
    for (i = 0; i < nbins; i++)
        bins[i].n = 0;
    store_used = 0;
    ks_now = NULL;
    state_new = 1;
    kept_cmds = 0;
    epoch_n++;
}

/* U is the state k kept (its textures' descriptions compared too). */
static int same_as_kept(const kept_state *k)
{
    soft_state a = U;
    int i;
    for (i = 0; i < 2; i++) {
        if (!a.stage[i].tex != !k->st.stage[i].tex)
            return 0;
        if (a.stage[i].tex) {
            if (memcmp(a.stage[i].tex, &k->tex[i], sizeof k->tex[i]))
                return 0;
            a.stage[i].tex = k->st.stage[i].tex;
        }
    }
    return !memcmp(&a, &k->st, sizeof a);
}

/* U as kept: the last copy if it is the same, else a new one; NULL: no room. */
static const kept_state *state_kept(void)
{
    kept_state *k;
    int i;
    if (ks_now && (!state_new || same_as_kept(ks_now))) {
        state_new = 0;
        return ks_now;
    }
    if (!(k = (kept_state *)keep_bytes(sizeof *k)))
        return NULL;
    k->st = U;
    for (i = 0; i < 2; i++)
        if (U.stage[i].tex) {
            k->tex[i] = *U.stage[i].tex;
            k->st.stage[i].tex = &k->tex[i];
        }
    ks_now = k;
    state_new = 0;
    return k;
}

/* The bins for the target's size; 0: no memory. */
static int grid(void)
{
    int tx = (T.width + tile_size - 1) / tile_size, ty = (T.height + tile_size - 1) / tile_size, i;
    if (tx * ty > nbins) {
        bin *b = (bin *)realloc(bins, (size_t)(tx * ty) * sizeof *b);
        if (!b)
            return 0;
        for (i = nbins; i < tx * ty; i++)
            b[i].at = NULL, b[i].n = b[i].cap = 0;
        bins = b, nbins = tx * ty;
    }
    tiles_x = tx, tiles_y = ty;
    return 1;
}

/* A new command under U; NULL: no room. */
static kept_cmd *new_cmd(int kind)
{
    const kept_state *k;
    kept_cmd *c;
    if ((!kept_cmds && !grid()) || !(k = state_kept()) || !(c = (kept_cmd *)keep_bytes(sizeof *c)))
        return NULL;
    memset(c, 0, sizeof *c);
    c->kind = kind;
    c->ks = k;
    return c;
}

/* Command c into the bins of the tiles its pixels [x0, x1) x [y0, y1) touch
 * within its scissor; 0: no memory (then nothing is binned). */
static int bin_cmd(const kept_cmd *c, int x0, int y0, int x1, int y1)
{
    const int *sc = c->ks->st.scissor;
    uint32_t off = (uint32_t)((const uint8_t *)c - store);
    int tx, ty, tx0, ty0, tx1, ty1, pass;
    if (x0 < sc[0]) x0 = sc[0];
    if (y0 < sc[1]) y0 = sc[1];
    if (x1 > sc[2]) x1 = sc[2];
    if (y1 > sc[3]) y1 = sc[3];
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > T.width) x1 = T.width;
    if (y1 > T.height) y1 = T.height;
    kept_cmds++;
    if (x0 >= x1 || y0 >= y1)
        return 1;                               /* (nothing of it is drawn) */
    tx0 = x0 / tile_size, tx1 = (x1 - 1) / tile_size, ty0 = y0 / tile_size, ty1 = (y1 - 1) / tile_size;
    for (pass = 0; pass < 2; pass++)            /* (room first, then the entries: all or none) */
        for (ty = ty0; ty <= ty1; ty++)
            for (tx = tx0; tx <= tx1; tx++) {
                bin *b = &bins[ty * tiles_x + tx];
                if (pass)
                    b->at[b->n++] = off;
                else if (b->n == b->cap) {
                    int cap = b->cap ? 2 * b->cap : 64;
                    uint32_t *a = (uint32_t *)realloc(b->at, (size_t)cap * sizeof *a);
                    if (!a) {
                        kept_cmds--;
                        return 0;
                    }
                    b->at = a, b->cap = cap;
                }
            }
    return 1;
}

static int clampi(int64_t v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)v; }

static void emit_cover(const int64_t *X, const int64_t *Y, const attrs *A, const attrs **kept)
{
    int tries;
    for (tries = 0; tile_size && tries < 2; tries++) {
        kept_cmd *c = NULL;
        if (!*kept) {
            attrs *k = (attrs *)keep_bytes(sizeof *k);
            if (k)
                *k = *A, *kept = k;
        }
        if (*kept && (c = new_cmd(CMD_COVER)) != NULL) {
            int64_t xmin = X[0], xmax = X[0], ymin = Y[0], ymax = Y[0];
            int i;
            for (i = 0; i < 3; i++) {
                c->X[i] = X[i], c->Y[i] = Y[i];
                if (X[i] < xmin) xmin = X[i];
                if (X[i] > xmax) xmax = X[i];
                if (Y[i] < ymin) ymin = Y[i];
                if (Y[i] > ymax) ymax = Y[i];
            }
            c->A = *kept;
            /* (a pixel drawn has its centre within the bounds: its index from floor(min / 16) to floor(max / 16)) */
            if (bin_cmd(c, clampi(floor_div(xmin, 16), -1, T.width + 1), clampi(floor_div(ymin, 16), -1, T.height + 1),
                        clampi(floor_div(xmax, 16) + 1, -1, T.width + 1),
                        clampi(floor_div(ymax, 16) + 1, -1, T.height + 1)))
                return;
        }
        soft_count.overflows++;                 /* (no room: draw what is kept, then try again) */
        soft_flush();
        *kept = NULL;
    }
    cover(X, Y, A);
}

static void emit_rect(int x0, int y0, int x1, int y1, const attrs *A)
{
    int tries;
    for (tries = 0; tile_size && tries < 2; tries++) {
        attrs *k = (attrs *)keep_bytes(sizeof *k);
        kept_cmd *c = k ? new_cmd(CMD_RECT) : NULL;
        if (c) {
            *k = *A;
            c->A = k, c->x0 = x0, c->y0 = y0, c->x1 = x1, c->y1 = y1;
            if (bin_cmd(c, x0, y0, x1, y1))
                return;
        }
        soft_count.overflows++;
        soft_flush();
    }
    draw_rect(x0, y0, x1, y1, A);
}

static void clear_now(unsigned what, const float rgba[4], double depth, unsigned stencil)
{
    int tries;
    for (tries = 0; tile_size && tries < 2; tries++) {
        kept_cmd *c = new_cmd(CMD_CLEAR);
        if (c) {
            c->what = what, c->stencil = stencil, c->depth = depth;
            memcpy(c->rgba, rgba, sizeof c->rgba);
            if (bin_cmd(c, 0, 0, T.width, T.height))
                return;
        }
        soft_count.overflows++;
        soft_flush();
    }
    draw_clear(what, rgba, depth, stencil);
}

static void flush_now(void)
{
    int tx, ty, i;
    if (!kept_cmds)
        return;
    soft_count.flushes++;
    for (ty = 0; ty < tiles_y; ty++)
        for (tx = 0; tx < tiles_x; tx++) {
            const bin *b = &bins[ty * tiles_x + tx];
            const kept_state *cur = NULL;
            for (i = 0; i < b->n; i++) {
                const kept_cmd *c = (const kept_cmd *)(store + b->at[i]);
                if (c->ks != cur) {             /* its state, its scissor cut to the tile */
                    const int *sc = c->ks->st.scissor;
                    cur = c->ks;
                    S = cur->st;
                    S.scissor[0] = sc[0] > tx * tile_size ? sc[0] : tx * tile_size;
                    S.scissor[1] = sc[1] > ty * tile_size ? sc[1] : ty * tile_size;
                    S.scissor[2] = sc[2] < (tx + 1) * tile_size ? sc[2] : (tx + 1) * tile_size;
                    S.scissor[3] = sc[3] < (ty + 1) * tile_size ? sc[3] : (ty + 1) * tile_size;
                    select_span();
                }
                if (c->kind == CMD_COVER)
                    cover(c->X, c->Y, c->A);
                else if (c->kind == CMD_RECT)
                    draw_rect(c->x0, c->y0, c->x1, c->y1, c->A);
                else
                    draw_clear(c->what, c->rgba, c->depth, c->stencil);
            }
        }
    S = U;
    select_span();
    keep_reset();
}

void soft_drop(void)
{
    S = U;
    select_span();
    keep_reset();
}

void soft_tiles(int size)
{
    soft_flush();
    tile_size = size > 0 ? size : 0;
}

int soft_pending(void) { return kept_cmds != 0; }
unsigned soft_epoch(void) { return epoch_n; }

/* ---- The entry points, at 53 bits (CORE_FPU_ENTER) -------------------------- */

void soft_triangle(const soft_vertex *a, const soft_vertex *b, const soft_vertex *c)
{
    CORE_FPU_ENTER();
    triangle_now(a, b, c);
    CORE_FPU_LEAVE();
}

void soft_line(const soft_vertex *a, const soft_vertex *b, float width)
{
    CORE_FPU_ENTER();
    line_now(a, b, width);
    CORE_FPU_LEAVE();
}

void soft_point(const soft_vertex *v, float size)
{
    CORE_FPU_ENTER();
    point_now(v, size);
    CORE_FPU_LEAVE();
}

void soft_clear(unsigned what, const float rgba[4], double depth, unsigned stencil)
{
    CORE_FPU_ENTER();
    clear_now(what, rgba, depth, stencil);
    CORE_FPU_LEAVE();
}

void soft_flush(void)
{
    CORE_FPU_ENTER();
    flush_now();
    CORE_FPU_LEAVE();
}
