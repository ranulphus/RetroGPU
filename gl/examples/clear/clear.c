/* clear - M2 rung 1: dglInit, engine clears of colour and depth, page flips
 * with vsync for --frames N (default 60), dglShutdown. Reports the swap
 * rate; the last frame is a known colour checked in the snapshot. */
#include "hx.h"
#include "../../src/dgl/dgl.h"
#include <time.h>

int main(int argc, char **argv)
{
    static const uint16_t colours[4] = { 0xF800, 0x07E0, 0x001F, 0xFFE0 };
    int f;
    uclock_t t0, t1;
    hx_init(argc, argv, "clear");
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    t0 = uclock();
    for (f = 0; f < hx_args.frames; f++) {
        engine_fill(0, 0, dgl_ctx.width, dgl_ctx.height, colours[f & 3]);
        engine_fill_depth(0, 0, dgl_ctx.width, dgl_ctx.height, 0xFFFF);
        dglSwapBuffers();
    }
    t1 = uclock();
    hx_stat("swaps=%lu seconds=%.2f rate=%.1f/s resets=%lu", dglGetStats()->swaps,
            (double)(t1 - t0) / UCLOCKS_PER_SEC, f * (double)UCLOCKS_PER_SEC / (double)(t1 - t0 + 1),
            (unsigned long)engine_resets);
    hx_test("engine", engine_resets == 0 && engine_timeouts == 0, "resets=%lu timeouts=%lu",
            (unsigned long)engine_resets, (unsigned long)engine_timeouts);
    hx_snap_screen("clear");
    dglShutdown();
    hx_done(0);
    return 0;
}
