/* prof.h - exclusive cycle accounting by stage, for builds with MGA_PROF.
 *
 * One stage is current at a time. prof_switch(c) charges the cycles since
 * the last switch (RDTSC; in 86Box the emulated CPU's cycles) to the current
 * stage, makes c current and counts an entry; prof_back(o) returns to an
 * earlier stage without counting one. Stage 0 (PROF_APP) is everything
 * outside the library: the game. The HAL owns stages 1-7; MGA-Glide and
 * DOS-GL number theirs from PROF_CLIENT. Each switch costs a few dozen
 * cycles (measured and printed by the clients), so the timers sit at stage
 * boundaries, never per pixel or per register. Needs a Pentium (RDTSC).
 * Without MGA_PROF every macro is empty. */
#ifndef MGA_PROF_H
#define MGA_PROF_H
#include "mga/types.h"

enum {
    PROF_APP,           /* outside the library */
    PROF_FIFO,          /* fifo_reserve polling FIFOSTATUS */
    PROF_SPLANE,        /* setup_triangle: sorting and plane gradients */
    PROF_SINC,          /* setup_triangle: per-triangle increments */
    PROF_STRAP,         /* setup_triangle: per trapezoid (edges, starts, start) */
    PROF_VSYNC,         /* a swap waiting for the retrace (the device's: rg/mga.c) */
    PROF_CLIENT = 8,    /* the first stage a client may use */
    PROF_N = 24
};

#ifdef MGA_PROF
extern uint64_t prof_cyc[PROF_N];      /* cycles charged to each stage */
extern uint32_t prof_n[PROF_N];        /* entries into each stage */
extern uint64_t prof_t;                /* time of the last switch */
extern int      prof_cur;              /* the current stage */
extern uint32_t prof_wr, prof_fifo_rd; /* register writes, FIFOSTATUS reads */

#  if defined(__WATCOMC__)
uint64_t prof_rdtsc(void);
#    pragma aux prof_rdtsc = 0x0F 0x31 value [edx eax] modify exact [eax edx];
#  else
MGA_INLINE uint64_t prof_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#  endif

MGA_INLINE int prof_switch(int c)
{
    uint64_t t = prof_rdtsc();
    int o = prof_cur;
    prof_cyc[o] += t - prof_t;
    prof_t = t;
    prof_cur = c;
    prof_n[c]++;
    return o;
}

MGA_INLINE void prof_back(int c)
{
    uint64_t t = prof_rdtsc();
    prof_cyc[prof_cur] += t - prof_t;
    prof_t = t;
    prof_cur = c;
}

/* Clear the counters and start timing now, in stage c. */
MGA_INLINE void prof_reset(int c)
{
    int i;
    for (i = 0; i < PROF_N; i++) {
        prof_cyc[i] = 0;
        prof_n[i] = 0;
    }
    prof_wr = prof_fifo_rd = 0;
    prof_cur = c;
    prof_t = prof_rdtsc();
}

#  define PROF_SWITCH(c) prof_switch(c)
#  define PROF_BACK(o)   prof_back(o)
#  define PROF_WR()      (prof_wr++)
#  if defined(__GNUC__)
MGA_INLINE void prof_scope_end_(int *o) { prof_back(*o); }
/* GCC only: the rest of this block is stage c; the stage before comes back
 * at any exit. */
#    define PROF_SCOPE(c) int prof_scope_ __attribute__((cleanup(prof_scope_end_))) = prof_switch(c)
#  endif
#else
MGA_INLINE int prof_switch_off(int c) { (void)c; return 0; }
#  define PROF_SWITCH(c) prof_switch_off(c)
#  define PROF_BACK(o)   ((void)(o))
#  define PROF_WR()      ((void)0)
#  define PROF_SCOPE(c)  ((void)0)
#endif

#endif
