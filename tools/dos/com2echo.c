/* COM2ECHO [secs] - the harness's COM2 check. Prints "HX-COM2 ready" on COM1,
 * reads one line from COM2 (115200 8N1, polled, at most secs seconds,
 * default 30), sends "ECHO:<line>" back on COM2 and reports
 *     HX-COM2 got=<line>        or        HX-COM2 timeout
 * on COM1. run.py --com2 bridges COM2 to a host TCP port. */
#include <conio.h>
#include <dos.h>
#include <stdlib.h>

#define COM1 0x3F8
#define COM2 0x2F8

static void uart_init(unsigned base)
{
    outp(base + 3, 0x80); outp(base, 1); outp(base + 1, 0); outp(base + 3, 0x03);
    outp(base + 2, 0xC7); outp(base + 4, 0x03);
}

static void put(unsigned base, char c)
{
    long spin = 0;
    while (!(inp(base + 5) & 0x20) && ++spin < 100000L) ;
    outp(base, c);
}

static void puts_(unsigned base, const char *s) { while (*s) put(base, *s++); }

static unsigned long ticks(void)
{
    return *(volatile unsigned long far *)MK_FP(0x40, 0x6C);
}

/* BIOS ticks from a to b, across the counter's start again at midnight (1800B0h a day). */
static unsigned long since(unsigned long a, unsigned long b) { return b >= a ? b - a : b + 0x1800B0UL - a; }

int main(int argc, char **argv)
{
    char line[80];
    unsigned n = 0;
    unsigned long limit = (unsigned long)(argc > 1 ? atoi(argv[1]) : 30) * 182 / 10, t0;
    int got_line = 0;

    uart_init(COM1);
    uart_init(COM2);
    while (inp(COM2 + 5) & 1)                   /* drop anything already waiting */
        (void)inp(COM2);
    puts_(COM1, "HX-COM2 ready\r\n");
    t0 = ticks();
    while (since(t0, ticks()) < limit) {
        if (inp(COM2 + 5) & 1) {
            char c = (char)inp(COM2);
            if (c == '\n') { got_line = 1; break; }
            if (c != '\r' && n < sizeof line - 1)
                line[n++] = c;
        }
    }
    line[n] = 0;
    if (!got_line) {
        puts_(COM1, "HX-COM2 timeout\r\n");
        return 1;
    }
    puts_(COM2, "ECHO:");
    puts_(COM2, line);
    puts_(COM2, "\r\n");
    puts_(COM1, "HX-COM2 got=");
    puts_(COM1, line);
    puts_(COM1, "\r\n");
    return 0;
}
