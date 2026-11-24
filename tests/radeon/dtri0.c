/* dtri0.c - DTRI0: the Radeon's first triangles (docs/radeon.md, RD2's bring-up), as RTRI0 was the Rage 128's: a
 * 640x480 16-bit mode, colour and a Z16 buffer cleared by the 2D engine, the 3D state written (plain register
 * writes: no command processor), three overlapping Gouraud triangles at depths 0.5, 0.25 and 0.75 (LESS, written),
 * then the frame read back. The buffers from megabyte 16 on (the P3's card's sound memory), shown there.
 * and compared with a reference that rasterises the same: coverage (pixel centres at half pixels, and at whole
 * ones: which the chip uses), the colours at each pixel (plane through the vertices), and which triangle won.
 * Vertices at sixteenths of a pixel that keep pixel centres off the edges.
 *
 *   DTRI0
 *
 * HX-TEST lines: map, mode, coverage, colour, depth. DTRI0-STAT: the engine's status after the triangles. */
#include "hx.h"
#include "radeon/radeon.h"
#include "radeon/regs3d_radeon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define BASE (16u << 20)

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
    radeon_mode m;
    radeon_vertex v[3];
    uint32_t zoff, coff;
    int k, i, x, y, cov_half = 0, cov_whole = 0, worst = 0, wrong_winner = 0, drawn = 0;
    char where[96] = "";
    static int hist[256];
    int p999 = 0;
    (void)argc;
    hx_init(1, argv, "dtri0");
    if (radeon_find() < 0 || radeon_map() < 0) {
        hx_test("map", 0, "no Radeon mapped");
        hx_done(1);
    }
    hx_test("map", 1, "%s %04x, %u KB%s%s", radeon.name, radeon.device_id, (unsigned)(radeon.vram_bytes >> 10),
            radeon.flags & RADEON_HAS_TCL ? ", T&L" : "", radeon.flags & RADEON_HAS_HIERZ ? ", hierarchical Z" : "");
    if (radeon_find_mode(W, H, 16, &m) < 0 || radeon_set_mode(&m) < 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x pitch %u", m.mode, m.pitch_bytes);
    coff = BASE;
    zoff = BASE + (((uint32_t)m.pitch_bytes * H + 4095u) & ~4095u);
    radeon_set_start(coff);
    radeon_set_target(coff, m.pitch_bytes, 16);
    radeon_set_clip(0, 0, W, H);
    radeon_fill(0, 0, W, H, 0);
    radeon_set_target(zoff, m.pitch_bytes, 16);
    radeon_fill(0, 0, W, H, 0xFFFF);
    radeon_3d_init(coff, m.pitch_bytes, 16, zoff, m.pitch_bytes, 16, W, H);
    radeon_3d_emit();
    radeon_3d_format(RADEON_VTX_RHW | RADEON_VTX_ARGB);
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 3; i++) {
            memset(&v[i], 0, sizeof v[i]);
            v[i].x = (float)tris[k][i].x, v[i].y = (float)tris[k][i].y, v[i].z = (float)tris[k][i].z;
            v[i].rhw = 1.0f;
            v[i].argb = argb_of(&tris[k][i]);
        }
        radeon_tri(&v[0], &v[1], &v[2]);
    }
    if (radeon_sync(2000000) != 0)
        hx_log("DTRI0 the engine did not go idle");
    hx_log("DTRI0-STAT rbbm_status=%08lx", (unsigned long)radeon_rd(0x0E40));
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
                uint16_t p = *(volatile uint16_t *)(radeon.fb + coff + (uint32_t)y * m.pitch_bytes + (uint32_t)x * 2);
                miss += (p != 0) != (who[y * W + x] >= 0);
            }
        if (k) cov_whole = miss; else cov_half = miss;
    }
    reference(cov_whole < cov_half ? 0.0 : 0.5);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint16_t p = *(volatile uint16_t *)(radeon.fb + coff + (uint32_t)y * m.pitch_bytes + (uint32_t)x * 2);
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
            hist[e > 255 ? 255 : e]++;
            if (e > worst && e <= 40) {            /* (beyond 40: counted below, another triangle's or a pixel the
                                                    * P3's card's memory lost a byte of) */
                worst = e;
                sprintf(where, "at %d,%d (triangle %d): %04x", x, y, w, p);
            }
            if (e > 40) {
                if (wrong_winner < 8)
                    hx_log("DTRI0-PIX %d,%d: %04x (r %d g %d b %d), the reference's triangle %d wants r %d g %d b %d", x, y,
                           p, r, g, b, w, (int)(l[0] * tris[w][0].r + l[1] * tris[w][1].r + l[2] * tris[w][2].r),
                           (int)(l[0] * tris[w][0].g + l[1] * tris[w][1].g + l[2] * tris[w][2].g),
                           (int)(l[0] * tris[w][0].b + l[1] * tris[w][1].b + l[2] * tris[w][2].b));
                wrong_winner++;
            }
        }
    hx_test("coverage", (cov_half < cov_whole ? cov_half : cov_whole) < 200,
            "pixels differing: %d with centres at halves, %d at wholes", cov_half, cov_whole);
    /* The colours by the error 999 pixels in 1,000 stay within (the P3's card loses a byte of a few pixels a frame,
     * at the same places run after run: those are beyond it, and listed). */
    {
        long seen = 0, sum = 0;
        int beyond = 0;
        for (i = 0; i < 256; i++) {
            if (seen * 1000 < (long)drawn * 999)
                p999 = i;
            seen += hist[i];
            sum += (long)hist[i] * i;
            if (i > 16)
                beyond += hist[i];
        }
        hx_test("colour", drawn > 0 && p999 <= 16, "999 in 1,000 within %d of 255, mean %ld.%02ld, %d beyond 16; worst "
                "under 40: %d %s (%d drawn)", p999, drawn ? sum / drawn : 0, drawn ? sum * 100 / drawn % 100 : 0, beyond,
                worst, where, drawn);
    }
    hx_test("depth", wrong_winner < 50, "%d pixels far from the reference's colour (another triangle's at an edge, or "
            "a byte the card's memory lost: DTRI0-PIX)", wrong_winner);
    radeon_text_mode();
    radeon_unmap();
    hx_done(hx_failures() ? 1 : 0);
}
