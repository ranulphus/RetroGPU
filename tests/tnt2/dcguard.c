/* dcguard.c - DCGUARD: Celsius's (0x96) guard band on the NV1x (docs/nv10.md, the Quadro2 plan's Q4): how far off the
 * screen a vertex may lie and still be drawn right, so DOS-GL's clipper need only clip what lies beyond it.
 *
 * For each reach R (window pixels), one triangle with corners at (-R, -R), (W + 2R + W, -R), (-R, H + 2R + H) in
 * window coordinates (y up; w = 1), which covers the whole screen, its green going 0 to 1 from x = -R to the right
 * corner; then the same triangle with w = 4 at every corner (x, y, z scaled by it), and once more with the corners
 * the other way round (the far corner at -R): every pixel should be drawn, and the green at sampled points should be
 * the plane's. Read back after each; nothing is clipped here.
 *
 *   DCGUARD
 *
 * HX-TEST lines: map, mode, accel, celsius, and one per reach and variant. "DCGUARD ..." lines: the counts. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480
#define S7 7

static int pitch_px;
static uint32_t vb, zoff;

static void m1(uint32_t mthd, uint32_t v) { tnt2_methods(S7, mthd, &v, 1); }
static uint32_t fb(float f) { union { float f; uint32_t u; } c; c.f = f; return c.u; }

static int trap_ok(const char *name)
{
    uint32_t a, d, i = tnt2_3d_trap(&a, &d);
    if (i)
        hx_test(name, 0, "PGRAPH_INTR %08lx: trapped %08lx (subchannel %lu, method %03lx), data %08lx",
                (unsigned long)i, (unsigned long)a, (unsigned long)((a >> 16) & 7), (unsigned long)(a & 0x1FFC),
                (unsigned long)d);
    return i == 0;
}

static void frame(void)
{
    uint32_t pm[16];
    int i;
    tnt2_set_target(0, pitch_px * 4, 32);
    tnt2_fill(0, 0, W, H, 0x00FF00FFu);                 /* (magenta: never drawn by the triangle) */
    tnt2_sync(1000000);
    tnt2_celsius_target(0, zoff, pitch_px * 4, W, H, 32);
    m1(0x6E8, fb(-2048.0f + W / 2.0f));
    m1(0x6EC, fb(-2048.0f + H / 2.0f));
    m1(0x6F0, fb(0.0f));
    m1(0x6F4, 0);
    for (i = 0; i < 16; i++)
        pm[i] = 0;
    pm[0] = fb(1.0f), pm[3] = fb(-W / 2.0f);
    pm[5] = fb(-1.0f), pm[7] = fb(H / 2.0f);
    pm[10] = fb(1.0f);
    pm[15] = fb(1.0f);
    tnt2_methods(S7, 0x500, pm, 16);
    m1(0x30C, 0); m1(0x35C, 0); m1(0x228, 0);
    m1(0x260, 0); m1(0x268, 0); m1(0x270, 0); m1(0x278, 0); m1(0x280, 0);
    m1(0x2A4, 0);                                       /* fog off */
    m1(0x288, 0x20042000); m1(0x28C, 0x20001400);       /* the vertex colour */
}

/* One triangle; returns 0 drawn, -1 trapped or stuck. */
static int draw(const char *name, const float v[3][2], float w, float g0x, float g1x)
{
    static const uint32_t fmt[8] = { 0x01001C42, 0x1C32, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002 };
    float q[3][7];
    int i;
    for (i = 0; i < 3; i++) {
        float g = (v[i][0] - g0x) / (g1x - g0x);
        q[i][0] = v[i][0] * w, q[i][1] = v[i][1] * w, q[i][2] = 0, q[i][3] = w;
        q[i][4] = 0, q[i][5] = g, q[i][6] = 0;
    }
    memcpy((void *)(tnt2_fb + vb), q, sizeof q);
    (void)tnt2_fb[vb];
    for (i = 0; i < 8; i++)
        m1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    m1(0xD00, vb);
    m1(0xD08, vb + 16);
    m1(0xCF0, 0);
    m1(0x13FC, 5);
    m1(0x1400, 2u << 24);
    m1(0x13FC, 0);
    tnt2_kick();
    if (tnt2_sync(1000000) != 0 || !trap_ok(name)) {
        hx_test(name, 0, "the engine didn't go idle, or trapped");
        return -1;
    }
    return 0;
}

/* Every pixel drawn (not magenta), and the green at a grid of points against the plane's (screen-linear: w equal at
 * the corners, so perspective and screen space agree). */
static void check(const char *name, int reach, float g0x, float g1x)
{
    volatile uint32_t *scr = (volatile uint32_t *)tnt2_fb;
    int x, y, undrawn = 0, off = 0, n = 0;
    double worst = 0;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if ((scr[y * pitch_px + x] & 0xFFFFFFu) == 0xFF00FFu)
                undrawn++;
    for (y = 20; y < H; y += 40)
        for (x = 20; x < W; x += 40) {
            double want = (x + 0.5 - g0x) / (g1x - g0x), got = ((scr[(H - 1 - y) * pitch_px + x] >> 8) & 255) / 255.0;
            n++;
            if (fabs(got - want) > worst)
                worst = fabs(got - want);
            if (fabs(got - want) > 2.0 / 255.0)
                off++;
        }
    hx_log("DCGUARD %s reach=%d undrawn=%d off=%d/%d worst=%.4f", name, reach, undrawn, off, n, worst);
    hx_test(name, undrawn == 0 && off == 0, "reach %d: %d pixels undrawn, %d of %d samples off (worst %.4f)", reach,
            undrawn, off, n, worst);
}

int main(int argc, char **argv)
{
    static const int reach[] = { 100, 700, 1000, 1400, 1800, 2000, 3000, 5000, 10000 };
    mga_vbe_mode m;
    unsigned r;
    char name[40];
    hx_init(1, argv, "dcguard");
    (void)argc;
    if (tnt2_find() != 0 || tnt2_map() != 0 || tnt2.arch != 10) {
        hx_test("map", 0, "no NV1x chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s, %lu MB", tnt2.name, (unsigned long)(tnt2.fb_size >> 20));
    if (vbe_find_mode(W, H, 32, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x32 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x, pitch %d bytes", m.mode, pitch_px * 4);
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    zoff = ((uint32_t)pitch_px * 4u * H + 4095u) & ~4095u;
    vb = tnt2.fb_usable + 0x8000u;
    tnt2_set_target(0, pitch_px * 4, 32);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    if (tnt2_3d_init() != 0 || tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 set up");
    for (r = 0; r < sizeof reach / sizeof reach[0]; r++) {
        float R = (float)reach[r];
        /* (y up: corners below-left of the screen, far right, far up) */
        float a[3][2] = { { -R, -R }, { W + 2 * R + W, -R }, { -R, H + 2 * R + H } };
        /* (the other way: the right angle beyond the top right) */
        float b[3][2] = { { W + R, H + R }, { -2 * R - W, H + R }, { W + R, -2 * R - H } };
        snprintf(name, sizeof name, "reach-%d", reach[r]);
        frame();
        if (draw(name, (const float (*)[2])a, 1.0f, -R, W + 2 * R + W) != 0)
            break;
        check(name, reach[r], -R, W + 2 * R + W);
        snprintf(name, sizeof name, "reach-%d-w4", reach[r]);
        frame();
        if (draw(name, (const float (*)[2])a, 4.0f, -R, W + 2 * R + W) != 0)
            break;
        check(name, reach[r], -R, W + 2 * R + W);
        snprintf(name, sizeof name, "reach-%d-flip", reach[r]);
        frame();
        if (draw(name, (const float (*)[2])b, 1.0f, -2 * R - W, W + R) != 0)
            break;
        check(name, reach[r], -2 * R - W, W + R);
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
