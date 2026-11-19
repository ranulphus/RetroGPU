/* nprobe.c - the Riva TNT2's step T0 (docs/tnt2.md): find the chip on PCI, read its windows, its ID, memory,
 * straps and clock registers through BAR0 (reads only: nothing is written to the chip), and list every VBE mode
 * its BIOS offers (NPROBE-MODE lines, unfiltered).
 *
 *   NPROBE
 *
 * Lines: NPROBE-PCI, NPROBE-REG, NPROBE-MEM, NPROBE-CLK, NPROBE-VBE, NPROBE-MODE; tests "pci", "id", "vbe".
 * Register offsets are the NV4/NV5's (envytools' rnndb; X.org's xf86-video-nv, riva_hw.c); the memory size and
 * bus width are decoded as riva_hw.c reads them, and printed raw beside, for T0 to confirm. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>

#define NV_VENDOR 0x10DE

static const char *nv_name(uint16_t id)
{
    switch (id) {
    case 0x0020: return "Riva TNT (NV4)";
    case 0x0028: return "Riva TNT2 (NV5)";
    case 0x0029: return "Riva TNT2 Ultra (NV5)";
    case 0x002C: return "Vanta (NV5)";
    case 0x002D: return "Riva TNT2 M64 (NV5)";
    case 0x00A0: return "Aladdin TNT2 (NV5, integrated)";
    default:     return "not a TNT/TNT2";
    }
}

static uint8_t *vbuf;
static uint16_t vseg;

static int vbe(sys_rmregs *r)
{
    if (sys_rm_int(0x10, r) < 0)
        return -1;
    return (r->eax & 0xFFFF) == 0x004F ? 0 : -1;
}

static const char *far_str(const uint8_t *p, char *out, int n)
{
    uint16_t off = (uint16_t)(p[0] | (p[1] << 8)), seg = (uint16_t)(p[2] | (p[3] << 8));
    const char *s = (seg || off) ? (const char *)sys_real_ptr(seg, off) : NULL;
    int i = 0;
    if (s)
        for (; i < n - 1 && s[i] && s[i] >= 32 && s[i] < 127; i++)
            out[i] = s[i];
    out[i] = 0;
    return out;
}

/* A PLL's frequency in kHz from its register (NV4/NV5: M bits 7-0, N bits 15-8, P bits 18-16) and the crystal. */
static unsigned pll_khz(uint32_t v, unsigned xtal_khz)
{
    unsigned m = v & 0xFF, n = (v >> 8) & 0xFF, p = (v >> 16) & 7;
    return m ? (unsigned)(((unsigned long)xtal_khz * n / m) >> p) : 0;
}

int main(int argc, char **argv)
{
    int bus, dev, found = 0, bx = 0, dx = 0, i, n = 0;
    uint16_t did = 0, list[160];
    uint32_t bar0, bar1, cmd, boot0, pfb0, straps, xtal;
    volatile uint32_t *r32;
    sys_rmregs r;
    char oem[64], vend[64], prod[64];

    hx_init(argc, argv, "nprobe");
    for (bus = 0; bus < 4 && !found; bus++)       /* (an AGP card sits behind the AGP bridge: bus 1) */
        for (dev = 0; dev < 32 && !found; dev++) {
            uint32_t id = mga_pci_read32(bus, dev, 0, 0);
            if ((id & 0xFFFF) == NV_VENDOR) {
                found = 1;
                bx = bus;
                dx = dev;
                did = (uint16_t)(id >> 16);
            }
        }
    hx_test("pci", found, "NVIDIA device on PCI");
    if (!found)
        hx_done(1);
    cmd = mga_pci_read32(bx, dx, 0, 0x04);
    bar0 = mga_pci_read32(bx, dx, 0, 0x10);
    bar1 = mga_pci_read32(bx, dx, 0, 0x14);
    hx_log("NPROBE-PCI bus=%d dev=%d id=10de:%04x (%s) rev=%02x cmd=%04x bar0=%08x bar1=%08x subsys=%08x irq=%u", bx,
           dx, did, nv_name(did), (unsigned)(mga_pci_read32(bx, dx, 0, 0x08) & 0xFF), (unsigned)(cmd & 0xFFFF),
           (unsigned)bar0, (unsigned)bar1, (unsigned)mga_pci_read32(bx, dx, 0, 0x2C),
           (unsigned)(mga_pci_read32(bx, dx, 0, 0x3C) & 0xFF));

    /* BAR0: the registers (16 MB). Reads only. */
    r32 = (volatile uint32_t *)sys_map_phys(bar0 & ~0xFu, 0x01000000u);
    if (!r32) {
        hx_test("id", 0, "BAR0 not mapped");
        hx_done(1);
    }
    boot0 = r32[0x000000 / 4];                  /* PMC_BOOT_0: the chip's ID and revision */
    pfb0 = r32[0x100000 / 4];                   /* PFB_BOOT_0: memory amount and width */
    straps = r32[0x101000 / 4];                 /* PEXTDEV_BOOT_0: the board's straps */
    hx_log("NPROBE-REG pmc_boot0=%08lx pmc_enable=%08lx pfb_boot0=%08lx pfb_cfg0=%08lx pfb_cfg1=%08lx straps=%08lx "
           "pgraph_status=%08lx pcrtc_start=%08lx", (unsigned long)boot0, (unsigned long)r32[0x000200 / 4],
           (unsigned long)pfb0, (unsigned long)r32[0x100200 / 4], (unsigned long)r32[0x100204 / 4],
           (unsigned long)straps, (unsigned long)r32[0x400700 / 4], (unsigned long)r32[0x600800 / 4]);
    hx_test("id", boot0 != 0 && boot0 != 0xFFFFFFFFu, "PMC_BOOT_0 = %08lx", (unsigned long)boot0);
    {
        /* riva_hw.c's reading for NV4/NV5: bits 1-0 the amount (0: 32 MB on NV5, 1: 4, 2: 8, 3: 16 MB), bit 2 a
         * 128-bit bus. Tentative: T0 confirms it against the card's known size. */
        static const unsigned mb[4] = { 32, 4, 8, 16 };
        hx_log("NPROBE-MEM amount=%u MB (pfb_boot0 bits 1-0 = %lu) bus=%u bits (bit 2 = %lu)", mb[pfb0 & 3],
               (unsigned long)(pfb0 & 3), (pfb0 & 4) ? 128u : 64u, (unsigned long)((pfb0 >> 2) & 1));
    }
    xtal = (straps & 0x40) ? 14318 : 13500;     /* (the strap's crystal: 14.318 or 13.5 MHz) */
    hx_log("NPROBE-CLK xtal=%lu kHz nvpll=%08lx (%u kHz) mpll=%08lx (%u kHz) vpll=%08lx (%u kHz)", (unsigned long)xtal,
           (unsigned long)r32[0x680500 / 4], pll_khz(r32[0x680500 / 4], xtal), (unsigned long)r32[0x680504 / 4],
           pll_khz(r32[0x680504 / 4], xtal), (unsigned long)r32[0x680508 / 4], pll_khz(r32[0x680508 / 4], xtal));
    sys_unmap_phys(r32, 0x01000000u);

    /* VBE: version, names, memory, and every mode the BIOS lists. */
    vbuf = (uint8_t *)sys_dos_alloc(1024, &vseg);
    if (!vbuf)
        hx_done(1);
    memset(vbuf, 0, 512);
    memcpy(vbuf, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00;
    r.es = vseg;
    r.edi = 0;
    if (vbe(&r) < 0 || memcmp(vbuf, "VESA", 4) != 0) {
        hx_test("vbe", 0, "no VBE BIOS");
        hx_done(1);
    }
    {
        uint16_t ver = (uint16_t)(vbuf[4] | (vbuf[5] << 8));
        const uint8_t *ml = vbuf + 14;
        uint16_t off = (uint16_t)(ml[0] | (ml[1] << 8)), seg = (uint16_t)(ml[2] | (ml[3] << 8));
        const uint16_t *modes = (const uint16_t *)sys_real_ptr(seg, off);
        for (n = 0; modes && n < 160 && modes[n] != 0xFFFF; n++)
            list[n] = modes[n];
        hx_log("NPROBE-VBE ver=%x.%x mem=%uKB modes=%d oem=\"%s\" vendor=\"%s\" product=\"%s\"", ver >> 8, ver & 0xFF,
               (unsigned)(vbuf[18] | (vbuf[19] << 8)) * 64u, n, far_str(vbuf + 6, oem, sizeof oem),
               ver >= 0x200 ? far_str(vbuf + 22, vend, sizeof vend) : "",
               ver >= 0x200 ? far_str(vbuf + 26, prod, sizeof prod) : "");
        hx_test("vbe", 1, "VBE %x.%x", ver >> 8, ver & 0xFF);
    }
    for (i = 0; i < n; i++) {
        uint8_t *b = vbuf + 512;
        memset(b, 0, 256);
        memset(&r, 0, sizeof r);
        r.eax = 0x4F01;
        r.ecx = list[i];
        r.es = vseg;
        r.edi = 512;
        if (vbe(&r) < 0)
            continue;
        hx_log("NPROBE-MODE m=%03x %ux%ux%u model=%u attr=%02x pitch=%u lfb=%08lx", list[i],
               (unsigned)(b[0x12] | (b[0x13] << 8)), (unsigned)(b[0x14] | (b[0x15] << 8)), b[0x19], b[0x1B], b[0],
               (unsigned)(b[0x10] | (b[0x11] << 8)),
               (unsigned long)((uint32_t)b[0x28] | ((uint32_t)b[0x29] << 8) | ((uint32_t)b[0x2A] << 16) |
                               ((uint32_t)b[0x2B] << 24)));
    }
    hx_done(0);
}
