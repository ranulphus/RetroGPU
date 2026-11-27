/* dcel0.c - DCEL0: the NV1x's own 3D class, Celsius (0x96), first triangle (docs/nv10.md, the Quadro2 plan's Q3).
 * Celsius set up as Mesa's NV11 driver set it up under nouveau on bench E (the layer's tnt2_celsius_init: its
 * captured init, every DMA handle on video memory), then one Gouraud triangle (the capture's: red at 100,100, green
 * at 500,120, blue at 300,400, GL window coordinates, y up) from three {r, g, b, x, y} float vertices in video
 * memory, drawn as Mesa's software-transform path draws (a viewport-only projection, a batch from vertex 0).
 * Checked by reading the screen back: coverage against the triangle's edges, the colour near each corner, traps.
 *
 *   DCEL0
 *
 * HX-TEST lines: map, mode, accel, 3d-init, celsius, tri, coverage, colours. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480

static int trap_ok(const char *name)
{
    uint32_t a, d, i = tnt2_3d_trap(&a, &d);
    if (i)
        hx_test(name, 0, "PGRAPH_INTR %08lx: trapped %08lx (subchannel %lu, method %03lx), data %08lx",
                (unsigned long)i, (unsigned long)a, (unsigned long)((a >> 16) & 7), (unsigned long)(a & 0x1FFC),
                (unsigned long)d);
    return i == 0;
}

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

int main(int argc, char **argv)
{
    /* GL window coordinates (y up), as glOrtho(0, W, 0, H) gives Mesa; the screen's row is H - y */
    static const float tv[3][5] = { { 1, 0, 0, 100, 100 }, { 0, 1, 0, 500, 120 }, { 0, 0, 1, 300, 400 } };
    mga_vbe_mode m;
    int pitch_px = 0, x, y, want = 0, got = 0, extra = 0, missing = 0, i;
    uint32_t zoff, vb;
    volatile uint16_t *fb;
    hx_init(1, argv, "dcel0");
    (void)argc;
    if (tnt2_find() != 0 || tnt2_map() != 0 || tnt2.arch != 10) {
        hx_test("map", 0, "no NV1x chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s, %lu MB", tnt2.name, (unsigned long)(tnt2.fb_size >> 20));
    if (vbe_find_mode(W, H, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x, pitch %d bytes", m.mode, pitch_px * 2);
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    if (tnt2_3d_init() != 0 || !trap_ok("3d-init")) {
        hx_test("3d-init", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("3d-init", 1, "the 3D objects");
    if (tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 on subchannel 7, Mesa's init (%s)", "captured on bench E");

    /* The depth buffer after the screen; the vertices in the layer's image scratch area. */
    zoff = ((uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    vb = tnt2.fb_usable + 0x8000u;                     /* (the image scratch, after the 32 KB ring: tnt2.h) */
    for (i = 0; i < 3; i++)
        memcpy((void *)(tnt2_fb + vb + (uint32_t)i * 20u), tv[i], 20);
    (void)tnt2_fb[vb];                                  /* (the CPU's writes out before the chip reads them) */
    tnt2_celsius_target(0, zoff, pitch_px * 2, W, H, 16);
    tnt2_celsius_tris(vb, 3, W, H);
    if (tnt2_sync(1000000) != 0 || !trap_ok("tri")) {
        hx_test("tri", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("tri", 1, "drawn, nothing trapped");

    /* Coverage: pixel centres inside the triangle (screen rows: H - y) against non-black pixels. */
    fb = (volatile uint16_t *)tnt2_fb;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            double px = x + 0.5, py = H - (y + 0.5);
            double e0 = edge(tv[0][3], tv[0][4], tv[1][3], tv[1][4], px, py);
            double e1 = edge(tv[1][3], tv[1][4], tv[2][3], tv[2][4], px, py);
            double e2 = edge(tv[2][3], tv[2][4], tv[0][3], tv[0][4], px, py);
            int in = (e0 > 1.0 && e1 > 1.0 && e2 > 1.0) || (e0 < -1.0 && e1 < -1.0 && e2 < -1.0);
            int out = (e0 > -1.0 && e0 < 1.0) || (e1 > -1.0 && e1 < 1.0) || (e2 > -1.0 && e2 < 1.0);
            int lit = fb[y * pitch_px + x] != 0;
            if (out && !in)
                continue;                               /* (on an edge: either is fine) */
            want += in;
            got += in && lit;
            extra += !in && lit;
            missing += in && !lit;
        }
    hx_test("coverage", want > 50000 && missing < want / 200 && extra < want / 200,
            "%d pixels drawn of %d expected: %d outside, %d missing", got, want, extra, missing);
    {
        /* Near each corner, its colour should lead (565: red bits 15-11, green 10-5, blue 4-0). */
        static const int cx[3] = { 130, 470, 300 }, cy[3] = { 108, 125, 370 };
        char line[160];
        int n = 0, ok = 1;
        for (i = 0; i < 3; i++) {
            uint16_t p = fb[(H - cy[i]) * pitch_px + cx[i]];
            int r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            int lead = i == 0 ? r * 2 > g && r > b : i == 1 ? g > r * 2 && g > b * 2 : b > r && b * 2 > g;
            ok &= lead;
            n += sprintf(line + n, "%s at %d,%d: %04x; ", i == 0 ? "red" : i == 1 ? "green" : "blue", cx[i], cy[i], p);
        }
        hx_test("colours", ok, "%s", line);
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
