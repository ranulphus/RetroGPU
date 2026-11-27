/* tribuf.c - triple buffering for swaps without the vertical sync (rg/tribuf.h). */
#include "rg/tribuf.h"
#include "mga/sys.h"
#include <string.h>

#define MARGIN 16                       /* lines before the visible area's end where no start is written */
#define CAP_US 100000u                  /* (the longest any wait here lasts: a stuck counter) */

static uint32_t elapsed(uint32_t since)
{
    int32_t d = (int32_t)(sys_time_us() - since);
    return d > 0 ? (uint32_t)d : 0u;
}

int rg_tri_init(rg_tribuf *t, uint32_t a, uint32_t b, uint32_t c, int height, uint32_t (*line)(void),
                void (*set)(uint32_t off))
{
    uint32_t t0, w[2], prev, now, top, backs;
    int wraps, tries = 0;
    memset(t, 0, sizeof *t);
    t->off[0] = a, t->off[1] = b, t->off[2] = c;
    t->shown = 0, t->draw = 1, t->pend = -1;
    t->height = height, t->line = line, t->set = set;
    /* A frame's time between two wraps of the line (a wrap: the line falling by more than a few), and the highest
     * line read between them (the lines a frame, the blank's included). */
again:
    tries++, wraps = 0, top = 0, w[0] = w[1] = 0;
    t0 = sys_time_us();
    backs = sys_time_backs;
    prev = line();
    while (wraps < 2) {
        now = line();
        if (now + 8u < prev)
            w[wraps++] = sys_time_us();
        else if (wraps == 1 && now > top)
            top = now;
        prev = now;
        if (elapsed(t0) > 120000u)
            return -1;                  /* (the line doesn't move: no triple buffering) */
    }
    t->frame_us = w[1] - w[0];
    t->lines = (int)top + 1;
    if (sys_time_backs != backs || t->frame_us < 5000u || t->frame_us > 50000u || t->lines <= height ||
        t->lines > 4096) {
        if (tries < 6)
            goto again;                 /* (the clock held still meanwhile, up to 55 ms: measure again) */
        return -1;
    }
    t->on = 1;
    return 0;
}

/* Until the line is in the visible area, MARGIN lines short of its end (the blank and the lines before it: the next
 * frame's top, ~2 ms at most). */
static void visible(rg_tribuf *t)
{
    uint32_t t0 = sys_time_us();
    int waited = 0;
    while ((int)t->line() >= t->height - MARGIN) {
        waited = 1;
        if (elapsed(t0) > CAP_US)
            break;
    }
    t->waits += (unsigned)waited;
}

/* Until the waiting start is certainly taken, then it is the one shown, by the line alone (the clock may be what
 * can't be trusted): one written late (in or near a blank, or with the retrace) is taken in that blank or the next, so
 * certainly by the line's second wrap; one written in the visible area by its first wrap after the line it was
 * written at. (The time only caps these: a line that stopped.) */
static void settle(rg_tribuf *t)
{
    uint32_t t0 = sys_time_us(), prev = t->line(), now;
    int wraps = 0;
    if (t->pend_late) {
        while (wraps < 2 && elapsed(t0) < 3u * CAP_US) {
            now = t->line();
            if (now + 8u < prev)
                wraps++;
            prev = now;
        }
    } else
        while ((int)t->line() >= t->pend_line && elapsed(t0) < CAP_US)
            ;
    t->shown = t->pend, t->pend = -1;
}

/* Was the waiting start taken by line l (in the visible area)? It was written at pend_line, also in the visible
 * area, and the chip takes it in the blank: so if the line has wrapped since, or if the time since says the line has
 * also gone round a whole frame (time and line together, to half a frame). */
static int taken(const rg_tribuf *t, int l)
{
    uint32_t e, est;
    if (l < t->pend_line)
        return 1;
    e = elapsed(t->pend_us);
    if (e >= 1000000u)
        return 1;
    est = e * (uint32_t)t->lines / t->frame_us;
    return est >= (uint32_t)(l - t->pend_line) + (uint32_t)t->lines / 2u;
}

uint32_t rg_tri_flip(rg_tribuf *t)
{
    int show, l, l2;
    if (t->pend >= 0 && (t->pend_late || t->pend_held || sys_time_backs != t->pend_backs || sys_time_held))
        settle(t);                      /* (when it was taken can't be told from the time) */
    /* The judgement of the waiting start and the new start's write with one look at the line, in the visible area:
     * no blank between them in which the chip could take the waiting start after all. */
    visible(t);
    l = (int)t->line();
    if (t->pend >= 0) {
        if (taken(t, l))
            t->shown = t->pend;
        else
            t->overtaken++;
        t->pend = -1;
    }
    show = t->draw;
    t->set(t->off[show]);
    l2 = (int)t->line();
    t->pend = show, t->pend_line = l, t->pend_us = sys_time_us(), t->pend_backs = sys_time_backs, t->pend_late = 0;
    t->pend_held = (int)sys_time_held;
    if (l2 < l || l2 >= t->height - MARGIN / 4) {
        /* Held up around the write (GLOS's other work) into the blank or past it: the waiting start judged before may
         * have been taken since, and when this one is taken isn't known: wait until it certainly is (rare). */
        t->late++;
        t->pend_late = 1;
        settle(t);
        t->draw = (show + 1) % 3;
    } else
        t->draw = 3 - t->shown - t->pend;
    return t->off[show];
}

uint32_t rg_tri_flip_wait(rg_tribuf *t)
{
    /* The one that may be on screen: the waiting start if any (taken by this retrace at the latest), else the one
     * shown. The new start is taken at this retrace or the next: a late one, for a swap without the retrace next. */
    int maybe = t->pend >= 0 ? t->pend : t->shown, show = t->draw;
    t->shown = maybe;
    t->pend = show, t->pend_late = 1;
    t->draw = 3 - show - maybe;
    return t->off[show];
}

uint32_t rg_tri_draw(const rg_tribuf *t) { return t->off[t->draw]; }
uint32_t rg_tri_front(const rg_tribuf *t) { return t->off[t->pend >= 0 ? t->pend : t->shown]; }
