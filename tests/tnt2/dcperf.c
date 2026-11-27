/* dcperf.c - DCPERF: what Celsius (0x96) draws cost on the NV1x (the Quadro2 plan's Q4: GLQuake drew at 19 fps on bench
 * E with the chip the bottleneck, ~50 ms a frame for ~4,000 triangles in ~1,000 draws). Each case draws the same 3,000
 * small triangles (12 pixels a side, in the vertex layout DOS-GL's back end uses: 40 bytes, clip space with w), timed
 * from the first command to the chip idle, the median of five:
 *   one   - one BEGIN/END, its batches of 255 vertices;
 *   draws - 1,000 BEGIN/END of three triangles each;
 *   tex   - as draws, textured, TEX_OFFSET alternating between two textures before each draw;
 *   comb  - as draws, a combiner word changed before each;
 *   valid - as draws, VTXBUF_VALIDATE before each;
 *   fmt   - as draws, the vertex formats sent again before each;
 *   nop   - as draws, a no-op (0x100) before each;
 * and "big": 300 triangles covering the screen (fill rate).
 *
 *   DCPERF
 *
 * HX-TEST lines: map, mode, accel, celsius, and one per case ("DCPERF case us=..." lines too). */
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
#define NT 3000

typedef struct { float x, y, z, w; uint32_t col; float s, t, s1, t1, fog; } cvert;

static int pitch_px;
static uint32_t vb, zoff, toff[2];

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

static void formats(void)
{
    static const uint32_t fmt[8] = { 0x01002842, 0x2840, 0x0002, 0x2822, 0x2822, 0x0002, 0x0002, 0x2812 };
    static const uint32_t at[8] = { 0, 16, 0, 20, 28, 0, 0, 36 };
    int i;
    for (i = 0; i < 8; i++)
        m1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    for (i = 0; i < 8; i++)
        if (fmt[i] & 0xF0)
            m1(0xD00 + 8u * (uint32_t)i, vb + at[i]);
}

static void setup(void)
{
    uint32_t pm[16];
    int i;
    tnt2_celsius_target(0, zoff, pitch_px * 2, W, H, 16);
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
    m1(0x30C, 0); m1(0x35C, 0); m1(0x2A4, 0);
    formats();
    m1(0xCF0, 0);
}

static void untextured(void)
{
    m1(0x228, 0); m1(0x22C, 0);
    m1(0x260, 0); m1(0x268, 0); m1(0x270, 0); m1(0x278, 0); m1(0x280, 0); m1(0x284, 0x18000000);
    m1(0x288, 0x20042000); m1(0x28C, 0x20001400);
}

static void textured(void)
{
    m1(0x220, 0x11661351);                              /* TEX_FORMAT(0): A8R8G8B8 64x64, repeat */
    m1(0x218, toff[0]);
    m1(0x248, 0x22000000);
    m1(0x228, 0x40000000);
    m1(0x260, 0x18140000); m1(0x268, 0x08040000); m1(0x270, 0); m1(0x278, 0xC0); m1(0x280, 0xC0); m1(0x284, 0x18000000);
    m1(0x288, 0x200C2000); m1(0x28C, 0x20001C00);
}

static void draw(uint32_t first, uint32_t n)
{
    m1(0x13FC, 5);
    while (n) {
        uint32_t k = n > 255u ? 255u : n;
        m1(0x1400, (k - 1u) << 24 | first);
        first += k, n -= k;
    }
    m1(0x13FC, 0);
}

enum { C_ONE, C_DRAWS, C_TEX, C_COMB, C_VALID, C_FMT, C_NOP, C_BIG };

static uint32_t run_case(int c)
{
    uint32_t t0, i;
    tnt2_sync(1000000);
    t0 = sys_time_us();
    if (c == C_ONE)
        draw(0, NT * 3);
    else if (c == C_BIG)
        draw(NT * 3, 300 * 3);
    else
        for (i = 0; i < NT / 3; i++) {
            switch (c) {
            case C_TEX: m1(0x218, toff[i & 1]); break;
            case C_COMB: m1(0x270, i & 1 ? 0x10101010u : 0x20202020u); break;
            case C_VALID: m1(0xCF0, 0); break;
            case C_FMT: formats(); break;
            case C_NOP: m1(0x100, 0); break;
            default: break;
            }
            draw(i * 9u, 9);
            if ((i & 31) == 31)
                tnt2_kick();
        }
    tnt2_sync(2000000);
    return sys_time_us() - t0;
}

static int cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    static const char *const names[] = { "one", "draws", "tex", "comb", "valid", "fmt", "nop", "big" };
    static uint8_t tex[64 * 64 * 4];
    mga_vbe_mode m;
    int c, k, i;
    hx_init(1, argv, "dcperf");
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
    hx_test("mode", 1, "mode %03x", m.mode);
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    zoff = ((uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    toff[0] = (zoff + (uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    toff[1] = toff[0] + 16384u;
    vb = toff[1] + 16384u;
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    if (tnt2_3d_init() != 0 || tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 set up");
    for (k = 0; k < 2; k++) {
        const uint8_t *lv[1];
        for (i = 0; i < 64 * 64; i++)
            tex[i * 4] = (uint8_t)(i * (k + 3)), tex[i * 4 + 1] = (uint8_t)(i >> 3), tex[i * 4 + 2] = 90, tex[i * 4 + 3] = 255;
        lv[0] = tex;
        tnt2_tex_load_rgba8(toff[k], TNT2_TEX_A8R8G8B8, 6, 6, 1, lv);
    }
    {
        /* 3,000 triangles of 12 pixels a side on a grid, then 300 screen-sized ones */
        cvert *v = (cvert *)malloc(sizeof(cvert) * (NT * 3 + 900));
        for (i = 0; i < NT; i++) {
            float x = (float)(20 + (i % 50) * 12), y = (float)(20 + ((i / 50) % 36) * 12);
            int j;
            for (j = 0; j < 3; j++) {
                cvert *p = &v[i * 3 + j];
                p->x = x + (j == 1 ? 11.0f : 0.0f), p->y = y + (j == 2 ? 11.0f : 0.0f), p->z = 0.5f, p->w = 1.0f;
                p->col = 0xFF000000u | (uint32_t)(i * 2654435761u >> 8 & 0xFFFFFFu);
                p->s = (float)j * 0.3f, p->t = (float)(j == 2) * 0.3f, p->s1 = p->t1 = 0, p->fog = 0;
            }
        }
        for (i = 0; i < 300; i++) {
            static const float bx[3] = { 0, 1280, 0 }, by[3] = { 0, 0, 960 };
            int j;
            for (j = 0; j < 3; j++) {
                cvert *p = &v[NT * 3 + i * 3 + j];
                p->x = bx[j], p->y = by[j], p->z = 0.5f, p->w = 1.0f, p->col = 0xFF404040u + (uint32_t)i;
                p->s = bx[j] / 64.0f, p->t = by[j] / 64.0f, p->s1 = p->t1 = 0, p->fog = 0;
            }
        }
        memcpy((void *)(tnt2_fb + vb), v, sizeof(cvert) * (NT * 3 + 900));
        (void)tnt2_fb[vb];
        free(v);
    }
    setup();
    for (c = C_ONE; c <= C_BIG; c++) {
        uint32_t us[5];
        if (c == C_TEX || c == C_BIG)
            textured();
        else
            untextured();
        for (k = 0; k < 5; k++)
            us[k] = run_case(c);
        if (!trap_ok(names[c]))
            break;
        qsort(us, 5, sizeof us[0], cmp);
        hx_log("DCPERF %s us=%lu (%lu..%lu)", names[c], (unsigned long)us[2], (unsigned long)us[0], (unsigned long)us[4]);
        hx_test(names[c], 1, "%lu us (median of five)", (unsigned long)us[2]);
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
