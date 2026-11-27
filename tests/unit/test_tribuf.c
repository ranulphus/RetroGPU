/* test_tribuf.c - triple buffering's bookkeeping (hal/src/rg/tribuf.c) against a simulated chip: the raster line
 * from a simulated clock (525 lines, 480 visible, 16.675 ms a frame), a display start the chip takes when the line
 * reaches its latch line, and a program drawing frames of random lengths. The rule checked: no visible line is ever
 * scanned out of the buffer being drawn. Varied: the latch line (in the blank, or at the wrap), frame lengths
 * (short, steady near one frame, long), swaps with and without the retrace and the devices' two orders for the
 * retrace, the program held up around its start write (GLOS's other work), and DJGPP's clock holding still. */
#include "unit.h"
#include "rg/tribuf.h"
#include "mga/sys.h"
#include <stdint.h>

#define LINES    525
#define HEIGHT   480
#define FRAME_NS 16675000ull
#define RETRACE  490                    /* (where the devices' retrace waits end) */

uint32_t sys_time_backs, sys_time_held;

static uint64_t now_ns;                 /* the simulated time */
static uint64_t abs_line;               /* lines begun since time 0 */
static int latch;                       /* the line at whose start the chip takes the start written */
static uint32_t start_reg, on_screen;
static uint32_t drawing;                /* the buffer being drawn, or 0 (offsets 1-3) */
static unsigned painted;                /* visible lines scanned out of the buffer being drawn */
static uint64_t hold_until_ns, hold_value_us;  /* DJGPP's clock held still until then */
static uint64_t next_hold_ns;           /* ... and the next hold (every half a second or so: real ones are 4 a minute) */
static int holds, preempts;
static uint32_t rng = 12345;

static uint32_t rnd(uint32_t n)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % (n ? n : 1);
}

static void advance(uint64_t ns)
{
    uint64_t to;
    now_ns += ns;
    to = now_ns * LINES / FRAME_NS;
    while (abs_line < to) {
        int l = (int)(++abs_line % LINES);
        if (l == latch)
            on_screen = start_reg;
        if (l < HEIGHT && drawing && drawing == on_screen)
            painted++;
    }
}

uint32_t sys_time_us(void)
{
    advance(200);                       /* (a call's cost) */
    if (holds && !hold_until_ns && now_ns >= next_hold_ns) {
        hold_until_ns = now_ns + 54925000ull;   /* (uclock 54.9 ms behind: the port holds its last reading) */
        next_hold_ns = hold_until_ns + rnd(1000) * 1000000ull;
        hold_value_us = now_ns / 1000;
        sys_time_backs++;
    }
    if (hold_until_ns) {
        if (now_ns < hold_until_ns)
            return sys_time_held = 1, (uint32_t)hold_value_us;
        hold_until_ns = 0;
    }
    sys_time_held = 0;
    return (uint32_t)(now_ns / 1000);
}

static uint32_t line(void)
{
    advance(300);
    return (uint32_t)(abs_line % LINES);
}

static void set(uint32_t off)
{
    if (preempts && rnd(40) == 0)
        advance(rnd(3000000));          /* (held up between reading the line and writing the start) */
    start_reg = off;
    if (preempts && rnd(40) == 0)
        advance(rnd(3000000));          /* (... or just after) */
}

static void until_line(int l)
{
    uint64_t t0 = now_ns;
    while ((int)(abs_line % LINES) != l && now_ns - t0 < 100000000ull)
        advance(1000);
}

/* A frame's drawing time: kind 0 any up to 2.5 frames; 1 steady near one frame (the old rule's hole); 2 short. */
static uint64_t frame_ns(int kind)
{
    switch (kind) {
    case 1: return FRAME_NS + rnd(FRAME_NS / 5) - FRAME_NS / 20;
    case 2: return 200000 + rnd(3000000);
    default: return 100000 + rnd((uint32_t)(FRAME_NS * 5 / 2));
    }
}

/* vsync: 0 never, 1 always, 2 mixed; order (with the retrace): 0 start then wait (the TNT2's), 1 wait then start
 * (the Matrox's). Returns the visible lines painted. */
static unsigned run(int latch_line, int kind, int vsync, int order, int frames)
{
    rg_tribuf t;
    int f;
    now_ns = 0, abs_line = 0, latch = latch_line, painted = 0, drawing = 0;
    start_reg = on_screen = 1;
    hold_until_ns = 0, next_hold_ns = rnd(1000) * 1000000ull;
    CHECK_EQ(rg_tri_init(&t, 1, 2, 3, HEIGHT, line, set), 0);
    CHECK(t.lines >= LINES - 1 && t.lines <= LINES);
    for (f = 0; f < frames; f++) {
        int vs = vsync == 2 ? rnd(4) == 0 : vsync;
        drawing = rg_tri_draw(&t);
        CHECK(drawing != start_reg);    /* (nor the one waiting to be taken) */
        advance(frame_ns(kind));
        drawing = 0;                    /* (the engine drained before the swap) */
        if (!vs)
            (void)rg_tri_flip(&t);
        else if (order == 0) {
            set(rg_tri_flip_wait(&t));
            until_line(RETRACE);
        } else {
            until_line(RETRACE);
            set(rg_tri_flip_wait(&t));
        }
    }
    return painted;
}

int unit_main(void)
{
    static const int latches[] = { 480, 490, 500, 524, 0 };   /* (in the blank, or at the wrap) */
    unsigned i, kind, vs, order;
    for (preempts = 0; preempts < 2; preempts++)
        for (holds = 0; holds < 2; holds++)
            for (i = 0; i < sizeof latches / sizeof latches[0]; i++)
                for (kind = 0; kind < 3; kind++)
                    for (vs = 0; vs < 3; vs++)
                        for (order = 0; order < (vs ? 2u : 1u); order++) {
                            unsigned p = run(latches[i], (int)kind, (int)vs, (int)order, 600);
                            if (p)
                                fprintf(stderr, "tribuf: latch %d kind %u vsync %u order %u preempts %d holds %d: "
                                        "%u visible lines painted\n", latches[i], kind, vs, order, preempts, holds, p);
                            CHECK_EQ(p, 0);
                        }
    return 0;
}
