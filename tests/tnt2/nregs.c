/* nregs.c - NREGS: the NV1x's set-up registers after the TNT2 layer has set the chip up for Celsius (the Quadro2
 * plan's Q4 speed: compared word for word with a dump taken under nouveau on bench E's Ubuntu, ~/bench-archive's
 * nvregs.c, the same ranges). Reads only, after: 640x480x16, the ring, the 3D objects, Celsius's set-up, with
 * the engine idle. The pipe's index/data window (0x400F50/54) is skipped.
 *
 *   NREGS
 *
 * "NREGS addr value" lines on the serial port. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>

static const uint32_t ranges[][2] = {
    { 0x000000, 0x000260 }, { 0x001080, 0x001100 }, { 0x002000, 0x002300 }, { 0x003200, 0x003260 },
    { 0x100000, 0x100400 }, { 0x400000, 0x400200 }, { 0x400600, 0x400900 }, { 0x400A00, 0x400C00 },
    { 0x400E00, 0x400F00 }, { 0x680500, 0x680510 },
};

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch_px = 0;
    unsigned i;
    uint32_t a;
    hx_init(1, argv, "nregs");
    (void)argc;
    if (tnt2_find() != 0 || tnt2_map() != 0 || tnt2.arch != 10) {
        hx_test("map", 0, "no NV1x chip");
        hx_done(1);
    }
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0 || tnt2_accel_init() != 0 ||
        tnt2_3d_init() != 0 || tnt2_celsius_init() != 0) {
        vbe_set_text_mode();
        hx_test("setup", 0, "the mode, the ring or Celsius's set-up failed");
        hx_done(1);
    }
    tnt2_sync(1000000);
    vbe_set_text_mode();
    for (i = 0; i < sizeof ranges / sizeof ranges[0]; i++)
        for (a = ranges[i][0]; a < ranges[i][1]; a += 4)
            hx_log("NREGS %06lx %08lx", (unsigned long)a, (unsigned long)tnt2_regs[a / 4]);
    hx_test("dump", 1, "done");
    hx_done(0);
    return 0;
}
