/* clkset.c - CLKSET: the G400 at the clocks Matrox's own driver uses (gl/docs/g4path.md, C0), measured, then
 * the BIOS's clocks put back.
 *
 *   CLKSET [dry] [emu]
 *
 * Matrox's Windows 98 driver on bench-g400 runs the card at its PInS v4 block's second clock record (system PLL
 * 252 MHz: M=02 N=1B P=18; OPTION3 019B8419) and raises OPTION's refresh count (bits 15-20) and sets bit 22;
 * the BIOS leaves the first record (164.7 MHz, OPTION3 0190A419). CLKSET takes the record from the card's own
 * PInS (it must say 252 MHz and 019B8419, or nothing is written) and switches as Linux's matroxfb
 * (g450_mclk_init) does: the clocks onto the PCI clock (OPTION3's sources cleared, with OPTION's sysclkdis
 * around each OPTION3 write), the PLL programmed and its lock waited for, then OPTION3. Steps:
 *   A  the BIOS's clocks: fill rate and a read-back check
 *   B  record 2's clocks, the refresh count as the BIOS has it (more refreshes than needed: the safe side)
 *   C  as B with OPTION as Windows has it (refresh count 27h, bit 22)
 *   D  after a VBE mode set: are the clocks still B/C's? (if the BIOS puts its own back, the HAL must set them
 *      after every mode set)
 *   E  the BIOS's clocks back (the values read at the start), checked by reading them again
 * A fill is 100 whole-screen fills of 640x480x16, timed; the check fills patterns and reads 64 KB back by the
 * CPU. Any check that fails or a PLL that doesn't lock goes straight to E. dry: everything but the writes to
 * OPTION, OPTION3 and the PLL. emu (86Box, whose G400 BIOS has no second record): the same steps without the
 * PInS check; refused where the PInS has a second record (a real card). HX-STAT clkset lines; HX-TEST clkset. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>
#include <go32.h>
#include <sys/farptr.h>

#define PCI_OPTION 0x40
#define PCI_OPTION3 0x54
#define OPT_SYSCLKDIS 0x00000004u
#define OPT_WIN_BITS 0x005F8000u        /* rfhcnt (15-20) and bit 22 */
#define OPT3_SOURCES 0x00300C03u        /* gclksel, mclksel, wclksel */

static int dry, emu;
static uint32_t opt0, opt30;
static uint8_t m0, n0, p0;

static uint8_t xread(uint8_t idx)
{
    MGA_WR8(MGAREG_PALWTADD, idx);
    return MGA_RD8(MGAREG_X_DATAREG);
}

static void xwrite(uint8_t idx, uint8_t v)
{
    MGA_WR8(MGAREG_PALWTADD, idx);
    MGA_WR8(MGAREG_X_DATAREG, v);
}

static uint32_t cfg_read(int reg) { return mga_pci_read32(mga.bus, mga.dev, mga.fn, reg); }

static void cfg_write(int reg, uint32_t v)
{
    if (!dry)
        mga_pci_write32(mga.bus, mga.dev, mga.fn, reg, v);
}

static void show(const char *what)
{
    uint8_t m = xread(0x2C), n = xread(0x2D), p = xread(0x2E);
    double f = 27.0 * (double)((n & 0x7F) + 1) / (double)((m & 0x1F) + 1) / (double)((p & 7) + 1);
    hx_log("HX-STAT clkset %s option=%08lx option3=%08lx syspll m=%02x n=%02x p=%02x stat=%02x mhz=%.1f", what,
           (unsigned long)cfg_read(PCI_OPTION), (unsigned long)cfg_read(PCI_OPTION3), m, n, p, xread(0x2F), f);
}

/* The switch: sources to the PCI clock, the PLL, its lock, then OPTION3 and OPTION. 0, or -1 if it didn't lock
 * (the clocks are then left on the PCI clock: the caller switches back). */
static int set_clocks(uint8_t m, uint8_t n, uint8_t p, uint32_t opt3, uint32_t opt)
{
    uint32_t cur = cfg_read(PCI_OPTION), t0;
    int locked = 0;
    engine_sync(500000);
    cfg_write(PCI_OPTION, cur | OPT_SYSCLKDIS);
    cfg_write(PCI_OPTION3, cfg_read(PCI_OPTION3) & ~OPT3_SOURCES);
    cfg_write(PCI_OPTION, cur & ~OPT_SYSCLKDIS);
    if (!dry) {
        xwrite(0x2C, m);
        xwrite(0x2D, n);
        xwrite(0x2E, p);
    }
    t0 = sys_time_us();
    while (sys_time_us() - t0 < 100000u)
        if (xread(0x2F) & 0x40) {
            locked = 1;
            break;
        }
    if (!locked)
        return -1;
    cfg_write(PCI_OPTION, cur | OPT_SYSCLKDIS);
    cfg_write(PCI_OPTION3, opt3);
    cfg_write(PCI_OPTION, opt & ~OPT_SYSCLKDIS);
    return 0;
}

static void restore(void)
{
    if (set_clocks(m0, n0, p0, opt30, opt0) != 0) {
        /* (no lock back at the BIOS's values: OPTION3's sources stay on the PCI clock, which runs) */
        hx_log("HX-STAT clkset restore: the PLL did not lock at the BIOS's values; clocks left on the PCI clock");
        cfg_write(PCI_OPTION, opt0);
    }
}

/* Fill rate (Mpix/s) and a read-back check: 0 bad pixels or the count. */
static double fill_test(const char *step, int *bad)
{
    volatile uint16_t *fb = (volatile uint16_t *)mga_fb;
    uint32_t u0, u1;
    int i, x, y;
    double rate;
    engine_sync(500000);
    u0 = sys_time_us();
    for (i = 0; i < 100; i++)
        engine_fill(0, 0, 640, 480, (uint32_t)(i * 0x0841u));
    engine_sync(2000000);
    u1 = sys_time_us();
    rate = 100.0 * 640 * 480 / (double)(u1 - u0);
    *bad = 0;
    for (i = 0; i < 4; i++) {
        uint16_t c = (uint16_t)(0x5A5Au ^ (uint16_t)(i * 0x1111u));
        engine_fill(0, 0, 640, 480, c);
        engine_sync(500000);
        for (y = 0; y < 480; y += 15)
            for (x = 0; x < 640; x += 3)
                if (fb[y * 1024 + x] != c)
                    (*bad)++;
    }
    hx_log("HX-STAT clkset %s fill_mpix_s=%.1f bad_pixels=%d", step, rate, *bad);
    return rate;
}

int main(int argc, char **argv)
{
    mga_vbe_mode mode;
    mga_target tg;
    int pitch, bad = 0, ok = 1, i;
    unsigned off;
    uint8_t pins[128];
    uint32_t r2_opt3, opt_win;
    double fa, fb_ = 0, fc = 0, fe = 0;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "dry")) dry = 1;
        else if (!strcmp(argv[i], "emu")) emu = 1;
    hx_init(1, argv, "clkset");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0 || mga.device_id != 0x0525) {
        hx_test("init", 0, "a G400 is needed");
        hx_done(HX_INIT_FAILED);
    }
    off = _farpeekw(_dos_ds, 0xC0000u + 0x7FFCu);
    for (i = 0; i < 128; i++)
        pins[i] = _farpeekb(_dos_ds, 0xC0000u + off + (unsigned)i);
    r2_opt3 = (uint32_t)pins[67] | (uint32_t)pins[68] << 8 | (uint32_t)pins[69] << 16 | (uint32_t)pins[70] << 24;
    hx_log("HX-STAT clkset dry=%d pins ver=%u record2 pll2=%02x pll4=%02x option3=%08lx", dry, pins[5], pins[65],
           pins[66], (unsigned long)r2_opt3);
    if (emu && r2_opt3 == 0xFFFFFFFFu)
        r2_opt3 = 0x019B8419u;              /* (no second record: the emulator) */
    else if (emu) {
        hx_test("clkset", 0, "emu refused: this PInS has a second record (a real card)");
        hx_done(1);
    } else if (pins[0] != 0x2E || pins[1] != 0x41 || pins[5] != 4 || pins[66] != 0x3F || r2_opt3 != 0x019B8419u) {
        hx_test("clkset", 0, "the PInS's second record is not the one Matrox's driver used on bench-g400 (252 MHz, 019B8419)");
        hx_done(1);
    }
    opt0 = cfg_read(PCI_OPTION);
    opt30 = cfg_read(PCI_OPTION3);
    m0 = xread(0x2C);
    n0 = xread(0x2D);
    p0 = xread(0x2E);
    opt_win = (opt0 & ~OPT_WIN_BITS) | (0x50538120u & OPT_WIN_BITS);
    show("start");
    if (vbe_find_mode(640, 480, 16, &mode) != 0 || vbe_set_mode(&mode, 1024, &pitch) != 0 || pitch != 1024) {
        hx_test("init", 0, "640x480x16 with pitch 1024");
        hx_done(HX_INIT_FAILED);
    }
    engine_init(pitch, 16);
    memset(&tg, 0, sizeof tg);
    tg.color_off = 0; tg.z_off = 1024u * 480u * 2u; tg.pitch_px = 1024; tg.bpp = 16; tg.zbits = 16;
    engine_set_target(&tg);
    engine_set_clip(0, 0, 640, 480);
    show("A");
    fa = fill_test("A", &bad);
    ok = bad == 0;

    /* B: record 2's clocks (252 MHz: M=02 N=1B P=18, as Windows set them), OPTION as the BIOS has it */
    if (ok && set_clocks(0x02, 0x1B, 0x18, r2_opt3, opt0) != 0) {
        hx_log("HX-STAT clkset B: the PLL did not lock");
        ok = 0;
    }
    if (ok) {
        show("B");
        fb_ = fill_test("B", &bad);
        ok = bad == 0;
    }
    /* C: OPTION's refresh count and bit 22 as Windows has them */
    if (ok) {
        engine_sync(500000);
        cfg_write(PCI_OPTION, opt_win);
        show("C");
        fc = fill_test("C", &bad);
        ok = bad == 0;
    }
    /* D: a VBE mode set: does the BIOS put its clocks back? */
    if (ok) {
        vbe_set_mode(&mode, 1024, &pitch);
        engine_init(pitch, 16);
        engine_set_target(&tg);
        engine_set_clip(0, 0, 640, 480);
        show("D");
    }
    /* E: the BIOS's clocks back */
    restore();
    show("E");
    fe = fill_test("E", &bad);
    vbe_set_text_mode();
    {
        int back = cfg_read(PCI_OPTION3) == opt30 && cfg_read(PCI_OPTION) == opt0 && xread(0x2C) == m0 &&
                   xread(0x2D) == n0 && xread(0x2E) == p0;
        hx_log("HX-STAT clkset summary fill A=%.1f B=%.1f C=%.1f E=%.1f restored=%d", fa, fb_, fc, fe, back);
        hx_test("clkset", ok && back && bad == 0, "%s; BIOS clocks %s", ok ? "steps clean" : "stopped early",
                back ? "restored" : "NOT restored");
        hx_done(ok && back ? 0 : 1);
    }
}
