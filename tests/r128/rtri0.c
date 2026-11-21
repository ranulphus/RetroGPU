/* rtri0.c - RTRI0: the Rage 128's first triangles (docs/rage128.md, R2's bring-up), as NTRI0 was the TNT2's: a
 * 640x480 16-bit mode, the CCE started, colour and a linear Z16 buffer cleared by the 2D engine, the 3D context
 * sent, three overlapping Gouraud triangles at depths 0.5, 0.25 and 0.75 (LESS, written), then the frame read back
 * and compared with a reference that rasterises the same: coverage (pixel centres at half pixels, and at whole
 * ones: which the chip uses), the colours at each pixel (plane through the vertices), and which triangle won.
 * Vertices at sixteenths of a pixel that keep pixel centres off the edges.
 *
 *   RTRI0
 *
 * HX-TEST lines: map, mode, cce, coverage, colour, depth. */
#include "hx.h"
#include "r128/r128.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

typedef struct { double x, y, z; double r, g, b; } V;

static const V tris[3][3] = {
    { { 100.1875, 100.3125, 0.5, 255, 0, 0 }, { 500.4375, 120.0625, 0.5, 0, 255, 0 }, { 300.5625, 400.6875, 0.5, 0, 0, 255 } },
    { { 200.3125, 150.1875, 0.25, 255, 255, 255 }, { 450.0625, 160.5625, 0.25, 255, 255, 255 }, { 320.6875, 350.4375, 0.25, 255, 255, 255 } },
    { { 150.5625, 300.0625, 0.75, 255, 255, 0 }, { 250.1875, 50.4375, 0.75, 255, 255, 0 }, { 400.3125, 380.5625, 0.75, 255, 255, 0 } },
};

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* Whether (px, py) is inside triangle t (either winding), with its barycentrics. */
static int inside(const V *t, double px, double py, double l[3])
{
    double area = edge(t[0].x, t[0].y, t[1].x, t[1].y, t[2].x, t[2].y), e0, e1, e2;
    if (area == 0)
        return 0;
    e0 = edge(t[1].x, t[1].y, t[2].x, t[2].y, px, py) / area;
    e1 = edge(t[2].x, t[2].y, t[0].x, t[0].y, px, py) / area;
    e2 = edge(t[0].x, t[0].y, t[1].x, t[1].y, px, py) / area;
    l[0] = e0, l[1] = e1, l[2] = e2;
    return e0 >= 0 && e1 >= 0 && e2 >= 0;
}

static uint32_t argb_of(const V *v)
{
    return 0xFF000000u | (uint32_t)v->r << 16 | (uint32_t)v->g << 8 | (uint32_t)v->b;
}

static signed char *who;        /* the reference's winner per pixel (-1 none) */
static uint16_t *zref;

static void reference(double off)
{
    int x, y, k;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            who[y * W + x] = -1;
            zref[y * W + x] = 0xFFFF;
        }
    for (k = 0; k < 3; k++)
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                double l[3];
                uint16_t z;
                if (!inside(tris[k], x + off, y + off, l))
                    continue;
                z = (uint16_t)(tris[k][0].z * 65535.0);
                if (z < zref[y * W + x]) {
                    zref[y * W + x] = z;
                    who[y * W + x] = (signed char)k;
                }
            }
}

int main(int argc, char **argv)
{
    r128_mode m;
    r128_vertex v[3];
    uint32_t zoff;
    int k, i, x, y, cov_half = 0, cov_whole = 0, worst = 0, wrong_winner = 0, drawn = 0;
    char where[96] = "";
    (void)argc;
    hx_init(1, argv, "rtri0");
    if (r128_find() < 0 || r128_map() < 0) {
        hx_test("map", 0, "no Rage 128 mapped");
        hx_done(1);
    }
    hx_test("map", 1, "Rage 128 %04x, %u KB", r128.device_id, (unsigned)(r128.vram_bytes >> 10));
    if (r128_find_mode(W, H, 16, &m) < 0 || r128_set_mode(&m) < 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x pitch %u", m.mode, m.pitch_bytes);
    if (r128_cce_init(NULL) != 0) {
        hx_test("cce", 0, "the CCE did not start");
        r128_text_mode();
        hx_done(1);
    }
    hx_test("cce", 1, "192PIO");
    zoff = ((uint32_t)m.pitch_bytes * H + 4095u) & ~4095u;
    r128_set_target(0, m.pitch_bytes, 16);
    r128_set_clip(0, 0, W, H);
    r128_fill(0, 0, W, H, 0);
    r128_set_target(zoff, W * 2, 16);
    r128_fill(0, 0, W, H, 0xFFFF);
    r128_3d_init(0, m.pitch_bytes, 16, zoff, W * 2, 16);
    r128_3d.tex_cntl_c |= 1u;                   /* (Z_ENABLE) */
    r128_3d_emit();
    r128_3d_format(0x009);                      /* (RHW, diffuse ARGB) */
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 3; i++) {
            memset(&v[i], 0, sizeof v[i]);
            v[i].x = (float)tris[k][i].x, v[i].y = (float)tris[k][i].y, v[i].z = (float)tris[k][i].z;
            v[i].rhw = 1.0f;
            v[i].argb = argb_of(&tris[k][i]);
        }
        r128_tri(&v[0], &v[1], &v[2]);
    }
    if (r128_sync(2000000) != 0)
        hx_log("RTRI0 the engine did not go idle");
    who = malloc(W * H);
    zref = malloc(W * H * 2);
    if (!who || !zref)
        hx_done(1);
    /* Coverage both ways; the colours and winners against the centres that match better. */
    for (k = 0; k < 2; k++) {
        int miss = 0;
        reference(k ? 0.0 : 0.5);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                uint16_t p = *(volatile uint16_t *)(r128.fb + (uint32_t)y * m.pitch_bytes + (uint32_t)x * 2);
                miss += (p != 0) != (who[y * W + x] >= 0);
            }
        if (k) cov_whole = miss; else cov_half = miss;
    }
    reference(cov_whole < cov_half ? 0.0 : 0.5);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint16_t p = *(volatile uint16_t *)(r128.fb + (uint32_t)y * m.pitch_bytes + (uint32_t)x * 2);
            int w = who[y * W + x];
            double l[3], off = cov_whole < cov_half ? 0.0 : 0.5;
            int r = ((p >> 11) & 31) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, b = (p & 31) * 255 / 31, e;
            if (w < 0 || p == 0)
                continue;
            drawn++;
            inside(tris[w], x + off, y + off, l);
            e = abs(r - (int)(l[0] * tris[w][0].r + l[1] * tris[w][1].r + l[2] * tris[w][2].r));
            if (abs(g - (int)(l[0] * tris[w][0].g + l[1] * tris[w][1].g + l[2] * tris[w][2].g)) > e)
                e = abs(g - (int)(l[0] * tris[w][0].g + l[1] * tris[w][1].g + l[2] * tris[w][2].g));
            if (abs(b - (int)(l[0] * tris[w][0].b + l[1] * tris[w][1].b + l[2] * tris[w][2].b)) > e)
                e = abs(b - (int)(l[0] * tris[w][0].b + l[1] * tris[w][1].b + l[2] * tris[w][2].b));
            if (e > worst) {
                worst = e;
                sprintf(where, "at %d,%d (triangle %d): %04x", x, y, w, p);
            }
            if (e > 40)
                wrong_winner++;
        }
    hx_test("coverage", (cov_half < cov_whole ? cov_half : cov_whole) < 200,
            "pixels differing: %d with centres at halves, %d at wholes", cov_half, cov_whole);
    hx_test("colour", worst <= 16, "worst %d of 255 %s (%d drawn)", worst, where, drawn);
    hx_test("depth", wrong_winner < 50, "%d pixels from another triangle than the reference's", wrong_winner);
    r128_text_mode();
    r128_unmap();
    hx_done(hx_failures() ? 1 : 0);
}
