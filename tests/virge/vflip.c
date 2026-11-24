/* vflip.c - when the ViRGE/DX takes a new display start (docs/virge.md, V5's W1), for someone watching the screen.
 * Two 640x480 15-bit buffers are flipped every frame; after each flip the buffer just hidden is cleared to black,
 * left so for 8 ms, then filled with the phase's colour. If the chip shows a start from the next frame on, the
 * screen shows only finished frames: one steady colour. If it takes the start a frame late, the cleared buffer is
 * still on screen while it is black: black flashes.
 *
 *   VFLIP [secs=N]           each phase N seconds (8)
 *
 * Phase 1, blue: the device's order today (the retrace wait, then the start written).
 * Phase 2, green: the start written, then the retrace wait (the order the TNT2 needed, docs/tnt2.md).
 * Lines: VFLIP-PHASE (frames, retrace waits that timed out, the average frame in microseconds). */
#include "hx.h"
#include "virge/virge.h"
#include "mga/sys.h"
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

static void phase(const char *name, int start_first, uint16_t colour, uint32_t off[2], int pitch, int secs)
{
    uint32_t t0 = sys_time_us(), t;
    unsigned frames = 0, fails = 0;
    int shown = 0;
    for (;;) {
        int hidden;
        t = sys_time_us();
        if (t - t0 >= (uint32_t)secs * 1000000u)
            break;
        virge_sync(500000);
        if (start_first) {
            virge_set_start(off[!shown]);
            if (virge_vsync_wait(50000) != 0)
                fails++;
        } else {
            if (virge_vsync_wait(50000) != 0)
                fails++;
            virge_set_start(off[!shown]);
        }
        shown = !shown;
        hidden = !shown;
        /* The buffer just hidden: black, held 8 ms, then the phase's colour. */
        virge_set_target(off[hidden], pitch, 16);
        virge_set_clip(0, 0, W, H);
        virge_fill(0, 0, W, H, 0);
        virge_sync(500000);
        t = sys_time_us();
        while (sys_time_us() - t < 8000u)
            ;
        virge_fill(0, 0, W, H, colour);
        frames++;
    }
    hx_log("VFLIP-PHASE %s order=%s frames=%u retrace_timeouts=%u frame_us=%lu", name,
           start_first ? "start-then-retrace" : "retrace-then-start", frames, fails,
           frames ? (unsigned long)((sys_time_us() - t0) / frames) : 0ul);
}

int main(int argc, char **argv)
{
    virge_mode m;
    uint32_t off[2];
    int i, secs = 8;
    hx_init(1, argv, "vflip");                  /* (its own arguments only) */
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "secs=", 5))
            secs = atoi(argv[i] + 5);
    if (virge_find() < 0 || virge_map() < 0) {
        hx_test("map", 0, "no ViRGE/DX mapped");
        hx_done(1);
    }
    if (virge_find_mode(W, H, 15, &m) < 0 || virge_set_mode(&m) < 0) {
        hx_test("mode", 0, "no 640x480x15 mode");
        virge_unmap();
        hx_done(1);
    }
    off[0] = 0;
    off[1] = ((uint32_t)m.pitch_bytes * H + 4095u) & ~4095u;
    for (i = 0; i < 2; i++) {               /* both buffers start finished: the first phase's colour */
        virge_set_target(off[i], m.pitch_bytes, 16);
        virge_set_clip(0, 0, W, H);
        virge_fill(0, 0, W, H, 0x001F);
    }
    virge_sync(500000);
    virge_set_start(off[0]);
    phase("blue", 0, 0x001F, off, m.pitch_bytes, secs);       /* RGB555 blue */
    phase("green", 1, 0x03E0, off, m.pitch_bytes, secs);      /* RGB555 green */
    virge_text_mode();
    virge_unmap();
    hx_test("ran", 1, "both phases shown");
    hx_done(0);
}
