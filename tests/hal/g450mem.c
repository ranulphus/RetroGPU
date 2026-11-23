/* g450mem.c - G450MEM: the G450's memory set up again after a drawing-engine soft reset (bench F, 2026-11-16: one
 * soft reset, MGAREG_RST bit 0, leaves the G450's DDR memory unrefreshed until the next boot).
 *
 *   G450MEM [noreset] [noinit]
 *
 * Logs the memory registers, soft-resets the engine (unless noreset), logs them, then runs Linux matroxfb's
 * g450_memory_init sequence (unless noinit) with the values of the card's PInS v5 block, OPTION2 as the card has
 * it, and logs them again. Only on a G450 (device 0525, revision >= 80h) or a G550 (2527) with a PInS v5 block.
 * Run MODETEST decay=N after it: the memory holds, or it doesn't. HX-STAT g450mem lines. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include <string.h>

#define PCI_OPTION 0x40
#define PCI_OPTION2 0x50
#define PCI_OPTION3 0x54
#define PCI_MEMMISC 0x58
#define REG_MEMRDBK 0x1E44

static uint32_t cfg(int r) { return mga_pci_read32(mga.bus, mga.dev, mga.fn, r); }
static void cfg_w(int r, uint32_t v) { mga_pci_write32(mga.bus, mga.dev, mga.fn, r, v); }
static uint32_t le32(const volatile uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void show(const char *when)
{
    hx_log("HX-STAT g450mem %s option=%08lx option2=%08lx option3=%08lx memmisc=%08lx memrdbk=%08lx maccess=%08lx "
           "status=%08lx", when, (unsigned long)cfg(PCI_OPTION), (unsigned long)cfg(PCI_OPTION2),
           (unsigned long)cfg(PCI_OPTION3), (unsigned long)cfg(PCI_MEMMISC), (unsigned long)MGA_RD32(REG_MEMRDBK),
           (unsigned long)MGA_RD32(MGAREG_MACCESS), (unsigned long)MGA_RD32(MGAREG_STATUS));
}

int main(int argc, char **argv)
{
    const volatile uint8_t *rom, *p;
    uint32_t opt, opt2, mctlwtst, memmisc, memrdbk, maccess, core, o;
    int ddr, dll, emrswen, i, do_reset = 1, do_init = 1;
    uint16_t at;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "noreset")) do_reset = 0;
        else if (!strcmp(argv[i], "noinit")) do_init = 0;
    }
    hx_init(1, argv, "g450mem");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0 ||
        !(mga.device_id == 0x2527 || (mga.device_id == 0x0525 && mga.revision >= 0x80))) {
        hx_test("init", 0, "a G450 or G550 is needed (device 0525 revision 80h or later, or 2527)");
        hx_done(HX_INIT_FAILED);
    }
    rom = (const volatile uint8_t *)sys_real_ptr(0xC000, 0);
    at = (uint16_t)(rom[0x7FFC] | rom[0x7FFD] << 8);
    p = rom + at;
    if (p[0] != 0x2E || p[1] != 0x41 || p[5] != 5 || p[2] < 116) {
        hx_test("pins", 0, "no PInS v5 block (at %04x: %02x %02x len %u ver %u)", at, p[0], p[1], p[2], p[5]);
        hx_done(1);
    }
    opt = le32(p + 48);
    opt2 = le32(p + 52);
    mctlwtst = le32(p + 98);
    memmisc = le32(p + 102);
    memrdbk = le32(p + 106);
    ddr = (p[114] & 0x60) == 0x20;
    dll = (p[115] & 0x02) != 0;
    emrswen = (p[115] & 0x01) != 0;
    maccess = emrswen ? 0x00004000u : 0;
    if (p[115] & 4)
        core = mctlwtst;
    else {
        static const uint8_t xlat[8] = { 0, 1, 5, 6, 7, 5, 2, 3 };
        core = (mctlwtst & ~7u) | xlat[mctlwtst & 7];
    }
    hx_log("HX-STAT g450mem pins opt=%08lx opt2=%08lx mctlwtst=%08lx core=%08lx memmisc=%08lx memrdbk=%08lx ddr=%d dll=%d "
           "emrswen=%d", (unsigned long)opt, (unsigned long)opt2, (unsigned long)mctlwtst, (unsigned long)core,
           (unsigned long)memmisc, (unsigned long)memrdbk, ddr, dll, emrswen);
    show("start");
    if (do_reset) {
        engine_init(1024, 16);
        engine_reset();
        show("after-reset");
    }
    if (do_init) {
        /* Linux matroxfb g450_memory_init, OPTION2 as the card has it */
        uint32_t cur2 = cfg(PCI_OPTION2);
        o = cfg(PCI_OPTION);
        o &= ~0x001F8000u;                          /* refresh off */
        cfg_w(PCI_OPTION, o);
        o = (o & ~0x00207E00u) | (opt & 0x00207E00u);   /* the memory interface */
        cfg_w(PCI_OPTION, o);
        cfg_w(PCI_OPTION2, cur2);
        MGA_WR32_DIRECT(MGAREG_MCTLWTST, mctlwtst);
        cfg_w(PCI_MEMMISC, memmisc & ~0x80000000u);     /* memory clocks off */
        MGA_WR32_DIRECT(REG_MEMRDBK, memrdbk);
        MGA_WR32_DIRECT(MGAREG_MACCESS, maccess);
        cfg_w(PCI_MEMMISC, memmisc | 0x80000000u);      /* memory clocks on */
        sys_delay_us(200);
        if (ddr && (!emrswen || !dll))
            MGA_WR32_DIRECT(REG_MEMRDBK, memrdbk & ~0x1000u);
        MGA_WR32_DIRECT(MGAREG_MACCESS, maccess | 0x8000u);     /* the memory's mode registers (reset) */
        sys_delay_us(200);
        o |= 0x001F8000u & opt;                     /* refresh on */
        cfg_w(PCI_OPTION, o);
        MGA_WR32_DIRECT(MGAREG_PLNWT, 0);
        MGA_WR32_DIRECT(MGAREG_PLNWT, 0xFFFFFFFFu);
        if (mctlwtst != core)
            MGA_WR32_DIRECT(MGAREG_MCTLWTST, core);
        show("after-init");
        engine_init(1024, 16);                      /* (MACCESS as the engine wants it again) */
    }
    hx_test("g450mem", 1, "%s%s; run MODETEST decay=N now", do_reset ? "reset" : "no reset",
            do_init ? ", memory set up" : "");
    hx_done(0);
}
