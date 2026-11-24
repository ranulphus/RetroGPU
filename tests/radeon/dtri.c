/* dtri.c - DTRI: the Radeon's 3D triangles (docs/radeon.md, RD2) against OpenGL's rules, worked out here: DTRI's
 * scenes and reference (tests/r128/rtri.c, from which it is made; NTRI's and VTRI's before it) by plain register
 * writes (hal/src/radeon/tri.c: no command processor). A pixel is drawn when its centre is inside the triangle (on
 * an edge: the top-left rule), with each attribute the plane through the vertices at that centre (texture
 * coordinates perspective-correct when asked), textures sampled as OpenGL samples them (NEAREST, LINEAR, the
 * mipmap level from the footprint), and kept when its Z passes. At 640x480 in R5G6B5 with a 16-bit Z buffer, read
 * back from VRAM. Everything from megabyte 16 of the card on (base=MB): the P3's card's sound memory.
 *
 *   DTRI [n=N] [only=CASE] [k=MIN,MAX] [snap=N] [base=MB] [lodmode=N] [lodsel=N] [lodlin=N] [lodbias=F] [chipbias=B]
 *
 * HX-TEST lines: map, mode, 3d, then the cases: random, mesh, zbuf (Gouraud and Z); blend (the vertex's alpha);
 * tex-nearest, tex-linear, tex-persp, tex-persp-linear, tex-mip, tex-modulate, tex-8888, tex-alpha, tex-fog.
 * Each compares the pixels: coverage (drawn or not) and colour within a step or two of 5 bits (green's 6 halved). */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "radeon/regs3d_radeon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
static uint32_t base = 16u << 20;   /* (base=MB) the colour buffer, shown; the Z buffer 1 MB on; textures 2 MB on */
#define COFF base
#define ZOFF (base + 0x100000u)
#define TOFF (base + 0x200000u)

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
/* (the Radeon, the P3, 2026-11-24, DMIP's sweeps: the largest single derivative (lodmode 3), the level changing at
 * 1.5 times a power of two (lodlin: log2 taken linearly between powers of two, then the nearest level): 15 to 36
 * of about 47,000 pixels off in tex-mip on three seeds; OpenGL's rule (0) 2,246 to 10,939) */
static int lodmode = 3, lodsel, lodlin = 1;
static double lodbias;
static int chipbias = 0;             /* (PP_TXFILTER's LOD bias byte) */
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

/* (experiments, RD2: cfmt=HEX SE_COORD_FMT's word; vmode=1: s and t multiplied by 1/w here; 2: x, y and z
 * multiplied by w and the fourth coordinate w; 3: both) */
static int use_ring;                 /* (ring[=KB]: the command processor from a ring, RD3) */
static uint32_t ring_kb = 64;
static void cp_log(const char *stage, uint32_t a, uint32_t b)
{
    hx_log("DTRI-CP %s: %08lx %08lx", stage, (unsigned long)a, (unsigned long)b);
}
static int vmode, cfmt_set;
static uint32_t cfmt;

static void to_tl(const gl_vertex *g, radeon_vertex *v)
{
    memset(v, 0, sizeof *v);
    v->x = g->x, v->y = g->y, v->z = g->z / 65535.0f, v->rhw = g->oow;
    if (vmode & 2) {
        float w = 1.0f / g->oow;
        v->x *= w, v->y *= w, v->z *= w, v->rhw = w;
    }
    v->argb = ub(g->a) << 24 | ub(g->r) << 16 | ub(g->g) << 8 | ub(g->b);
    v->spec = (fog ? ub(g->a) : 255u) << 24;            /* (the fog factor: 255 none, as the vertex's alpha here) */
    v->s = g->s, v->t = g->t;
    if (vmode & 1)
        v->s *= g->oow, v->t *= g->oow;
}

static int zmode = -1, zupdate;                  /* VTRI's Z function codes (4: less) and the update */

static void both(const gl_vertex *a0, const gl_vertex *b0, const gl_vertex *c0, int zfunc, int zwrite)
{
    radeon_vertex va, vb, vc;
    gl_vertex sa = *a0, sb = *b0, sc = *c0;
    const gl_vertex *a = &sa, *b = &sb, *c = &sc;
    if (snap >= 0) {
        float q = ldexpf(1.0f, snap);
        sa.x = floorf(sa.x * q + 0.5f) / q, sa.y = floorf(sa.y * q + 0.5f) / q;
        sb.x = floorf(sb.x * q + 0.5f) / q, sb.y = floorf(sb.y * q + 0.5f) / q;
        sc.x = floorf(sc.x * q + 0.5f) / q, sc.y = floorf(sc.y * q + 0.5f) / q;
    }
    to_tl(a, &va), to_tl(b, &vb), to_tl(c, &vc);
    radeon_tri(&va, &vb, &vc);
    ref_tri(a, b, c, zfunc, zwrite);
}

/* The context's words for the state: the texture's control, combine, size and level offsets, the 3D enables, the
 * blend, Z's test (VTRI's codes: 0 never, 1 >, 2 =, 3 >=, 4 <, 5 !=, 6 <=, 7 always; the chip's: never, <, <=, =,
 * >=, >, !=, always), the fog colour. */
static uint32_t lvoff[10];

static void send_state(void)
{
    static const uint32_t dt[3] = { RADEON_TXFORMAT_ARGB8888, RADEON_TXFORMAT_ARGB4444, RADEON_TXFORMAT_ARGB1555 };
    static const uint32_t zf[8] = { 0, 5, 3, 4, 1, 6, 2, 7 };
    radeon_ctx3d *c = &radeon_3d;
    c->pp_cntl &= ~(RADEON_TEX_0_ENABLE | RADEON_FOG_ENABLE);
    c->pp_cntl |= RADEON_TEX_BLEND_0_ENABLE;
    c->rb3d_cntl &= ~(RADEON_ALPHA_BLEND_ENABLE | RADEON_Z_ENABLE);
    c->rb3d_zstencilcntl &= ~(RADEON_Z_TEST_MASK | RADEON_Z_WRITE_ENABLE);
    if (tex_on) {
        uint32_t L = (uint32_t)tx.log2size;
        /* (the level filters: NEAREST_MIP_NEAREST and LINEAR_MIP_NEAREST with levels, as DTRI's) */
        c->tex[0].txfilter = (tx.linear ? RADEON_MAG_FILTER_LINEAR : RADEON_MAG_FILTER_NEAREST) |
            (tx.levels > 1 ? (tx.linear ? RADEON_MIN_FILTER_LINEAR_MIP_NEAREST : RADEON_MIN_FILTER_NEAREST_MIP_NEAREST)
                           : (tx.linear ? RADEON_MIN_FILTER_LINEAR : RADEON_MIN_FILTER_NEAREST)) |
            ((uint32_t)chipbias & 0xFFu) << RADEON_LOD_BIAS_SHIFT |
            (uint32_t)(tx.levels - 1) << RADEON_MAX_MIP_LEVEL_SHIFT | RADEON_CLAMP_S_WRAP | RADEON_CLAMP_T_WRAP;
        c->tex[0].txformat = dt[tx.fmt] | RADEON_TXFORMAT_ALPHA_IN_MAP | L << RADEON_TXFORMAT_WIDTH_SHIFT |
            L << RADEON_TXFORMAT_HEIGHT_SHIFT | RADEON_TXFORMAT_ST_ROUTE_STQ0 | RADEON_TXFORMAT_PERSPECTIVE_ENABLE;
        c->tex[0].txoffset = radeon.fb_location + lvoff[0];
        /* (the unit computes A * B + C. DECAL: the texel and its alpha; MODULATE: the products with the vertex's) */
        if (tx.env == ENV_MODULATE) {
            c->tex[0].txcblend = RADEON_COLOR_ARG_A_T0_COLOR | RADEON_COLOR_ARG_B_DIFFUSE_COLOR | RADEON_COLOR_ARG_C_ZERO |
                                 RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
            c->tex[0].txablend = RADEON_ALPHA_ARG_A_T0_ALPHA | RADEON_ALPHA_ARG_B_DIFFUSE_ALPHA | RADEON_ALPHA_ARG_C_ZERO |
                                 RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        } else {
            c->tex[0].txcblend = RADEON_COLOR_ARG_A_ZERO | RADEON_COLOR_ARG_B_ZERO | RADEON_COLOR_ARG_C_T0_COLOR |
                                 RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
            c->tex[0].txablend = RADEON_ALPHA_ARG_A_ZERO | RADEON_ALPHA_ARG_B_ZERO | RADEON_ALPHA_ARG_C_T0_ALPHA |
                                 RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        }
        c->pp_cntl |= RADEON_TEX_0_ENABLE;
    } else {
        c->tex[0].txcblend = RADEON_COLOR_ARG_A_ZERO | RADEON_COLOR_ARG_B_ZERO | RADEON_COLOR_ARG_C_DIFFUSE_COLOR |
                             RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        c->tex[0].txablend = RADEON_ALPHA_ARG_A_ZERO | RADEON_ALPHA_ARG_B_ZERO | RADEON_ALPHA_ARG_C_DIFFUSE_ALPHA |
                             RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
    }
    if (blend) {
        c->rb3d_cntl |= RADEON_ALPHA_BLEND_ENABLE;
        c->rb3d_blendcntl = RADEON_COMB_FCN_ADD_CLAMP | RADEON_SRC_BLEND_GL_SRC_ALPHA | RADEON_DST_BLEND_GL_ONE_MINUS_SRC_ALPHA;
    } else
        c->rb3d_blendcntl = RADEON_COMB_FCN_ADD_CLAMP | RADEON_SRC_BLEND_GL_ONE | RADEON_DST_BLEND_GL_ZERO;
    if (fog)
        c->pp_cntl |= RADEON_FOG_ENABLE;
    c->pp_fog_color = (fogc & RADEON_FOG_COLOR_MASK) | RADEON_FOG_VERTEX | RADEON_FOG_USE_SPEC_ALPHA;
    if (zmode >= 0)
        c->rb3d_cntl |= RADEON_Z_ENABLE;
    c->rb3d_zstencilcntl |= zf[zmode >= 0 ? zmode : 7] << 4 | (zmode >= 0 && zupdate ? RADEON_Z_WRITE_ENABLE : 0u);
    if (cfmt_set)
        c->se_coord_fmt = cfmt;
    radeon_3d_format(RADEON_VTX_RHW | RADEON_VTX_ARGB | RADEON_VTX_SPEC | RADEON_VTX_ST);
    radeon_3d_dirty = 1;
    radeon_3d_emit();
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
    radeon_set_target(ZOFF, pitch, 16);
    radeon_set_clip(0, 0, W, H);
    radeon_fill(0, 0, W, H, 0xFFFF);
    radeon_set_target(COFF, pitch, 16);
    radeon_fill(0, 0, W, H, 0);
    radeon_sync(2000000);
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
    if (radeon_sync(4000000) != 0) {
        hx_test(name, 0, "the engine never went idle (RBBM_STATUS %08lx)", (unsigned long)radeon_rd(RADEON_RBBM_STATUS));
        radeon_engine_reset();
        return;
    }
    for (y = 0; y < H; y++) {
        const volatile uint16_t *row = (const volatile uint16_t *)(radeon.fb + COFF + (long)y * pitch);
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
    /* The levels one after another from TOFF, each starting on a 32-byte boundary, its rows padded to 32 bytes (a
     * level narrower than that: Mesa's radeonSetTexImages and the DRM's radeon_cp_dispatch_texture); written by
     * the CPU with the engine idle, then the chip's caches flushed. */
    radeon_sync(2000000);
    {
        uint32_t off = TOFF;
        for (n = 0; n < levels; n++) {
            int sz = 1 << (log2size - n), row = sz * cpp, stride = (row + 31) & ~31, y;
            volatile uint8_t *d = radeon.fb + off;
            const uint8_t *src = (const uint8_t *)lv[n];
            for (y = 0; y < sz; y++)
                for (i = 0; i < stride; i++)
                    d[y * stride + i] = i < row ? src[y * row + i] : 0;
            lvoff[n] = off;
            off = (off + (uint32_t)(stride * sz) + 31u) & ~31u;
        }
        radeon_vram_written(off - 4);
    }
    return 0;
}

/* (only=A,B,...: those cases, in the program's order) */
static int want(const char *name)
{
    const char *p = only;
    size_t n = strlen(name);
    if (!only)
        return 1;
    while (p) {
        if (!strncmp(p, name, n) && (p[n] == 0 || p[n] == ','))
            return 1;
        p = strchr(p, ',');
        if (p)
            p++;
    }
    return 0;
}

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
    hx_log("DTRI %s: %d triangles, fmt %d, %dx%d, %d levels%s%s", name, ntex, fmt, 1 << log2size, 1 << log2size,
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
    radeon_mode m;
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
        else if (!strncmp(argv[i], "ring=", 5))
            use_ring = 1, ring_kb = (uint32_t)atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "seed=", 5))
            seed = (unsigned)atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "vmode=", 6))
            vmode = atoi(argv[i] + 6);
        else if (!strncmp(argv[i], "cfmt=", 5))
            cfmt_set = 1, cfmt = (uint32_t)strtoul(argv[i] + 5, NULL, 16);
        else if (!strncmp(argv[i], "base=", 5))
            base = (uint32_t)atoi(argv[i] + 5) << 20;
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
    hx_init(1, argv, "dtri");                   /* (its own arguments only) */
    if (radeon_find() != 0 || radeon_map() != 0) {
        hx_test("map", 0, "no Radeon");
        hx_done(1);
    }
    hx_test("map", 1, "%s %04x, %lu MB", radeon.name, radeon.device_id, (unsigned long)(radeon.vram_bytes >> 20));
    if (radeon_find_mode(W, H, 16, &m) != 0 || radeon_set_mode(&m) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    pitch = m.pitch_bytes;
    hx_test("mode", 1, "mode %03x, pitch %d", m.mode, pitch);
    if (use_ring) {
        int rc;
        radeon_cp_trace = cp_log;
        rc = radeon_cp_init(NULL, ring_kb * 1024u);
        hx_test("cp", rc == 0, rc == 0 ? "running from a %lu KB ring in system memory (the GART's range at %08lx)"
                                        : "did not start: plain register writes (%lu KB asked, %08lx)",
                (unsigned long)ring_kb, (unsigned long)radeon_cp_gart_start);
    }
    ref = malloc(W * H * 2);
    refz = malloc(W * H * 2);
    refm = malloc(W * H);
    if (!ref || !refz || !refm)
        hx_done(1);
    radeon_set_start(COFF);
    radeon_3d_init(COFF, pitch, 16, ZOFF, pitch, 16, W, H);
    hx_test("3d", 1, "colour at %u MB, Z 1 MB on, textures 2 MB on", (unsigned)(base >> 20));
    set_state(0, BLEND_NONE, 0);

    /* Random triangles, no Z, drawn in order over each other. */
    if (want("random")) {
        clear();
        z_state(-1, 0);
        hx_log("DTRI random: %d triangles", ntri);
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
        hx_log("DTRI mesh: 8 x 6 quads");
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
        hx_log("DTRI zbuf: %d triangles", ntri);
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
        hx_log("DTRI blend: %d triangles", ntri);
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

    radeon_sync(1000000);
    radeon_cp_stop();
    radeon_text_mode();
    hx_done(hx_failures() ? 1 : 0);
}
