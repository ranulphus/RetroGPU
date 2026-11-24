/* dprobe.c - the ATI Radeon 7000 / VE's first step (RD0, docs/radeon.md): find the chip on PCI, read its windows
 * and its AGP capability, its configuration, memory, CRTC, engine and command processor registers through the
 * register window (BAR2, 64 KB), a few PLLs through the clock index, the video BIOS's signature and name, and list
 * every VBE mode its BIOS offers (DPROBE-MODE lines, unfiltered). RPROBE (tests/r128/rprobe.c), from which it is
 * made.
 *
 *   DPROBE [nopll]
 *
 * Reads only, with one exception: the PLL registers are read through CLOCK_CNTL_INDEX (an index write selects one,
 * with the write-enable bit clear), and the index the BIOS left there is put back at the end. After every PLL
 * access 5 ms pass: on the RV100, RS100 and RS200 "the chip could hang on a subsequent access" otherwise (X.org's
 * RADEONPllErrataAfterData, CHIP_ERRATA_PLL_DELAY). nopll: no PLL reads at all.
 *
 * Lines: DPROBE-PCI, DPROBE-AGP, DPROBE-REG, DPROBE-MEM, DPROBE-CRTC, DPROBE-CP, DPROBE-PLL, DPROBE-BIOS,
 * DPROBE-VBE, DPROBE-MODE; tests "pci", "regs", "bios", "vbe". Register offsets are X.org's xf86-video-ati
 * driver's (MIT: radeon_reg.h), printed raw beside each reading for RD0 to confirm. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>

#define ATI_VENDOR 0x1002

/* The register window's offsets (radeon_reg.h). */
#define RADEON_CLOCK_CNTL_INDEX   0x0008
#define RADEON_CLOCK_CNTL_DATA    0x000C
#define RADEON_BIOS_0_SCRATCH     0x0010
#define RADEON_BUS_CNTL           0x0030
#define RADEON_GEN_INT_CNTL       0x0040
#define RADEON_GEN_INT_STATUS     0x0044
#define RADEON_CRTC_GEN_CNTL      0x0050
#define RADEON_CRTC_EXT_CNTL      0x0054
#define RADEON_DAC_CNTL           0x0058
#define RADEON_CONFIG_CNTL        0x00E0
#define RADEON_CONFIG_XSTRAP      0x00E4
#define RADEON_CONFIG_BONDS       0x00E8
#define RADEON_RBBM_SOFT_RESET    0x00F0
#define RADEON_CONFIG_MEMSIZE     0x00F8
#define RADEON_CONFIG_APER_0_BASE 0x0100
#define RADEON_CONFIG_APER_1_BASE 0x0104
#define RADEON_CONFIG_APER_SIZE   0x0108
#define RADEON_CONFIG_REG_1_BASE  0x010C
#define RADEON_CONFIG_REG_APER_SIZE 0x0110
#define RADEON_HOST_PATH_CNTL     0x0130
#define RADEON_MEM_CNTL           0x0140
#define RADEON_MC_FB_LOCATION     0x0148
#define RADEON_MC_AGP_LOCATION    0x014C
#define RADEON_MEM_SDRAM_MODE_REG 0x0158
#define RADEON_AGP_BASE           0x0170
#define RADEON_AGP_CNTL           0x0174
#define RADEON_AIC_CNTL           0x01D0
#define RADEON_CRTC_H_TOTAL_DISP  0x0200
#define RADEON_CRTC_V_TOTAL_DISP  0x0208
#define RADEON_CRTC_OFFSET        0x0224
#define RADEON_CRTC_OFFSET_CNTL   0x0228
#define RADEON_CRTC_PITCH         0x022C
#define RADEON_DISPLAY_BASE_ADDR  0x023C
#define RADEON_CP_RB_CNTL         0x0704
#define RADEON_CP_CSQ_CNTL        0x0740
#define RADEON_CP_CSQ_STAT        0x07F8
#define RADEON_RBBM_STATUS        0x0E40
#define RADEON_SE_CNTL_STATUS     0x2140
#define RADEON_RB3D_DSTCACHE_CTLSTAT 0x325C
#define RADEON_MMIO_BYTES         0x10000u

/* (xf86-video-ati's chip list: the R100 family's device IDs) */
static const char *radeon_name(uint16_t id)
{
    switch (id) {
    case 0x5159: return "Radeon RV100 QY (7000/VE)";
    case 0x515A: return "Radeon RV100 QZ (7000/VE)";
    case 0x5144: case 0x5145: case 0x5146: case 0x5147: return "Radeon R100 (QD-QG)";
    case 0x5157: return "Radeon RV200 QW (7500)";
    case 0x5158: return "Radeon RV200 QX (7500)";
    case 0x4C59: case 0x4C5A: return "Radeon Mobility M6";
    case 0x4C57: case 0x4C58: return "Radeon Mobility M7";
    case 0x4136: case 0x4336: return "Radeon IGP 320 (RS100)";
    case 0x4137: case 0x4337: case 0x4237: case 0x4437: return "Radeon IGP 330/340/350 (RS200)";
    default: return "not an R100-family Radeon known here";
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

/* A PLL register through the clock index (radeon_driver.c's INPLL: the index's low 6 bits, PLL_WR_EN clear), then
 * the RV100's 5 ms. */
static uint32_t rd_pll(int idx)
{
    uint32_t v;
    ((volatile uint8_t *)mmio)[RADEON_CLOCK_CNTL_INDEX] = (uint8_t)(idx & 0x3F);
    v = rd(RADEON_CLOCK_CNTL_DATA);
    sys_delay_us(5000);
    return v;
}

static unsigned rom16(const uint8_t *rom, unsigned off) { return (unsigned)(rom[off] | (rom[off + 1] << 8)); }

int main(int argc, char **argv)
{
    int bus, dev, found = 0, bx = 0, dx = 0, i, n = 0, nopll = 0;
    uint16_t did = 0, list[160];
    uint32_t bar0, bar1, bar2, cmd, idx_was;
    sys_rmregs r;
    char oem[64], vend[64], prod[64];

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "nopll"))
            nopll = 1;
    hx_init(1, argv, "dprobe");                 /* (its own arguments only) */
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
    hx_log("DPROBE-PCI bus=%d dev=%d id=1002:%04x (%s) rev=%02x cmd=%04x status=%04x bar0=%08x bar1=%08x bar2=%08x "
           "rom=%08x subsys=%08x irq=%u", bx, dx, did, radeon_name(did),
           (unsigned)(mga_pci_read32(bx, dx, 0, 0x08) & 0xFF), (unsigned)(cmd & 0xFFFF), (unsigned)(cmd >> 16),
           (unsigned)bar0, (unsigned)bar1, (unsigned)bar2, (unsigned)mga_pci_read32(bx, dx, 0, 0x30),
           (unsigned)mga_pci_read32(bx, dx, 0, 0x2C), (unsigned)(mga_pci_read32(bx, dx, 0, 0x3C) & 0xFF));
    {
        /* The capability list (status bit 4): AGP's (ID 2) status and command words. */
        uint32_t cap = (cmd >> 16) & 0x10 ? mga_pci_read32(bx, dx, 0, 0x34) & 0xFC : 0;
        int guard = 0;
        hx_log("DPROBE-AGP cap_ptr=%02x (config 0x34 = %08x)", (unsigned)cap, (unsigned)mga_pci_read32(bx, dx, 0, 0x34));
        while (cap && guard++ < 16) {
            uint32_t w = mga_pci_read32(bx, dx, 0, cap);
            if ((w & 0xFF) == 0x02)
                hx_log("DPROBE-AGP cap=%02x version=%u.%u status=%08x command=%08x", (unsigned)cap,
                       (unsigned)((w >> 20) & 0xF), (unsigned)((w >> 16) & 0xF),
                       (unsigned)mga_pci_read32(bx, dx, 0, cap + 4), (unsigned)mga_pci_read32(bx, dx, 0, cap + 8));
            else
                hx_log("DPROBE-AGP cap=%02x id=%02x", (unsigned)cap, (unsigned)(w & 0xFF));
            cap = (w >> 8) & 0xFC;
        }
    }

    /* BAR2: the registers (64 KB). Reads only, but for the clock index (put back). */
    mmio = (volatile uint32_t *)sys_map_phys(bar2 & ~0xFu, RADEON_MMIO_BYTES);
    if (!mmio) {
        hx_test("regs", 0, "BAR2 not mapped");
        hx_done(1);
    }
    hx_log("DPROBE-REG config_cntl=%08lx xstrap=%08lx bonds=%08lx memsize=%08lx aper0=%08lx aper1=%08lx aper_size=%08lx "
           "reg1=%08lx reg_aper_size=%08lx", (unsigned long)rd(RADEON_CONFIG_CNTL), (unsigned long)rd(RADEON_CONFIG_XSTRAP),
           (unsigned long)rd(RADEON_CONFIG_BONDS), (unsigned long)rd(RADEON_CONFIG_MEMSIZE),
           (unsigned long)rd(RADEON_CONFIG_APER_0_BASE), (unsigned long)rd(RADEON_CONFIG_APER_1_BASE),
           (unsigned long)rd(RADEON_CONFIG_APER_SIZE), (unsigned long)rd(RADEON_CONFIG_REG_1_BASE),
           (unsigned long)rd(RADEON_CONFIG_REG_APER_SIZE));
    hx_log("DPROBE-REG bus_cntl=%08lx gen_int_cntl=%08lx gen_int_status=%08lx bios0=%08lx host_path=%08lx "
           "agp_base=%08lx agp_cntl=%08lx aic_cntl=%08lx", (unsigned long)rd(RADEON_BUS_CNTL),
           (unsigned long)rd(RADEON_GEN_INT_CNTL), (unsigned long)rd(RADEON_GEN_INT_STATUS),
           (unsigned long)rd(RADEON_BIOS_0_SCRATCH), (unsigned long)rd(RADEON_HOST_PATH_CNTL),
           (unsigned long)rd(RADEON_AGP_BASE), (unsigned long)rd(RADEON_AGP_CNTL), (unsigned long)rd(RADEON_AIC_CNTL));
    hx_test("regs", rd(RADEON_CONFIG_MEMSIZE) != 0 && rd(RADEON_CONFIG_MEMSIZE) != 0xFFFFFFFFu, "CONFIG_MEMSIZE = %08lx",
            (unsigned long)rd(RADEON_CONFIG_MEMSIZE));
    hx_log("DPROBE-MEM size=%lu KB (memsize) mem_cntl=%08lx sdram_mode=%08lx mc_fb_location=%08lx mc_agp_location=%08lx "
           "display_base=%08lx", (unsigned long)(rd(RADEON_CONFIG_MEMSIZE) / 1024u), (unsigned long)rd(RADEON_MEM_CNTL),
           (unsigned long)rd(RADEON_MEM_SDRAM_MODE_REG), (unsigned long)rd(RADEON_MC_FB_LOCATION),
           (unsigned long)rd(RADEON_MC_AGP_LOCATION), (unsigned long)rd(RADEON_DISPLAY_BASE_ADDR));
    hx_log("DPROBE-CRTC gen_cntl=%08lx ext_cntl=%08lx dac_cntl=%08lx h_total_disp=%08lx v_total_disp=%08lx "
           "offset=%08lx offset_cntl=%08lx pitch=%08lx", (unsigned long)rd(RADEON_CRTC_GEN_CNTL),
           (unsigned long)rd(RADEON_CRTC_EXT_CNTL), (unsigned long)rd(RADEON_DAC_CNTL),
           (unsigned long)rd(RADEON_CRTC_H_TOTAL_DISP), (unsigned long)rd(RADEON_CRTC_V_TOTAL_DISP),
           (unsigned long)rd(RADEON_CRTC_OFFSET), (unsigned long)rd(RADEON_CRTC_OFFSET_CNTL),
           (unsigned long)rd(RADEON_CRTC_PITCH));
    hx_log("DPROBE-CP rbbm_status=%08lx rbbm_soft_reset=%08lx cp_csq_cntl=%08lx cp_csq_stat=%08lx cp_rb_cntl=%08lx "
           "se_cntl_status=%08lx dstcache_ctlstat=%08lx", (unsigned long)rd(RADEON_RBBM_STATUS),
           (unsigned long)rd(RADEON_RBBM_SOFT_RESET), (unsigned long)rd(RADEON_CP_CSQ_CNTL),
           (unsigned long)rd(RADEON_CP_CSQ_STAT), (unsigned long)rd(RADEON_CP_RB_CNTL),
           (unsigned long)rd(RADEON_SE_CNTL_STATUS), (unsigned long)rd(RADEON_RB3D_DSTCACHE_CTLSTAT));
    if (!nopll) {
        /* (CLK_PIN 01, PPLL_CNTL 02, PPLL_REF_DIV 03, PPLL_DIV_0-3 04-07, VCLK_ECP 08, HTOTAL 09, M_SPLL_REF_FB_DIV
         * 0A, AGP_PLL 0B, SPLL 0C, SCLK 0D, MPLL 0E, MCLK 12) */
        static const uint8_t which[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x12 };
        char line[400];
        int len;
        idx_was = rd(RADEON_CLOCK_CNTL_INDEX);
        len = sprintf(line, "DPROBE-PLL index_was=%08lx", (unsigned long)idx_was);
        for (i = 0; i < (int)sizeof which; i++)
            len += sprintf(line + len, " %02x=%08lx", which[i], (unsigned long)rd_pll(which[i]));
        hx_log("%s", line);
        ((volatile uint8_t *)mmio)[RADEON_CLOCK_CNTL_INDEX] = (uint8_t)(idx_was & 0xFF);
        sys_delay_us(5000);
    }
    sys_unmap_phys((void *)mmio, RADEON_MMIO_BYTES);

    /* The video BIOS (the shadow at C000:0): the signature and its name. */
    {
        const uint8_t *rom = (const uint8_t *)sys_real_ptr(0xC000, 0);
        int sig = rom && rom[0] == 0x55 && rom[1] == 0xAA, ati = 0, at = -1;
        char name[80];
        int k = 0;
        unsigned hdr = sig ? rom16(rom, 0x48) : 0;
        for (i = 0x20; sig && i < 0x80 && at < 0; i++)     /* (ATI's "761295520", near 0x30) */
            if (!memcmp(rom + i, "761295520", 9))
                at = i, ati = 1;
        if (sig) {
            for (i = 0; i < 0x7F00 && !k; i++)
                if (!memcmp(rom + i, "RADEON", 6) || !memcmp(rom + i, "Radeon", 6) || !memcmp(rom + i, "RV100", 5)) {
                    int j = i;
                    while (j > 0 && rom[j - 1] >= 32 && rom[j - 1] < 127 && i - j < 24)
                        j--;
                    for (; k < (int)sizeof name - 1 && rom[j + k] >= 32 && rom[j + k] < 127; k++)
                        name[k] = (char)rom[j + k];
                }
        }
        name[k] = 0;
        hx_log("DPROBE-BIOS sig=%d ati=%d at=%02x size=%u KB header=%04x name=\"%s\"", sig, ati,
               (unsigned)(at < 0 ? 0 : at), sig ? rom[2] * 512u / 1024u : 0u, hdr, name);
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
        hx_log("DPROBE-VBE ver=%x.%x mem=%uKB modes=%d oem=\"%s\" vendor=\"%s\" product=\"%s\"", ver >> 8, ver & 0xFF,
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
        hx_log("DPROBE-MODE m=%03x %ux%ux%u model=%u attr=%02x pitch=%u lfb=%08lx", list[i],
               (unsigned)(b[0x12] | (b[0x13] << 8)), (unsigned)(b[0x14] | (b[0x15] << 8)), b[0x19], b[0x1B], b[0],
               (unsigned)(b[0x10] | (b[0x11] << 8)),
               (unsigned long)((uint32_t)b[0x28] | ((uint32_t)b[0x29] << 8) | ((uint32_t)b[0x2A] << 16) |
                               ((uint32_t)b[0x2B] << 24)));
    }
    hx_done(0);
}
