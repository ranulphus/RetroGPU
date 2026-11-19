/* ntri0.c - NTRI0: the Riva TNT2's first 3D (docs/tnt2.md, T2's bring-up): the 3D surfaces and textured-triangle
 * objects set up, then one Gouraud triangle (red, green and blue corners; the untextured way: a white texel
 * modulated) on a black 640x480x16 screen. Its coverage is counted against OpenGL's rule (pixel centres inside, the
 * top-left rule), the colours read near each corner, and PGRAPH's interrupt status read after each step: a method
 * that trapped stalls the engine, which is reported rather than waited for.
 *
 *   NTRI0
 *
 * HX-TEST lines: map, accel, mode, 3d-init, target, tri, coverage, colours. */
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
        hx_test(name, 0, "PGRAPH_INTR %08lx: trapped method %08lx (subchannel %lu, method %03lx), data %08lx",
                (unsigned long)i, (unsigned long)a, (unsigned long)((a >> 13) & 7), (unsigned long)(a & 0x1FFC),
                (unsigned long)d);
    return i == 0;
}

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* OpenGL's coverage of a pixel centre (top-left rule for centres on an edge), the triangle either winding. */
static int inside(const tnt2_vertex *v, double px, double py)
{
    double area = edge(v[0].sx, v[0].sy, v[1].sx, v[1].sy, v[2].sx, v[2].sy), s = area > 0 ? 1 : -1;
    int i;
    for (i = 0; i < 3; i++) {
        const tnt2_vertex *a = &v[i], *b = &v[(i + 1) % 3];
        double e = s * edge(a->sx, a->sy, b->sx, b->sy, px, py);
        if (e < 0)
            return 0;
        if (e == 0) {                           /* (top or left edge only) */
            double dx = s * (b->sx - a->sx), dy = s * (b->sy - a->sy);
            if (!((dy == 0 && dx > 0) || dy < 0))
                return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch_px = 0, x, y, want = 0, got = 0, extra = 0, missing = 0;
    uint32_t zoff;
    volatile uint16_t *fb;
    tnt2_vertex v[3];
    hx_init(1, argv, "ntri0");
    (void)argc;
    if (tnt2_find() != 0 || tnt2_map() != 0) {
        hx_test("map", 0, "no NV4-family chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s, %lu MB", tnt2.name, (unsigned long)(tnt2.fb_size >> 20));
    if (vbe_find_mode(W, H, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    hx_test("mode", 1, "mode %03x, pitch %d bytes", m.mode, pitch_px * 2);
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);

    if (tnt2_3d_init() != 0 || !trap_ok("3d-init")) {
        hx_test("3d-init", 0, "the engine didn't go idle, or trapped");
        trap_ok("3d-init-trap");
        goto out;
    }
    hx_test("3d-init", 1, "0x53 on subchannel 4, 0x54 on 7, SURFACES bound");
    zoff = ((uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    tnt2_3d_target(0, zoff, pitch_px * 2, 16, W, H);
    if (tnt2_sync(1000000) != 0 || !trap_ok("target")) {
        hx_test("target", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("target", 1, "colour 0, Z %lu, pitch %d", (unsigned long)zoff, pitch_px * 2);

    /* The untextured state (Mesa's): the 1x1 white texel (FORMAT A8R8G8B8, 1 level, repeat, DMA_A), nearest;
     * BLEND MODULATEALPHA | MSB | Gouraud | perspective | ONE, ZERO; CONTROL Z fixed | corner origin | alpha
     * ALWAYS | Z ALWAYS | no culling, no Z test or write. */
    tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x120001A4u, 0x40182800u, 0);
    memset(v, 0, sizeof v);
    v[0].sx = 100.25f, v[0].sy = 100.5f, v[0].color = 0xFFFF0000u;
    v[1].sx = 500.5f,  v[1].sy = 120.25f, v[1].color = 0xFF00FF00u;
    v[2].sx = 300.75f, v[2].sy = 400.5f, v[2].color = 0xFF0000FFu;
    for (x = 0; x < 3; x++)
        v[x].sz = 0.5f, v[x].rhw = 1.0f, v[x].specular = 0xFF000000u;
    tnt2_tri(&v[0], &v[1], &v[2]);
    if (tnt2_sync(1000000) != 0 || !trap_ok("tri")) {
        hx_test("tri", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("tri", 1, "drawn, nothing trapped");

    fb = (volatile uint16_t *)tnt2_fb;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int in = inside(v, x + 0.5, y + 0.5), lit = fb[y * pitch_px + x] != 0;
            want += in, got += lit;
            if (lit && !in) extra++;
            if (in && !lit) missing++;
        }
    hx_test("coverage", extra == 0 && missing == 0, "%d pixels drawn, %d expected: %d outside, %d missing", got, want,
            extra, missing);
    {
        static const int px[3][2] = { { 130, 108 }, { 470, 125 }, { 300, 370 } };
        static const char *what[3] = { "red", "green", "blue" };
        char buf[160] = "";
        int i, ok = 1;
        for (i = 0; i < 3; i++) {
            uint16_t p = fb[px[i][1] * pitch_px + px[i][0]];
            int r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            int big = i == 0 ? r : i == 1 ? g / 2 : b;  /* (5-bit scale) */
            int small = i == 0 ? (g / 2 > b ? g / 2 : b) : i == 1 ? (r > b ? r : b) : (r > g / 2 ? r : g / 2);
            sprintf(buf + strlen(buf), "%s at %d,%d: %04x; ", what[i], px[i][0], px[i][1], p);
            if (big < 20 || small > 12)
                ok = 0;
        }
        hx_test("colours", ok, "%s", buf);
    }
out:
    tnt2_sync(200000);
    vbe_set_text_mode();
    hx_done(0);
}
