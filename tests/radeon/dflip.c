/* dflip.c - when the Radeon takes a new display start (docs/radeon.md, RD1), for someone watching the screen: RFLIP
 * (tests/r128/rflip.c), from which it is made. Two 640x480 16-bit buffers are flipped every frame; after each flip
 * the buffer just hidden is cleared to black, left so for 8 ms, then filled with the phase's colour. If the chip
 * shows a start from the next frame on, the screen shows only finished frames: one steady colour. If it takes the
 * start a frame late, the cleared buffer is still on screen while it is black: black flashes.
 *
 *   DFLIP [secs=N]           each phase N seconds (8)
 *
 * Phase 1, blue: the retrace wait, then the start written.
 * Phase 2, green: the start written, then the retrace wait (the order the TNT2 and the Rage 128 needed).
 * The buffers at 0 and at 1 MB: megabytes 0 and 1, which the P3's card's memory fault leaves alone.
 * Lines: DFLIP-PHASE (frames, retrace waits that timed out, the average frame in microseconds). */
#include "hx.h"
#include "radeon/radeon.h"
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
        radeon_sync(500000);
        if (start_first) {
            radeon_set_start(off[!shown]);
            if (radeon_vsync_wait(50000) != 0)
                fails++;
        } else {
            if (radeon_vsync_wait(50000) != 0)
                fails++;
            radeon_set_start(off[!shown]);
        }
        shown = !shown;
        hidden = !shown;
        /* The buffer just hidden: black, held 8 ms, then the phase's colour. */
        radeon_set_target(off[hidden], pitch, 16);
        radeon_set_clip(0, 0, W, H);
        radeon_fill(0, 0, W, H, 0);
        radeon_sync(500000);
        t = sys_time_us();
        while (sys_time_us() - t < 8000u)
            ;
        radeon_fill(0, 0, W, H, colour);
        frames++;
    }
    hx_log("DFLIP-PHASE %s order=%s frames=%u retrace_timeouts=%u frame_us=%lu", name,
           start_first ? "start-then-retrace" : "retrace-then-start", frames, fails,
           frames ? (unsigned long)((sys_time_us() - t0) / frames) : 0ul);
}

int main(int argc, char **argv)
{
    radeon_mode m;
    uint32_t off[2];
    int i, secs = 8;
    hx_init(1, argv, "dflip");                  /* (its own arguments only) */
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "secs=", 5))
            secs = atoi(argv[i] + 5);
    if (radeon_find() < 0 || radeon_map() < 0) {
        hx_test("map", 0, "no Rage 128 mapped");
        hx_done(1);
    }
    if (radeon_find_mode(W, H, 16, &m) < 0 || radeon_set_mode(&m) < 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        radeon_unmap();
        hx_done(1);
    }
    off[0] = 0;
    off[1] = 1u << 20;
    for (i = 0; i < 2; i++) {               /* both buffers start finished: the first phase's colour */
        radeon_set_target(off[i], m.pitch_bytes, 16);
        radeon_set_clip(0, 0, W, H);
        radeon_fill(0, 0, W, H, 0x001F);
    }
    radeon_sync(500000);
    radeon_set_start(off[0]);
    phase("blue", 0, 0x001F, off, m.pitch_bytes, secs);       /* RGB565 blue */
    phase("green", 1, 0x07E0, off, m.pitch_bytes, secs);      /* RGB565 green */
    radeon_text_mode();
    radeon_unmap();
    hx_test("ran", 1, "the phases shown");
    hx_done(0);
}
