/* dcursor.c - DCURSOR: the Radeon's hardware cursor, for someone watching (docs/radeon.md): a 640x480 16-bit mode
 * with grey bars, a 64x64 cursor (a white arrow with a black outline, a see-through background, and an inverted
 * square at its tail) moved in a circle for secs seconds (10), colours swapped half way. If the cursor draws,
 * the arrow goes round smoothly over the bars, its background clear, the square inverting what is under it.
 *
 *   DCURSOR [secs=N]
 *
 * Lines: DCURSOR-LOAD (where the image went), HX-TEST shown. */
#include "hx.h"
#include "radeon/radeon.h"
#include "mga/sys.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

/* The image as AND and XOR masks, 64 x 64, one bit a pixel, leftmost in bit 7: AND 1 XOR 0 see-through, AND 0
 * XOR 0 colour 0, AND 0 XOR 1 colour 1, AND 1 XOR 1 the screen inverted. */
static void put(uint8_t *a, uint8_t *x, int px, int py, int an, int xo)
{
    int i = py * 8 + px / 8, b = 0x80 >> (px & 7);
    if (an) a[i] |= (uint8_t)b; else a[i] &= (uint8_t)~b;
    if (xo) x[i] |= (uint8_t)b; else x[i] &= (uint8_t)~b;
}

int main(int argc, char **argv)
{
    static uint8_t am[512], xm[512];
    radeon_mode m;
    int i, x, y, secs = 10, n = 0;
    uint32_t t0, off;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "secs=", 5)) secs = atoi(argv[i] + 5);
    hx_init(1, argv, "dcursor");
    if (radeon_find() != 0 || radeon_map() != 0 || radeon_find_mode(W, H, 16, &m) != 0 || radeon_set_mode(&m) != 0) {
        hx_test("init", 0, "no Radeon or no 640x480x16 mode");
        hx_done(1);
    }
    radeon_set_target(0, m.pitch_bytes, 16);
    radeon_set_clip(0, 0, W, H);
    for (i = 0; i < 16; i++)
        radeon_fill(i * 40, 0, 40, H, (uint32_t)(i * 2) << 11 | (uint32_t)(i * 4) << 5 | (uint32_t)(i * 2));
    memset(am, 0xFF, sizeof am);                /* (all see-through) */
    memset(xm, 0, sizeof xm);
    for (y = 0; y < 40; y++)                    /* an arrow: a black outline round white */
        for (x = 0; x <= y / 2 && x < 24; x++) {
            int edge = x == 0 || x == y / 2 || y == 39;
            put(am, xm, x, y, 0, edge ? 0 : 1);
        }
    for (y = 44; y < 60; y++)                   /* the inverted square */
        for (x = 4; x < 20; x++)
            put(am, xm, x, y, 1, 1);
    off = (((uint32_t)m.pitch_bytes * H) + 4095u) & ~4095u;
    if (radeon_cursor_load(off, am, xm) != 0) {
        hx_test("shown", 0, "the cursor didn't load");
        radeon_text_mode();
        hx_done(1);
    }
    hx_log("DCURSOR-LOAD the image at %lu, colours black and white", (unsigned long)off);
    radeon_cursor_colours(0x000000u, 0xFFFFFFu);
    radeon_cursor_show(1);
    t0 = sys_time_us();
    while (sys_time_us() - t0 < (uint32_t)secs * 1000000u) {
        double a = (double)(sys_time_us() - t0) / 1e6 * 2.0;
        radeon_cursor_move(320 + (int)(200.0 * cos(a)), 240 + (int)(160.0 * sin(a)));
        if (n == 0 && sys_time_us() - t0 > (uint32_t)secs * 500000u) {
            radeon_cursor_colours(0xFF0000u, 0x00FF00u);    /* (half way: red outline, green fill) */
            n = 1;
        }
        radeon_vsync_wait(50000);
    }
    radeon_cursor_show(0);
    radeon_text_mode();
    hx_test("shown", 1, "moved for %d s (the watcher says whether it drew)", secs);
    hx_done(0);
}
