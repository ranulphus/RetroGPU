/* WAITSEC <n> - wait n seconds using the BIOS tick counter. The counter starts
 * again from 0 at midnight (after 1800B0h ticks): a wait that crosses it goes
 * on (it ended at once, and a GLOS suite that ran through midnight failed on
 * it, 2026-11-13). */
#include <dos.h>
#include <stdlib.h>

#define DAY 0x1800B0UL

int main(int argc, char **argv)
{
    unsigned long far *ticks = (unsigned long far *)MK_FP(0x40, 0x6C);
    unsigned long start = *ticks, n = (unsigned long)(argc > 1 ? atoi(argv[1]) : 1) * 182UL / 10UL, now;
    for (;;) {
        now = *ticks;
        if ((now >= start ? now - start : now + DAY - start) >= n)
            break;
    }
    return 0;
}
