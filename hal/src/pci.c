/* pci.c - PCI configuration mechanism #1 and Matrox discovery. */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include <string.h>

mga_chip mga;
volatile uint8_t *mga_mmio;
volatile uint8_t *mga_fb;

#define PCI_ADDR(b, d, f, r) (0x80000000u | ((uint32_t)(b) << 16) | ((uint32_t)(d) << 11) | \
                              ((uint32_t)(f) << 8) | ((uint32_t)(r) & 0xFC))

uint32_t mga_pci_read32(int bus, int dev, int fn, int reg)
{
    sys_outl(0xCF8, PCI_ADDR(bus, dev, fn, reg));
    return sys_inl(0xCFC);
}

void mga_pci_write32(int bus, int dev, int fn, int reg, uint32_t v)
{
    sys_outl(0xCF8, PCI_ADDR(bus, dev, fn, reg));
    sys_outl(0xCFC, v);
}

static const struct { uint16_t id; mga_family fam; const char *name; } known[] = {
    { 0x1000, MGA_FAMILY_G100, "MGA-G100 (PCI)" },
    { 0x1001, MGA_FAMILY_G100, "MGA-G100 (AGP)" },
    { 0x0520, MGA_FAMILY_G200, "MGA-G200 (PCI)" },
    { 0x0521, MGA_FAMILY_G200, "MGA-G200 (AGP)" },
    { 0x0525, MGA_FAMILY_G400, "MGA-G400/G450" },
    { 0x0522, MGA_FAMILY_G200E, "G200e" },
    { 0x0530, MGA_FAMILY_G200E, "G200EV" },
    { 0x0532, MGA_FAMILY_G200E, "G200eW" },
    { 0x0533, MGA_FAMILY_G200E, "G200EH" },
    { 0x0534, MGA_FAMILY_G200E, "G200eR2" },
};

const char *mga_family_name(mga_family f)
{
    switch (f) {
    case MGA_FAMILY_G100: return "G100";
    case MGA_FAMILY_G200: return "G200";
    case MGA_FAMILY_G400: return "G400";
    case MGA_FAMILY_G200E: return "G200e";
    default: return "none";
    }
}

void mga_chip_caps(mga_chip *c);   /* chip.c */

static void g450_memory_prepare(mga_chip *c);

int mga_find(mga_chip *c)
{
    int bus, dev, fn;
    memset(c, 0, sizeof *c);
    for (bus = 0; bus < 256; bus++) {    /* cards behind bridges sit on high buses */
        for (dev = 0; dev < 32; dev++) {
            for (fn = 0; fn < 8; fn++) {
                uint32_t id = mga_pci_read32(bus, dev, fn, 0);
                unsigned i;
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (fn == 0)
                        break;
                    continue;
                }
                if ((id & 0xFFFF) != 0x102B)
                    goto next;
                for (i = 0; i < MGA_ARRAY_LEN(known); i++) {
                    if (known[i].id == (id >> 16)) {
                        c->device_id = (uint16_t)(id >> 16);
                        c->family = known[i].fam;
                        c->name = known[i].name;
                        c->bus = (uint8_t)bus; c->dev = (uint8_t)dev; c->fn = (uint8_t)fn;
                        c->revision = (uint8_t)mga_pci_read32(bus, dev, fn, 8);
                        c->subsys = mga_pci_read32(bus, dev, fn, 0x2C);
                        c->option = mga_pci_read32(bus, dev, fn, 0x40);
                        /* "New BAR" layout: BAR0 framebuffer, BAR1 MMIO, BAR2 ILOAD. */
                        c->fb_phys = mga_pci_read32(bus, dev, fn, 0x10) & ~0xFu;
                        c->mmio_phys = mga_pci_read32(bus, dev, fn, 0x14) & ~0xFu;
                        c->iload_phys = mga_pci_read32(bus, dev, fn, 0x18) & ~0xFu;
                        if (c->family == MGA_FAMILY_G400 && c->revision >= 0x80) {
                            c->name = "MGA-G450";
                            g450_memory_prepare(c);
                        }
                        mga_chip_caps(c);
                        return 0;
                    }
                }
            next:
                if (fn == 0 && !(mga_pci_read32(bus, dev, 0, 0x0C) & 0x00800000))
                    break;               /* single-function device */
            }
        }
    }
    return -1;
}

/* ---- The G450's memory after a soft reset (bench F, 2026-11-16) -------------
 * A drawing-engine soft reset (MGAREG_RST, engine_reset) leaves a G450's DDR
 * memory un-initialised: nothing written holds for more than a second or two,
 * until the next boot (bench F: writes to 8-byte-aligned words read back 0,
 * all of VRAM changed within 3 s). The G400's SGRAM doesn't mind. The memory
 * registers themselves survive the reset; the chips need their set-up again,
 * as Linux matroxfb's g450_memory_init does at start, with the PInS v5
 * block's values (OPTION2 as the card has it). mga_find reads them for a G450
 * and points mga_memory_reinit at the sequence, which engine_reset runs. No
 * PInS v5 block (or no BIOS to read): no hook, and a reset stays unsafe. */
static struct { uint32_t opt, mctlwtst, core, memmisc, memrdbk, maccess; int ddr, dll, emrswen; } g450mem;
static mga_chip *g450chip;

static uint32_t le32b(const volatile uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void g450_memory_reinit(void)
{
    mga_chip *c = g450chip;
    uint32_t o, opt2;
    if (!c || !mga_mmio)
        return;
    opt2 = mga_pci_read32(c->bus, c->dev, c->fn, 0x50);
    o = mga_pci_read32(c->bus, c->dev, c->fn, 0x40) & ~0x001F8000u;     /* refresh off */
    mga_pci_write32(c->bus, c->dev, c->fn, 0x40, o);
    o = (o & ~0x00207E00u) | (g450mem.opt & 0x00207E00u);               /* the memory interface */
    mga_pci_write32(c->bus, c->dev, c->fn, 0x40, o);
    mga_pci_write32(c->bus, c->dev, c->fn, 0x50, opt2);
    MGA_WR32_DIRECT(MGAREG_MCTLWTST, g450mem.mctlwtst);
    mga_pci_write32(c->bus, c->dev, c->fn, 0x58, g450mem.memmisc & ~0x80000000u);   /* memory clocks off */
    MGA_WR32_DIRECT(MGAREG_MEMRDBK, g450mem.memrdbk);
    MGA_WR32_DIRECT(MGAREG_MACCESS, g450mem.maccess);
    mga_pci_write32(c->bus, c->dev, c->fn, 0x58, g450mem.memmisc | 0x80000000u);    /* memory clocks on */
    sys_delay_us(200);
    if (g450mem.ddr && (!g450mem.emrswen || !g450mem.dll))
        MGA_WR32_DIRECT(MGAREG_MEMRDBK, g450mem.memrdbk & ~0x1000u);
    MGA_WR32_DIRECT(MGAREG_MACCESS, g450mem.maccess | 0x8000u);         /* the chips' mode registers */
    sys_delay_us(200);
    o |= 0x001F8000u & g450mem.opt;                                     /* refresh on */
    mga_pci_write32(c->bus, c->dev, c->fn, 0x40, o);
    MGA_WR32_DIRECT(MGAREG_PLNWT, 0);
    MGA_WR32_DIRECT(MGAREG_PLNWT, 0xFFFFFFFFu);
    if (g450mem.mctlwtst != g450mem.core)
        MGA_WR32_DIRECT(MGAREG_MCTLWTST, g450mem.core);
    mga_g450_reinits++;
}

static void g450_memory_prepare(mga_chip *c)
{
    const volatile uint8_t *rom = (const volatile uint8_t *)sys_real_ptr(0xC000, 0), *p;
    uint16_t at;
    if (!rom || rom[0] != 0x55 || rom[1] != 0xAA)
        return;                         /* (no video BIOS here: Loop C, the host) */
    at = (uint16_t)(rom[0x7FFC] | rom[0x7FFD] << 8);
    if (at < 0x40 || at > 0x7F00)
        return;
    p = rom + at;
    if (p[0] != 0x2E || p[1] != 0x41 || p[5] != 5 || p[2] < 116)
        return;
    g450mem.opt = le32b(p + 48);
    g450mem.mctlwtst = le32b(p + 98);
    g450mem.memmisc = le32b(p + 102);
    g450mem.memrdbk = le32b(p + 106);
    g450mem.ddr = (p[114] & 0x60) == 0x20;
    g450mem.dll = (p[115] & 0x02) != 0;
    g450mem.emrswen = (p[115] & 0x01) != 0;
    g450mem.maccess = g450mem.emrswen ? 0x00004000u : 0;
    if (p[115] & 4)
        g450mem.core = g450mem.mctlwtst;
    else {
        static const uint8_t xlat[8] = { 0, 1, 5, 6, 7, 5, 2, 3 };
        g450mem.core = (g450mem.mctlwtst & ~7u) | xlat[g450mem.mctlwtst & 7];
    }
    g450chip = c;
    mga_memory_reinit = g450_memory_reinit;
}

int mga_map(mga_chip *c)
{
    uint32_t cmd = mga_pci_read32(c->bus, c->dev, c->fn, 4);
    if ((cmd & 0x6) != 0x6)          /* memory space + bus master */
        mga_pci_write32(c->bus, c->dev, c->fn, 4, cmd | 0x6);
    mga_mmio = (volatile uint8_t *)sys_map_phys(c->mmio_phys, 0x4000);
    if (!mga_mmio)
        return -1;
    if (!c->fb_size)
        c->fb_size = 16u << 20;
    mga_fb = (volatile uint8_t *)sys_map_phys(c->fb_phys, c->fb_size);
    if (!mga_fb)
        return -1;
    return 0;
}

void mga_unmap(void)
{
    if (mga_fb)
        sys_unmap_phys(mga_fb, mga.fb_size);
    if (mga_mmio)
        sys_unmap_phys(mga_mmio, 0x4000);
    mga_fb = NULL;
    mga_mmio = NULL;
}

/* 86Box's unit-tester device answers on a configurable port; real
 * hardware leaves the port floating (0xFF). Writes go to POST port 0x80,
 * which is harmless on real machines. */
int mga_detect_emulator(void)
{
    static const char magic[] = "86Box";
    int i, present;
    for (i = 0; i < 5; i++)
        sys_outb(0x80, (uint8_t)magic[i]);
    sys_outb(0x80, 0x80);
    sys_outb(0x80, 0x0E);
    present = sys_inb(0x0E80) != 0xFF;
    return present;
}
