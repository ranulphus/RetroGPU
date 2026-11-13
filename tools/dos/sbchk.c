/* SBCHK - is the Sound Blaster's DMA still running? Reads the current
 * count of BLASTER's 16-bit channel (H), or its 8-bit one (D) when there is
 * no H, twice 100 ms apart and reports on COM1 "HX-SBCHK dma=N stopped" or
 * "HX-SBCHK dma=N running" (the count moved: a program left the card
 * playing, typically looping its auto-init buffer). */
#include <conio.h>
#include <dos.h>
#include <stdlib.h>
#include <string.h>

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void puts_com(const char *s)
{
    while (*s) put(*s++);
}

static unsigned count(int ch)
{
    unsigned lo, hi;
    _disable();
    if (ch >= 4) {
        outp(0xD8, 0);                          /* clear the flip-flop */
        lo = inp(0xC2 + (ch - 4) * 4);
        hi = inp(0xC2 + (ch - 4) * 4);
    } else {
        outp(0x0C, 0);
        lo = inp(1 + ch * 2);
        hi = inp(1 + ch * 2);
    }
    _enable();
    return lo | (hi << 8);
}

/* BIOS ticks from a to b, across the counter's start again at midnight (1800B0h a day). */
static unsigned long since(unsigned long a, unsigned long b) { return b >= a ? b - a : b + 0x1800B0UL - a; }

int main(void)
{
    unsigned long far *ticks = (unsigned long far *)MK_FP(0x40, 0x6C);
    const char *b = getenv("BLASTER"), *p;
    int ch = -1, moved = 0, i;
    unsigned a, c;
    unsigned long t;
    char msg[48];

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    if (b && (p = strchr(b, 'H')) != NULL)
        ch = atoi(p + 1);
    else if (b && (p = strchr(b, 'D')) != NULL)
        ch = atoi(p + 1);
    if (ch < 0 || ch > 7 || ch == 4) {
        puts_com("HX-SBCHK no DMA channel in BLASTER\r\n");
        return 2;
    }
    a = count(ch);
    for (i = 0; i < 4 && !moved; i++) {         /* ~100 ms in 2-tick steps */
        t = *ticks;
        while (since(t, *ticks) < 2)
            continue;
        c = count(ch);
        moved = c != a;
    }
    strcpy(msg, "HX-SBCHK dma=");
    msg[13] = (char)('0' + ch);
    msg[14] = 0;
    strcat(msg, moved ? " running\r\n" : " stopped\r\n");
    puts_com(msg);
    return moved;
}
