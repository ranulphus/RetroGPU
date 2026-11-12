/* vtwod.c - VTWOD: the ViRGE/DX layer's 2D drawing (docs/virge.md, V1),
 * as TWODTEST checks the Matrox HAL's: at each depth (8 and 16 bits) it
 * draws with each primitive on the screen and on two surfaces off it, of
 * other pitches, draws the same with the software model (tests/hal/sw2d.c)
 * in a copy of VRAM, and compares the two byte for byte.
 *
 *   VTWOD [8] [16] [ops=N]
 *
 * HX-TEST lines: map, mode-BPP, fill-BPP, copy-BPP, copy-from-BPP, mono-BPP,
 * image-BPP, mixed-BPP. */
#include "hx.h"
#include "sw2d.h"
#include "virge/virge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 480

typedef struct {
    const char *name;
    uint32_t off;               /* in VRAM, a multiple of 8 */
    int w, h, pitch_b;          /* the pitch in bytes, a multiple of 8 */
    sw_surf s;
} surface;

static uint8_t *shadow;         /* VRAM [0, used) as it should be */
static uint32_t used;
static int bpp, bpp_b;
static surface scr, s1, s2;
static unsigned seed = 1;
static int nops = 300;

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
    virge_set_target(t->off, t->pitch_b, bpp);
    virge_set_clip(cx0, cy0, cx1, cy1);
    t->s.cx0 = cx0, t->s.cy0 = cy0, t->s.cx1 = cx1, t->s.cy1 = cy1;
}

static void random_clip(surface *t)
{
    if (rnd(3) == 0) {
        target(t, 0, 0, t->w, t->h);
    } else {
        int x0 = rint_(0, t->w - 2), y0 = rint_(0, t->h - 2);
        target(t, x0, y0, rint_(x0 + 1, t->w), rint_(y0 + 1, t->h));
    }
}

static int differs(const surface *t, char *why)
{
    int y, x;
    for (y = 0; y < t->h; y++) {
        const uint8_t *a = (const uint8_t *)virge.fb + t->off + (long)y * t->pitch_b, *b = shadow + t->off + (long)y * t->pitch_b;
        if (memcmp(a, b, (size_t)(t->w * bpp_b)) == 0)
            continue;
        for (x = 0; x < t->w && !memcmp(a + x * bpp_b, b + x * bpp_b, (size_t)bpp_b); x++)
            ;
        {
            sw_surf v = t->s;
            uint32_t want = sw_get(&v, x, y);
            v.mem = (uint8_t *)virge.fb + t->off;
            sprintf(why, "%s at %d,%d: %0*lx, want %0*lx", t->name, x, y, bpp_b * 2, (unsigned long)sw_get(&v, x, y),
                    bpp_b * 2, (unsigned long)want);
        }
        return 1;
    }
    return 0;
}

static int check(const char *name)
{
    char why[160] = "";
    int bad;
    if (virge_sync(2000000) < 0) {
        hx_test(name, 0, "the engine never went idle");
        return 0;
    }
    bad = differs(&scr, why) || differs(&s1, why) || differs(&s2, why);
    hx_test(name, !bad, "%s", bad ? why : "same");
    if (bad)                                    /* the next case starts from what the card has */
        memcpy(shadow, (const void *)virge.fb, used);
    return !bad;
}

static void name_of(char *out, const char *what) { sprintf(out, "%s-%d", what, bpp); }

static void do_fill(surface *t)
{
    int x = rint_(-20, t->w), y = rint_(-20, t->h), w = rint_(1, 200), h = rint_(1, 150);
    uint32_t v = rpix();
    virge_fill(x, y, w, h, v);
    sw_fill(&t->s, x, y, w, h, v);
}

static void do_copy(surface *t)
{
    int w = rint_(1, t->w / 2), h = rint_(1, t->h / 2);
    int sx = rint_(0, t->w - w), sy = rint_(0, t->h - h), dx, dy;
    if (rnd(2)) {
        dx = sx + rint_(-w / 2, w / 2), dy = sy + rint_(-h / 2, h / 2);
    } else {
        dx = rint_(-10, t->w - 1), dy = rint_(-10, t->h - 1);
    }
    virge_copy(sx, sy, dx, dy, w, h);
    sw_copy(&t->s, dx, dy, &t->s, sx, sy, w, h);
}

static void do_copy_from(surface *t, surface *from)
{
    int w = rint_(1, from->w), h = rint_(1, from->h);
    int sx = rint_(0, from->w - w), sy = rint_(0, from->h - h), dx = rint_(-10, t->w - 1), dy = rint_(-10, t->h - 1);
    virge_copy_from(from->off, from->pitch_b, sx, sy, dx, dy, w, h);
    sw_copy(&t->s, dx, dy, &from->s, sx, sy, w, h);
}

static uint8_t bits[64 * 1024];

static void do_mono(surface *t, int w, int h)
{
    static const int widths[] = { 1, 7, 8, 9, 16, 31, 32, 33, 63, 64, 65, 100, 200 };
    int stride, x, y, i;
    unsigned flags = rnd(2) ? VIRGE_MONO_TRANSPARENT : 0;
    uint32_t fg = rpix(), bg = rpix();
    if (!w)
        w = widths[rnd(sizeof widths / sizeof widths[0])], h = rint_(1, 40);
    stride = (w + 7) / 8 + (int)rnd(3);
    for (i = 0; i < stride * h; i++)
        bits[i] = (uint8_t)rnd(256);
    x = rint_(-20, t->w - 1), y = rint_(-20, t->h - 1);
    virge_mono(x, y, w, h, bits, stride, fg, bg, flags);
    sw_mono(&t->s, x, y, w, h, bits, stride, fg, bg, flags & VIRGE_MONO_TRANSPARENT);
}

static void do_image(surface *t, int w, int h)
{
    uint8_t *px;
    int x, y, i;
    if (!w)
        w = rint_(1, 150), h = rint_(1, 60);
    px = malloc((size_t)w * (size_t)h * (size_t)bpp_b);
    for (i = 0; i < w * h * bpp_b; i++)
        px[i] = (uint8_t)rnd(256);
    x = rint_(-20, t->w - 1), y = rint_(-20, t->h - 1);
    virge_image(x, y, w, h, px, w * bpp_b);
    sw_image(&t->s, x, y, w, h, px, w * bpp_b);
    free(px);
}

static void depth_cases(void)
{
    char n[32];
    int i, k;
    surface *ts[3] = { &scr, &s1, &s2 };

    name_of(n, "fill");
    for (i = 0; i < 40; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_fill(t);
    }
    check(n);

    name_of(n, "copy");
    for (i = 0; i < 40; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_copy(t);
    }
    target(&scr, 0, 0, SW, SH);                 /* the whole screen a line up, a line down, a column each way */
    virge_copy(0, 1, 0, 0, SW, SH - 1);
    sw_copy(&scr.s, 0, 0, &scr.s, 0, 1, SW, SH - 1);
    virge_copy(0, 0, 0, 1, SW, SH - 1);
    sw_copy(&scr.s, 0, 1, &scr.s, 0, 0, SW, SH - 1);
    virge_copy(1, 0, 0, 0, SW - 1, SH);
    sw_copy(&scr.s, 0, 0, &scr.s, 1, 0, SW - 1, SH);
    virge_copy(0, 0, 1, 0, SW - 1, SH);
    sw_copy(&scr.s, 1, 0, &scr.s, 0, 0, SW - 1, SH);
    check(n);

    name_of(n, "copy-from");
    for (i = 0; i < 30; i++) {
        surface *t = ts[i % 3], *f = ts[(i + 1 + (int)rnd(2)) % 3];
        random_clip(t);
        do_copy_from(t, f);
    }
    check(n);

    name_of(n, "mono");
    for (i = 0; i < 40; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_mono(t, 0, 0);
    }
    target(&scr, 0, 0, SW, SH);
    do_mono(&scr, 600, 300);
    check(n);

    name_of(n, "image");
    for (i = 0; i < 30; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_image(t, 0, 0);
    }
    target(&scr, 0, 0, SW, SH);
    do_image(&scr, SW, 200);
    check(n);

    name_of(n, "mixed");
    for (i = 0; i < nops; i++) {
        surface *t = ts[rnd(3)];
        random_clip(t);
        k = (int)rnd(5);
        if (k == 0)
            do_fill(t);
        else if (k == 1)
            do_copy(t);
        else if (k == 2)
            do_copy_from(t, ts[rnd(3)]);
        else if (k == 3)
            do_mono(t, 0, 0);
        else
            do_image(t, 0, 0);
    }
    check(n);
}

static void place(surface *t, const char *name, uint32_t off, int w, int h, int pitch_px)
{
    t->name = name;
    t->off = off;
    t->w = w, t->h = h, t->pitch_b = pitch_px * bpp_b;
    t->s.mem = shadow + off;
    t->s.pitch_px = pitch_px;
    t->s.bpp = bpp;
}

static int depth(int b)
{
    virge_mode m;
    char n[32];
    uint32_t off;
    bpp = b, bpp_b = b / 8;
    name_of(n, "mode");
    if (virge_find_mode(SW, SH, b, &m) < 0 || virge_set_mode(&m) < 0) {
        hx_test(n, 0, "no %dx%dx%d mode", SW, SH, b);
        return 0;
    }
    hx_test(n, 1, "mode %03x, pitch %u", m.mode, m.pitch_bytes);
    /* The screen, then two surfaces of other pitches off it. */
    off = (uint32_t)m.pitch_bytes * SH;
    used = off + 400u * 1024u;
    place(&scr, "screen", 0, SW, SH, m.pitch_bytes / bpp_b);
    place(&s1, "s1", off, 300, 200, 328);
    place(&s2, "s2", (off + 328u * bpp_b * 200 + 7) & ~7u, 500, 120, 520);
    memset((void *)virge.fb, 0, used);
    memset(shadow, 0, used);
    depth_cases();
    return 1;
}

int main(int argc, char **argv)
{
    int i, d8 = 0, d16 = 0;
    hx_init(argc, argv, "vtwod");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "8")) d8 = 1;
        else if (!strcmp(argv[i], "16")) d16 = 1;
        else if (!strncmp(argv[i], "ops=", 4)) nops = atoi(argv[i] + 4);
    }
    if (!d8 && !d16)
        d8 = d16 = 1;
    if (virge_find() < 0 || virge_map() < 0) {
        hx_test("map", 0, "no ViRGE/DX mapped");
        hx_done(1);
    }
    hx_test("map", 1, "ViRGE %04x rev %02x at %08x, %u KB", virge.device_id, virge.revision, (unsigned)virge.bar0,
            (unsigned)(virge.vram_bytes >> 10));
    shadow = malloc(2u << 20);
    if (!shadow)
        hx_done(1);
    if (d8)
        depth(8);
    if (d16)
        depth(16);
    virge_text_mode();
    virge_unmap();
    hx_done(hx_failures() ? 1 : 0);
}
