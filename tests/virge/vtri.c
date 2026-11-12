/* vtri.c - VTRI: the ViRGE/DX's 3D triangles (docs/virge.md, V2) against
 * OpenGL's rasterisation rules, worked out here: a pixel is drawn when its
 * centre is inside the triangle (on an edge: the top-left rule), coloured
 * by the plane through the vertices at that centre, and kept when its Z
 * passes. At 640x480 in 15-bit colour (the 3D engine's 16-bit format), with
 * a 16-bit Z buffer, read back from VRAM.
 *
 *   VTRI [n=N]
 *
 * HX-TEST lines: map, mode, random, mesh, zbuf. Each compares the pixels:
 * coverage (drawn or not) and colour within one step of 5 bits. */
#include "hx.h"
#include "virge/virge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define ZOFF 0x100000u              /* the Z buffer, 1 MB in */

static uint16_t *ref;               /* the expected screen, RGB555 */
static uint16_t *refz;
static int pitch;                   /* bytes */
static unsigned seed = 7;
static int ntri = 60;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

static double dmin(double a, double b) { return a < b ? a : b; }
static double dmax(double a, double b) { return a > b ? a : b; }

static float rf(float lo, float hi) { return lo + (hi - lo) * (float)rnd(100000) / 100000.0f; }

/* OpenGL's rule for one triangle into ref/refz. Edge functions at pixel
 * centres; a centre on an edge counts when the edge is a top or a left one. */
static void ref_tri(const virge_vertex *a, const virge_vertex *b, const virge_vertex *c, int zfunc, int zwrite)
{
    const virge_vertex *v[3] = { a, b, c };
    double area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    int x0, x1, y0, y1, x, y, i;
    if (area == 0)
        return;
    if (area < 0) {                 /* counter-clockwise on the screen (y down) */
        const virge_vertex *t = v[1];
        v[1] = v[2], v[2] = t;
        area = -area;
    }
    x0 = (int)floor(dmin(a->x, dmin(b->x, c->x))) - 1, x1 = (int)ceil(dmax(a->x, dmax(b->x, c->x))) + 1;
    y0 = (int)floor(dmin(a->y, dmin(b->y, c->y))) - 1, y1 = (int)ceil(dmax(a->y, dmax(b->y, c->y))) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            double px = x + 0.5, py = y + 0.5, w[3];
            int in = 1;
            for (i = 0; i < 3; i++) {
                const virge_vertex *p = v[(i + 1) % 3], *q = v[(i + 2) % 3];
                double e = (q->x - p->x) * (py - p->y) - (q->y - p->y) * (px - p->x);
                /* (screen y down, clockwise: inside is e > 0; on the edge, a top edge
                 * (horizontal, going right) or a left one (going up) counts) */
                if (e < 0 || (e == 0 && !((q->y == p->y && q->x > p->x) || q->y < p->y)))
                    in = 0;
                w[i] = e / area;
            }
            if (!in)
                continue;
            {
                double r = w[0] * v[0]->r + w[1] * v[1]->r + w[2] * v[2]->r;
                double g = w[0] * v[0]->g + w[1] * v[1]->g + w[2] * v[2]->g;
                double bl = w[0] * v[0]->b + w[1] * v[1]->b + w[2] * v[2]->b;
                double z = w[0] * v[0]->z + w[1] * v[1]->z + w[2] * v[2]->z;
                uint16_t zq = (uint16_t)dmin(dmax(floor(z), 0), 65535);
                uint16_t *zp = refz + y * W + x;
                if (zfunc >= 0) {
                    int pass = zfunc == 7 || (zfunc == 4 && zq < *zp) || (zfunc == 6 && zq <= *zp) ||
                               (zfunc == 1 && zq > *zp) || (zfunc == 3 && zq >= *zp) || (zfunc == 2 && zq == *zp) ||
                               (zfunc == 5 && zq != *zp);
                    if (!pass)
                        continue;
                    if (zwrite)
                        *zp = zq;
                }
                ref[y * W + x] = (uint16_t)(((int)dmin(dmax(r, 0), 255) >> 3) << 10 |
                                            ((int)dmin(dmax(g, 0), 255) >> 3) << 5 | ((int)dmin(dmax(bl, 0), 255) >> 3));
            }
        }
}

static void both(const virge_vertex *a, const virge_vertex *b, const virge_vertex *c, int zfunc, int zwrite)
{
    virge_tri(a, b, c);
    ref_tri(a, b, c, zfunc, zwrite);
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
}

/* The screen against ref: pixels drawn where they should not be (or not
 * drawn where they should), and colours more than a step of 5 bits out. */
static void compare(const char *name, double max_frac)
{
    long drawn = 0, cover = 0, colour = 0;
    int x, y, fx = -1, fy = -1;
    char first[96] = "";
    virge_sync(4000000);
    for (y = 0; y < H; y++) {
        const uint16_t *row = (const uint16_t *)(virge.fb + (long)y * pitch);
        for (x = 0; x < W; x++) {
            uint16_t got = row[x] & 0x7FFF, want = ref[y * W + x];
            int dr, dg, db;
            if (want)
                drawn++;
            if ((got != 0) != (want != 0)) {
                if (cover++ == 0)
                    fx = x, fy = y, sprintf(first, "coverage at %d,%d: %04x, want %04x", x, y, got, want);
                continue;
            }
            dr = abs((got >> 10) - (want >> 10)), dg = abs(((got >> 5) & 31) - ((want >> 5) & 31)),
            db = abs((got & 31) - (want & 31));
            if (dr > 1 || dg > 1 || db > 1)
                if (colour++ == 0 && !first[0])
                    fx = x, fy = y, sprintf(first, "colour at %d,%d: %04x, want %04x", x, y, got, want);
        }
    }
    (void)fx, (void)fy;
    hx_test(name, drawn > 0 && cover <= max_frac * drawn && colour <= max_frac * drawn,
            "%ld drawn; coverage %ld, colour %ld off%s%s", drawn, cover, colour, first[0] ? "; first: " : "", first);
}

/* Within a 150-pixel box at (bx, by): triangles of every shape, small enough
 * that the reference keeps up on an emulated 486. */
static float bx, by;

static void random_box(void) { bx = rf(2, W - 152), by = rf(2, H - 152); }

static void random_vertex(virge_vertex *v)
{
    v->x = bx + rf(0, 150), v->y = by + rf(0, 150), v->z = rf(0, 65000);
    v->r = rf(16, 255), v->g = rf(16, 255), v->b = rf(16, 255), v->a = 255;   /* (never black: black is "not drawn") */
}

int main(int argc, char **argv)
{
    virge_mode m;
    int i, j, k;
    hx_init(argc, argv, "vtri");
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "n=", 2))
            ntri = atoi(argv[i] + 2);
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
    if (!ref || !refz)
        hx_done(1);
    virge_3d_target(0, pitch, 16, W, H, ZOFF, pitch);

    /* Random triangles, no Z, drawn in order over each other. */
    clear();
    virge_3d_state(-1, 0);
    hx_log("VTRI random: %d triangles", ntri);
    for (i = 0; i < ntri; i++) {
        virge_vertex a, b, c;
        random_box();
        random_vertex(&a), random_vertex(&b), random_vertex(&c);
        both(&a, &b, &c, -1, 0);
    }
    compare("random", 0.002);

    /* A mesh: a jittered grid of quads, each two triangles, every pixel of
     * its area drawn once (a gap or a pixel drawn twice shows as coverage). */
    clear();
    {
        static virge_vertex g[7][9];
        hx_log("VTRI mesh: 8 x 6 quads");
        for (j = 0; j < 7; j++)
            for (k = 0; k < 9; k++) {
                virge_vertex *p = &g[j][k];
                p->x = 100 + k * 37.5f + (k && k < 8 ? rf(-12, 12) : 0);
                p->y = 100 + j * 37.0f + (j && j < 6 ? rf(-12, 12) : 0);
                p->z = 0, p->r = rf(20, 255), p->g = rf(20, 255), p->b = rf(20, 255), p->a = 255;
            }
        for (j = 0; j < 6; j++)
            for (k = 0; k < 8; k++) {
                both(&g[j][k], &g[j][k + 1], &g[j + 1][k], -1, 0);
                both(&g[j][k + 1], &g[j + 1][k + 1], &g[j + 1][k], -1, 0);
            }
    }
    compare("mesh", 0.002);

    /* Z: random triangles with depth, the nearer kept (less, with update). */
    clear();
    virge_3d_state(4, 1);
    hx_log("VTRI zbuf: %d triangles", ntri);
    for (i = 0; i < ntri; i++) {
        virge_vertex a, b, c;
        if (i % 6 == 0)
            random_box();                       /* (several in each box, so they overlap) */
        random_vertex(&a), random_vertex(&b), random_vertex(&c);
        both(&a, &b, &c, 4, 1);
    }
    compare("zbuf", 0.005);

    hx_snap_screen("VTRI");
    virge_text_mode();
    virge_unmap();
    hx_done(hx_failures() ? 1 : 0);
}
