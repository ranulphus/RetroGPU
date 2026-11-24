/* rprobe.c - the ATI Rage 128 Pro's first step (R0, docs/rage128.md): find the chip on PCI, read its windows and its
 * AGP capability, its configuration, memory, CRTC and engine registers through the register window (BAR2), its
 * PLLs through the clock index, the video BIOS's signature and PLL table, and list every VBE mode its BIOS offers
 * (RPROBE-MODE lines, unfiltered).
 *
 *   RPROBE
 *
 * Reads only, with one exception: the PLL registers are read through CLOCK_CNTL_INDEX (an index write selects one,
 * with the write-enable bit clear), and the index the BIOS left there is put back at the end.
 *
 * Lines: RPROBE-PCI, RPROBE-AGP, RPROBE-REG, RPROBE-MEM, RPROBE-CRTC, RPROBE-PLL, RPROBE-BIOS, RPROBE-VBE,
 * RPROBE-MODE; tests "pci", "regs", "bios", "vbe". Register offsets and the memory and BIOS readings are X.org's
 * xf86-video-r128 driver's (MIT: r128_reg.h, r128_driver.c), printed raw beside each reading for R0 to confirm. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>

#define ATI_VENDOR 0x1002

/* The register window's offsets (r128_reg.h). */
#define R128_CLOCK_CNTL_INDEX   0x0008
#define R128_CLOCK_CNTL_DATA    0x000C
#define R128_BIOS_0_SCRATCH     0x0010
#define R128_GEN_INT_CNTL       0x0040
#define R128_GEN_INT_STATUS     0x0044
#define R128_CRTC_GEN_CNTL      0x0050
#define R128_CRTC_EXT_CNTL      0x0054
#define R128_DAC_CNTL           0x0058
#define R128_CONFIG_CNTL        0x00E0
#define R128_CONFIG_XSTRAP      0x00E4
#define R128_CONFIG_BONDS       0x00E8
#define R128_CONFIG_MEMSIZE     0x00F8
#define R128_CONFIG_APER_0_BASE 0x0100
#define R128_CONFIG_APER_1_BASE 0x0104
#define R128_CONFIG_APER_SIZE   0x0108
#define R128_CONFIG_REG_1_BASE  0x010C
#define R128_CONFIG_REG_APER_SIZE 0x0110
#define R128_MEM_CNTL           0x0140
#define R128_AGP_BASE           0x0170
#define R128_AGP_CNTL           0x0174
#define R128_PC_NGUI_CTLSTAT    0x0184
#define R128_CRTC_H_TOTAL_DISP  0x0200
#define R128_CRTC_V_TOTAL_DISP  0x0208
#define R128_CRTC_OFFSET        0x0224
#define R128_CRTC_OFFSET_CNTL   0x0228
#define R128_CRTC_PITCH         0x022C
#define R128_PM4_BUFFER_CNTL    0x0704
#define R128_PM4_STAT           0x07B8
#define R128_GUI_STAT           0x1740

static const char *r128_name(uint16_t id)
{
    switch (id) {
    case 0x5046: return "Rage 128 Pro PF (AGP 4x)";
    case 0x5041: case 0x5042: case 0x5043: case 0x5044: case 0x5045: case 0x5047: case 0x5048: case 0x5049:
    case 0x504A: case 0x504B: case 0x504C: case 0x504D: case 0x504E: case 0x504F: case 0x5050: case 0x5051:
    case 0x5052: case 0x5053: case 0x5054: case 0x5055: case 0x5056: case 0x5057: case 0x5058:
        return "Rage 128 Pro (P?)";
    case 0x5245: case 0x5246: case 0x5247: case 0x524B: case 0x524C: return "Rage 128 GL/VR (R?)";
    case 0x5345: case 0x5346: case 0x5347: case 0x5348: case 0x534B: case 0x534C: case 0x534D:
        return "Rage 128 4x (S?)";
    case 0x5446: case 0x544C: case 0x5452: case 0x5453: case 0x5454: case 0x5455: return "Rage 128 Pro Ultra (T?)";
    case 0x4C45: case 0x4C46: case 0x4D46: case 0x4D4C: return "Rage 128 Mobility";
    default: return "not a Rage 128";
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

static volatile uint32_t *mmio;
static uint32_t rd(uint32_t off) { return mmio[off / 4]; }

/* A PLL register through the clock index (r128_driver.c's INPLL: the index's low 6 bits, PLL_WR_EN clear). */
static uint32_t rd_pll(int idx)
{
    ((volatile uint8_t *)mmio)[R128_CLOCK_CNTL_INDEX] = (uint8_t)(idx & 0x3F);
    return rd(R128_CLOCK_CNTL_DATA);
}

static unsigned rom16(const uint8_t *rom, unsigned off) { return (unsigned)(rom[off] | (rom[off + 1] << 8)); }
static unsigned long rom32(const uint8_t *rom, unsigned off)
{
    return (unsigned long)rom16(rom, off) | ((unsigned long)rom16(rom, off + 2) << 16);
}

int main(int argc, char **argv)
{
    int bus, dev, found = 0, bx = 0, dx = 0, i, n = 0;
    uint16_t did = 0, list[160];
    uint32_t bar0, bar1, bar2, cmd, memcntl, idx_was;
    sys_rmregs r;
    char oem[64], vend[64], prod[64];

    hx_init(argc, argv, "rprobe");
    for (bus = 0; bus < 4 && !found; bus++)       /* (an AGP card sits behind the AGP bridge: bus 1) */
        for (dev = 0; dev < 32 && !found; dev++) {
            uint32_t id = mga_pci_read32(bus, dev, 0, 0);
            uint32_t cls = mga_pci_read32(bus, dev, 0, 0x08) >> 16;
            if ((id & 0xFFFF) == ATI_VENDOR && cls == 0x0300) {
                found = 1;
                bx = bus;
                dx = dev;
                did = (uint16_t)(id >> 16);
            }
        }
    hx_test("pci", found, "ATI display device on PCI");
    if (!found)
        hx_done(1);
    cmd = mga_pci_read32(bx, dx, 0, 0x04);
    bar0 = mga_pci_read32(bx, dx, 0, 0x10);
    bar1 = mga_pci_read32(bx, dx, 0, 0x14);
    bar2 = mga_pci_read32(bx, dx, 0, 0x18);
    hx_log("RPROBE-PCI bus=%d dev=%d id=1002:%04x (%s) rev=%02x cmd=%04x status=%04x bar0=%08x bar1=%08x bar2=%08x "
           "rom=%08x subsys=%08x irq=%u", bx, dx, did, r128_name(did),
           (unsigned)(mga_pci_read32(bx, dx, 0, 0x08) & 0xFF), (unsigned)(cmd & 0xFFFF), (unsigned)(cmd >> 16),
           (unsigned)bar0, (unsigned)bar1, (unsigned)bar2, (unsigned)mga_pci_read32(bx, dx, 0, 0x30),
           (unsigned)mga_pci_read32(bx, dx, 0, 0x2C), (unsigned)(mga_pci_read32(bx, dx, 0, 0x3C) & 0xFF));
    {
        /* The capability list (status bit 4): AGP's (ID 2) status and command words. */
        uint32_t cap = (cmd >> 16) & 0x10 ? mga_pci_read32(bx, dx, 0, 0x34) & 0xFC : 0;
        int guard = 0;
        hx_log("RPROBE-AGP cap_ptr=%02x (config 0x34 = %08x)", (unsigned)cap, (unsigned)mga_pci_read32(bx, dx, 0, 0x34));
        while (cap && guard++ < 16) {
            uint32_t w = mga_pci_read32(bx, dx, 0, cap);
            if ((w & 0xFF) == 0x02)
                hx_log("RPROBE-AGP cap=%02x version=%u.%u status=%08x command=%08x", (unsigned)cap,
                       (unsigned)((w >> 20) & 0xF), (unsigned)((w >> 16) & 0xF),
                       (unsigned)mga_pci_read32(bx, dx, 0, cap + 4), (unsigned)mga_pci_read32(bx, dx, 0, cap + 8));
            else
                hx_log("RPROBE-AGP cap=%02x id=%02x", (unsigned)cap, (unsigned)(w & 0xFF));
            cap = (w >> 8) & 0xFC;
        }
    }

    /* BAR2: the registers (16 KB). Reads only, but for the clock index (put back). */
    mmio = (volatile uint32_t *)sys_map_phys(bar2 & ~0xFu, 0x4000u);
    if (!mmio) {
        hx_test("regs", 0, "BAR2 not mapped");
        hx_done(1);
    }
    hx_log("RPROBE-REG config_cntl=%08lx xstrap=%08lx bonds=%08lx memsize=%08lx aper0=%08lx aper1=%08lx aper_size=%08lx "
           "reg1=%08lx reg_aper_size=%08lx", (unsigned long)rd(R128_CONFIG_CNTL), (unsigned long)rd(R128_CONFIG_XSTRAP),
           (unsigned long)rd(R128_CONFIG_BONDS), (unsigned long)rd(R128_CONFIG_MEMSIZE),
           (unsigned long)rd(R128_CONFIG_APER_0_BASE), (unsigned long)rd(R128_CONFIG_APER_1_BASE),
           (unsigned long)rd(R128_CONFIG_APER_SIZE), (unsigned long)rd(R128_CONFIG_REG_1_BASE),
           (unsigned long)rd(R128_CONFIG_REG_APER_SIZE));
    hx_log("RPROBE-REG gen_int_cntl=%08lx gen_int_status=%08lx bios0=%08lx agp_base=%08lx agp_cntl=%08lx "
           "ngui_ctlstat=%08lx pm4_buffer_cntl=%08lx pm4_stat=%08lx gui_stat=%08lx",
           (unsigned long)rd(R128_GEN_INT_CNTL), (unsigned long)rd(R128_GEN_INT_STATUS),
           (unsigned long)rd(R128_BIOS_0_SCRATCH), (unsigned long)rd(R128_AGP_BASE), (unsigned long)rd(R128_AGP_CNTL),
           (unsigned long)rd(R128_PC_NGUI_CTLSTAT), (unsigned long)rd(R128_PM4_BUFFER_CNTL),
           (unsigned long)rd(R128_PM4_STAT), (unsigned long)rd(R128_GUI_STAT));
    hx_test("regs", rd(R128_CONFIG_MEMSIZE) != 0 && rd(R128_CONFIG_MEMSIZE) != 0xFFFFFFFFu, "CONFIG_MEMSIZE = %08lx",
            (unsigned long)rd(R128_CONFIG_MEMSIZE));
    memcntl = rd(R128_MEM_CNTL);
    {
        /* r128_driver.c's reading of MEM_CNTL bits 1-0 (tentative: R0 confirms it against the card). */
        static const char *const ram[4] = { "128-bit SDR SGRAM 1:1", "64-bit SDR SGRAM 1:1", "64-bit SDR SGRAM 2:1",
                                            "64-bit DDR SGRAM" };
        hx_log("RPROBE-MEM size=%lu KB (memsize) mem_cntl=%08lx type=\"%s\" (bits 1-0 = %lu)",
               (unsigned long)(rd(R128_CONFIG_MEMSIZE) / 1024u), (unsigned long)memcntl, ram[memcntl & 3],
               (unsigned long)(memcntl & 3));
    }
    hx_log("RPROBE-CRTC gen_cntl=%08lx ext_cntl=%08lx dac_cntl=%08lx h_total_disp=%08lx v_total_disp=%08lx "
           "offset=%08lx offset_cntl=%08lx pitch=%08lx", (unsigned long)rd(R128_CRTC_GEN_CNTL),
           (unsigned long)rd(R128_CRTC_EXT_CNTL), (unsigned long)rd(R128_DAC_CNTL),
           (unsigned long)rd(R128_CRTC_H_TOTAL_DISP), (unsigned long)rd(R128_CRTC_V_TOTAL_DISP),
           (unsigned long)rd(R128_CRTC_OFFSET), (unsigned long)rd(R128_CRTC_OFFSET_CNTL),
           (unsigned long)rd(R128_CRTC_PITCH));
    idx_was = rd(R128_CLOCK_CNTL_INDEX);
    {
        char line[400];
        int len = sprintf(line, "RPROBE-PLL index_was=%08lx", (unsigned long)idx_was);
        for (i = 0; i < 0x20; i++)
            len += sprintf(line + len, " %02x=%08lx", i, (unsigned long)rd_pll(i));
        hx_log("%s", line);
    }
    ((volatile uint8_t *)mmio)[R128_CLOCK_CNTL_INDEX] = (uint8_t)(idx_was & 0xFF);
    sys_unmap_phys((void *)mmio, 0x4000u);

    /* The video BIOS (the shadow at C000:0): the signature, its name, and the PLL table as r128_driver.c reads it
     * (the header at word 0x48; the PLL block at the header's +0x30; xclk at +0x08 and the reference at +0x0E, in
     * 10 kHz; the reference divider +0x10; the PLL's range +0x12, +0x16). */
    {
        const uint8_t *rom = (const uint8_t *)sys_real_ptr(0xC000, 0);
        int sig = rom && rom[0] == 0x55 && rom[1] == 0xAA, ati = 0, at = -1;
        char name[80];
        int k = 0;
        for (i = 0x20; sig && i < 0x80 && at < 0; i++)     /* (ATI's "761295520", near 0x30) */
            if (!memcmp(rom + i, "761295520", 9))
                at = i, ati = 1;
        unsigned hdr = sig ? rom16(rom, 0x48) : 0, pll = 0;
        if (sig) {
            for (i = 0; i < 0x7F00 && !k; i++)
                if (!memcmp(rom + i, "RAGE", 4) || !memcmp(rom + i, "Rage", 4)) {
                    int j = i;
                    while (j > 0 && rom[j - 1] >= 32 && rom[j - 1] < 127 && i - j < 24)
                        j--;
                    for (; k < (int)sizeof name - 1 && rom[j + k] >= 32 && rom[j + k] < 127; k++)
                        name[k] = (char)rom[j + k];
                }
        }
        name[k] = 0;
        if (hdr && hdr < 0x7F00)
            pll = rom16(rom, hdr + 0x30);
        hx_log("RPROBE-BIOS sig=%d ati=%d at=%02x size=%u KB header=%04x pll_block=%04x name=\"%s\"", sig, ati,
               (unsigned)(at < 0 ? 0 : at), sig ? rom[2] * 512u / 1024u : 0u, hdr, pll, name);
        if (pll && pll < 0x7F00)
            hx_log("RPROBE-BIOS-PLL xclk=%u ref_freq=%u ref_div=%u min=%lu max=%lu (10 kHz units)",
                   rom16(rom, pll + 0x08), rom16(rom, pll + 0x0E), rom16(rom, pll + 0x10), rom32(rom, pll + 0x12),
                   rom32(rom, pll + 0x16));
        hx_test("bios", ati, "ATI signature in the video BIOS");
    }

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
        hx_log("RPROBE-VBE ver=%x.%x mem=%uKB modes=%d oem=\"%s\" vendor=\"%s\" product=\"%s\"", ver >> 8, ver & 0xFF,
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
        hx_log("RPROBE-MODE m=%03x %ux%ux%u model=%u attr=%02x pitch=%u lfb=%08lx", list[i],
               (unsigned)(b[0x12] | (b[0x13] << 8)), (unsigned)(b[0x14] | (b[0x15] << 8)), b[0x19], b[0x1B], b[0],
               (unsigned)(b[0x10] | (b[0x11] << 8)),
               (unsigned long)((uint32_t)b[0x28] | ((uint32_t)b[0x29] << 8) | ((uint32_t)b[0x2A] << 16) |
                               ((uint32_t)b[0x2B] << 24)));
    }
    hx_done(0);
}
