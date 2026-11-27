/* dcel1.c - DCEL1: Celsius (0x96) texturing and depth on the NV1x (docs/nv10.md, the Quadro2 plan's Q3), the state
 * as Mesa's NV11 driver sent it on bench E (run1's texquad and depth scenes), every buffer in video memory.
 *  A: a 64x64 A8R8G8B8 checker (swizzled, one level, repeat, linear) on a quad, texture coordinates 0..2, modulated
 *     by white: stage 0 = texture 0 x primary into spare0, the final combiner spare0. Checked at texel-cell centres.
 *  B: two quads over the same rectangle crossing in depth (red: z 10000 at its left, 50000 at its right; green the
 *     other way), Z16 cleared to 0xFFFF, the test LESS: red on the left half, green on the right.
 *
 *   DCEL1
 *
 * HX-TEST lines: map, mode, accel, celsius, tex-draw, texels, depth-draw, depth. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480
#define S7 7

static uint8_t tex[64 * 64 * 4];

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

/* The viewport and a projection that takes window coordinates (y up) as they are, z too (Z16: 0..65535). */
static void window_space(void)
{
    uint32_t pm[16];
    int i;
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
    m1(0x394, fb(0.0f));                                /* DEPTH_RANGE_NEAR, _FAR: Z16's */
    m1(0x398, fb(65535.0f));
}

static void vtxbuf(const uint32_t fmt[8], const uint32_t off[8])
{
    int i;
    for (i = 0; i < 8; i++)
        m1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    for (i = 0; i < 8; i++)
        if (fmt[i] & 0xF0)
            m1(0xD00 + 8u * (uint32_t)i, off[i]);
    m1(0xCF0, 0);                                       /* VTXBUF_VALIDATE */
}

static void draw(uint32_t prim_plus1, int n)
{
    m1(0x13FC, prim_plus1);
    m1(0x1400, (uint32_t)(n - 1) << 24);
    m1(0x13FC, 0);
    tnt2_kick();
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch_px = 0, i, x, y, bad = 0, checked = 0;
    uint32_t zoff, vb, toff;
    volatile uint16_t *scr;
    const uint8_t *lv[1];
    hx_init(1, argv, "dcel1");
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
    zoff = ((uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    toff = (zoff + (uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    vb = tnt2.fb_usable + 0x8000u;
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    if (tnt2_3d_init() != 0 || tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 set up");
    scr = (volatile uint16_t *)tnt2_fb;

    /* A: the checker, swizzled into video memory by the layer's loader (its NV4 swizzle is Celsius's). */
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            uint8_t *t = tex + (y * 64 + x) * 4;
            int on = ((x >> 3) ^ (y >> 3)) & 1;
            t[0] = on ? 230 : (uint8_t)(x * 4), t[1] = on ? 200 : (uint8_t)(y * 4), t[2] = on ? 60 : 120, t[3] = 255;
        }
    lv[0] = tex;
    tnt2_tex_load_rgba8(toff, TNT2_TEX_A8R8G8B8, 6, 6, 1, lv);
    tnt2_celsius_target(0, zoff, pitch_px * 2, W, H, 16);
    window_space();
    {
        /* {r, g, b, s, t, x, y}: Mesa's texquad layout (colour, texture 0, position) */
        static const float q[4][7] = { { 1, 1, 1, 0, 0, 150, 100 }, { 1, 1, 1, 2, 0, 500, 100 },
                                       { 1, 1, 1, 2, 2, 500, 400 }, { 1, 1, 1, 0, 2, 150, 400 } };
        static const uint32_t fmt[8] = { 0x1C22, 0x1C32, 0x0002, 0x1C22, 0x0002, 0x0002, 0x0002, 0x0002 };
        uint32_t off[8] = { 0 };
        for (i = 0; i < 4; i++)
            memcpy((void *)(tnt2_fb + vb + (uint32_t)i * 28u), q[i], 28);
        (void)tnt2_fb[vb];
        off[0] = vb + 20, off[1] = vb, off[3] = vb + 12;
        m1(0x260, 0x18140000); m1(0x268, 0x08040000); m1(0x270, 0); m1(0x278, 0xC0); m1(0x280, 0xC0);
        m1(0x288, 0x200C2000); m1(0x28C, 0x20001C00);
        m1(0x220, 0x11661351);                          /* TEX_FORMAT(0): DMA A, A8R8G8B8, 1 level, 64x64, repeat */
        m1(0x218, toff);                                /* TEX_OFFSET(0) */
        m1(0x248, 0x22000000);                          /* TEX_FILTER(0): linear, linear */
        m1(0x228, 0x40000000);                          /* TEX_ENABLE(0) */
        vtxbuf(fmt, off);
        draw(8, 4);                                     /* GL_QUADS + 1 */
    }
    if (tnt2_sync(1000000) != 0 || !trap_ok("tex-draw")) {
        hx_test("tex-draw", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("tex-draw", 1, "drawn, nothing trapped");
    for (y = 0; y < 16; y++)                            /* (each 8x8 cell's centre, both repeats) */
        for (x = 0; x < 16; x++) {
            int tx = (x * 8 + 4) & 63, ty = (y * 8 + 4) & 63;
            int sx = (int)(150 + (x * 8 + 4) / 128.0 * 350.0), wy = (int)(100 + (y * 8 + 4) / 128.0 * 300.0);
            const uint8_t *t = tex + (ty * 64 + tx) * 4;
            uint16_t p = scr[(H - 1 - wy) * pitch_px + sx];
            int r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
            checked++;
            if (r - t[0] > 24 || t[0] - r > 24 || g - t[1] > 24 || t[1] - g > 24 || b - t[2] > 24 || t[2] - b > 24) {
                if (!bad)
                    hx_log("DCEL1 first texel off at cell %d,%d (screen %d,%d): %04x, want %02x%02x%02x", x, y, sx,
                           H - 1 - wy, p, t[0], t[1], t[2]);
                bad++;
            }
        }
    hx_test("texels", bad == 0, "%d of %d cell centres within 24 of the texel", checked - bad, checked);

    /* B: depth. */
    tnt2_set_target(zoff, pitch_px * 2, 16);
    tnt2_fill(0, 0, W, H, 0xFFFF);
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    tnt2_celsius_target(0, zoff, pitch_px * 2, W, H, 16);
    window_space();
    m1(0x228, 0);                                       /* texture off */
    m1(0x260, 0); m1(0x268, 0); m1(0x270, 0); m1(0x278, 0); m1(0x280, 0);
    m1(0x288, 0x20042000); m1(0x28C, 0x20001400);       /* (the untextured final combiner: the primary colour) */
    m1(0x30C, 1);                                       /* DEPTH_TEST_ENABLE */
    m1(0x354, 0x201);                                   /* DEPTH_FUNC: GL_LESS */
    m1(0x35C, 1);                                       /* DEPTH_WRITE_ENABLE */
    {
        /* {r, g, b, x, y, z} */
        static const float q[8][6] = {
            { 1, 0, 0, 100, 100, 10000 }, { 1, 0, 0, 540, 100, 50000 }, { 1, 0, 0, 540, 380, 50000 }, { 1, 0, 0, 100, 380, 10000 },
            { 0, 1, 0, 100, 100, 50000 }, { 0, 1, 0, 540, 100, 10000 }, { 0, 1, 0, 540, 380, 10000 }, { 0, 1, 0, 100, 380, 50000 } };
        static const uint32_t fmt[8] = { 0x1832, 0x1832, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002 };
        uint32_t off[8] = { 0 };
        for (i = 0; i < 8; i++)
            memcpy((void *)(tnt2_fb + vb + (uint32_t)i * 24u), q[i], 24);
        (void)tnt2_fb[vb];
        off[0] = vb + 12, off[1] = vb;                  /* position (x, y, z) after the colour; stride 24 */
        vtxbuf(fmt, off);
        draw(8, 8);
    }
    if (tnt2_sync(1000000) != 0 || !trap_ok("depth-draw")) {
        hx_test("depth-draw", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("depth-draw", 1, "drawn, nothing trapped");
    {
        int wrong = 0, n = 0;
        uint16_t lp = 0, rp = 0;
        for (y = 120; y < 360; y += 20)
            for (x = 110; x < 530; x += 20) {
                uint16_t p = scr[(H - 1 - y) * pitch_px + x];
                int left = x < 310, right = x > 330;
                if (!left && !right)
                    continue;
                n++;
                if (left) lp = p;
                if (right) rp = p;
                if ((left && p != 0xF800) || (right && p != 0x07E0))
                    wrong++;
            }
        hx_test("depth", wrong == 0, "%d of %d points as LESS wants (left %04x, right %04x)", n - wrong, n, lp, rp);
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
