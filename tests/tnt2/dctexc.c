/* dctexc.c - DCTEXC: Celsius's (0x96) texture cache against texels the CPU rewrites (the Quadro2 plan's Q4: with
 * PGRAPH's DEBUG_2 as nouveau sets it, DOS-GL's conformance tests that change a texture between draws drew the old
 * texels). For each way of telling the chip, a 64x64 texture is loaded with colour A and drawn (a quad), the chip
 * left idle, its texels rewritten by the CPU to colour B, the way applied, and the texture drawn again elsewhere: the
 * second quad should show B.
 *   none   - nothing;
 *   offset - TEX_OFFSET written again (the same value);
 *   format - TEX_FORMAT written again;
 *   enable - TEX_ENABLE off, then on;
 *   away   - TEX_OFFSET pointed at another texture, then back;
 *   fill   - a 2D fill of a few pixels elsewhere;
 *   nop    - the no-op method (0x100);
 *   valid  - VTXBUF_VALIDATE;
 *   target - the render target sent again (tnt2_celsius_target: its six no-ops).
 *
 *   DCTEXC
 *
 * HX-TEST lines: map, mode, accel, celsius, and one per way. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480
#define S7 7

typedef struct { float x, y, z, w; uint32_t col; float s, t, s1, t1, fog; } cvert;

static int pitch_px;
static uint32_t vb, zoff, toff, toff2;
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

static void load(uint32_t off, uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t *lv[1];
    int i;
    for (i = 0; i < 64 * 64; i++)
        tex[i * 4] = r, tex[i * 4 + 1] = g, tex[i * 4 + 2] = b, tex[i * 4 + 3] = 255;
    lv[0] = tex;
    tnt2_tex_load_rgba8(off, TNT2_TEX_A8R8G8B8, 6, 6, 1, lv);
    (void)tnt2_fb[off];
}

static void setup(void)
{
    static const uint32_t fmt[8] = { 0x01002842, 0x2840, 0x0002, 0x2822, 0x2822, 0x0002, 0x0002, 0x2812 };
    static const uint32_t at[8] = { 0, 16, 0, 20, 28, 0, 0, 36 };
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
    m1(0x30C, 0); m1(0x35C, 0); m1(0x2A4, 0); m1(0x304, 0);
    for (i = 0; i < 8; i++)
        m1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    for (i = 0; i < 8; i++)
        if (fmt[i] & 0xF0)
            m1(0xD00 + 8u * (uint32_t)i, vb + at[i]);
    m1(0xCF0, 0);
    m1(0x220, 0x11661351);
    m1(0x218, toff);
    m1(0x248, 0x11000000);                              /* nearest */
    m1(0x228, 0x40000000);
    m1(0x22C, 0);
    m1(0x260, 0x18140000); m1(0x268, 0x08040000); m1(0x270, 0); m1(0x278, 0xC0); m1(0x280, 0xC0);
    m1(0x284, 0x18000000);
    m1(0x288, 0x200C2000); m1(0x28C, 0x20001C00);
}

/* A quad at (x, y), 64 pixels a side, one texel a pixel: vertices k*4.. in the buffer. */
static void quad(int k, float x, float y)
{
    cvert v[4];
    static const float qx[4] = { 0, 64, 64, 0 }, qy[4] = { 0, 0, 64, 64 };
    int j;
    for (j = 0; j < 4; j++) {
        v[j].x = x + qx[j], v[j].y = y + qy[j], v[j].z = 0.5f, v[j].w = 1.0f, v[j].col = 0xFFFFFFFFu;
        v[j].s = qx[j] / 64.0f, v[j].t = qy[j] / 64.0f, v[j].s1 = v[j].t1 = 0, v[j].fog = 0;
    }
    memcpy((void *)(tnt2_fb + vb + (uint32_t)k * 4u * sizeof(cvert)), v, sizeof v);
    (void)tnt2_fb[vb];
    m1(0xCF0, 0);
    m1(0x13FC, 8);                                      /* GL_QUADS + 1 */
    m1(0x1400, 3u << 24 | (uint32_t)k * 4u);
    m1(0x13FC, 0);
    tnt2_kick();
}

/* The colour at a quad's centre (565). */
static uint16_t centre(float x, float y)
{
    int sx = (int)x + 32, sy = H - 1 - ((int)y + 32);
    return ((volatile uint16_t *)tnt2_fb)[sy * pitch_px + sx];
}

int main(int argc, char **argv)
{
    static const char *const ways[] = { "none", "offset", "format", "enable", "away", "fill", "nop", "valid", "target" };
    mga_vbe_mode m;
    int w;
    hx_init(1, argv, "dctexc");
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
    toff = (zoff + (uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    toff2 = toff + 16384u;
    vb = toff2 + 16384u;
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    if (tnt2_3d_init() != 0 || tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 set up");
    load(toff2, 0, 0, 255);
    setup();
    for (w = 0; w < (int)(sizeof ways / sizeof ways[0]); w++) {
        float x = 20.0f + (float)w * 68.0f;
        uint16_t a, b;
        load(toff, 255, 0, 0);                          /* A: red */
        m1(0x218, toff);
        quad(w * 2, x, 60);
        tnt2_sync(1000000);
        load(toff, 0, 255, 0);                          /* B: green, by the CPU, the chip idle */
        switch (w) {
        case 1: m1(0x218, toff); break;
        case 2: m1(0x220, 0x11661351); break;
        case 3: m1(0x228, 0); m1(0x228, 0x40000000); break;
        case 4: m1(0x218, toff2); m1(0x218, toff); break;
        case 5:
            tnt2_set_target(0, pitch_px * 2, 16);
            tnt2_fill(W - 4, 0, 4, 4, 0xFFFF);
            break;
        case 6: m1(0x100, 0); break;
        case 7: m1(0xCF0, 0); break;
        case 8: tnt2_celsius_target(0, zoff, pitch_px * 2, W, H, 16); break;
        default: break;
        }
        quad(w * 2 + 1, x, 200);
        if (tnt2_sync(1000000) != 0 || !trap_ok(ways[w]))
            break;
        a = centre(x, 60), b = centre(x, 200);
        hx_test(ways[w], a == 0xF800 && b == 0x07E0, "first quad %04x (red f800), second %04x (green 07e0)", a, b);
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
