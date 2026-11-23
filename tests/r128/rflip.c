/* rflip.c - when the Rage 128 takes a new display start (docs/rage128.md, R1), for someone watching the screen: VFLIP
 * (tests/virge/vflip.c), from which it is made. Two 640x480 16-bit buffers are flipped every frame; after each flip
 * the buffer just hidden is cleared to black, left so for 8 ms, then filled with the phase's colour. If the chip shows a start from the next frame on, the
 * screen shows only finished frames: one steady colour. If it takes the start a frame late, the cleared buffer is
 * still on screen while it is black: black flashes.
 *
 *   RFLIP [secs=N]           each phase N seconds (8)
 *
 * Phase 1, blue: the device's order today (the retrace wait, then the start written).
 * Phase 2, green: the start written, then the retrace wait (the order the TNT2 needed, docs/tnt2.md).
 * Phase 3, red (RFLIP ring: this phase alone): the CCE from a ring, each flip in the command stream
 * (r128_flip_queue: the start, WAIT_UNTIL's EVENT_CRTC_OFFSET, a sequence number into GUI_SCRATCH_REG0) and the
 * hidden buffer's black clear queued straight after it, the CPU not waiting for the retrace. The engine has to stall
 * until the flip is shown, or the black clear lands on the buffer still on screen: black flashes. The CPU then
 * watches the scratch register: the number should come a while after the flip was queued (not at once: the wait
 * not taken) and in or just after the retrace.
 * Lines: RFLIP-PHASE (frames, retrace waits that timed out, the average frame in microseconds); RFLIP-RING (flips
 * never seen, flips seen within 300 us of queuing, flips seen in the retrace, the delay's minimum, average and
 * maximum in microseconds). */
#include "hx.h"
#include "r128/r128.h"
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
        r128_sync(500000);
        if (start_first) {
            r128_set_start(off[!shown]);
            if (r128_vsync_wait(50000) != 0)
                fails++;
        } else {
            if (r128_vsync_wait(50000) != 0)
                fails++;
            r128_set_start(off[!shown]);
        }
        shown = !shown;
        hidden = !shown;
        /* The buffer just hidden: black, held 8 ms, then the phase's colour. */
        r128_set_target(off[hidden], pitch, 16);
        r128_set_clip(0, 0, W, H);
        r128_fill(0, 0, W, H, 0);
        r128_sync(500000);
        t = sys_time_us();
        while (sys_time_us() - t < 8000u)
            ;
        r128_fill(0, 0, W, H, colour);
        frames++;
    }
    hx_log("RFLIP-PHASE %s order=%s frames=%u retrace_timeouts=%u frame_us=%lu", name,
           start_first ? "start-then-retrace" : "retrace-then-start", frames, fails,
           frames ? (unsigned long)((sys_time_us() - t0) / frames) : 0ul);
}

static void phase_ring(uint16_t colour, uint32_t off[2], int pitch, int secs)
{
    uint32_t t0 = sys_time_us(), t, tq, d, dmin = ~0u, dmax = 0, dsum = 0, seq = r128_flip_seen() + 1u;
    unsigned frames = 0, unseen = 0, early = 0, in_retrace = 0;
    int shown = 0;
    for (;;) {
        int hidden, seen = 1;
        t = sys_time_us();
        if (t - t0 >= (uint32_t)secs * 1000000u)
            break;
        tq = sys_time_us();
        r128_flip_queue(off[!shown], seq, 1);
        shown = !shown;
        hidden = !shown;
        r128_set_target(off[hidden], pitch, 16);
        r128_set_clip(0, 0, W, H);
        r128_fill(0, 0, W, H, 0);
        r128_cce_flush();
        while (r128_flip_seen() != seq)
            if (sys_time_us() - tq > 100000u) {
                seen = 0;
                break;
            }
        d = sys_time_us() - tq;
        if (!seen)
            unseen++;
        else {
            if (sys_inb(0x3DA) & 0x08)
                in_retrace++;
            if (d < 300u)
                early++;
            dsum += d;
            if (d < dmin) dmin = d;
            if (d > dmax) dmax = d;
        }
        t = sys_time_us();
        while (sys_time_us() - t < 8000u)
            ;
        r128_fill(0, 0, W, H, colour);
        r128_cce_flush();
        frames++, seq++;
    }
    r128_sync(500000);
    hx_log("RFLIP-PHASE red order=in-the-stream frames=%u frame_us=%lu", frames,
           frames ? (unsigned long)((sys_time_us() - t0) / frames) : 0ul);
    hx_log("RFLIP-RING flips=%u unseen=%u early=%u in_retrace=%u delay_us=%lu/%lu/%lu", frames, unseen, early,
           in_retrace, (unsigned long)(dmin == ~0u ? 0 : dmin),
           (unsigned long)(frames > unseen ? dsum / (frames - unseen) : 0), (unsigned long)dmax);
    hx_test("ring-flip", frames > 0 && !unseen && early == 0, "flips %u, unseen %u, early %u", frames, unseen, early);
}

int main(int argc, char **argv)
{
    r128_mode m;
    uint32_t off[2];
    int i, secs = 8, ring_only = 0;
    hx_init(1, argv, "rflip");                  /* (its own arguments only) */
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "secs=", 5))
            secs = atoi(argv[i] + 5);
        else if (!strcmp(argv[i], "ring"))
            ring_only = 1;
    if (r128_find() < 0 || r128_map() < 0) {
        hx_test("map", 0, "no Rage 128 mapped");
        hx_done(1);
    }
    if (r128_find_mode(W, H, 16, &m) < 0 || r128_set_mode(&m) < 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        r128_unmap();
        hx_done(1);
    }
    off[0] = 0;
    off[1] = ((uint32_t)m.pitch_bytes * H + 4095u) & ~4095u;
    for (i = 0; i < 2; i++) {               /* both buffers start finished: the first phase's colour */
        r128_set_target(off[i], m.pitch_bytes, 16);
        r128_set_clip(0, 0, W, H);
        r128_fill(0, 0, W, H, 0x001F);
    }
    r128_sync(500000);
    r128_set_start(off[0]);
    if (!ring_only) {
        phase("blue", 0, 0x001F, off, m.pitch_bytes, secs);       /* RGB565 blue */
        phase("green", 1, 0x07E0, off, m.pitch_bytes, secs);      /* RGB565 green */
    }
    if (r128_cce_init_ring(NULL, 0x10000u) != 0)
        hx_test("ring", 0, "the CCE's ring didn't start");
    else {
        for (i = 0; i < 2; i++) {           /* both buffers the third phase's colour */
            r128_set_target(off[i], m.pitch_bytes, 16);
            r128_set_clip(0, 0, W, H);
            r128_fill(0, 0, W, H, 0xF800);
        }
        r128_sync(500000);
        phase_ring(0xF800, off, m.pitch_bytes, secs);          /* RGB565 red */
    }
    r128_text_mode();
    r128_unmap();
    hx_test("ran", 1, "the phases shown");
    hx_done(0);
}
