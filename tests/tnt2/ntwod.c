/* ntwod.c - NTWOD: the Riva TNT2 layer's first 2D (docs/tnt2.md, T1), as VTWOD checks the ViRGE's: at each depth
 * (16 and 8 bits) solid fills and screen-to-screen copies (overlapping ones too), on the screen and on a surface off
 * it of another pitch, each also drawn by the software model (tests/hal/sw2d.c) in a copy of VRAM, the two compared
 * byte for byte. Coordinates stay inside the surface (the methods' x and y are 16 bits each); clipping is tested
 * through the clip object.
 *
 *   NTWOD [16] [8] [ops=N]
 *
 * HX-TEST lines: map, accel, mode-BPP, fill-BPP, clip-BPP, copy-BPP, copy-off-BPP, mixed-BPP. */
#include "hx.h"
#include "sw2d.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 480

typedef struct {
    const char *name;
    uint32_t off;
    int w, h, pitch_b;
    sw_surf s;
} surface;

static uint8_t *shadow;
static uint32_t used;
static int bpp, bpp_b;
static surface scr, off1;
static unsigned seed = 1;
static int nops = 200;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

static int rint_(int lo, int hi) { return lo + (int)rnd((unsigned)(hi - lo + 1)); }

static uint32_t rpix(void)
{
    uint32_t v = (rnd(65536) << 16) ^ rnd(65536);
    return bpp == 16 ? v & 0xFFFF : v & 0xFF;
}

static void target(surface *t, int cx0, int cy0, int cx1, int cy1)
{
    tnt2_set_target(t->off, t->pitch_b, bpp);
    tnt2_set_clip(cx0, cy0, cx1, cy1);
    t->s.cx0 = cx0, t->s.cy0 = cy0, t->s.cx1 = cx1, t->s.cy1 = cy1;
}

static int differs(const surface *t, char *why)
{
    int y, x;
    for (y = 0; y < t->h; y++) {
        const uint8_t *a = (const uint8_t *)tnt2_fb + t->off + (long)y * t->pitch_b,
                      *b = shadow + t->off + (long)y * t->pitch_b;
        if (memcmp(a, b, (size_t)(t->w * bpp_b)) == 0)
            continue;
        for (x = 0; x < t->w && !memcmp(a + x * bpp_b, b + x * bpp_b, (size_t)bpp_b); x++)
            ;
        {
            sw_surf v = t->s;
            uint32_t want = sw_get(&v, x, y);
            v.mem = (uint8_t *)tnt2_fb + t->off;
            sprintf(why, "%s at %d,%d: %0*lx, want %0*lx", t->name, x, y, bpp_b * 2, (unsigned long)sw_get(&v, x, y),
                    bpp_b * 2, (unsigned long)want);
        }
        return 1;
    }
    return 0;
}

static int check(const char *what)
{
    char why[160] = "", name[32];
    int bad;
    sprintf(name, "%s-%d", what, bpp);
    if (tnt2_sync(2000000) < 0) {
        hx_test(name, 0, "the engine never went idle");
        return 0;
    }
    bad = differs(&scr, why) || differs(&off1, why);
    hx_test(name, !bad, "%s", bad ? why : "same");
    if (bad)                                    /* (the next case starts from what the card has) */
        memcpy(shadow, (const void *)tnt2_fb, used);
    return !bad;
}

static void do_fill(surface *t)
{
    int x = rint_(0, t->w - 1), y = rint_(0, t->h - 1), w = rint_(1, 200), h = rint_(1, 150);
    uint32_t v = rpix();
    if (x + w > t->w) w = t->w - x;
    if (y + h > t->h) h = t->h - y;
    tnt2_fill(x, y, w, h, v);
    sw_fill(&t->s, x, y, w, h, v);
}

static void do_copy(surface *t)
{
    int w = rint_(1, t->w / 2), h = rint_(1, t->h / 2);
    int sx = rint_(0, t->w - w), sy = rint_(0, t->h - h), dx, dy;
    if (rnd(2)) {                               /* overlapping */
        dx = sx + rint_(-w / 2, w / 2), dy = sy + rint_(-h / 2, h / 2);
        if (dx < 0) dx = 0;
        if (dy < 0) dy = 0;
    } else
        dx = rint_(0, t->w - 1), dy = rint_(0, t->h - 1);
    if (dx + w > t->w) dx = t->w - w;
    if (dy + h > t->h) dy = t->h - h;
    tnt2_copy(sx, sy, dx, dy, w, h);
    sw_copy(&t->s, dx, dy, &t->s, sx, sy, w, h);
}

static void clear(surface *t, uint32_t v)
{
    target(t, 0, 0, t->w, t->h);
    tnt2_fill(0, 0, t->w, t->h, v);
    sw_fill(&t->s, 0, 0, t->w, t->h, v);
}

static void depth(int b)
{
    mga_vbe_mode m;
    int pitch_px = 0, i;
    char name[16];
    bpp = b;
    bpp_b = b / 8;
    sprintf(name, "mode-%d", b);
    if (vbe_find_mode(SW, SH, b, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test(name, 0, "no %dx%dx%d mode with a linear window", SW, SH, b);
        return;
    }
    hx_test(name, 1, "mode %03x, pitch %d", m.mode, pitch_px * bpp_b);
    scr.name = "screen";
    scr.off = 0;
    scr.w = SW, scr.h = SH, scr.pitch_b = pitch_px * bpp_b;
    off1.name = "off-screen";
    off1.off = (uint32_t)((scr.pitch_b * SH + 4095) & ~4095);
    off1.w = 300, off1.h = 200, off1.pitch_b = (off1.w * bpp_b + 63) & ~63;
    off1.pitch_b += 64;                         /* (another pitch than the screen's) */
    used = off1.off + (uint32_t)off1.pitch_b * (uint32_t)off1.h;
    memset(shadow, 0, used);
    scr.s.mem = shadow + scr.off; scr.s.pitch_px = scr.pitch_b / bpp_b; scr.s.bpp = bpp;
    off1.s.mem = shadow + off1.off; off1.s.pitch_px = off1.pitch_b / bpp_b; off1.s.bpp = bpp;
    clear(&scr, 0);
    clear(&off1, 0);
    check("clear");

    target(&scr, 0, 0, SW, SH);
    for (i = 0; i < nops; i++)
        do_fill(&scr);
    target(&off1, 0, 0, off1.w, off1.h);
    for (i = 0; i < nops / 4; i++)
        do_fill(&off1);
    check("fill");

    {                                           /* fills inside random clips */
        int k;
        for (k = 0; k < 20; k++) {
            int x0 = rint_(0, SW - 2), y0 = rint_(0, SH - 2);
            target(&scr, x0, y0, rint_(x0 + 1, SW), rint_(y0 + 1, SH));
            for (i = 0; i < 10; i++)
                do_fill(&scr);
        }
        check("clip");
    }

    target(&scr, 0, 0, SW, SH);
    for (i = 0; i < nops; i++)
        do_copy(&scr);
    check("copy");

    target(&off1, 0, 0, off1.w, off1.h);
    for (i = 0; i < nops / 2; i++)
        do_copy(&off1);
    check("copy-off");

    for (i = 0; i < nops; i++) {
        surface *t = rnd(3) ? &scr : &off1;
        target(t, 0, 0, t->w, t->h);
        if (rnd(2)) do_fill(t);
        else do_copy(t);
    }
    check("mixed");
}

int main(int argc, char **argv)
{
    int i, want16 = 0, want8 = 0;
    hx_init(1, argv, "ntwod");                  /* (its own arguments only) */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "16")) want16 = 1;
        else if (!strcmp(argv[i], "8")) want8 = 1;
        else if (!strncmp(argv[i], "ops=", 4)) nops = atoi(argv[i] + 4);
    }
    if (!want16 && !want8)
        want16 = want8 = 1;
    if (tnt2_find() != 0 || tnt2_map() != 0) {
        hx_test("map", 0, "no NV4-family chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s 10de:%04x rev %02x, %lu MB, PMC_BOOT_0 %08lx", tnt2.name, tnt2.device, tnt2.revision,
            (unsigned long)(tnt2.fb_size >> 20), (unsigned long)tnt2.boot0);
    shadow = (uint8_t *)malloc(4u << 20);
    if (!shadow)
        hx_done(1);
    /* The mode first (the BIOS's mode set leaves the engine as it was), then the engine. */
    if (want16) {
        mga_vbe_mode m;
        int p;
        if (vbe_find_mode(SW, SH, 16, &m) == 0)
            vbe_set_mode(&m, 0, &p);
    }
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring's commands were never fetched (GET stayed behind PUT)");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    if (want16)
        depth(16);
    if (want8)
        depth(8);
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
}
