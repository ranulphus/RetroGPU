/* rsttest.c - RSTTEST: one engine_reset (the drawing engine's soft reset, RST bit 0, as warp.c does before each
 * WARP microcode load), nothing else (gl/docs/g4path.md: does it break the G450's memory? run MODETEST after). */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

int main(int argc, char **argv)
{
    (void)argc;
    hx_init(1, argv, "rsttest");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        hx_test("init", 0, "no Matrox card");
        hx_done(HX_INIT_FAILED);
    }
    engine_init(1024, 16);
    hx_log("HX-STAT rsttest before status=%08lx", (unsigned long)MGA_RD32(MGAREG_STATUS));
    engine_reset();
    hx_log("HX-STAT rsttest after status=%08lx resets=%lu", (unsigned long)MGA_RD32(MGAREG_STATUS),
           (unsigned long)engine_resets);
    hx_test("rsttest", 1, "one soft reset done");
    hx_done(0);
}
