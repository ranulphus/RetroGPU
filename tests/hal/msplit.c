/* msplit.c - MSPLIT: when a Matrox chip takes a new display start, for someone watching. Buffer A red, buffer B blue;
 * every frame the start is set to A at the frame's top (VCOUNT wrapped) and to B at line 240, for --secs seconds
 * (default 10), by the BIOS's 4F07 (BL 0, no wait) as DOS-GL's device sets it.
 *  - a steady red top half and blue bottom half: the start is taken at once (or at the next line): a swap without
 *    the retrace tears, never draws into the frame on screen;
 *  - all blue: the start is taken in the blank (the last one written before it): a swap without the retrace shows
 *    its frame only from the next blank, and two buffers draw into the one on screen meanwhile.
 * Writes only the display start and the two buffers' pixels (640x480x16). Lines: MSPLIT frames=N.
 *
 *   MSPLIT [--secs N] */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
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
    hx_init(argc, argv, "msplit");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        hx_test("map", 0, "no Matrox chip");
        hx_done(1);
    }
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x pitch %d", m.mode, pitch_px);
    off_b = (uint32_t)pitch_px * 2u * 480u;
    for (y = 0; y < 480; y++)
        for (x = 0; x < pitch_px; x++) {
            ((volatile uint16_t *)mga_fb)[y * pitch_px + x] = 0xF800;             /* A red */
            ((volatile uint16_t *)(mga_fb + off_b))[y * pitch_px + x] = 0x001F;   /* B blue */
        }
    vbe_set_display_start(0, pitch_px * 2, 16);
    t0 = sys_time_us();
    while (sys_time_us() - t0 < (uint32_t)secs * 1000000u) {
        t1 = sys_time_us();
        while (engine_vcount() < 240 && sys_time_us() - t1 < 100000u)
            ;
        vbe_set_display_start(off_b, pitch_px * 2, 16);
        t1 = sys_time_us();
        while (engine_vcount() >= 240 && sys_time_us() - t1 < 100000u)
            ;
        if (sys_time_us() - t1 >= 100000u)
            stuck++;
        vbe_set_display_start(0, pitch_px * 2, 16);
        frames++;
    }
    vbe_set_text_mode();
    hx_log("MSPLIT frames=%lu in %d s (VCOUNT stuck %lu times)", (unsigned long)frames, secs, (unsigned long)stuck);
    hx_test("ran", frames > 0 && !stuck, "%lu frames", (unsigned long)frames);
    hx_done(0);
    return 0;
}
