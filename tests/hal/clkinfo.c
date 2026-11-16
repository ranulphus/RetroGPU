/* clkinfo.c - CLKINFO: the Matrox card's clocks as the BIOS left them
 * (gl/docs/g4path.md, M0). Reads only, except the DAC's index register to
 * select the PLL registers it reads:
 *   - OPTION, OPTION2, OPTION3 (PCI configuration 40h, 50h, 54h): the
 *     clock sources and dividers of the graphics, memory and WARP clocks;
 *   - the system PLL's M, N, P (indirect registers 2Ch-2Eh) and its
 *     frequency from a 27 MHz reference;
 *   - MCTLWTST (memory timing) and the video BIOS's PInS block (the
 *     clocks and timings the card was made for), dumped in hex.
 * HX-STAT clkinfo lines; nothing is changed. CLKINFO FILL also times
 * whole-screen fills (it sets 640x480x16: not under Windows). */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>
#ifdef __DJGPP__
#include <go32.h>
#include <sys/farptr.h>
#endif

static uint8_t xread(uint8_t idx)
{
    MGA_WR8(MGAREG_PALWTADD, idx);
    return MGA_RD8(MGAREG_X_DATAREG);
}

int main(int argc, char **argv)
{
    uint32_t opt, opt2, opt3, wtst;
    uint8_t m, n, p, st;
    double fvco, fo;
    int fill = argc > 1 && (argv[1][0] == 'f' || argv[1][0] == 'F');
    hx_init(1, argv, "clkinfo");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        hx_test("init", 0, "no Matrox card");
        hx_done(HX_INIT_FAILED);
    }
    opt = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0x40);
    opt2 = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0x50);
    opt3 = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0x54);
    wtst = MGA_RD32(MGAREG_MCTLWTST);
    m = xread(0x2C);
    n = xread(0x2D);
    p = xread(0x2E);
    st = xread(0x2F);
    fvco = 27.0 * (double)((n & 0x7F) + 1) / (double)((m & 0x1F) + 1);
    fo = fvco / (double)((p & 7) + 1);
    hx_log("HX-STAT clkinfo device=%04x option=%08lx option2=%08lx option3=%08lx mctlwtst=%08lx", mga.device_id,
           (unsigned long)opt, (unsigned long)opt2, (unsigned long)opt3, (unsigned long)wtst);
    hx_log("HX-STAT clkinfo syspll m=%02x n=%02x p=%02x stat=%02x vco_mhz=%.1f out_mhz=%.1f", m, n, p, st, fvco, fo);
    hx_log("HX-STAT clkinfo option3 gclksel=%lu gclkdiv=%lu gclkdcyc=%lu mclksel=%lu mclkdiv=%lu mclkdcyc=%lu "
           "wclksel=%lu wclkdiv=%lu wclkdcyc=%lu",
           (unsigned long)(opt3 & 3), (unsigned long)((opt3 >> 3) & 7), (unsigned long)((opt3 >> 6) & 1),
           (unsigned long)((opt3 >> 10) & 3), (unsigned long)((opt3 >> 13) & 7), (unsigned long)((opt3 >> 16) & 1),
           (unsigned long)((opt3 >> 20) & 3), (unsigned long)((opt3 >> 23) & 7), (unsigned long)((opt3 >> 26) & 1));
#ifdef __DJGPP__
    {
        /* The PInS: the ROM's word at 7FFCh points at it (C000:xxxx). */
        unsigned off = _farpeekw(_dos_ds, 0xC0000u + 0x7FFCu), i, len;
        char line[160];
        len = _farpeekb(_dos_ds, 0xC0000u + off + 2);
        hx_log("HX-STAT clkinfo pins at=%04x sig=%04x len=%u ver=%u", off, _farpeekw(_dos_ds, 0xC0000u + off), len,
               _farpeekb(_dos_ds, 0xC0000u + off + 5));
        if (len < 64 || len > 160)
            len = 128;
        for (i = 0; i < len; i += 32) {
            unsigned k, used = 0;
            for (k = 0; k < 32 && i + k < len; k++)
                used += (unsigned)snprintf(line + used, sizeof line - used, "%02x",
                                           _farpeekb(_dos_ds, 0xC0000u + off + i + k));
            hx_log("HX-STAT clkinfo pins+%02x %s", i, line);
        }
    }
#endif
    {
        /* The engine's speed: whole-screen fills of 640x480x16 (a mode set
         * for them), colour then depth, each timed to the engine idle. */
        mga_vbe_mode mode;
        mga_target tg;
        int pitch, i;
        if (fill && vbe_find_mode(640, 480, 16, &mode) == 0 && vbe_set_mode(&mode, 1024, &pitch) == 0) {
            uint64_t t0, t1;
            uint32_t u0, u1;
            engine_init(pitch, 16);
            memset(&tg, 0, sizeof tg);
            tg.color_off = 0; tg.z_off = 1024u * 480u * 2u; tg.pitch_px = 1024; tg.bpp = 16; tg.zbits = 16;
            engine_set_target(&tg);
            engine_set_clip(0, 0, 640, 480);
            engine_sync(200000);
            u0 = sys_time_us();
            for (i = 0; i < 100; i++)
                engine_fill(0, 0, 640, 480, (uint32_t)(i * 0x0841u));
            engine_sync(2000000);
            u1 = sys_time_us();
            hx_log("HX-STAT clkinfo fill colour 100x640x480 us=%lu mpix_s=%.1f", (unsigned long)(u1 - u0),
                   100.0 * 640 * 480 / (double)(u1 - u0));
            u0 = sys_time_us();
            for (i = 0; i < 100; i++)
                engine_fill_depth(0, 0, 640, 480, 0xFFFFu - (uint32_t)i);
            engine_sync(2000000);
            u1 = sys_time_us();
            hx_log("HX-STAT clkinfo fill depth 100x640x480 us=%lu mpix_s=%.1f", (unsigned long)(u1 - u0),
                   100.0 * 640 * 480 / (double)(u1 - u0));
            (void)t0; (void)t1;
            vbe_set_text_mode();
        }
    }
    hx_test("clkinfo", 1, "read");
    hx_done(0);
}
