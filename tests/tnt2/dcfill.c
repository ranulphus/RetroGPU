/* dcfill.c - DCFILL: what Celsius's (0x96) pixels cost on the NV1x, state by state (the Quadro2 plan's Q4: GLQuake on
 * bench E was fill-bound, 19 fps at 640x480 and 69 at 320x240, while DCPERF's plain textured fill was fast). Each case
 * draws 40 triangles covering the 640x480 screen (corners 0,0 1280,0 0,960, so half of each lies off the screen), the
 * median of five timed from the first command to the chip idle, at 16 bits with Z16:
 *   flat    - untextured;
 *   tex     - a 64x64 A8R8G8B8 texture at one texel a pixel, bilinear;
 *   depth   - tex with the depth test ALWAYS and writes;
 *   less    - tex with LESS and writes, each triangle nearer than the last (every pixel passes);
 *   two     - two textures (the second's colour times the first's, GLQuake's -mtex);
 *   min     - a 256x256 texture minified 8 times, bilinear, no mipmaps;
 *   mip     - the same with its nine levels (LINEAR_MIPMAP_NEAREST);
 *   persp   - tex, w going 1 to 4 across the screen;
 *   blend   - tex blended (SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
 *   twotex  - both textures fetched, the second not combined (one general combiner);
 *   twoef   - both combined by one general combiner and the final combiner's E x F;
 *   comb2   - one texture, the second general combiner on (passing spare0 through);
 *   fblend  - untextured, blended (the colour read and written: 4 or 8 bytes a pixel);
 *   fless   - untextured, the depth test LESS with writes (depth read and written, colour written).
 * Mpixels a second are on-screen pixels: 40 x 640 x 480 a case.
 *
 *   DCFILL [dbg] [32]    (dbg: PGRAPH DEBUG_2 as nouveau sets it, as the layer does since; 32: 32 bits a pixel)
 *
 * HX-TEST lines: map, mode, accel, celsius, and one per case ("DCFILL case us=... mpix=..." lines too). */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define S7 7
#define NB 40

typedef struct { float x, y, z, w; uint32_t col; float s, t, s1, t1, fog; } cvert;

static int pitch_px, bpp = 16;
static uint32_t vb, zoff, t64[2], t256;

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

static void setup(void)
{
    static const uint32_t fmt[8] = { 0x01002842, 0x2840, 0x0002, 0x2822, 0x2822, 0x0002, 0x0002, 0x2812 };
    static const uint32_t at[8] = { 0, 16, 0, 20, 28, 0, 0, 36 };
    uint32_t pm[16];
    int i;
    tnt2_celsius_target(0, zoff, pitch_px * (bpp / 8), W, H, bpp);
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
    m1(0x394, fb(0.0f));
    m1(0x398, fb(bpp == 32 ? 16777215.0f : 65535.0f));
    m1(0x2A4, 0);
    for (i = 0; i < 8; i++)
        m1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    for (i = 0; i < 8; i++)
        if (fmt[i] & 0xF0)
            m1(0xD00 + 8u * (uint32_t)i, vb + at[i]);
    m1(0xCF0, 0);
}

enum { F_FLAT, F_TEX, F_DEPTH, F_LESS, F_TWO, F_MIN, F_MIP, F_PERSP, F_BLEND, F_TWOTEX, F_TWOEF, F_COMB2, F_FBLEND,
       F_FLESS, F_N };

/* The state, and the vertices (their texture coordinates and w), for a case. */
static void state(int c)
{
    cvert v[NB * 3];
    static const float bx[3] = { 0, 1280, 0 }, by[3] = { 0, 0, 960 };
    float scale = c == F_MIN || c == F_MIP ? 8.0f / 256.0f : 1.0f / 64.0f;
    int i, j;
    for (i = 0; i < NB; i++)
        for (j = 0; j < 3; j++) {
            cvert *p = &v[i * 3 + j];
            float w = c == F_PERSP ? (j == 1 ? 4.0f : 1.0f) : 1.0f;
            p->x = bx[j] * w, p->y = by[j] * w, p->z = (c == F_LESS || c == F_FLESS ? 60000.0f - (float)i * 1000.0f : 30000.0f) * w * (bpp == 32 ? 256.0f : 1.0f);
            p->w = w;
            p->col = 0x80FFFFFFu - (uint32_t)i * 0x00030303u;
            p->s = bx[j] * scale, p->t = by[j] * scale, p->s1 = bx[j] / 64.0f, p->t1 = by[j] / 64.0f, p->fog = 0;
        }
    memcpy((void *)(tnt2_fb + vb), v, sizeof v);
    (void)tnt2_fb[vb];
    m1(0xCF0, 0);
    /* depth */
    m1(0x30C, c == F_DEPTH || c == F_LESS || c == F_FLESS);
    m1(0x354, c == F_LESS || c == F_FLESS ? 0x201u : 0x207u);
    m1(0x35C, c == F_DEPTH || c == F_LESS || c == F_FLESS);
    /* blending */
    m1(0x304, c == F_BLEND || c == F_FBLEND);
    m1(0x344, 0x302); m1(0x348, 0x303); m1(0x350, 0x8006);
    /* textures and combiners */
    m1(0x228, 0); m1(0x22C, 0);
    m1(0x260, 0); m1(0x264, 0); m1(0x268, 0); m1(0x26C, 0); m1(0x278, 0); m1(0x27C, 0); m1(0x280, 0);
    m1(0x284, 0x18000000);
    if (c == F_FLAT || c == F_FBLEND || c == F_FLESS) {
        m1(0x288, 0x20042000); m1(0x28C, 0x20001400);
        return;
    }
    if (c == F_MIN || c == F_MIP) {
        m1(0x220, (c == F_MIP ? 0x8000u : 0u) | 0x11881351u);  /* 256x256 A8R8G8B8, repeat */
        m1(0x218, t256);
        m1(0x248, c == F_MIP ? 0x24000000u : 0x22000000u);    /* linear; min LINEAR_MIPMAP_NEAREST or LINEAR */
        m1(0x228, 0x40000000u | (c == F_MIP ? 8u << 14 : 0u));
    } else {
        m1(0x220, 0x11661351);
        m1(0x218, t64[0]);
        m1(0x248, 0x22000000);
        m1(0x228, 0x40000000);
    }
    m1(0x260, 0x18140000); m1(0x268, 0x08040000); m1(0x278, 0xC0); m1(0x280, 0xC0);
    if (c == F_TWO || c == F_TWOTEX || c == F_TWOEF) {
        m1(0x224, 0x11661351);
        m1(0x21C, t64[1]);
        m1(0x24C, 0x22000000);
        m1(0x22C, 0x40000000);
    }
    if (c == F_TWO)                             /* (the second combiner: spare0 x texture 1) */
        m1(0x264, 0x1C190000), m1(0x26C, 0x0C090000), m1(0x27C, 0xC0), m1(0x284, 0x280000C0);
    if (c == F_COMB2)                           /* (the second combiner on, passing spare0 through) */
        m1(0x264, 0x1C200000), m1(0x26C, 0x0C200000), m1(0x27C, 0xC0), m1(0x284, 0x280000C0);
    if (c == F_TWOEF) {                         /* (one combiner; the final's E x F = spare0 x texture 1 as B) */
        m1(0x288, 0x200F0000); m1(0x28C, 0x0C091C00);
        return;
    }
    m1(0x288, 0x200C2000); m1(0x28C, 0x20001C00);
}

static uint32_t run(void)
{
    uint32_t t0, first = 0, n = NB * 3;
    tnt2_set_target(zoff, pitch_px * (bpp / 8), bpp);
    tnt2_fill(0, 0, W, H, 0xFFFFFFFFu);
    tnt2_sync(1000000);
    t0 = sys_time_us();
    m1(0x13FC, 5);
    while (n) {
        uint32_t k = n > 255u ? 255u : n;
        m1(0x1400, (k - 1u) << 24 | first);
        first += k, n -= k;
    }
    m1(0x13FC, 0);
    tnt2_sync(3000000);
    return sys_time_us() - t0;
}

static int cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    static const char *const names[F_N] = { "flat", "tex", "depth", "less", "two", "min", "mip", "persp", "blend",
                                            "twotex", "twoef", "comb2", "fblend", "fless" };
    static uint8_t lv[9][256 * 256 * 4];
    const uint8_t *lp[9];
    mga_vbe_mode m;
    int c, k, i, l;
    hx_init(1, argv, "dcfill");
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "32"))
            bpp = 32;                           /* (X8R8G8B8 with Z24S8: the bus's width shows in what reads memory) */
    if (tnt2_find() != 0 || tnt2_map() != 0 || tnt2.arch != 10) {
        hx_test("map", 0, "no NV1x chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s, %lu MB", tnt2.name, (unsigned long)(tnt2.fb_size >> 20));
    if (vbe_find_mode(W, H, bpp, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x", m.mode);
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    zoff = ((uint32_t)pitch_px * (uint32_t)(bpp / 8) * H + 4095u) & ~4095u;
    t64[0] = (zoff + (uint32_t)pitch_px * (uint32_t)(bpp / 8) * H + 4095u) & ~4095u;
    t64[1] = t64[0] + 16384u;
    t256 = t64[1] + 16384u;
    vb = t256 + 512u * 1024u;
    tnt2_set_target(0, pitch_px * (bpp / 8), bpp);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    if (tnt2_3d_init() != 0 || tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 set up");
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "dbg")) {
            /* nouveau's nv10_gr_init words in place of xf86-video-nv's (0x24E00810, 0x55DE0030) */
            volatile uint32_t *pg = tnt2_regs + 0x400000 / 4;
            tnt2_sync(1000000);
            hx_log("DCFILL debug before: %08lx %08lx %08lx %08lx", (unsigned long)pg[0x84 / 4], (unsigned long)pg[0x88 / 4],
                   (unsigned long)pg[0x8C / 4], (unsigned long)pg[0x90 / 4]);
            pg[0x88 / 4] = 0x25F92AD9u;
            /* (DEBUG_3 as nouveau sets it traps the layer's 2D fill: subchannel 6, method 0x404) */
            pg[0x90 / 4] = 0;
            hx_test("dbg", 1, "DEBUG_2 %08lx DEBUG_3 %08lx", (unsigned long)pg[0x88 / 4], (unsigned long)pg[0x8C / 4]);
        }
    for (k = 0; k < 2; k++) {
        for (i = 0; i < 64 * 64; i++)
            lv[0][i * 4] = (uint8_t)(i * (k + 3)), lv[0][i * 4 + 1] = (uint8_t)(i >> 3), lv[0][i * 4 + 2] = 90,
            lv[0][i * 4 + 3] = 255;
        lp[0] = lv[0];
        tnt2_tex_load_rgba8(t64[k], TNT2_TEX_A8R8G8B8, 6, 6, 1, lp);
    }
    for (l = 0; l < 9; l++) {
        int s = 256 >> l;
        for (i = 0; i < s * s; i++)
            lv[l][i * 4] = (uint8_t)(i * 7 + l * 40), lv[l][i * 4 + 1] = (uint8_t)(i >> 2), lv[l][i * 4 + 2] = (uint8_t)(l * 28),
            lv[l][i * 4 + 3] = 255;
        lp[l] = lv[l];
    }
    tnt2_tex_load_rgba8(t256, TNT2_TEX_A8R8G8B8, 8, 8, 9, lp);
    setup();
    for (c = 0; c < F_N; c++) {
        uint32_t us[5];
        double mpix;
        state(c);
        for (k = 0; k < 5; k++)
            us[k] = run();
        if (!trap_ok(names[c]))
            break;
        qsort(us, 5, sizeof us[0], cmp);
        mpix = (double)NB * W * H / (double)us[2];
        hx_log("DCFILL %s us=%lu mpix=%.1f (%lu..%lu)", names[c], (unsigned long)us[2], mpix, (unsigned long)us[0],
               (unsigned long)us[4]);
        hx_test(names[c], 1, "%lu us, %.1f Mpixels/s", (unsigned long)us[2], mpix);
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
