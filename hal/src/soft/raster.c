/* raster.c - the software core's triangles and clears (soft/soft.h; gl/docs/
 * b2.md B2.2): scanline spans. Exactness first: edges walked in integers on
 * the 1/16-pixel grid (two divisions an edge a row), attributes as planes in
 * double precision through the snapped vertices, taken at pixel centres.
 * The speed work (fixed-point steps, span routines per state, MMX, tiles)
 * comes in later steps against the same pixels. */
#include "soft/soft.h"
#include <math.h>
#include <string.h>

static soft_target T;
static soft_state S;
soft_stats soft_count;

void soft_target_set(const soft_target *t) { T = *t; }
void soft_state_set(const soft_state *s) { S = *s; }

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
        uint8_t *p = row + x, v = (uint8_t)((r >> 5) << 5 | (g >> 5) << 2 | (b >> 6));
        if ((m & 7u) != 7u) {
            uint8_t keep = (uint8_t)(((m & 1u) ? 0 : 0xE0u) | ((m & 2u) ? 0 : 0x1Cu) | ((m & 4u) ? 0 : 0x03u));
            v = (uint8_t)((*p & keep) | (v & ~keep));
        }
        *p = v;
    }
}

/* ---- Clears -------------------------------------------------------------- */

void soft_clear(unsigned what, const float rgba[4], double depth, unsigned stencil)
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

/* ---- Triangles ----------------------------------------------------------- */

/* An attribute's plane: its value at the first vertex, and its steps. */
typedef struct { double a0, dx, dy; } plane;

static plane plane_of(double a0, double a1, double a2, const double *px, const double *py, double det)
{
    plane p;
    p.a0 = a0;
    p.dx = ((a1 - a0) * (py[2] - py[0]) - (a2 - a0) * (py[1] - py[0])) / det;
    p.dy = ((a2 - a0) * (px[1] - px[0]) - (a1 - a0) * (px[2] - px[0])) / det;
    return p;
}

/* The first pixel whose centre is at or right of the edge Va->Vb on the row
 * whose centre is yc (all in 1/16 pixels; Yb > Ya). */
static int edge_x(int64_t Xa, int64_t Ya, int64_t Xb, int64_t Yb, int64_t yc)
{
    int64_t dy = Yb - Ya;
    return (int)ceil_div((Xa - 8) * dy + (yc - Ya) * (Xb - Xa), 16 * dy);
}

void soft_triangle(const soft_vertex *a, const soft_vertex *b, const soft_vertex *c)
{
    const soft_vertex *v[3] = { a, b, c }, *t;
    int64_t X[3], Y[3], area2;
    double px[3], py[3], det;
    plane pr, pg, pb, pa, pz, pq, pf = { 0, 0, 0 }, ps[2] = { { 0, 0, 0 }, { 0, 0, 0 } }, pt[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
    int i, j, j0, j1, long_left, k, texturing = 0;
    int depth_on = T.depth && S.depth_test, stencil_on = T.stencil && S.stencil_test;
    uint32_t zmax = depth_max();

    /* Snapped to 1/16 of a pixel, sorted top (smallest y) first. */
    if (v[1]->y < v[0]->y) t = v[0], v[0] = v[1], v[1] = t;
    if (v[2]->y < v[1]->y) t = v[1], v[1] = v[2], v[2] = t;
    if (v[1]->y < v[0]->y) t = v[0], v[0] = v[1], v[1] = t;
    for (i = 0; i < 3; i++) {
        X[i] = (int64_t)floor((double)v[i]->x * 16.0 + 0.5);
        Y[i] = (int64_t)floor((double)v[i]->y * 16.0 + 0.5);
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
    long_left = area2 > 0;                      /* (y down: the middle vertex right of the long edge) */
    det = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
    if (S.smooth) {
        pr = plane_of(v[0]->r * 255.0, v[1]->r * 255.0, v[2]->r * 255.0, px, py, det);
        pg = plane_of(v[0]->g * 255.0, v[1]->g * 255.0, v[2]->g * 255.0, px, py, det);
        pb = plane_of(v[0]->b * 255.0, v[1]->b * 255.0, v[2]->b * 255.0, px, py, det);
        pa = plane_of(v[0]->a * 255.0, v[1]->a * 255.0, v[2]->a * 255.0, px, py, det);
    } else {
        pr.a0 = v[0]->r * 255.0, pg.a0 = v[0]->g * 255.0, pb.a0 = v[0]->b * 255.0, pa.a0 = v[0]->a * 255.0;
        pr.dx = pr.dy = pg.dx = pg.dy = pb.dx = pb.dy = pa.dx = pa.dy = 0;
    }
    pz = plane_of(v[0]->z * (double)zmax, v[1]->z * (double)zmax, v[2]->z * (double)zmax, px, py, det);
    /* Texture coordinates over w, and 1/w: planes on the screen (perspective-correct per pixel). */
    pq = plane_of(v[0]->oow, v[1]->oow, v[2]->oow, px, py, det);
    for (k = 0; k < 2; k++)
        if (S.stage[k].tex) {
            int c = S.stage[k].coord;
            ps[k] = plane_of(v[0]->tex[c][0] * v[0]->oow, v[1]->tex[c][0] * v[1]->oow, v[2]->tex[c][0] * v[2]->oow, px, py, det);
            pt[k] = plane_of(v[0]->tex[c][1] * v[0]->oow, v[1]->tex[c][1] * v[1]->oow, v[2]->tex[c][1] * v[2]->oow, px, py, det);
            texturing = 1;
        }
    /* Fog: the coordinate (or, per vertex, the factor) over w. */
    if (S.fog) {
        double f0 = v[0]->fog, f1 = v[1]->fog, f2 = v[2]->fog;
        if (S.fog_per_vertex)
            f0 = fog_factor(f0), f1 = fog_factor(f1), f2 = fog_factor(f2);
        pf = plane_of(f0 * v[0]->oow, f1 * v[1]->oow, f2 * v[2]->oow, px, py, det);
    }

    /* The rows whose centres are in [Y0, Y2), within the scissor. */
    j0 = (int)ceil_div(Y[0] - 8, 16);
    j1 = (int)ceil_div(Y[2] - 8, 16);
    if (j0 < S.scissor[1]) j0 = S.scissor[1];
    if (j1 > S.scissor[3]) j1 = S.scissor[3];
    for (j = j0; j < j1; j++) {
        int64_t yc = 16 * (int64_t)j + 8;
        int xlong = edge_x(X[0], Y[0], X[2], Y[2], yc), xshort, xl, xr;
        double fy, r, g, bl, al, z, q, fv = 0, sv[2] = { 0, 0 }, tv[2] = { 0, 0 };
        if (yc < Y[1])
            xshort = edge_x(X[0], Y[0], X[1], Y[1], yc);
        else
            xshort = edge_x(X[1], Y[1], X[2], Y[2], yc);
        xl = long_left ? xlong : xshort;
        xr = long_left ? xshort : xlong;
        if (xl < S.scissor[0]) xl = S.scissor[0];
        if (xr > S.scissor[2]) xr = S.scissor[2];
        if (xl >= xr)
            continue;
        fy = (j + 0.5) - py[0];
        {
            double fx = (xl + 0.5) - px[0];
            r = pr.a0 + pr.dx * fx + pr.dy * fy;
            g = pg.a0 + pg.dx * fx + pg.dy * fy;
            bl = pb.a0 + pb.dx * fx + pb.dy * fy;
            al = pa.a0 + pa.dx * fx + pa.dy * fy;
            z = pz.a0 + pz.dx * fx + pz.dy * fy;
            q = pq.a0 + pq.dx * fx + pq.dy * fy;
            fv = pf.a0 + pf.dx * fx + pf.dy * fy;
            for (k = 0; k < 2; k++)
                if (S.stage[k].tex) {
                    sv[k] = ps[k].a0 + ps[k].dx * fx + ps[k].dy * fy;
                    tv[k] = pt[k].a0 + pt[k].dx * fx + pt[k].dy * fy;
                }
        }
        for (i = xl; i < xr; i++, r += pr.dx, g += pg.dx, bl += pb.dx, al += pa.dx, z += pz.dx, q += pq.dx,
                              fv += pf.dx, sv[0] += ps[0].dx, tv[0] += pt[0].dx, sv[1] += ps[1].dx, tv[1] += pt[1].dx) {
            float c[4];
            uint32_t zi = 0;
            uint8_t *sp = stencil_on ? T.stencil + (size_t)j * T.stencil_pitch + i : NULL;
            if (depth_on) {
                zi = (uint32_t)(z <= 0 ? 0 : z >= (double)zmax ? zmax : z);
                if (!S.alpha_test && !stencil_on && !depth_pass(zi, depth_get(i, j)))
                    continue;                   /* (nothing before the depth test could discard it: not shaded) */
            }
            /* The fragment's colour: the primary colour through the texture stages, then fog. */
            c[0] = clampf((float)(r / 255.0)), c[1] = clampf((float)(g / 255.0)), c[2] = clampf((float)(bl / 255.0)),
            c[3] = clampf((float)(al / 255.0));
            if (texturing) {
                float prim[4], tex[4];
                prim[0] = c[0], prim[1] = c[1], prim[2] = c[2], prim[3] = c[3];
                for (k = 0; k < 2; k++)
                    if (S.stage[k].tex) {
                        double s0 = sv[k] / q, t0 = tv[k] / q;
                        soft_sample(S.stage[k].tex, s0, t0, (ps[k].dx - s0 * pq.dx) / q, (ps[k].dy - s0 * pq.dy) / q,
                                    (pt[k].dx - t0 * pq.dx) / q, (pt[k].dy - t0 * pq.dy) / q, tex);
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
}
