/* KEYWAIT <seconds> - wait for a key through the BIOS (INT 16h) and report
 * it on COM1 as "HX-KEY scan=XX ascii=YY", or "HX-KEY none" after the
 * timeout. Run after a program that hooks IRQ 1, it shows the keyboard was
 * given back to the BIOS. Keys already in the BIOS buffer (the harness's F1
 * taps at boot) are dropped first; "HX-KEYWAIT ready" then marks the moment
 * to type (a --keys anchor). */
#include <bios.h>
#include <conio.h>
#include <dos.h>
#include <stdlib.h>

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

static void hex2(unsigned v)
{
    static const char d[] = "0123456789abcdef";
    put(d[(v >> 4) & 15]);
    put(d[v & 15]);
}

/* BIOS ticks from a to b, across the counter's start again at midnight (1800B0h a day). */
static unsigned long since(unsigned long a, unsigned long b) { return b >= a ? b - a : b + 0x1800B0UL - a; }

int main(int argc, char **argv)
{
    unsigned long far *ticks = (unsigned long far *)MK_FP(0x40, 0x6C);
    unsigned long start = *ticks, n = (unsigned long)(argc > 1 ? atoi(argv[1]) : 10) * 182UL / 10UL;
    unsigned k;

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    while (_bios_keybrd(_KEYBRD_READY))
        _bios_keybrd(_KEYBRD_READ);
    puts_com("HX-KEYWAIT ready\r\n");
    while (since(start, *ticks) < n) {
        if (_bios_keybrd(_KEYBRD_READY)) {     /* INT 16h AH=01h */
            k = _bios_keybrd(_KEYBRD_READ);     /* INT 16h AH=00h: scan << 8 | ascii */
            puts_com("HX-KEY scan=");
            hex2(k >> 8);
            puts_com(" ascii=");
            hex2(k & 0xff);
            puts_com("\r\n");
            return 0;
        }
    }
    puts_com("HX-KEY none\r\n");
    return 1;
}
