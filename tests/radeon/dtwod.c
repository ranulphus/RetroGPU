/* dtwod.c - DTWOD: the Radeon layer's 2D drawing (docs/radeon.md, RD1), as RTWOD checks the Rage 128's
 * (tests/r128/rtwod.c, from which it is made) and TWODTEST the Matrox HAL's: at each depth (8, 16 and 32 bits) it
 * draws with each primitive on the screen and on two surfaces off it, of
 * other pitches, draws the same with the software model (tests/hal/sw2d.c)
 * in a copy of VRAM, and compares the two byte for byte.
 *
 * Then, at 16 bits, the hardware cursor and the display start, read from the
 * emulated monitor (86Box's unit tester).
 *
 *   DTWOD [8] [16] [32] [ops=N] [each]       (each: checked after every operation, DTWOD-OP lines)
 *
 * HX-TEST lines: map, mode-BPP, fill-BPP, copy-BPP, copy-from-BPP, mono-BPP,
 * image-BPP, mixed-BPP, cursor, cursor-edge, start, start-high. */
#include "hx.h"
#include "sw2d.h"
#include "radeon/radeon.h"
#include "mga/sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 480

typedef struct {
    const char *name;
    uint32_t off;               /* in VRAM (the layer moves what is below 1 KB of it into x) */
    int w, h, pitch_b;          /* the pitch in bytes, a multiple of 64 */
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
    return bpp == 32 ? v : bpp == 16 ? v & 0xFFFF : v & 0xFF;
}

static void target(surface *t, int cx0, int cy0, int cx1, int cy1)
{
    radeon_set_target(t->off, t->pitch_b, bpp);
    radeon_set_clip(cx0, cy0, cx1, cy1);
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

static int each;                        /* (DTWOD each: below) */
static int aligned;                     /* (DTWOD aligned: s2 on the engine's 1 KB grid too) */

static int differs(const surface *t, char *why)
{
    int y, x;
    for (y = 0; y < t->h; y++) {
        const uint8_t *a = (const uint8_t *)radeon.fb + t->off + (long)y * t->pitch_b, *b = shadow + t->off + (long)y * t->pitch_b;
        if (memcmp(a, b, (size_t)(t->w * bpp_b)) == 0)
            continue;
        for (x = 0; x < t->w && !memcmp(a + x * bpp_b, b + x * bpp_b, (size_t)bpp_b); x++)
            ;
        {
            sw_surf v = t->s;
            uint32_t want = sw_get(&v, x, y);
            int n = 0, x0 = x, x1 = x, y1 = y, xx, yy;
            /* (how many pixels differ, and the box they are in) */
            for (yy = y; yy < t->h; yy++) {
                const uint8_t *a2 = (const uint8_t *)radeon.fb + t->off + (long)yy * t->pitch_b;
                const uint8_t *b2 = shadow + t->off + (long)yy * t->pitch_b;
                for (xx = 0; xx < t->w; xx++)
                    if (memcmp(a2 + xx * bpp_b, b2 + xx * bpp_b, (size_t)bpp_b)) {
                        n++;
                        if (xx < x0) x0 = xx;
                        if (xx > x1) x1 = xx;
                        y1 = yy;
                    }
            }
            v.mem = (uint8_t *)radeon.fb + t->off;
            sprintf(why, "%s at %d,%d: %0*lx, want %0*lx; %d pixels differ in %d,%d-%d,%d (byte %lu)", t->name, x, y,
                    bpp_b * 2, (unsigned long)sw_get(&v, x, y), bpp_b * 2, (unsigned long)want, n, x0, y, x1, y1,
                    (unsigned long)(t->off + (uint32_t)y * (uint32_t)t->pitch_b + (uint32_t)(x * bpp_b)));
            if (each) {
                /* Read again: at once, after reading far away (another megabyte), after another sync, and after
                 * half a second: a value that comes right was a stale read, one that stays is in the memory. */
                volatile uint32_t *far_ = (volatile uint32_t *)(radeon.fb + (20u << 20));
                uint32_t r1 = sw_get(&v, x, y), r2, r3, r4, t0, sink = 0;
                int k;
                for (k = 0; k < 64; k++)
                    sink += far_[k * 16];
                r2 = sw_get(&v, x, y);
                radeon_sync(1000000);
                r3 = sw_get(&v, x, y);
                t0 = sys_time_us();
                while ((uint32_t)(sys_time_us() - t0) < 500000u)
                    ;
                r4 = sw_get(&v, x, y);
                hx_log("DTWOD-REREAD %s %d,%d: again %lx, after far reads %lx, after a sync %lx, after 0.5 s %lx (want %lx) %lx",
                       t->name, x, y, (unsigned long)r1, (unsigned long)r2, (unsigned long)r3, (unsigned long)r4,
                       (unsigned long)want, (unsigned long)(sink & 0));
            }
        }
        return 1;
    }
    return 0;
}

static int check(const char *name)
{
    char why[240] = "";
    int bad;
    if (radeon_sync(2000000) < 0) {
        hx_test(name, 0, "the engine never went idle");
        return 0;
    }
    bad = differs(&scr, why) || differs(&s1, why) || differs(&s2, why);
    hx_test(name, !bad, "%s", bad ? why : "same");
    if (bad)                                    /* the next case starts from what the card has */
        memcpy(shadow, (const void *)radeon.fb, used);
    return !bad;
}

static void name_of(char *out, const char *what) { sprintf(out, "%s-%d", what, bpp); }

/* DTWOD each: the three surfaces compared after every operation, the first that leaves a difference named with its
 * numbers (DTWOD-OP lines, up to 12); the model then takes what the card has and the case goes on. */
static int each_said;
static char last_op[200];
static surface *op_t;

static void after_op(void)
{
    char why[240] = "";
    if (!each || each_said >= 12)
        return;
    if (radeon_sync(2000000) < 0) {
        hx_log("DTWOD-OP %s: the engine never went idle", last_op);
        each_said++;
        return;
    }
    if (differs(&scr, why) || differs(&s1, why) || differs(&s2, why)) {
        hx_log("DTWOD-OP %d bits, on %s clip %d,%d-%d,%d: %s -> %s", bpp, op_t ? op_t->name : "?", op_t ? op_t->s.cx0 : 0,
               op_t ? op_t->s.cy0 : 0, op_t ? op_t->s.cx1 : 0, op_t ? op_t->s.cy1 : 0, last_op, why);
        each_said++;
        memcpy(shadow, (const void *)radeon.fb, used);
    }
}

static void do_fill(surface *t)
{
    int x = rint_(-20, t->w), y = rint_(-20, t->h), w = rint_(1, 200), h = rint_(1, 150);
    uint32_t v = rpix();
    radeon_fill(x, y, w, h, v);
    sw_fill(&t->s, x, y, w, h, v);
    sprintf(last_op, "fill %d,%d %dx%d", x, y, w, h), op_t = t;
    after_op();
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
    radeon_copy(sx, sy, dx, dy, w, h);
    sw_copy(&t->s, dx, dy, &t->s, sx, sy, w, h);
    sprintf(last_op, "copy %d,%d -> %d,%d %dx%d", sx, sy, dx, dy, w, h), op_t = t;
    after_op();
}

static void do_copy_from(surface *t, surface *from)
{
    int w = rint_(1, from->w), h = rint_(1, from->h);
    int sx = rint_(0, from->w - w), sy = rint_(0, from->h - h), dx = rint_(-10, t->w - 1), dy = rint_(-10, t->h - 1);
    radeon_copy_from(from->off, from->pitch_b, sx, sy, dx, dy, w, h);
    sw_copy(&t->s, dx, dy, &from->s, sx, sy, w, h);
    sprintf(last_op, "copy-from %s %d,%d -> %d,%d %dx%d", from->name, sx, sy, dx, dy, w, h), op_t = t;
    after_op();
}

static uint8_t bits[64 * 1024];

static void do_mono(surface *t, int w, int h)
{
    static const int widths[] = { 1, 7, 8, 9, 16, 31, 32, 33, 63, 64, 65, 100, 200 };
    int stride, x, y, i;
    unsigned flags = rnd(2) ? RADEON_MONO_TRANSPARENT : 0;
    uint32_t fg = rpix(), bg = rpix();
    if (!w)
        w = widths[rnd(sizeof widths / sizeof widths[0])], h = rint_(1, 40);
    stride = (w + 7) / 8 + (int)rnd(3);
    for (i = 0; i < stride * h; i++)
        bits[i] = (uint8_t)rnd(256);
    x = rint_(-20, t->w - 1), y = rint_(-20, t->h - 1);
    radeon_mono(x, y, w, h, bits, stride, fg, bg, flags);
    sw_mono(&t->s, x, y, w, h, bits, stride, fg, bg, flags & RADEON_MONO_TRANSPARENT);
    sprintf(last_op, "mono %d,%d %dx%d stride %d flags %u", x, y, w, h, stride, flags), op_t = t;
    after_op();
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
    radeon_image(x, y, w, h, px, w * bpp_b);
    sw_image(&t->s, x, y, w, h, px, w * bpp_b);
    sprintf(last_op, "image %d,%d %dx%d", x, y, w, h), op_t = t;
    after_op();
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
    radeon_copy(0, 1, 0, 0, SW, SH - 1);
    sw_copy(&scr.s, 0, 0, &scr.s, 0, 1, SW, SH - 1);
    radeon_copy(0, 0, 0, 1, SW, SH - 1);
    sw_copy(&scr.s, 0, 1, &scr.s, 0, 0, SW, SH - 1);
    radeon_copy(1, 0, 0, 0, SW - 1, SH);
    sw_copy(&scr.s, 0, 0, &scr.s, 1, 0, SW - 1, SH);
    radeon_copy(0, 0, 1, 0, SW - 1, SH);
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

/* ---- the cursor and the display start, from the emulated monitor */

static uint8_t *grab(int x, int y, int w, int h)
{
    uint8_t *p = malloc((size_t)w * (size_t)h * 4);
    uint16_t cw, ch;
    radeon_vsync_wait(200000);
    radeon_vsync_wait(200000);                   /* (a whole frame since the change) */
    if (!p || hx_ut_capture(&cw, &ch) < 0 || hx_ut_read(x, y, w, h, p) < 0) {
        free(p);
        return NULL;
    }
    return p;
}

static uint32_t at_(const uint8_t *p, int w, int x, int y)
{
    const uint8_t *q = p + ((long)y * w + x) * 4;
    return (uint32_t)q[2] << 16 | (uint32_t)q[1] << 8 | q[0];
}

static uint8_t and_mask[512], xor_mask[512];

static void cursor_row(const uint8_t *under, const uint8_t *seen, int w, int y, int x0, int cx, int iy, uint32_t c0,
                       uint32_t c1)
{
    char got[65], want[65];
    int ix;
    for (ix = 0; ix < 64; ix++) {
        int x = cx + ix - x0, a = (and_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1;
        int o = (xor_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1;
        uint32_t u = x >= 0 && x < w ? at_(under, w, x, y) : 0, g = x >= 0 && x < w ? at_(seen, w, x, y) : 0;
        want[ix] = !a ? (o ? '1' : '0') : o ? 'x' : '.';
        got[ix] = x < 0 || x >= w ? ' ' : g == c0 ? '0' : g == c1 ? '1' : g == u ? '.' : g == (u ^ 0xFFFFFF) ? 'x' : '?';
    }
    got[64] = want[64] = 0;
    hx_log("HX-STAT cursor row %d seen %s", iy, got);
    hx_log("HX-STAT cursor row %d want %s", iy, want);
}

static int cursor_differs(const uint8_t *under, const uint8_t *seen, int x0, int y0, int w, int h, int cx, int cy,
                          uint32_t c0, uint32_t c1, char *why)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int ix = x + x0 - cx, iy = y + y0 - cy;
            uint32_t want = at_(under, w, x, y), got = at_(seen, w, x, y);
            if (ix >= 0 && ix < 64 && iy >= 0 && iy < 64) {
                int a = (and_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1, o = (xor_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1;
                want = !a ? (o ? c1 : c0) : o ? want ^ 0xFFFFFF : want;
            }
            if (got != want) {
                sprintf(why, "at %d,%d (image %d,%d): %06lx, want %06lx", x + x0, y + y0, ix, iy, (unsigned long)got,
                        (unsigned long)want);
                if (iy >= 0 && iy < 64)
                    cursor_row(under, seen, w, y, x0, cx, iy, c0, c1);
                return 1;
            }
        }
    return 0;
}

static void cursor_cases(void)
{
    /* Full or empty channels, which 16-bit pixels hold exactly; red and blue told apart. */
    const uint32_t c0 = 0x00FFFF, c1 = 0xFF00FF;
    uint32_t off = (radeon.vram_bytes - RADEON_CURSOR_BYTES) & ~1023u;
    uint8_t *under, *seen;
    char why[160] = "";
    int i, bad;

    for (i = 0; i < 512; i++) {                 /* each of the four kinds, in bands and specks */
        int y = i / 8, x8 = i % 8;
        and_mask[i] = (uint8_t)(y < 16 ? 0x00 : y < 32 ? 0xFF : (0xF0 >> (x8 & 1)) ^ (uint8_t)(y * 7));
        xor_mask[i] = (uint8_t)(y < 8 ? 0x00 : y < 16 ? 0xFF : y < 24 ? 0x00 : y < 32 ? 0xFF : (uint8_t)(x8 * 37 + y));
    }
    if (radeon_cursor_load(off, and_mask, xor_mask) != 0) {
        hx_test("cursor", 0, "no place for the image");
        return;
    }
    hx_stat("cursor at=%08lx", (unsigned long)off);
    radeon_cursor_colours(c0, c1);
    radeon_cursor_show(0);
    under = grab(80, 40, 96, 96);
    radeon_cursor_move(100, 50);
    radeon_cursor_show(1);
    seen = grab(80, 40, 96, 96);
    bad = !under || !seen || cursor_differs(under, seen, 80, 40, 96, 96, 100, 50, c0, c1, why);
    hx_test("cursor", !bad, "%s", !under || !seen ? "no capture" : bad ? why : "image, colours and place");
    free(under);
    free(seen);

    radeon_cursor_show(0);                       /* partly off the top left corner */
    under = grab(0, 0, 64, 64);
    radeon_cursor_move(-10, -20);
    radeon_cursor_show(1);
    seen = grab(0, 0, 64, 64);
    bad = !under || !seen || cursor_differs(under, seen, 0, 0, 64, 64, -10, -20, c0, c1, why);
    hx_test("cursor-edge", !bad, "%s", !under || !seen ? "no capture" : bad ? why : "clipped at the top left");
    free(under);
    free(seen);
    radeon_cursor_show(0);
}

static void start_case(const char *name, uint32_t at)
{
    uint8_t *before, *after;
    uint32_t row = (uint32_t)scr.pitch_b;
    int bad;
    radeon_sync(2000000);
    before = grab(0, 100, SW, 64);              /* rows 100-163, which `at` holds a copy of */
    memcpy((uint8_t *)radeon.fb + at, (const uint8_t *)radeon.fb + 100 * row, 64 * row);
    radeon_vsync_wait(200000);
    radeon_set_start(at);
    after = grab(0, 0, SW, 64);
    bad = !before || !after || memcmp(before, after, (size_t)SW * 64 * 4) != 0;
    hx_test(name, !bad, "start=%08lx%s", (unsigned long)at, !before || !after ? " no capture" : bad ? " other pixels" : "");
    free(before);
    free(after);
    radeon_vsync_wait(200000);
    radeon_set_start(0);
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
    radeon_mode m;
    char n[32];
    uint32_t off;
    bpp = b, bpp_b = b / 8;
    name_of(n, "mode");
    if (radeon_find_mode(SW, SH, b, &m) < 0 || radeon_set_mode(&m) < 0) {
        hx_test(n, 0, "no %dx%dx%d mode", SW, SH, b);
        return 0;
    }
    hx_test(n, 1, "mode %03x, pitch %u", m.mode, m.pitch_bytes);
    /* The screen, then two surfaces of other pitches off it. */
    off = (uint32_t)m.pitch_bytes * SH;
    used = off + 1024u * 1024u;                 /* (s1 and s2 at 32 bits: 512 KB) */
    place(&scr, "screen", 0, SW, SH, m.pitch_bytes / bpp_b);
    /* (pitches a multiple of 64 bytes at every depth, the engine's unit; s1 a whole number of kilobytes in, s2 not:
     * the layer moves the rest of its offset into x) */
    place(&s1, "s1", off, 300, 200, 384);
    place(&s2, "s2", ((off + 384u * bpp_b * 200 + 1023) & ~1023u) + (aligned ? 0u : 64u), 500, 120, 576);
    memset((void *)radeon.fb, 0, used);
    memset(shadow, 0, used);
    depth_cases();
    if (b == 16 && hx_ut_present()) {
        cursor_cases();
        start_case("start", (used + 4095) & ~4095u);           /* past 1 MB: CR69's bits */
        start_case("start-high", 3u << 20);                    /* bit 19 of the 4-byte units */
    }
    return 1;
}

int main(int argc, char **argv)
{
    int i, d8 = 0, d16 = 0, d32 = 0;
    hx_init(1, argv, "dtwod");                  /* (its own arguments only) */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "8")) d8 = 1;
        else if (!strcmp(argv[i], "16")) d16 = 1;
        else if (!strcmp(argv[i], "32")) d32 = 1;
        else if (!strcmp(argv[i], "each")) each = 1;
        else if (!strcmp(argv[i], "aligned")) aligned = 1;
        else if (!strncmp(argv[i], "ops=", 4)) nops = atoi(argv[i] + 4);
    }
    if (!d8 && !d16 && !d32)
        d8 = d16 = d32 = 1;
    if (radeon_find() < 0 || radeon_map() < 0) {
        hx_test("map", 0, "no Rage 128 mapped");
        hx_done(1);
    }
    hx_test("map", 1, "Radeon %04x rev %02x at %08x, %u KB", radeon.device_id, radeon.revision, (unsigned)radeon.fb_phys,
            (unsigned)(radeon.vram_bytes >> 10));
    shadow = malloc(4u << 20);
    if (!shadow)
        hx_done(1);
    if (d8)
        depth(8);
    if (d16)
        depth(16);
    if (d32)
        depth(32);
    radeon_text_mode();
    radeon_unmap();
    hx_done(hx_failures() ? 1 : 0);
}
