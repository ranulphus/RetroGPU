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

static unsigned long phase(const char *name, int start_first, uint16_t colour, uint32_t off[2], int pitch, int secs)
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
    return frames ? (unsigned long)((sys_time_us() - t0) / frames) : 0ul;
}

/* The ring's phases (DFLIP ring): each flip queued in the stream, then the buffer just hidden cleared to black and
 * filled with the phase's colour behind it: with the wait, the stream holds those until the display has taken the
 * start, so a frame lasts one refresh and the screen shows only finished frames; without it they are drawn while
 * the old buffer may still be shown (black flashes, and frames as fast as the fills). */
static unsigned long stream_phase(const char *name, int wait, uint16_t colour, uint32_t off[2], int pitch, int secs)
{
    uint32_t t0, seq = 0, seen;
    unsigned frames = 0, late = 0;
    int shown = 0;
    unsigned long us;
    /* (the scratch register keeps its value across runs: 0 first, in the stream) */
    radeon_cp_reg(0x15E0, 0);
    radeon_sync(2000000);
    t0 = sys_time_us();
    while (sys_time_us() - t0 < (uint32_t)secs * 1000000u) {
        uint32_t t;
        int hidden;
        radeon_flip_queue(off[!shown], ++seq, wait);
        shown = !shown, hidden = !shown;
        radeon_set_target(off[hidden], pitch, 16);
        radeon_set_clip(0, 0, W, H);
        radeon_fill(0, 0, W, H, 0);
        radeon_fill(0, 0, W, H, colour);
        radeon_cp_flush();
        /* (two flips at most ahead of the display) */
        t = sys_time_us();
        while ((int32_t)(seq - radeon_flip_seen()) > 1)
            if (sys_time_us() - t > 500000u) {
                late++;
                break;
            }
        frames++;
    }
    us = frames ? (unsigned long)((sys_time_us() - t0) / frames) : 0ul;
    radeon_sync(2000000);
    seen = radeon_flip_seen();
    hx_log("DFLIP-PHASE %s order=%s frames=%u flips_not_seen=%u last_seq=%lu seen=%lu frame_us=%lu", name,
           wait ? "stream, the display waited for" : "stream, no wait", frames, late, (unsigned long)seq,
           (unsigned long)seen, us);
    return seen == seq ? us : 0;
}

int main(int argc, char **argv)
{
    radeon_mode m;
    uint32_t off[2];
    int i, secs = 8, use_ring = 0;
    unsigned long us_wait = 0, us_free = 0, us_cpu;
    hx_init(1, argv, "dflip");                  /* (its own arguments only) */
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "secs=", 5))
            secs = atoi(argv[i] + 5);
        else if (!strcmp(argv[i], "ring"))
            use_ring = 1;
    if (radeon_find() < 0 || radeon_map() < 0) {
        hx_test("map", 0, "no Radeon mapped");
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
    us_cpu = phase("green", 1, 0x07E0, off, m.pitch_bytes, secs);      /* RGB565 green */
    if (use_ring) {
        int rc = radeon_cp_init(NULL, 65536u);
        hx_test("cp", rc == 0, rc == 0 ? "running from a 64 KB ring" : "did not start");
        if (rc == 0) {
            /* (what a new start does with FLIP_CNTL: how long CRTC_OFFSET's pending bit 30 stays set after a
             * write by register, and after one through the ring) */
            hx_log("DFLIP-PROBE CRTC_OFFSET_CNTL=%08lx CRTC_OFFSET=%08lx", (unsigned long)radeon_rd(0x0228),
                   (unsigned long)radeon_rd(0x0224));
            {
                int fm;
                for (fm = 0; fm < 2; fm++) {
                    radeon_flip_at_line(fm);
                    hx_log("DFLIP-PROBE FLIP_CNTL %s: CRTC_OFFSET_CNTL=%08lx", fm ? "set" : "clear", (unsigned long)radeon_rd(0x0228));
                    for (i = 0; i < 8; i++) {
                        static const int at_ms[4] = { 0, 3, 8, 13 };
                        uint32_t t, first, n = 0, dt;
                        radeon_vsync_wait(50000);
                        t = sys_time_us();
                        while (sys_time_us() - t < (uint32_t)(1000 * at_ms[i % 4]))
                            ;
                        if (i < 4)
                            radeon_wr(0x0224, off[i & 1]);
                        else {
                            radeon_cp_reg(0x0224, off[i & 1]);
                            radeon_cp_flush();
                        }
                        t = sys_time_us();
                        first = radeon_rd(0x0224);
                        while ((radeon_rd(0x0224) & 0x40000000u) && sys_time_us() - t < 100000u)
                            n++;
                        dt = sys_time_us() - t;
                        hx_log("DFLIP-PROBE   %s write %2d ms after a retrace: first read %08lx, pending %lu us", i < 4 ? "register" : "ring    ",
                               at_ms[i % 4], (unsigned long)first, (unsigned long)dt);
                    }
                }
                radeon_flip_at_line(0);
            }
            us_wait = stream_phase("red", 1, 0xF800, off, m.pitch_bytes, secs);
            us_free = stream_phase("white", 0, 0xFFFF, off, m.pitch_bytes, secs);
            /* (with the wait a frame is one refresh: the retrace-waited phase's frame was one too, plus its 8 ms
             * hold and syncs, so between half of it and all of it) */
            hx_test("stream-flip", us_wait && us_wait * 2 >= us_cpu / 2 && us_wait <= us_cpu + 2000,
                    "a frame with the stream waiting for the display: %lu us; without the wait %lu us; by the CPU's "
                    "retrace wait %lu us", us_wait, us_free, us_cpu);
        }
    }
    radeon_text_mode();
    radeon_unmap();
    hx_test("ran", 1, "the phases shown");
    hx_done(0);
}
