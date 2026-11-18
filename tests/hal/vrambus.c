/* vrambus.c - VRAMBUS: what a Matrox card returns from frame-buffer addresses past its memory (bench-g400's G100,
 * 2026-11-18: 4 MB in a 16 MB aperture; the HAL's VRAM probe once read 16 MB there), for 86Box's model of it.
 *
 *   VRAMBUS [MB]        (MB: the address to try, default 6)
 *
 * At MB (absent memory on a 4 MB card) and at the same offset past each power of two below it: writes a value,
 * reads it back at once, after draining the write-combining buffers, after a write of another value to memory that
 * is there, and a word never written; and whether the first MB changed (a mirror). HX-STAT vrambus lines. Writes
 * only to the frame buffer's first words and to the address tried; no mode set. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include <stdlib.h>
#include <string.h>

static void drain(void)
{
#if defined(__GNUC__) && defined(__i386__)
    __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory");
#endif
}

static void probe(uint32_t off)
{
    volatile uint32_t *fb = (volatile uint32_t *)mga_fb, *p = (volatile uint32_t *)(mga_fb + off);
    uint32_t v = 0xA5000000u | (off >> 12), r_now, r_drain, r_other, r_unwritten, f0, f1, m4, m8;
    volatile uint32_t *q4 = (volatile uint32_t *)(mga_fb + (off & 0x3FFFFFu)),
                      *q8 = (volatile uint32_t *)(mga_fb + (off & 0x7FFFFFu));
    if (off >= (1u << 22)) q4[0] = 0x44444444u;
    if (off >= (1u << 23)) q8[0] = 0x88888888u;
    fb[0] = 0x11111111u;
    fb[1] = 0x22222222u;
    drain();
    p[0] = v;
    r_now = p[0];
    drain();
    r_drain = p[0];
    fb[1] = 0x5A5A5A5Au;
    drain();
    r_other = p[0];
    r_unwritten = p[2];
    f0 = fb[0];
    f1 = fb[1];
    m4 = q4[0];                         /* (a mirror at 4 MB would have written v there) */
    m8 = q8[0];
    hx_log("HX-STAT vrambus off=%08lx wrote=%08lx now=%08lx drained=%08lx after-other=%08lx unwritten=%08lx fb0=%08lx "
           "fb1=%08lx mod4=%08lx mod8=%08lx", (unsigned long)off, (unsigned long)v, (unsigned long)r_now,
           (unsigned long)r_drain, (unsigned long)r_other, (unsigned long)r_unwritten, (unsigned long)f0,
           (unsigned long)f1, (unsigned long)m4, (unsigned long)m8);
}

int main(int argc, char **argv)
{
    uint32_t mb = argc > 1 ? (uint32_t)atoi(argv[1]) : 6, b;
    hx_init(1, argv, "vrambus");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        hx_test("init", 0, "no Matrox card");
        hx_done(HX_INIT_FAILED);
    }
    hx_log("HX-STAT vrambus card=%s fb_size=%lu probe=%lu", mga.name, (unsigned long)mga.fb_size,
           (unsigned long)mga_probe_vram());
    probe(1u << 20);                    /* (memory that is there: the control) */
    for (b = 4; b < mb; b *= 2)
        probe((b << 20) + 0x100);       /* (just past each power of two: a mirror would change fb[0]? no: fb[0x40]) */
    probe(mb << 20);
    probe((mga.fb_size - (1u << 20)));
    hx_test("vrambus", 1, "logged");
    hx_done(0);
}
