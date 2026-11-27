/* nraster.c - NRASTER: the NV4/NV5/NV1x's raster position (PCRTC 0x600808, nouveau's NV_PCRTC_RASTER; bits 10:0 the
 * line) read for 100 ms in the BIOS's 640x480 mode, beside the VGA status port's retrace bit: the lowest and highest
 * line seen, the wraps and the time between them (a frame), and the lines read while the status port said "retrace"
 * (where the blank lies). Reads only (the register and port 0x3DA); triple buffering's line (rg/tribuf.h).
 *
 *   NRASTER
 *
 * HX-TEST lines: map, mode, moves (the line moves and wraps at a frame's rate). */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch_px = 0, wraps = 0;
    uint32_t t0, prev, now, lo = ~0u, hi = 0, rlo = ~0u, rhi = 0, w0 = 0, w1 = 0, raw = 0, n = 0;
    hx_init(1, argv, "nraster");
    (void)argc;
    if (tnt2_find() != 0 || tnt2_map() != 0) {
        hx_test("map", 0, "no NV4-family chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s", tnt2.name);
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x", m.mode);
    prev = tnt2_regs[0x600808 / 4] & 0x7FFu;
    t0 = sys_time_us();
    while (sys_time_us() - t0 < 100000u) {
        uint32_t r = tnt2_regs[0x600808 / 4];
        now = r & 0x7FFu;
        raw |= r & ~0x7FFu;                     /* (the other bits ever set) */
        n++;
        if (now < lo) lo = now;
        if (now > hi) hi = now;
        if (sys_inb(0x3DA) & 0x08) {
            if (now < rlo) rlo = now;
            if (now > rhi) rhi = now;
        }
        if (now + 8u < prev) {
            if (wraps == 0) w0 = sys_time_us();
            else if (wraps == 1) w1 = sys_time_us();
            wraps++;
        }
        prev = now;
    }
    vbe_set_text_mode();
    hx_log("NRASTER %lu reads: lines %lu..%lu, %d wraps, a frame %lu us; in retrace (0x3DA bit 3) lines %lu..%lu; "
           "other bits seen %08lx", (unsigned long)n, (unsigned long)lo, (unsigned long)hi, wraps,
           (unsigned long)(w1 - w0), (unsigned long)rlo, (unsigned long)rhi, (unsigned long)raw);
    hx_test("moves", wraps >= 2 && hi >= 480 && lo < 16, "lines %lu..%lu, %d wraps in 100 ms", (unsigned long)lo,
            (unsigned long)hi, wraps);
    hx_done(0);
    return 0;
}
