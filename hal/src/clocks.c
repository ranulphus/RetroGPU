/* clocks.c - the G400 at the clocks Matrox's own driver uses (gl/docs/g4path.md, C).
 *
 * The BIOS starts a G400 on its PInS v4 block's first clock record (bench-g400: system PLL 164.7 MHz, OPTION3
 * 0190A419); Matrox's Windows driver runs it on the second (252 MHz, 019B8419; OPTION's refresh count 27h and
 * bit 22), half as fast again in the engine and the memory, with identical pixels. The PInS v4 records are 10
 * bytes at 55, 65 and 75: {PLL in 2 MHz, PLL in 4 MHz, OPTION3, MCTLWTST}. Only the pair of records seen on
 * bench-g400 is acted on, with the PLL values (M=02 N=1B P=18) and OPTION bits Matrox's driver set there; any
 * other PInS, or clocks not at the BIOS's first record, are left alone. The switch is Linux matroxfb's
 * (g450_mclk_init): the clocks onto the PCI clock, the PLL and its lock, then OPTION3, each OPTION3 write
 * between OPTION's sysclkdis.
 *
 * Needs the card found and its registers mapped (mga, mga_mmio), PCI configuration access (pci.c), the port's
 * sys_real_ptr (the video BIOS at C000:0) and sys_time_us; no engine context: the caller sees that the engine
 * is idle. GLOS calls it at its start for every program; the Matrox device (rg/mga.c) only without GLOS. */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"

#define CLK_OPTION 0x40
#define CLK_OPTION3 0x54
#define CLK_SYSCLKDIS 0x00000004u
#define CLK_OPT_BITS 0x005F8000u        /* rfhcnt (15-20) and bit 22 */
#define CLK_OPT3_SOURCES 0x00300C03u
#define BIOS_OPT3 0x0190A419u
#define FAST_OPT3 0x019B8419u
#define FAST_OPT 0x50538120u            /* (only CLK_OPT_BITS of it) */

const char *mga_clocks_why = "not tried";

static uint8_t clk_x(uint8_t idx)
{
    MGA_WR8(MGAREG_PALWTADD, idx);
    return MGA_RD8(MGAREG_X_DATAREG);
}

static void clk_xw(uint8_t idx, uint8_t v)
{
    MGA_WR8(MGAREG_PALWTADD, idx);
    MGA_WR8(MGAREG_X_DATAREG, v);
}

static uint32_t cfg(int reg) { return mga_pci_read32(mga.bus, mga.dev, mga.fn, reg); }
static void cfg_w(int reg, uint32_t v) { mga_pci_write32(mga.bus, mga.dev, mga.fn, reg, v); }

static uint32_t le32p(const volatile uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* The three steps; 0, or -1 if the PLL didn't lock within 100 ms (the clocks then run from the PCI clock). */
static int clk_switch(uint8_t m, uint8_t n, uint8_t p, uint32_t opt3, uint32_t opt)
{
    uint32_t cur = cfg(CLK_OPTION), t0;
    cfg_w(CLK_OPTION, cur | CLK_SYSCLKDIS);
    cfg_w(CLK_OPTION3, cfg(CLK_OPTION3) & ~CLK_OPT3_SOURCES);
    cfg_w(CLK_OPTION, cur & ~CLK_SYSCLKDIS);
    clk_xw(0x2C, m);
    clk_xw(0x2D, n);
    clk_xw(0x2E, p);
    t0 = sys_time_us();
    while (!(clk_x(0x2F) & 0x40))
        if (sys_time_us() - t0 > 100000u)
            return -1;
    cfg_w(CLK_OPTION, cur | CLK_SYSCLKDIS);
    cfg_w(CLK_OPTION3, opt3);
    cfg_w(CLK_OPTION, opt & ~CLK_SYSCLKDIS);
    return 0;
}

int mga_clocks_fast(mga_clocks *saved)
{
    const volatile uint8_t *rom, *pins;
    uint32_t opt, opt3, t0;
    uint16_t at;
    uint8_t m, n, p;
    saved->valid = 0;
    if (!mga_mmio || mga.device_id != 0x0525) {
        mga_clocks_why = "not a G400";      /* (the G450's PInS v5 not yet checked) */
        return 1;
    }
    rom = (const volatile uint8_t *)sys_real_ptr(0xC000, 0);
    if (!rom) {
        mga_clocks_why = "no video BIOS";
        return 1;
    }
    at = (uint16_t)(rom[0x7FFC] | rom[0x7FFD] << 8);
    pins = rom + at;
    if (at < 0x80 || at > 0x7F00 || pins[0] != 0x2E || pins[1] != 0x41 || pins[5] != 4 || pins[2] < 85 ||
        pins[55] != 0x52 || pins[56] != 0x29 || le32p(pins + 57) != BIOS_OPT3 ||
        pins[65] != 0x7E || pins[66] != 0x3F || le32p(pins + 67) != FAST_OPT3) {
        mga_clocks_why = "PInS not the one seen on bench-g400";
        return 1;
    }
    opt = cfg(CLK_OPTION);
    opt3 = cfg(CLK_OPTION3);
    m = clk_x(0x2C);
    n = clk_x(0x2D);
    p = clk_x(0x2E);
    if (opt3 == FAST_OPT3 && m == 0x02 && n == 0x1B) {
        mga_clocks_why = "already at Matrox's clocks";
        return 1;
    }
    if (opt3 != BIOS_OPT3 || m != 0x09 || n != 0x3C || (p & 7) != 0) {
        mga_clocks_why = "clocks not at the BIOS's first record";
        return 1;
    }
    t0 = sys_time_us();
    while (MGA_RD32(MGAREG_STATUS) & STATUS_DWGENGSTS)
        if (sys_time_us() - t0 > 200000u) {
            mga_clocks_why = "engine busy";
            return 1;
        }
    if (clk_switch(0x02, 0x1B, 0x18, FAST_OPT3, (opt & ~CLK_OPT_BITS) | (FAST_OPT & CLK_OPT_BITS)) != 0) {
        clk_switch(m, n, p, opt3, opt);
        mga_clocks_why = "the PLL did not lock at 252 MHz (BIOS clocks put back)";
        return -1;
    }
    saved->opt = opt;
    saved->opt3 = opt3;
    saved->m = m;
    saved->n = n;
    saved->p = p;
    saved->valid = 1;
    mga_clocks_why = "raised: system PLL 252 MHz, OPTION3 019B8419";
    return 0;
}

void mga_clocks_restore(mga_clocks *saved)
{
    uint32_t t0;
    if (!saved->valid || !mga_mmio)
        return;
    saved->valid = 0;
    t0 = sys_time_us();
    while ((MGA_RD32(MGAREG_STATUS) & STATUS_DWGENGSTS) && sys_time_us() - t0 < 200000u)
        ;
    if (clk_switch(saved->m, saved->n, saved->p, saved->opt3, saved->opt) != 0)
        cfg_w(CLK_OPTION, saved->opt);      /* (no lock: left on the PCI clock, which runs) */
}
