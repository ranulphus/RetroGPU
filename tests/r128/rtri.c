/* rtri.c - RTRI: the Rage 128's 3D triangles (docs/rage128.md, R2) against OpenGL's rules, worked out here: NTRI's
 * scenes and reference (tests/tnt2/ntri.c, from which it is made; VTRI's before it) through the Rage 128's CCE.
 * A pixel is drawn when its centre
 * is inside the triangle (on an edge: the top-left rule), with each attribute the plane through the vertices at that
 * centre (texture coordinates perspective-correct when asked), textures sampled as OpenGL samples them (NEAREST,
 * LINEAR, the mipmap level from the footprint), and kept when its Z passes. At 640x480 in R5G6B5 with a 16-bit Z
 * buffer, read back from VRAM. The chip sets triangles up itself from 3D_RNDR_GEN_PRIM's vertices (window x and y, z
 * in [0, 1], 1/w, colours, the fog factor in the specular's top byte, s and t), so this checks its set-up and sampling.
 *
 *   RTRI [n=N] [only=CASE] [k=MIN,MAX] [chipbias=B] [snap=N]
 *
 * HX-TEST lines: map, mode, cce, 3d, then the cases: random, mesh, zbuf (Gouraud and Z); blend (the vertex's
 * alpha); tex-nearest, tex-linear, tex-persp, tex-persp-linear, tex-mip, tex-modulate, tex-8888, tex-alpha, tex-fog.
 * Each compares the pixels: coverage (drawn or not) and colour within a step or two of 5 bits (green's 6 halved). */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define ZOFF 0x100000u              /* the Z buffer, 1 MB in */
#define TOFF 0x200000u              /* textures, 2 MB in (each level 32-byte aligned after it) */

static uint16_t *ref;               /* the expected screen, RGB565 */
static uint16_t *refz;
static uint8_t *refm;               /* drawn there */
static int pitch;                   /* bytes */
static unsigned seed = 7;
static int ntri = 60;
static const char *only;
static float kover[2];              /* k=MIN,MAX: texels a pixel, for every textured case */
/* The mipmap level (the P3, 2026-11-19, NTRI0's mipsweep): the chip measures the footprint by sums of absolute
 * derivatives (lodmode 4: |du/dx| + |dv/dx| against the same in y, within what OpenGL allows; at 45 degrees it is
 * sqrt(2) times the longest derivative) and takes log2 rounded down; its LOD bias (FILTER bits 23-16, eighths of a
 * level) is added first, so +4 gives OpenGL's nearest level (lodsel 0). The defaults: that. For experiments: lodmode
 * 0-3, lodsel 1 (rounded down), lodlin (log2 linear between powers of two), lodbias (the reference's), chipbias. */
/* (the Rage 128, the P3, 2026-11-21: lodmode 3, the largest single derivative, at its bias byte 63 matched best:
 * 957 of 47,007 pixels off in tex-mip; OpenGL's (0) 2,304, the TNT2's (4) 34,953) */
static int lodmode = 3, lodsel, lodlin;
static int use_ring;                    /* (ring: the CCE from a ring in VRAM, not PIO) */
static int use_burst;                   /* (burst: PIO in whole lines through the FIFO page) */
static double lodbias;
static int chipbias = 0x3F;          /* (TEX_CNTL_C's LOD bias byte: Mesa's r128 driver's 63 for no bias) */
static int snap = 4;                /* snap=N: vertices rounded to 1/2^N pixel for both: the chip sets triangles up from
                                       x and y in 1/16 pixel (the P3, 2026-11-19: exact at 4 bits, not at 5); -1 none */

/* VTRI's vertex: window x and y, z 0..65535, colour 0..255, s and t (OpenGL's), 1/w. */
typedef struct {
    float x, y, z;
    float r, g, b, a;
    float s, t, oow;
} gl_vertex;

enum { TEX_ARGB8888 = 0, TEX_ARGB4444 = 1, TEX_ARGB1555 = 2 };
enum { ENV_DECAL = 1, ENV_MODULATE = 4 };            /* Direct3D's DECAL (the texel) and MODULATEALPHA (products) */
enum { BLEND_NONE = 0, BLEND_TEXTURE = 2, BLEND_SOURCE = 3 };   /* (SRC_ALPHA, ONE_MINUS_SRC_ALPHA: whose alpha) */

typedef struct {
    int fmt, log2size, levels, linear, persp, env;
} gl_texture;

/* What both draw with: the texture (its levels as ARGB8888 for the
 * reference), the environment, blending and fog. */
static gl_texture tx;
static int tex_on, blend, fog;
static uint32_t fogc = 0xC86432;    /* (200, 100, 50) */
static uint32_t *tl[10];

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

static double dmin(double a, double b) { return a < b ? a : b; }
static double dmax(double a, double b) { return a > b ? a : b; }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static float rf(float lo, float hi) { return lo + (hi - lo) * (float)rnd(100000) / 100000.0f; }

static int wrapi(int i, int n) { return ((i % n) + n) % n; }

/* Texel (i, j) of level n, wrapped: r, g, b, a. */
static void texel(int n, int i, int j, double c[4])
{
    int sz = 1 << (tx.log2size - n);
    uint32_t p = tl[n][wrapi(j, sz) * sz + wrapi(i, sz)];
    c[0] = (p >> 16) & 255, c[1] = (p >> 8) & 255, c[2] = p & 255, c[3] = p >> 24;
}

/* OpenGL's sample at (u, v), texels of the largest level, from level n. */
static void sample(int n, double u, double v, double c[4])
{
    double s = ldexp(1.0, -n);
    u *= s, v *= s;
    if (!tx.linear) {
        texel(n, (int)floor(u), (int)floor(v), c);
    } else {
        double t00[4], t10[4], t01[4], t11[4], fu, fv;
        int i0, j0, k;
        u -= 0.5, v -= 0.5;
        i0 = (int)floor(u), j0 = (int)floor(v);
        fu = u - i0, fv = v - j0;
        texel(n, i0, j0, t00), texel(n, i0 + 1, j0, t10), texel(n, i0, j0 + 1, t01), texel(n, i0 + 1, j0 + 1, t11);
        for (k = 0; k < 4; k++)
            c[k] = (1 - fu) * (1 - fv) * t00[k] + fu * (1 - fv) * t10[k] + (1 - fu) * fv * t01[k] + fu * fv * t11[k];
    }
}

static int q5(double c) { return (int)clampd(c, 0, 255) >> 3; }
static int q6(double c) { return (int)clampd(c, 0, 255) >> 2; }
static double x5(int c) { return (c << 3) | (c >> 2); }
static double x6(int c) { return (c << 2) | (c >> 4); }

/* OpenGL's rule for one triangle into ref/refz/refm. Edge functions at pixel
 * centres; a centre on an edge counts when the edge is a top or a left one. */
static void ref_tri(const gl_vertex *a, const gl_vertex *b, const gl_vertex *c, int zfunc, int zwrite)
{
    const gl_vertex *v[3] = { a, b, c };
    double area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    double dwx[3], dwy[3], q[3], su[3], sv[3], size = ldexp(1.0, tx.log2size);
    int x0, x1, y0, y1, x, y, i;
    if (area == 0)
        return;
    if (area < 0) {                 /* counter-clockwise on the screen (y down) */
        const gl_vertex *t = v[1];
        v[1] = v[2], v[2] = t;
        area = -area;
    }
    for (i = 0; i < 3; i++) {
        const gl_vertex *p = v[(i + 1) % 3], *r = v[(i + 2) % 3];
        dwx[i] = -(r->y - p->y) / area, dwy[i] = (r->x - p->x) / area;
        q[i] = tx.persp ? v[i]->oow : 1.0;
        su[i] = v[i]->s * size * q[i], sv[i] = v[i]->t * size * q[i];
    }
    x0 = (int)floor(dmin(a->x, dmin(b->x, c->x))) - 1, x1 = (int)ceil(dmax(a->x, dmax(b->x, c->x))) + 1;
    y0 = (int)floor(dmin(a->y, dmin(b->y, c->y))) - 1, y1 = (int)ceil(dmax(a->y, dmax(b->y, c->y))) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            double px = x + 0.5, py = y + 0.5, w[3], col[4];
            int in = 1;
            for (i = 0; i < 3; i++) {
                const gl_vertex *p = v[(i + 1) % 3], *r = v[(i + 2) % 3];
                double e = (r->x - p->x) * (py - p->y) - (r->y - p->y) * (px - p->x);
                /* (screen y down, clockwise: inside is e > 0; on the edge, a top edge
                 * (horizontal, going right) or a left one (going up) counts) */
                if (e < 0 || (e == 0 && !((r->y == p->y && r->x > p->x) || r->y < p->y)))
                    in = 0;
                w[i] = e / area;
            }
            if (!in)
                continue;
            {
                double z = w[0] * v[0]->z + w[1] * v[1]->z + w[2] * v[2]->z;
                double av = w[0] * v[0]->a + w[1] * v[1]->a + w[2] * v[2]->a;
                uint16_t zq = (uint16_t)dmin(dmax(floor(z), 0), 65535);
                uint16_t *zp = refz + y * W + x, *dp = ref + y * W + x;
                double alpha;
                if (zfunc >= 0) {
                    int pass = zfunc == 7 || (zfunc == 4 && zq < *zp) || (zfunc == 6 && zq <= *zp) ||
                               (zfunc == 1 && zq > *zp) || (zfunc == 3 && zq >= *zp) || (zfunc == 2 && zq == *zp) ||
                               (zfunc == 5 && zq != *zp);
                    if (!pass)
                        continue;
                    if (zwrite)
                        *zp = zq;
                }
                for (i = 0; i < 3; i++)
                    col[i] = w[0] * (&v[0]->r)[i] + w[1] * (&v[1]->r)[i] + w[2] * (&v[2]->r)[i];
                col[3] = av;
                if (tex_on) {
                    /* The texel position, perspective-correct, and its derivatives. */
                    double A = 0, Ax = 0, Ay = 0, Bv = 0, Bx = 0, By = 0, Q = 0, Qx = 0, Qy = 0;
                    double u, vv, ux, uy, vx, vy, t[4];
                    int n = 0;
                    for (i = 0; i < 3; i++) {
                        A += w[i] * su[i], Ax += dwx[i] * su[i], Ay += dwy[i] * su[i];
                        Bv += w[i] * sv[i], Bx += dwx[i] * sv[i], By += dwy[i] * sv[i];
                        Q += w[i] * q[i], Qx += dwx[i] * q[i], Qy += dwy[i] * q[i];
                    }
                    u = A / Q, vv = Bv / Q;
                    ux = (Ax - u * Qx) / Q, uy = (Ay - u * Qy) / Q;
                    vx = (Bx - vv * Qx) / Q, vy = (By - vv * Qy) / Q;
                    if (tx.levels > 1) {
                        /* OpenGL's: the longer of the footprint's two screen-axis derivatives (lodmode 0); for
                         * the chip's rule: the footprint's area (1), the texture axes' lengths (2), the largest
                         * single derivative (3); the nearest level (lodsel 0) or rounded down (1). */
                        double rho2 = lodmode == 1 ? fabs(ux * vy - uy * vx)
                                    : lodmode == 2 ? dmax(ux * ux + uy * uy, vx * vx + vy * vy)
                                    : lodmode == 3 ? dmax(dmax(ux * ux, uy * uy), dmax(vx * vx, vy * vy))
                                    : lodmode == 4 ? pow(dmax(fabs(ux) + fabs(vx), fabs(uy) + fabs(vy)), 2)
                                    : lodmode == 5 ? pow(dmax(fabs(ux) + fabs(uy), fabs(vx) + fabs(vy)), 2)
                                    : dmax(ux * ux + vx * vx, uy * uy + vy * vy);
                        double lod = (rho2 > 0 ? 0.5 * log(rho2) / log(2.0) : -16) + lodbias;
                        if (lodlin && rho2 > 0) {
                            int e;
                            double m = frexp(sqrt(rho2), &e);           /* (m in [0.5, 1)) */
                            lod = (e - 1) + (2 * m - 1) + lodbias;
                        }
                        n = lod <= 0 ? 0 : lodsel ? (int)floor(lod) : (int)ceil(lod + 0.5) - 1;
                        if (n > tx.levels - 1) n = tx.levels - 1;
                        if (n < 0) n = 0;
                    }
                    sample(n, u, vv, t);
                    if (tx.env == ENV_MODULATE) {
                        for (i = 0; i < 3; i++)
                            col[i] = t[i] * col[i] / 255.0;
                        col[3] = t[3] * av / 255.0;
                    } else {
                        for (i = 0; i < 3; i++)
                            col[i] = t[i];
                        col[3] = blend == BLEND_SOURCE ? av : t[3];
                    }
                    if (fog) {
                        double f = clampd(av, 0, 255) / 255.0;
                        col[0] = col[0] * f + ((fogc >> 16) & 255) * (1 - f);
                        col[1] = col[1] * f + ((fogc >> 8) & 255) * (1 - f);
                        col[2] = col[2] * f + (fogc & 255) * (1 - f);
                    }
                }
                if (blend) {
                    double d[3];
                    d[0] = x5(*dp >> 11), d[1] = x6((*dp >> 5) & 63), d[2] = x5(*dp & 31);
                    alpha = clampd(col[3], 0, 255) / 255.0;
                    for (i = 0; i < 3; i++)
                        col[i] = col[i] * alpha + d[i] * (1 - alpha);
                }
                *dp = (uint16_t)(q5(col[0]) << 11 | q6(col[1]) << 5 | q5(col[2]));
                refm[y * W + x] = 1;
            }
        }
}

static uint32_t ub(double v) { return (uint32_t)clampd(floor(v + 0.5), 0, 255); }

static void to_tl(const gl_vertex *g, r128_vertex *v)
{
    memset(v, 0, sizeof *v);
    v->x = g->x, v->y = g->y, v->z = g->z / 65535.0f, v->rhw = g->oow;
    v->argb = ub(g->a) << 24 | ub(g->r) << 16 | ub(g->g) << 8 | ub(g->b);
    v->spec = (fog ? ub(g->a) : 255u) << 24;            /* (the fog factor: 255 none, as the vertex's alpha here) */
    v->s = g->s, v->t = g->t;
}

static int zmode = -1, zupdate;                  /* VTRI's Z function codes (4: less) and the update */

static void both(const gl_vertex *a0, const gl_vertex *b0, const gl_vertex *c0, int zfunc, int zwrite)
{
    r128_vertex va, vb, vc;
    gl_vertex sa = *a0, sb = *b0, sc = *c0;
    const gl_vertex *a = &sa, *b = &sb, *c = &sc;
    if (snap >= 0) {
        float q = ldexpf(1.0f, snap);
        sa.x = floorf(sa.x * q + 0.5f) / q, sa.y = floorf(sa.y * q + 0.5f) / q;
        sb.x = floorf(sb.x * q + 0.5f) / q, sb.y = floorf(sb.y * q + 0.5f) / q;
        sc.x = floorf(sc.x * q + 0.5f) / q, sc.y = floorf(sc.y * q + 0.5f) / q;
    }
    to_tl(a, &va), to_tl(b, &vb), to_tl(c, &vc);
    r128_tri(&va, &vb, &vc);
    ref_tri(a, b, c, zfunc, zwrite);
}

/* The context's words for the state: the texture's control, combine, size and level offsets, the 3D enables, the
 * blend, Z's test (VTRI's codes: 0 never, 1 >, 2 =, 3 >=, 4 <, 5 !=, 6 <=, 7 always; the chip's: never, <, <=, =,
 * >=, >, !=, always), the fog colour. */
static uint32_t lvoff[10];

static void send_state(void)
{
    static const uint32_t dt[3] = { 6, 15, 3 };                  /* ARGB8888, ARGB4444, ARGB1555 */
    static const uint32_t zf[8] = { 0, 5, 3, 4, 1, 6, 2, 7 };
    uint32_t tc = 0, comb = 0, sp = 0;
    int i;
    r128_ctx3d *c = &r128_3d;
    c->tex_cntl_c = R128_TEX_CACHE_FLUSH | ((uint32_t)chipbias & 0xFFu) << R128_LOD_BIAS_SHIFT;
    if (tex_on) {
        int L = tx.log2size, small = tx.log2size - (tx.levels - 1);
        uint32_t mag = tx.linear ? 1u : 0u, min = tx.levels > 1 ? (tx.linear ? 4u : 2u) : mag;
        tc = dt[tx.fmt] << 16 | min << 1 | mag << 4 | (tx.levels > 1 ? 0 : 1u << 7);   /* (wrapped both ways) */
        /* INPUT_INTERP; DECAL: the texel and its alpha (COMB_DIS, the texture's factors); MODULATE: the products. */
        comb = 4u << 10 | 2u << 25 | 4u << 4 | 6u << 18 | (tx.env == ENV_MODULATE ? 3u | 3u << 14 : 0u);
        sp = (uint32_t)L | (uint32_t)L << 4 | (uint32_t)L << 8 | (uint32_t)small << 12;
        c->tex_cntl_c |= R128_TEXMAP_ENABLE;
    }
    if (blend)
        c->tex_cntl_c |= R128_ALPHA_ENABLE;
    if (fog)
        c->tex_cntl_c |= R128_FOG_ENABLE;
    if (zmode >= 0)
        c->tex_cntl_c |= R128_Z_ENABLE | (zupdate ? R128_Z_WRITE_ENABLE : 0);
    c->z_sten_cntl_c = (c->z_sten_cntl_c & ~(7u << R128_Z_TEST_SHIFT)) | zf[zmode >= 0 ? zmode : 7] << R128_Z_TEST_SHIFT;
    c->misc_3d_state_cntl_reg = R128_MISC_SCALE_3D_TEXMAP_SHADE | R128_MISC_SCALE_PIX_REPLICATE | 7u << R128_ALPHA_TEST_SHIFT |
        (blend ? (uint32_t)R128_BLEND_SRCALPHA << R128_ALPHA_BLEND_SRC_SHIFT | (uint32_t)R128_BLEND_INVSRCALPHA << R128_ALPHA_BLEND_DST_SHIFT
               : (uint32_t)R128_BLEND_ONE << R128_ALPHA_BLEND_SRC_SHIFT | (uint32_t)R128_BLEND_ZERO << R128_ALPHA_BLEND_DST_SHIFT);
    c->fog_color_c = fogc;
    r128_3d_format(R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB | R128_VC_FRMT_SPEC_FRGB | R128_VC_FRMT_S_T);
    c->prim_tex_cntl_c = tc, c->prim_texture_combine_cntl_c = comb, c->tex_size_pitch_c = sp;
    /* (slot n is the level of 2^n texels a side: the smallest level in slot 0, as Mesa's r128UploadTexImages
     * fills them, numLevels - 1 - i for level i: RMIP on the P3; with mipmapping off every slot the base) */
    for (i = 0; i < 11; i++)
        c->prim_tex_offset_c[i] = tx.levels > 1 && i < tx.levels ? lvoff[tx.levels - 1 - i] : lvoff[0];
    r128_3d_emit();
}

static void set_state(const gl_texture *t, int blend_mode, int fog_on)
{
    tex_on = t != 0;
    if (t)
        tx = *t;
    blend = blend_mode, fog = fog_on;
    send_state();
}

static void z_state(int func, int update)
{
    zmode = func, zupdate = update;
    send_state();
}

static void clear(void)
{
    r128_set_target(ZOFF, W * 2, 16);
    r128_set_clip(0, 0, W, H);
    r128_fill(0, 0, W, H, 0xFFFF);
    r128_set_target(0, pitch, 16);
    r128_fill(0, 0, W, H, 0);
    r128_sync(2000000);
    memset(ref, 0, W * H * 2);
    memset(refz, 0xFF, W * H * 2);
    memset(refm, 0, W * H);
}

/* The screen against ref: pixels drawn where they should not be (or not
 * drawn where they should), and colours more than tol steps of 5 bits out. */
static void compare(const char *name, double max_frac, int tol)
{
    long drawn = 0, cover = 0, colour = 0, hist[4] = { 0, 0, 0, 0 };
    int x, y;
    char first[96] = "";
    if (r128_sync(4000000) != 0) {
        hx_test(name, 0, "the engine never went idle (PM4_STAT %08lx GUI_STAT %08lx)",
                (unsigned long)r128_rd(R128_PM4_STAT), (unsigned long)r128_rd(R128_GUI_STAT));
        return;
    }
    for (y = 0; y < H; y++) {
        const volatile uint16_t *row = (const volatile uint16_t *)(r128.fb + (long)y * pitch);
        for (x = 0; x < W; x++) {
            uint16_t got = row[x], want = ref[y * W + x];
            int dr, dg, db;
            if (!refm[y * W + x]) {
                if (got != 0 && cover++ == 0)
                    sprintf(first, "coverage at %d,%d: %04x, want none", x, y, got);
                continue;
            }
            drawn++;
            if (got == 0 && want != 0) {
                if (cover++ == 0)
                    sprintf(first, "coverage at %d,%d: none, want %04x", x, y, want);
                continue;
            }
            dr = abs((got >> 11) - (want >> 11)), dg = (abs(((got >> 5) & 63) - ((want >> 5) & 63)) + 1) / 2,
            db = abs((got & 31) - (want & 31));
            if (dg > dr) dr = dg;
            if (db > dr) dr = db;
            hist[dr > 3 ? 3 : dr]++;
            if (dr > tol)
                if (colour++ == 0 && !first[0])
                    sprintf(first, "colour at %d,%d: %04x, want %04x", x, y, got, want);
        }
    }
    hx_test(name, drawn > 0 && cover <= max_frac * drawn && colour <= max_frac * drawn,
            "%ld drawn; coverage %ld, colour %ld off (steps 1/2/3+: %ld/%ld/%ld)%s%s", drawn, cover, colour, hist[1],
            hist[2], hist[3], first[0] ? "; first: " : "", first);
}

/* Within a 150-pixel box at (bx, by): triangles of every shape, small enough
 * that the reference keeps up on an emulated 486. */
static float bx, by;

static void random_box(void) { bx = rf(2, W - 152), by = rf(2, H - 152); }

static void random_vertex(gl_vertex *v)
{
    v->x = bx + rf(0, 150), v->y = by + rf(0, 150), v->z = rf(0, 65000);
    v->r = rf(16, 255), v->g = rf(16, 255), v->b = rf(16, 255), v->a = 255;   /* (never black: black is "not drawn") */
    v->s = v->t = 0, v->oow = 1;
}

/* Texture coordinates for a triangle: a random rotation and scale of the
 * screen (k texels of the largest level a pixel, kmin..kmax) and offset,
 * with 1/w from qmin to 1 when persp. */
static void map_texture(gl_vertex *v, int nv, float kmin, float kmax, float qmin)
{
    double th = rf(0, 6.2832f), k = rf(kmin, kmax) / ldexp(1.0, tx.log2size), s0 = rf(-2, 2), t0 = rf(-2, 2);
    double cs = cos(th) * k, sn = sin(th) * k;
    int i;
    for (i = 0; i < nv; i++) {
        double dx = v[i].x - bx, dy = v[i].y - by;
        v[i].s = (float)(s0 + dx * cs - dy * sn);
        v[i].t = (float)(t0 + dx * sn + dy * cs);
        v[i].oow = qmin < 1 ? rf(qmin, 1) : 1;
    }
}

/* A texture of random texels (each channel cmin..255; alpha random when
 * alpha is set, else opaque) in VRAM at TOFF and in tl[]. */
static int make_texture(int fmt, int log2size, int levels, int linear, int persp, int env, int cmin, int alpha)
{
    static uint8_t *buf;
    int n, i;
    gl_texture t;
    static uint8_t *lvbuf[10];
    const void *lv[10];
    int cpp = fmt == TEX_ARGB8888 ? 4 : 2;
    t.fmt = fmt, t.log2size = log2size, t.levels = levels;
    t.linear = linear, t.persp = persp, t.env = env;
    if (!buf && !(buf = malloc(512 * 512 * 4)))
        return -1;
    tx = t;
    for (n = 0; n < levels; n++) {
        int sz = 1 << (log2size - n);
        free(tl[n]);
        if (!(tl[n] = malloc((size_t)sz * sz * 4)))
            return -1;
        for (i = 0; i < sz * sz; i++) {
            unsigned r = cmin + rnd(256 - cmin), g = cmin + rnd(256 - cmin), b = cmin + rnd(256 - cmin);
            unsigned a = alpha ? rnd(256) : 255;
            uint32_t p;
            if (fmt == TEX_ARGB1555) {
                uint16_t h = (uint16_t)((a >= 128) << 15 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
                r = (r >> 3) << 3 | r >> 5, g = (g >> 3) << 3 | g >> 5, b = (b >> 3) << 3 | b >> 5;
                a = a >= 128 ? 255 : 0;
                buf[2 * i] = (uint8_t)h, buf[2 * i + 1] = (uint8_t)(h >> 8);
            } else if (fmt == TEX_ARGB4444) {
                uint16_t h = (uint16_t)((a >> 4) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4));
                r = (r >> 4) * 17, g = (g >> 4) * 17, b = (b >> 4) * 17, a = (a >> 4) * 17;
                buf[2 * i] = (uint8_t)h, buf[2 * i + 1] = (uint8_t)(h >> 8);
            } else {
                p = a << 24 | r << 16 | g << 8 | b;
                memcpy(buf + 4 * i, &p, 4);
            }
            tl[n][i] = a << 24 | r << 16 | g << 8 | b;
        }
        free(lvbuf[n]);
        if (!(lvbuf[n] = malloc((size_t)sz * sz * cpp)))
            return -1;
        memcpy(lvbuf[n], buf, (size_t)sz * sz * cpp);
        lv[n] = lvbuf[n];
    }
    r128_sync(2000000);
    {
        uint32_t off = TOFF;
        for (n = 0; n < levels; n++) {
            int sz = 1 << (log2size - n);
            volatile uint8_t *d = r128.fb + off;
            const uint8_t *src = (const uint8_t *)lv[n];
            for (i = 0; i < sz * sz * cpp; i++)
                d[i] = src[i];
            lvoff[n] = off;
            off = (off + (uint32_t)(sz * sz * cpp) + 31u) & ~31u;
        }
    }
    return 0;
}

static int want(const char *name) { return !only || !strcmp(only, name); }

/* A textured case: ntex triangles over a cleared screen (over a Gouraud
 * layer when blending), each in its own box, kmin..kmax texels a pixel. */
static void tex_case(const char *name, int fmt, int log2size, int levels, int linear, int persp, int env,
                     int blend_mode, int fog_on, float kmin, float kmax, double max_frac, int tol)
{
    int i, ntex = ntri / 2 > 4 ? ntri / 2 : 4;
    if (!want(name))
        return;
    if (kover[1] > 0)
        kmin = kover[0], kmax = kover[1];
    clear();
    z_state(-1, 0);
    if (blend_mode) {
        set_state(0, BLEND_NONE, 0);
        for (i = 0; i < ntex; i++) {
            gl_vertex g[3];
            random_box();
            random_vertex(&g[0]), random_vertex(&g[1]), random_vertex(&g[2]);
            both(&g[0], &g[1], &g[2], -1, 0);
        }
    }
    if (make_texture(fmt, log2size, levels, linear, persp, env, env == ENV_MODULATE ? 96 : 40,
                     blend_mode == BLEND_TEXTURE) < 0) {
        hx_test(name, 0, "no memory for the texture");
        return;
    }
    set_state(&tx, blend_mode, fog_on);
    hx_log("RTRI %s: %d triangles, fmt %d, %dx%d, %d levels%s%s", name, ntex, fmt, 1 << log2size, 1 << log2size,
           levels, linear ? ", linear" : "", persp ? ", perspective" : "");
    for (i = 0; i < ntex; i++) {
        gl_vertex v[3];
        int k;
        random_box();
        for (k = 0; k < 3; k++) {
            random_vertex(&v[k]);
            if (env == ENV_MODULATE)
                v[k].r = rf(128, 255), v[k].g = rf(128, 255), v[k].b = rf(128, 255);
            if (blend_mode == BLEND_SOURCE || fog_on)
                v[k].a = rf(0, 255);
        }
        map_texture(v, 3, kmin, kmax, persp ? 0.25f : 1);
        both(&v[0], &v[1], &v[2], -1, 0);
    }
    compare(name, max_frac, tol);
    set_state(0, BLEND_NONE, 0);
}

int main(int argc, char **argv)
{
    r128_mode m;
    int i, j, k;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "n=", 2))
            ntri = atoi(argv[i] + 2);
        else if (!strncmp(argv[i], "only=", 5))
            only = argv[i] + 5;
        else if (!strncmp(argv[i], "k=", 2))
            sscanf(argv[i] + 2, "%f,%f", &kover[0], &kover[1]);
        else if (!strncmp(argv[i], "snap=", 5))
            snap = atoi(argv[i] + 5);
        else if (!strcmp(argv[i], "ring"))
            use_ring = 1;
        else if (!strcmp(argv[i], "burst"))
            use_burst = 1;
        else if (!strcmp(argv[i], "burstwc"))
            use_burst = 2;
        else if (!strncmp(argv[i], "lodmode=", 8))
            lodmode = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "lodsel=", 7))
            lodsel = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "chipbias=", 9))
            chipbias = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "lodlin=", 7))
            lodlin = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "lodbias=", 8))
            lodbias = atof(argv[i] + 8);
    hx_init(1, argv, "rtri");                   /* (its own arguments only) */
    if (r128_find() != 0 || r128_map() != 0) {
        hx_test("map", 0, "no Rage 128");
        hx_done(1);
    }
    hx_test("map", 1, "Rage 128 %04x, %lu MB", r128.device_id, (unsigned long)(r128.vram_bytes >> 20));
    if (r128_find_mode(W, H, 16, &m) != 0 || r128_set_mode(&m) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    pitch = m.pitch_bytes;
    hx_test("mode", 1, "mode %03x, pitch %d", m.mode, pitch);
    if ((use_ring ? r128_cce_init_ring(NULL, r128.vram_bytes - 0x40000u, 0x10000u) : r128_cce_init(NULL)) != 0) {
        r128_text_mode();
        hx_test("cce", 0, "the CCE did not start");
        hx_done(1);
    }
    if (use_burst == 2 && r128_cce_burst_wc() != 0) {    /* (burstwc: the second aperture's page, write-combining) */
        hx_test("cce", 0, "no write-combining FIFO page (GLOS's answer %d)", sys_cache_wc_err);
        r128_text_mode();
        hx_done(1);
    }
    if (use_burst == 1)                         /* (the burst path through aperture 0's own FIFO page, uncached) */
        r128_cce_burst_window(r128.mmio + 0x1000);
    hx_test("cce", 1, "%s%s", use_ring ? "192BM, its ring in VRAM" : "192PIO",
            use_burst == 2 ? ", 32-byte bursts (write-combining)" : use_burst ? ", 32-byte lines" : "");
    ref = malloc(W * H * 2);
    refz = malloc(W * H * 2);
    refm = malloc(W * H);
    if (!ref || !refz || !refm)
        hx_done(1);
    r128_3d_init(0, pitch, 16, ZOFF, W * 2, 16);
    hx_test("3d", 1, "colour 0, Z %u", (unsigned)ZOFF);
    set_state(0, BLEND_NONE, 0);

    /* Random triangles, no Z, drawn in order over each other. */
    if (want("random")) {
        clear();
        z_state(-1, 0);
        hx_log("RTRI random: %d triangles", ntri);
        for (i = 0; i < ntri; i++) {
            gl_vertex a, b, c;
            random_box();
            random_vertex(&a), random_vertex(&b), random_vertex(&c);
            both(&a, &b, &c, -1, 0);
        }
        compare("random", 0.002, 1);
    }

    /* A mesh: a jittered grid of quads, each two triangles, every pixel of
     * its area drawn once (a gap or a pixel drawn twice shows as coverage). */
    if (want("mesh")) {
        static gl_vertex g[7][9];
        clear();
        hx_log("RTRI mesh: 8 x 6 quads");
        for (j = 0; j < 7; j++)
            for (k = 0; k < 9; k++) {
                gl_vertex *p = &g[j][k];
                p->x = 100 + k * 37.5f + (k && k < 8 ? rf(-12, 12) : 0);
                p->y = 100 + j * 37.0f + (j && j < 6 ? rf(-12, 12) : 0);
                p->z = 0, p->r = rf(20, 255), p->g = rf(20, 255), p->b = rf(20, 255), p->a = 255;
                p->s = p->t = 0, p->oow = 1;
            }
        for (j = 0; j < 6; j++)
            for (k = 0; k < 8; k++) {
                both(&g[j][k], &g[j][k + 1], &g[j + 1][k], -1, 0);
                both(&g[j][k + 1], &g[j + 1][k + 1], &g[j + 1][k], -1, 0);
            }
        compare("mesh", 0.002, 1);
    }

    /* Z: random triangles with depth, the nearer kept (less, with update). */
    if (want("zbuf")) {
        clear();
        z_state(4, 1);
        hx_log("RTRI zbuf: %d triangles", ntri);
        for (i = 0; i < ntri; i++) {
            gl_vertex a, b, c;
            if (i % 6 == 0)
                random_box();                   /* (several in each box, so they overlap) */
            random_vertex(&a), random_vertex(&b), random_vertex(&c);
            both(&a, &b, &c, 4, 1);
        }
        compare("zbuf", 0.005, 1);
    }

    /* The vertex's alpha, blended over what is there. */
    if (want("blend")) {
        clear();
        z_state(-1, 0);
        hx_log("RTRI blend: %d triangles", ntri);
        for (i = 0; i < ntri; i++) {
            gl_vertex a, b, c;
            if (i % 4 == 0)
                random_box();
            random_vertex(&a), random_vertex(&b), random_vertex(&c);
            set_state(0, i % 4 ? BLEND_SOURCE : BLEND_NONE, 0);
            a.a = rf(0, 255), b.a = rf(0, 255), c.a = rf(0, 255);
            both(&a, &b, &c, -1, 0);
        }
        set_state(0, BLEND_NONE, 0);
        compare("blend", 0.005, 2);
    }

    /* Textures: magnified and minified, NEAREST and LINEAR, affine and perspective, mipmapped, modulated, 32-bit,
     * blended by the texel's alpha, fogged; VTRI's cases and tolerances. */
    tex_case("tex-nearest", TEX_ARGB1555, 8, 1, 0, 0, ENV_DECAL, 0, 0, 0.2f, 1.5f, 0.03, 1);
    tex_case("tex-linear", TEX_ARGB1555, 8, 1, 1, 0, ENV_DECAL, 0, 0, 0.2f, 1.5f, 0.01, 2);
    tex_case("tex-persp", TEX_ARGB1555, 8, 1, 0, 1, ENV_DECAL, 0, 0, 0.2f, 1.0f, 0.03, 1);
    tex_case("tex-persp-linear", TEX_ARGB1555, 8, 1, 1, 1, ENV_DECAL, 0, 0, 0.2f, 1.0f, 0.01, 2);
    tex_case("tex-mip", TEX_ARGB1555, 7, 8, 0, 0, ENV_DECAL, 0, 0, 0.5f, 6.0f, 0.03, 1);
    tex_case("tex-modulate", TEX_ARGB4444, 6, 1, 1, 0, ENV_MODULATE, 0, 0, 0.1f, 1.0f, 0.02, 1);
    tex_case("tex-8888", TEX_ARGB8888, 5, 1, 0, 1, ENV_DECAL, 0, 0, 0.05f, 0.5f, 0.03, 1);
    tex_case("tex-alpha", TEX_ARGB4444, 8, 1, 1, 0, ENV_DECAL, BLEND_TEXTURE, 0, 0.2f, 1.0f, 0.02, 2);
    tex_case("tex-fog", TEX_ARGB1555, 8, 1, 1, 0, ENV_DECAL, 0, 1, 0.2f, 1.0f, 0.02, 2);

    hx_snap_screen("RTRI");
    r128_sync(1000000);
    r128_text_mode();
    hx_done(hx_failures() ? 1 : 0);
}
