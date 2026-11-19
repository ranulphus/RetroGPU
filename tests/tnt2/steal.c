/* steal.c - how much of the CPU a program gets: a tight loop reads the time-stamp counter for a few seconds and
 * sums every gap longer than 400 cycles (time spent outside the loop: interrupts, the DPMI host, GLOS's kernel).
 * Prints the share lost, how many gaps, the longest, and a histogram of their sizes. STEAL [seconds] */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "hx.h"

static uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t)hi << 32 | lo;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    uint64_t t0, t1, prev, now, lost = 0, maxgap = 0, mhz_c;
    unsigned long gaps = 0, hist[8] = { 0 };
    clock_t c0, c1;
    hx_init(1, argv, "STEAL");
    /* the counter's rate, against the BIOS clock over a second */
    c0 = clock();
    while (clock() == c0)
        ;
    c0 = clock(), t0 = rdtsc();
    while (clock() - c0 < CLOCKS_PER_SEC)
        ;
    c1 = clock(), t1 = rdtsc();
    mhz_c = (t1 - t0) * CLOCKS_PER_SEC / (uint64_t)(c1 - c0);
    t0 = prev = rdtsc();
    t1 = t0 + mhz_c * (uint64_t)secs;
    while ((now = rdtsc()) < t1) {
        uint64_t g = now - prev;
        if (g > 400) {
            int b = 0;
            uint64_t k = g;
            lost += g, gaps++;
            if (g > maxgap)
                maxgap = g;
            while (k > 1000 && b < 7)
                k /= 4, b++;
            hist[b]++;
        }
        prev = now;
    }
    hx_log("STEAL mhz=%lu secs=%d lost=%.2f%% gaps=%lu per_s=%lu max_us=%lu avg_cycles=%lu",
              (unsigned long)(mhz_c / 1000000), secs, 100.0 * (double)lost / (double)(now - t0), gaps,
              gaps / (unsigned long)secs, (unsigned long)(maxgap * 1000000 / mhz_c),
              gaps ? (unsigned long)(lost / gaps) : 0ul);
    hx_log("STEAL hist <1k=%lu <4k=%lu <16k=%lu <64k=%lu <256k=%lu <1M=%lu <4M=%lu more=%lu", hist[0], hist[1],
              hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
    hx_done(0);
    return 0;
}
