/* nsplit.c - NSPLIT: when an NV4-family chip (TNT to Quadro2) takes a new display start (PCRTC 0x600800), for
 * someone watching; MSPLIT's test (tests/hal/msplit.c). Buffer A red, buffer B blue; every frame the start is set to
 * A at the frame's top (the raster line, 0x600808, wrapped) and to B at line 240, for --secs seconds (default 10).
 *  - a steady red top half and blue bottom half: the start is taken at once;
 *  - all blue: it is taken in the blank (two buffers without the retrace draw into the frame on screen).
 * Writes only the display start and the two buffers' pixels (640x480x16, the BIOS's mode). Lines: NSPLIT frames=N.
 *
 *   NSPLIT [--secs N] */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch_px = 0, secs = 10, i, x, y;
    uint32_t off_b, t0, t1, frames = 0, stuck = 0;
    for (i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--secs")) {
            secs = atoi(argv[i + 1]);
            memmove(&argv[i], &argv[i + 2], (size_t)(argc - i - 1) * sizeof *argv);
            argc -= 2, i--;
        }
    hx_init(argc, argv, "nsplit");
    if (tnt2_find() != 0 || tnt2_map() != 0) {
        hx_test("map", 0, "no NV4-family chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s", tnt2.name);
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x pitch %d", m.mode, pitch_px);
    off_b = ((uint32_t)pitch_px * 2u * 480u + 4095u) & ~4095u;
    for (y = 0; y < 480; y++)
        for (x = 0; x < pitch_px; x++) {
            ((volatile uint16_t *)tnt2_fb)[y * pitch_px + x] = 0xF800;            /* A red */
            ((volatile uint16_t *)(tnt2_fb + off_b))[y * pitch_px + x] = 0x001F;  /* B blue */
        }
    (void)tnt2_set_start(0);
    t0 = sys_time_us();
    while (sys_time_us() - t0 < (uint32_t)secs * 1000000u) {
        t1 = sys_time_us();
        while (tnt2_raster_line() < 240 && sys_time_us() - t1 < 100000u)
            ;
        (void)tnt2_set_start(off_b);
        t1 = sys_time_us();
        while (tnt2_raster_line() >= 240 && sys_time_us() - t1 < 100000u)
            ;
        if (sys_time_us() - t1 >= 100000u)
            stuck++;
        (void)tnt2_set_start(0);
        frames++;
    }
    vbe_set_text_mode();
    hx_log("NSPLIT frames=%lu in %d s (line stuck %lu times)", (unsigned long)frames, secs, (unsigned long)stuck);
    hx_test("ran", frames > 0 && !stuck, "%lu frames", (unsigned long)frames);
    hx_done(0);
    return 0;
}
