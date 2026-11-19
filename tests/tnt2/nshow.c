/* nshow.c - NSHOW: the Riva TNT2's cursor and page flips for a person to look at (docs/tnt2.md: no screen capture
 * on the P3). At 640x480x16, two pages shown in turn every 2 seconds (page A red with a white square top left, page B
 * blue with a white square bottom right, by the display start in the vertical blank), and the hardware cursor, an
 * arrow, circling the screen; after SECS/2 seconds the cursor's colours change (white with a black border, then
 * yellow with a green border). Text mode at the end.
 *
 *   NSHOW [secs=N]                (120 by default)
 *
 * NSHOW-PHASE lines on the log; HX-TEST: map, mode, accel, show. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

int main(int argc, char **argv)
{
    static uint8_t and_m[512], xor_m[512];
    mga_vbe_mode m;
    int pitch_px = 0, pitch, secs = 120, i, x, y, page = 0, flips = 0, phase = 0;
    uint32_t pb, t0, tflip;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "secs=", 5))
            secs = atoi(argv[i] + 5);
    hx_init(1, argv, "nshow");
    if (tnt2_find() != 0 || tnt2_map() != 0) {
        hx_test("map", 0, "no NV4-family chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s", tnt2.name);
    if (vbe_find_mode(W, H, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    pitch = pitch_px * 2;
    hx_test("mode", 1, "mode %03x", m.mode);
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "up");
    pb = ((uint32_t)pitch * H + 4095u) & ~4095u;
    tnt2_set_target(0, pitch, 16);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0xF800);              /* page A: red, a white square top left */
    tnt2_fill(50, 50, 100, 100, 0xFFFF);
    tnt2_set_target(pb, pitch, 16);
    tnt2_fill(0, 0, W, H, 0x001F);              /* page B: blue, a white square bottom right */
    tnt2_fill(490, 330, 100, 100, 0xFFFF);
    tnt2_sync(1000000);
    /* The arrow: a right triangle 24 rows high, its border colour 0, inside colour 1; the rest transparent. */
    memset(and_m, 0xFF, sizeof and_m);
    memset(xor_m, 0, sizeof xor_m);
    for (y = 0; y < 24; y++)
        for (x = 0; x <= y / 2; x++) {
            int k = y * 8 + x / 8, b = 7 - (x & 7), border = x == 0 || x == y / 2 || y == 23;
            and_m[k] &= (uint8_t)~(1u << b);
            if (!border)
                xor_m[k] |= (uint8_t)(1u << b);
        }
    tnt2_cursor_load(and_m, xor_m);
    tnt2_cursor_colours(0x000000, 0xFFFFFF);
    tnt2_cursor_move(W / 2, H / 2);
    tnt2_cursor_show(1);
    tnt2_set_start(0);
    hx_log("NSHOW-PHASE 1: pages A (red) and B (blue) every 2 s, a white arrow with a black border circling");
    t0 = tflip = sys_time_us();
    while ((uint32_t)(sys_time_us() - t0) < (uint32_t)secs * 1000000u) {
        double a = (double)(uint32_t)(sys_time_us() - t0) / 1e6 * 1.2;
        tnt2_vsync_wait(50000);
        tnt2_cursor_move((int)(W / 2 + 200 * cos(a)), (int)(H / 2 + 150 * sin(a)));
        if ((uint32_t)(sys_time_us() - tflip) >= 2000000u) {
            page ^= 1;
            tnt2_set_start(page ? pb : 0);
            tflip = sys_time_us();
            flips++;
        }
        if (!phase && (uint32_t)(sys_time_us() - t0) >= (uint32_t)secs * 500000u) {
            tnt2_cursor_colours(0x00FF00, 0xFFFF00);
            hx_log("NSHOW-PHASE 2: the arrow yellow with a green border");
            phase = 1;
        }
    }
    tnt2_cursor_show(0);
    tnt2_set_start(0);
    vbe_set_text_mode();
    hx_test("show", 1, "%d flips in %d s", flips, secs);
    hx_done(0);
}
