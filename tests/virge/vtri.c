/* vtri.c - VTRI: the ViRGE/DX's 3D triangles (docs/virge.md, V2) against
 * OpenGL's rules, worked out here: a pixel is drawn when its centre is
 * inside the triangle (on an edge: the top-left rule), with each attribute
 * the plane through the vertices at that centre (texture coordinates
 * perspective-correct when asked), textures sampled as OpenGL samples them
 * (NEAREST, LINEAR, the mipmap level from the footprint), and kept when its
 * Z passes. At 640x480 in 15-bit colour (the 3D engine's 16-bit format),
 * with a 16-bit Z buffer, read back from VRAM.
 *
 *   VTRI [n=N] [only=CASE] [k=MIN,MAX]
 *
 * (k: the texels a pixel of every textured case, for experiments.)
 *
 * HX-TEST lines: map, mode, then the cases: random, mesh, zbuf (Gouraud and
 * Z); blend (the vertex's alpha); tex-nearest, tex-linear, tex-persp,
 * tex-persp-linear, tex-mip, tex-modulate, tex-8888, tex-alpha, tex-fog.
 * Each compares the pixels: coverage (drawn or not) and colour within a
 * step or two of 5 bits. */
#include "hx.h"
#include "virge/virge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define ZOFF 0x100000u              /* the Z buffer, 1 MB in */
#define TOFF 0x200000u              /* textures, 2 MB in */

static uint16_t *ref;               /* the expected screen, RGB555 */
static uint16_t *refz;
static uint8_t *refm;               /* drawn there */
static int pitch;                   /* bytes */
static unsigned seed = 7;
static int ntri = 60;
static const char *only;
static float kover[2];              /* k=MIN,MAX: texels a pixel, for every textured case */

/* What both draw with: the texture (its levels as ARGB8888 for the
 * reference), the environment, blending and fog. */
static virge_texture tx;
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
static double x5(int c) { return (c << 3) | (c >> 2); }

/* OpenGL's rule for one triangle into ref/refz/refm. Edge functions at pixel
 * centres; a centre on an edge counts when the edge is a top or a left one. */
static void ref_tri(const virge_vertex *a, const virge_vertex *b, const virge_vertex *c, int zfunc, int zwrite)
{
    const virge_vertex *v[3] = { a, b, c };
    double area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    double dwx[3], dwy[3], q[3], su[3], sv[3], size = ldexp(1.0, tx.log2size);
    int x0, x1, y0, y1, x, y, i;
    if (area == 0)
        return;
    if (area < 0) {                 /* counter-clockwise on the screen (y down) */
        const virge_vertex *t = v[1];
        v[1] = v[2], v[2] = t;
        area = -area;
    }
    for (i = 0; i < 3; i++) {
        const virge_vertex *p = v[(i + 1) % 3], *r = v[(i + 2) % 3];
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
                const virge_vertex *p = v[(i + 1) % 3], *r = v[(i + 2) % 3];
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
                        double rho2 = dmax(ux * ux + vx * vx, uy * uy + vy * vy);
                        double lod = rho2 > 0 ? 0.5 * log(rho2) / log(2.0) : -16;
                        n = lod <= 0 ? 0 : (int)ceil(lod + 0.5) - 1;
                        if (n > tx.levels - 1) n = tx.levels - 1;
                        if (n < 0) n = 0;
                    }
                    sample(n, u, vv, t);
                    if (tx.env == VIRGE_TEXENV_MODULATE) {
                        for (i = 0; i < 3; i++)
                            col[i] = t[i] * col[i] / 255.0;
                    } else if (tx.env == VIRGE_TEXENV_REFLECTION) {
                        for (i = 0; i < 3; i++)
                            col[i] = dmin(t[i] + col[i], 255);
                    } else {
                        for (i = 0; i < 3; i++)
                            col[i] = t[i];
                    }
                    col[3] = blend == VIRGE_BLEND_SOURCE ? av : t[3];
                    if (fog) {
                        double f = clampd(av, 0, 255) / 255.0;
                        col[0] = col[0] * f + ((fogc >> 16) & 255) * (1 - f);
                        col[1] = col[1] * f + ((fogc >> 8) & 255) * (1 - f);
                        col[2] = col[2] * f + (fogc & 255) * (1 - f);
                    }
                }
                if (blend) {
                    double d[3];
                    d[0] = x5(*dp >> 10), d[1] = x5((*dp >> 5) & 31), d[2] = x5(*dp & 31);
                    alpha = clampd(col[3], 0, 255) / 255.0;
                    for (i = 0; i < 3; i++)
                        col[i] = col[i] * alpha + d[i] * (1 - alpha);
                }
                *dp = (uint16_t)(q5(col[0]) << 10 | q5(col[1]) << 5 | q5(col[2]));
                refm[y * W + x] = 1;
            }
        }
}

static void both(const virge_vertex *a, const virge_vertex *b, const virge_vertex *c, int zfunc, int zwrite)
{
    virge_tri(a, b, c);
    ref_tri(a, b, c, zfunc, zwrite);
}

static void set_state(const virge_texture *t, int blend_mode, int fog_on)
{
    tex_on = t != 0;
    if (t)
        tx = *t;
    blend = blend_mode, fog = fog_on;
    virge_3d_texture(t);
    virge_3d_blend(blend_mode);
    virge_3d_fog(fog_on, fogc);
}

static void clear(void)
{
    virge_set_target(0, pitch, 16);
    virge_set_clip(0, 0, W, H);
    virge_fill(0, 0, W, H, 0);
    virge_set_target(ZOFF, pitch, 16);
    virge_fill(0, 0, W, H, 0xFFFF);
    virge_sync(2000000);
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
    virge_sync(4000000);
    for (y = 0; y < H; y++) {
        const uint16_t *row = (const uint16_t *)(virge.fb + (long)y * pitch);
        for (x = 0; x < W; x++) {
            uint16_t got = row[x] & 0x7FFF, want = ref[y * W + x];
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
            dr = abs((got >> 10) - (want >> 10)), dg = abs(((got >> 5) & 31) - ((want >> 5) & 31)),
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

static void random_vertex(virge_vertex *v)
{
    v->x = bx + rf(0, 150), v->y = by + rf(0, 150), v->z = rf(0, 65000);
    v->r = rf(16, 255), v->g = rf(16, 255), v->b = rf(16, 255), v->a = 255;   /* (never black: black is "not drawn") */
    v->s = v->t = 0, v->oow = 1;
}

/* Texture coordinates for a triangle: a random rotation and scale of the
 * screen (k texels of the largest level a pixel, kmin..kmax) and offset,
 * with 1/w from qmin to 1 when persp. */
static void map_texture(virge_vertex *v, int nv, float kmin, float kmax, float qmin)
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
    virge_texture t;
    t.offset = TOFF, t.fmt = fmt, t.log2size = log2size, t.levels = levels;
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
            if (fmt == VIRGE_TEX_ARGB1555) {
                uint16_t h = (uint16_t)((a >= 128) << 15 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
                r = (r >> 3) << 3 | r >> 5, g = (g >> 3) << 3 | g >> 5, b = (b >> 3) << 3 | b >> 5;
                a = a >= 128 ? 255 : 0;
                buf[2 * i] = (uint8_t)h, buf[2 * i + 1] = (uint8_t)(h >> 8);
            } else if (fmt == VIRGE_TEX_ARGB4444) {
                uint16_t h = (uint16_t)((a >> 4) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4));
                r = (r >> 4) * 17, g = (g >> 4) * 17, b = (b >> 4) * 17, a = (a >> 4) * 17;
                buf[2 * i] = (uint8_t)h, buf[2 * i + 1] = (uint8_t)(h >> 8);
            } else {
                p = a << 24 | r << 16 | g << 8 | b;
                memcpy(buf + 4 * i, &p, 4);
            }
            tl[n][i] = a << 24 | r << 16 | g << 8 | b;
        }
        virge_tex_load(&t, n, buf);
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
    virge_3d_state(-1, 0);
    if (blend_mode) {
        set_state(0, VIRGE_BLEND_NONE, 0);
        for (i = 0; i < ntex; i++) {
            virge_vertex g[3];
            random_box();
            random_vertex(&g[0]), random_vertex(&g[1]), random_vertex(&g[2]);
            both(&g[0], &g[1], &g[2], -1, 0);
        }
    }
    if (make_texture(fmt, log2size, levels, linear, persp, env, env == VIRGE_TEXENV_MODULATE ? 96 : 40,
                     blend_mode == VIRGE_BLEND_TEXTURE) < 0) {
        hx_test(name, 0, "no memory for the texture");
        return;
    }
    set_state(&tx, blend_mode, fog_on);
    hx_log("VTRI %s: %d triangles, fmt %d, %dx%d, %d levels%s%s", name, ntex, fmt, 1 << log2size, 1 << log2size,
           levels, linear ? ", linear" : "", persp ? ", perspective" : "");
    for (i = 0; i < ntex; i++) {
        virge_vertex v[3];
        int k;
        random_box();
        for (k = 0; k < 3; k++) {
            random_vertex(&v[k]);
            if (env == VIRGE_TEXENV_MODULATE)
                v[k].r = rf(128, 255), v[k].g = rf(128, 255), v[k].b = rf(128, 255);
            if (blend_mode == VIRGE_BLEND_SOURCE || fog_on)
                v[k].a = rf(0, 255);
        }
        map_texture(v, 3, kmin, kmax, persp ? 0.25f : 1);
        both(&v[0], &v[1], &v[2], -1, 0);
    }
    compare(name, max_frac, tol);
    set_state(0, VIRGE_BLEND_NONE, 0);
}

int main(int argc, char **argv)
{
    virge_mode m;
    int i, j, k;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "n=", 2))
            ntri = atoi(argv[i] + 2);
        else if (!strncmp(argv[i], "only=", 5))
            only = argv[i] + 5;
        else if (!strncmp(argv[i], "k=", 2))
            sscanf(argv[i] + 2, "%f,%f", &kover[0], &kover[1]);
    hx_init(1, argv, "vtri");                   /* (its own arguments only) */
    if (virge_find() < 0 || virge_map() < 0) {
        hx_test("map", 0, "no ViRGE/DX mapped");
        hx_done(1);
    }
    hx_test("map", 1, "ViRGE %04x at %08x", virge.device_id, (unsigned)virge.bar0);
    if (virge_find_mode(W, H, 15, &m) < 0 || virge_set_mode(&m) < 0) {
        hx_test("mode", 0, "no 640x480x15 mode");
        hx_done(1);
    }
    pitch = m.pitch_bytes;
    hx_test("mode", 1, "mode %03x, pitch %d", m.mode, pitch);
    ref = malloc(W * H * 2);
    refz = malloc(W * H * 2);
    refm = malloc(W * H);
    if (!ref || !refz || !refm)
        hx_done(1);
    virge_3d_target(0, pitch, 16, W, H, ZOFF, pitch);
    set_state(0, VIRGE_BLEND_NONE, 0);

    /* Random triangles, no Z, drawn in order over each other. */
    if (want("random")) {
        clear();
        virge_3d_state(-1, 0);
        hx_log("VTRI random: %d triangles", ntri);
        for (i = 0; i < ntri; i++) {
            virge_vertex a, b, c;
            random_box();
            random_vertex(&a), random_vertex(&b), random_vertex(&c);
            both(&a, &b, &c, -1, 0);
        }
        compare("random", 0.002, 1);
    }

    /* A mesh: a jittered grid of quads, each two triangles, every pixel of
     * its area drawn once (a gap or a pixel drawn twice shows as coverage). */
    if (want("mesh")) {
        static virge_vertex g[7][9];
        clear();
        hx_log("VTRI mesh: 8 x 6 quads");
        for (j = 0; j < 7; j++)
            for (k = 0; k < 9; k++) {
                virge_vertex *p = &g[j][k];
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
        virge_3d_state(4, 1);
        hx_log("VTRI zbuf: %d triangles", ntri);
        for (i = 0; i < ntri; i++) {
            virge_vertex a, b, c;
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
        virge_3d_state(-1, 0);
        hx_log("VTRI blend: %d triangles", ntri);
        for (i = 0; i < ntri; i++) {
            virge_vertex a, b, c;
            if (i % 4 == 0)
                random_box();
            random_vertex(&a), random_vertex(&b), random_vertex(&c);
            set_state(0, i % 4 ? VIRGE_BLEND_SOURCE : VIRGE_BLEND_NONE, 0);
            a.a = rf(0, 255), b.a = rf(0, 255), c.a = rf(0, 255);
            both(&a, &b, &c, -1, 0);
        }
        set_state(0, VIRGE_BLEND_NONE, 0);
        compare("blend", 0.005, 2);
    }

    /* Textures: magnified and minified, NEAREST and LINEAR, affine and
     * perspective, mipmapped, modulated, 32-bit, blended by the texel's
     * alpha, fogged. The 256-texel ones are where 86Box's model and the data
     * book agree on U and V without perspective; the others are bench C's.
     * LINEAR's tolerance is two steps: the model places each line's first
     * pixel to 1/32 of a pixel, which minified (1.5 texels a pixel) moves the
     * sample up to 0.05 texel; NEAREST's limits are its texels picked
     * differently at their edges for the same reason. */
    tex_case("tex-nearest", VIRGE_TEX_ARGB1555, 8, 1, 0, 0, VIRGE_TEXENV_DECAL, 0, 0, 0.2f, 1.5f, 0.03, 1);
    tex_case("tex-linear", VIRGE_TEX_ARGB1555, 8, 1, 1, 0, VIRGE_TEXENV_DECAL, 0, 0, 0.2f, 1.5f, 0.01, 2);
    tex_case("tex-persp", VIRGE_TEX_ARGB1555, 8, 1, 0, 1, VIRGE_TEXENV_DECAL, 0, 0, 0.2f, 1.0f, 0.03, 1);
    tex_case("tex-persp-linear", VIRGE_TEX_ARGB1555, 8, 1, 1, 1, VIRGE_TEXENV_DECAL, 0, 0, 0.2f, 1.0f, 0.01, 2);
    tex_case("tex-mip", VIRGE_TEX_ARGB1555, 7, 8, 0, 0, VIRGE_TEXENV_DECAL, 0, 0, 0.5f, 6.0f, 0.03, 1);
    tex_case("tex-modulate", VIRGE_TEX_ARGB4444, 6, 1, 1, 0, VIRGE_TEXENV_MODULATE, 0, 0, 0.1f, 1.0f, 0.02, 1);
    tex_case("tex-8888", VIRGE_TEX_ARGB8888, 5, 1, 0, 1, VIRGE_TEXENV_DECAL, 0, 0, 0.05f, 0.5f, 0.03, 1);
    tex_case("tex-alpha", VIRGE_TEX_ARGB4444, 8, 1, 1, 0, VIRGE_TEXENV_DECAL, VIRGE_BLEND_TEXTURE, 0, 0.2f, 1.0f, 0.02, 2);
    tex_case("tex-fog", VIRGE_TEX_ARGB1555, 8, 1, 1, 0, VIRGE_TEXENV_DECAL, 0, 1, 0.2f, 1.0f, 0.02, 2);

    hx_snap_screen("VTRI");
    virge_text_mode();
    virge_unmap();
    hx_done(hx_failures() ? 1 : 0);
}
