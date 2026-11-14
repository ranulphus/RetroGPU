/* texture.c - the software core's texture sampling and combine (soft/soft.h;
 * gl/docs/b2.md B2.3): OpenGL 1.1's rules. Texel centres at half texels;
 * NEAREST the texel holding the point, LINEAR the four about it weighted by
 * the fraction; GL_REPEAT, GL_CLAMP (the border colour beyond the edge for
 * LINEAR) and GL_CLAMP_TO_EDGE; the level of detail as log2 of the largest
 * texel step a pixel makes along x or y (the approximation GL allows that
 * Mesa makes), magnification below c (0.5 for LINEAR beside a
 * NEAREST_MIPMAP_* minification, else 0), mipmap levels chosen and blended
 * as the four MIPMAP filters say. In floating point: exactness first. */
#include "soft/soft.h"
#include "internal.h"
#include "mga/fp.h"
#include <math.h>
#include <stddef.h>

static int wrap(int i, int n, int mode)
{
    if (mode == SOFT_REPEAT)
        return ((i % n) + n) % n;
    if (mode == SOFT_CLAMP_TO_EDGE)
        return i < 0 ? 0 : i >= n ? n - 1 : i;
    return i < 0 ? -1 : i >= n ? n : i;         /* (GL_CLAMP: outside is -1 or n, the border colour's) */
}

static void texel(const soft_texture *tx, const soft_level *L, int i, int j, float out[4])
{
    const uint8_t *p;
    if (i < 0 || i >= L->w || j < 0 || j >= L->h) {
        out[0] = tx->border[0], out[1] = tx->border[1], out[2] = tx->border[2], out[3] = tx->border[3];
        return;
    }
    p = L->rgba + ((size_t)j * (size_t)L->w + (size_t)i) * 4;
    out[0] = p[0] / 255.0f, out[1] = p[1] / 255.0f, out[2] = p[2] / 255.0f, out[3] = p[3] / 255.0f;
}

/* (s, t) into a level's texel space, by the wrap mode; NEAREST's texel. */
static double coord(double s, int n, int mode, int linear)
{
    double u;
    if (mode == SOFT_CLAMP)
        s = s < 0 ? 0 : s > 1 ? 1 : s;
    u = s * n;
    if (mode == SOFT_CLAMP_TO_EDGE) {
        double lo = linear ? 0.5 : 0.0, hi = linear ? n - 0.5 : (double)n;
        u = u < lo ? lo : u > hi ? hi : u;
    }
    return u;
}

static void sample_level(const soft_texture *tx, int lv, int linear, double s, double t, float out[4])
{
    const soft_level *L = &tx->level[lv];
    double u = coord(s, L->w, tx->wrap_s, linear), v = coord(t, L->h, tx->wrap_t, linear);
    if (!linear) {
        int i = (int)floor(u), j = (int)floor(v);
        if (tx->wrap_s != SOFT_REPEAT && i >= L->w) i = L->w - 1;      /* (s = 1 exactly: the last texel) */
        if (tx->wrap_t != SOFT_REPEAT && j >= L->h) j = L->h - 1;
        if (tx->wrap_s != SOFT_REPEAT && i < 0) i = 0;
        if (tx->wrap_t != SOFT_REPEAT && j < 0) j = 0;
        texel(tx, L, wrap(i, L->w, tx->wrap_s), wrap(j, L->h, tx->wrap_t), out);
    } else {
        double uu = u - 0.5, vv = v - 0.5, a, b;
        int i0 = (int)floor(uu), j0 = (int)floor(vv), k;
        float t00[4], t10[4], t01[4], t11[4];
        a = uu - i0, b = vv - j0;
        texel(tx, L, wrap(i0, L->w, tx->wrap_s), wrap(j0, L->h, tx->wrap_t), t00);
        texel(tx, L, wrap(i0 + 1, L->w, tx->wrap_s), wrap(j0, L->h, tx->wrap_t), t10);
        texel(tx, L, wrap(i0, L->w, tx->wrap_s), wrap(j0 + 1, L->h, tx->wrap_t), t01);
        texel(tx, L, wrap(i0 + 1, L->w, tx->wrap_s), wrap(j0 + 1, L->h, tx->wrap_t), t11);
        for (k = 0; k < 4; k++)
            out[k] = (float)((1 - a) * (1 - b) * t00[k] + a * (1 - b) * t10[k] + (1 - a) * b * t01[k] + a * b * t11[k]);
    }
}

void soft_sample(const soft_texture *tx, double s, double t, double dsdx, double dsdy, double dtdx, double dtdy,
                 float out[4])
{
    double lambda = -1e30, c = 0;
    int min = tx->min_filter, last = tx->levels - 1, k;
    if (min != tx->mag_filter || tx->levels > 1) {
        double w = tx->level[0].w, h = tx->level[0].h, rho;
        double ux = fabs(dsdx * w), uy = fabs(dsdy * w), vx = fabs(dtdx * h), vy = fabs(dtdy * h);
        rho = ux > uy ? ux : uy;
        if (vx > rho) rho = vx;
        if (vy > rho) rho = vy;
        lambda = rho > 0 ? log(rho) / log(2.0) : -1e30;
        if (tx->mag_filter == SOFT_LINEAR && (min == SOFT_NEAREST_MIPMAP_NEAREST || min == SOFT_NEAREST_MIPMAP_LINEAR))
            c = 0.5;
    }
    if (lambda <= c) {                          /* magnified */
        sample_level(tx, 0, tx->mag_filter == SOFT_LINEAR, s, t, out);
        return;
    }
    switch (min) {
    case SOFT_NEAREST:
    case SOFT_LINEAR:
        sample_level(tx, 0, min == SOFT_LINEAR, s, t, out);
        return;
    case SOFT_NEAREST_MIPMAP_NEAREST:
    case SOFT_LINEAR_MIPMAP_NEAREST: {
        int d = lambda <= 0.5 ? 0 : (int)ceil(lambda + 0.5) - 1;
        if (d > last) d = last;
        sample_level(tx, d, min == SOFT_LINEAR_MIPMAP_NEAREST, s, t, out);
        return;
    }
    default: {                                  /* *_MIPMAP_LINEAR */
        int linear = min == SOFT_LINEAR_MIPMAP_LINEAR, d = (int)floor(lambda);
        float a[4], b[4];
        double f = lambda - d;
        if (d >= last) {
            sample_level(tx, last, linear, s, t, out);
            return;
        }
        if (d < 0) d = 0, f = 0;
        sample_level(tx, d, linear, s, t, a);
        sample_level(tx, d + 1, linear, s, t, b);
        for (k = 0; k < 4; k++)
            out[k] = (float)((1 - f) * a[k] + f * b[k]);
        return;
    }
    }
}

/* ---- Integer sampling (B2.8's fast spans) -------------------------------- */

/* Bilinear filtering in two stages, along u and then v, each rounded to 8
 * bits (soft_lerp8: every product and sum fits 16 bits, as MMX's do). */

static int ilog2(int v)
{
    int l = 0;
    while ((1 << l) < v)
        l++;
    return l;
}

/* log2(v), v > 0, to about 1/300: the exponent and a table of the top 8
 * bits of the mantissa (a level of detail needs no more; log() costs a
 * hundred cycles a piece). */
static float log2_tab[256];

static double fast_log2(double v)
{
    union { double d; uint64_t u; } x;
    int e;
    if (!log2_tab[255]) {
        int m;
        for (m = 0; m < 256; m++)
            log2_tab[m] = (float)(log(1.0 + (m + 0.5) / 256.0) / log(2.0));
    }
    x.d = v;
    e = (int)((x.u >> 52) & 0x7FF) - 1023;
    return e + log2_tab[(x.u >> 44) & 255];
}

void soft_pick_lod(const soft_texture *tx, double dsdx, double dsdy, double dtdx, double dtdy, soft_pick *p)
{
    double lambda = -1e30, c = 0;
    int min = tx->min_filter, last = tx->levels - 1, k;
    p->n = 1, p->f8 = 0, p->lv[0] = p->lv[1] = 0;
    if (min != tx->mag_filter || tx->levels > 1) {     /* (as soft_sample) */
        double w = tx->level[0].w, h = tx->level[0].h, rho;
        double ux = fabs(dsdx * w), uy = fabs(dsdy * w), vx = fabs(dtdx * h), vy = fabs(dtdy * h);
        rho = ux > uy ? ux : uy;
        if (vx > rho) rho = vx;
        if (vy > rho) rho = vy;
        lambda = rho > 0 ? fast_log2(rho) : -1e30;
        if (tx->mag_filter == SOFT_LINEAR && (min == SOFT_NEAREST_MIPMAP_NEAREST || min == SOFT_NEAREST_MIPMAP_LINEAR))
            c = 0.5;
    }
    if (lambda <= c)
        p->linear = tx->mag_filter == SOFT_LINEAR;
    else
        switch (min) {
        case SOFT_NEAREST:
        case SOFT_LINEAR:
            p->linear = min == SOFT_LINEAR;
            break;
        case SOFT_NEAREST_MIPMAP_NEAREST:
        case SOFT_LINEAR_MIPMAP_NEAREST: {
            int d = lambda <= 0.5 ? 0 : lambda > 64 ? 64 : -mga_ifloor(-(lambda + 0.5)) - 1;
            p->lv[0] = d > last ? last : d;
            p->linear = min == SOFT_LINEAR_MIPMAP_NEAREST;
            break;
        }
        default: {
            int d = lambda > 64 ? 64 : mga_ifloor(lambda);
            double f = lambda - d;
            p->linear = min == SOFT_LINEAR_MIPMAP_LINEAR;
            if (d >= last)
                p->lv[0] = last;
            else {
                if (d < 0)
                    d = 0, f = 0;
                p->lv[0] = d, p->lv[1] = d + 1, p->f8 = (int)(f * 256 + 0.5);
                p->n = p->f8 ? 2 : 1;
            }
            break;
        }
        }
    for (k = 0; k < 2; k++) {
        p->sh_s[k] = ilog2(tx->level[0].w) - ilog2(tx->level[p->lv[k]].w);
        p->sh_t[k] = ilog2(tx->level[0].h) - ilog2(tx->level[p->lv[k]].h);
    }
}

static int wrap8(int i, int n, int mode)
{
    return mode == SOFT_REPEAT ? (i & (n - 1)) : i < 0 ? 0 : i >= n ? n - 1 : i;
}

static void texel8(const soft_level *L, int i, int j, int out[4])
{
    const uint8_t *p = L->rgba + ((size_t)j * (size_t)L->w + (size_t)i) * 4;
    out[0] = p[0], out[1] = p[1], out[2] = p[2], out[3] = p[3];
}

static void level8(const soft_texture *tx, int lv, int sh_s, int sh_t, int linear, int32_t U, int32_t V, int out[4])
{
    const soft_level *L = &tx->level[lv];
    int32_t u = U >> sh_s, v = V >> sh_t;
    int i0, j0, fu, fv, k, t00[4], t10[4], t01[4], t11[4], ia, ib, ja, jb;
    if (!linear) {
        texel8(L, wrap8(u >> 16, L->w, tx->wrap_s), wrap8(v >> 16, L->h, tx->wrap_t), out);
        return;
    }
    u -= 0x8000, v -= 0x8000;                   /* (the four texels about the point: centres at halves) */
    i0 = u >> 16, j0 = v >> 16, fu = (u >> 8) & 255, fv = (v >> 8) & 255;
    ia = wrap8(i0, L->w, tx->wrap_s), ib = wrap8(i0 + 1, L->w, tx->wrap_s);
    ja = wrap8(j0, L->h, tx->wrap_t), jb = wrap8(j0 + 1, L->h, tx->wrap_t);
    texel8(L, ia, ja, t00), texel8(L, ib, ja, t10), texel8(L, ia, jb, t01), texel8(L, ib, jb, t11);
    for (k = 0; k < 4; k++)
        out[k] = soft_lerp8(soft_lerp8(t00[k], t10[k], fu), soft_lerp8(t01[k], t11[k], fu), fv);
}

void soft_fetch8(const soft_texture *tx, const soft_pick *p, int32_t U, int32_t V, int out[4])
{
    level8(tx, p->lv[0], p->sh_s[0], p->sh_t[0], p->linear, U, V, out);
    if (p->n == 2) {
        int b[4], k;
        level8(tx, p->lv[1], p->sh_s[1], p->sh_t[1], p->linear, U, V, b);
        for (k = 0; k < 4; k++)
            out[k] = soft_lerp8(out[k], b[k], p->f8);
    }
}

/* ---- Combine ------------------------------------------------------------- */

static void arg(const soft_stage *st, int src, int op, const float *tex, const float *prim, const float *prev, float a[4])
{
    const float *p = src == SOFT_S_TEXTURE ? tex : src == SOFT_S_CONSTANT ? st->constant : src == SOFT_S_PRIMARY ? prim : prev;
    int k;
    for (k = 0; k < 4; k++) {
        float v = (op == SOFT_O_COLOR || op == SOFT_O_ONE_MINUS_COLOR) ? p[k] : p[3];
        a[k] = (op == SOFT_O_ONE_MINUS_COLOR || op == SOFT_O_ONE_MINUS_ALPHA) ? 1.0f - v : v;
    }
}

static float func(int f, float a0, float a1, float a2)
{
    switch (f) {
    case SOFT_C_REPLACE: return a0;
    case SOFT_C_MODULATE: return a0 * a1;
    case SOFT_C_ADD: return a0 + a1;
    case SOFT_C_ADD_SIGNED: return a0 + a1 - 0.5f;
    case SOFT_C_INTERPOLATE: return a0 * a2 + a1 * (1.0f - a2);
    default: return a0 - a1;                    /* SOFT_C_SUBTRACT */
    }
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void soft_combine_apply(const soft_stage *st, const float tex[4], const float prim[4], const float prev[4], float out[4])
{
    float a0[4], a1[4], a2[4];
    int k;
    arg(st, st->rgb.src[0], st->rgb.op[0], tex, prim, prev, a0);
    arg(st, st->rgb.src[1], st->rgb.op[1], tex, prim, prev, a1);
    arg(st, st->rgb.src[2], st->rgb.op[2], tex, prim, prev, a2);
    for (k = 0; k < 3; k++)
        out[k] = clamp01(func(st->rgb.func, a0[k], a1[k], a2[k]) * st->rgb.scale);
    arg(st, st->alpha.src[0], st->alpha.op[0], tex, prim, prev, a0);
    arg(st, st->alpha.src[1], st->alpha.op[1], tex, prim, prev, a1);
    arg(st, st->alpha.src[2], st->alpha.op[2], tex, prim, prev, a2);
    out[3] = clamp01(func(st->alpha.func, a0[3], a1[3], a2[3]) * st->alpha.scale);
}
