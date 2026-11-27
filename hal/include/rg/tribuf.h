/* tribuf.h - triple buffering for swaps without the vertical sync, on cards that take a new display start only at the
 * next vertical blank: the NVIDIA device's (NSPLIT on bench E's Quadro2, watched by the owner 2026-11-27: all blue).
 * With two buffers, the next frame drew into the one still shown until that blank: frames seen being painted (the
 * owner, 2026-11-27, on bench A's Rage 128, which takes its start at the next line instead now). With three, the next
 * frame goes into the one neither shown nor waiting to be, and nothing waits for the retrace: the frame rate isn't
 * capped. (The Matrox chips take a start at once, MSPLIT on bench A's G400: a swap without the retrace tears there,
 * never paints, and needs none of this.)
 *
 * Which buffer is shown comes from the raster line, read-only: a start is only written in the visible area (a swap
 * near the bottom waits for the next frame's top, about 2 ms at most), so the chip takes it in the blank that
 * follows. At the next swap it has been taken if the line has wrapped since, or if the time since says another
 * frame has begun (the time and the line together, to half a frame). Where neither can say (a start written late,
 * the clock held still), the swap waits until it is certain. */
#ifndef RG_TRIBUF_H
#define RG_TRIBUF_H

#include <stdint.h>

typedef struct {
    int      on;                /* three buffers, swaps without the retrace */
    uint32_t off[3];
    int      shown, pend, draw; /* indices into off (pend -1: no start waiting) */
    int      pend_line;         /* the line just before the waiting start was written */
    int      pend_late;         /* ... written in or near the blank (the program held up mid-swap), or with the
                                   retrace (rg_tri_flip_wait): taken by the line's second wrap, not known when */
    uint32_t pend_us;           /* the time just after it was written */
    uint32_t pend_backs;        /* ... and sys_time_backs then (the clock held still since: the time can't be used) */
    int      pend_held;         /* ... or held still then (the time read behind) */
    uint32_t frame_us;          /* a frame's time (measured at open) */
    int      lines;             /* the lines a frame, the blank's included (measured at open) */
    int      height;            /* the visible lines: the blank begins at or after this line */
    uint32_t (*line)(void);     /* the raster line now */
    void     (*set)(uint32_t off);  /* write the display start */
    unsigned waits, overtaken, late;    /* (counters: swaps that waited for the top; starts never shown; late ones) */
} rg_tribuf;

/* Measure the frame (two wraps of the line: up to ~50 ms); 0, or -1 if the line doesn't move (then don't use it). */
int      rg_tri_init(rg_tribuf *t, uint32_t a, uint32_t b, uint32_t c, int height, uint32_t (*line)(void),
                     void (*set)(uint32_t off));
/* The swap without the retrace: writes the start of the finished buffer (by set) and returns it; drawing moves on. */
uint32_t rg_tri_flip(rg_tribuf *t);
uint32_t rg_tri_draw(const rg_tribuf *t);   /* where drawing goes */
uint32_t rg_tri_front(const rg_tribuf *t);  /* the latest frame swapped (GL's front buffer) */
/* The swap with the retrace: the start the caller writes, before or after its wait for the retrace as the chip
 * wants; drawing goes where neither that start nor one that may still be shown are. */
uint32_t rg_tri_flip_wait(rg_tribuf *t);

#endif
