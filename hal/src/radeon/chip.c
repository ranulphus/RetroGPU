/* chip.c - the ATI Radeon (R100 family): finding it on PCI, its registers and framebuffer, modes through its VBE 2.0
 * BIOS with the linear window, the display start and pitch, and the 2D engine's set-up after a mode set
 * (docs/radeon.md, RD1; the sequences after X.org's xf86-video-ati, radeon_accel.c's RADEONEngineRestore, MIT). The
 * Rage 128's chip.c, from which it is made. */
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "mga/sys.h"
#include <string.h>

#define MMIO_BYTES 0x10000u

radeon_chip radeon;
int radeon_mode_bpp;

static uint32_t pci_read32(int bus, int dev, int reg)
{
    sys_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | (uint32_t)(reg & 0xFC));
    return sys_inl(0xCFC);
}

uint32_t radeon_rd(uint32_t off) { return *(volatile uint32_t *)(radeon.mmio + off); }
void radeon_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(radeon.mmio + off) = v; }

/* A PLL register through the clock index (radeon_driver.c's INPLL: the index's low 6 bits, PLL_WR_EN clear), with
 * the chips' errata: the RV200's two dummy reads after the index (RADEONPllErrataAfterIndex), the RV100's 5 ms
 * after the data (RADEONPllErrataAfterData; kept for a chip the table doesn't know either). */
uint32_t radeon_pll_rd(int index)
{
    uint32_t v;
    radeon.mmio[RADEON_CLOCK_CNTL_INDEX] = (uint8_t)(index & 0x3F);
    if (radeon.flags & RADEON_PLL_DUMMYREADS) {
        (void)radeon_rd(RADEON_CLOCK_CNTL_DATA);
        (void)radeon_rd(RADEON_CRTC_GEN_CNTL);
    }
    v = radeon_rd(RADEON_CLOCK_CNTL_DATA);
    if (radeon.flags & RADEON_PLL_DELAY)
        sys_delay_us(5000);
    return v;
}

static const struct {
    uint16_t id;
    unsigned flags;
    const char *name;
} chips[] = {
    { 0x5144, RADEON_HAS_TCL | RADEON_HAS_HIERZ | RADEON_BROKEN_STENCIL, "Radeon (R100)" },
    { 0x5145, RADEON_HAS_TCL | RADEON_HAS_HIERZ | RADEON_BROKEN_STENCIL, "Radeon (R100)" },
    { 0x5146, RADEON_HAS_TCL | RADEON_HAS_HIERZ | RADEON_BROKEN_STENCIL, "Radeon (R100)" },
    { 0x5147, RADEON_HAS_TCL | RADEON_HAS_HIERZ | RADEON_BROKEN_STENCIL, "Radeon (R100)" },
    { 0x5157, RADEON_HAS_TCL | RADEON_HAS_HIERZ | RADEON_PLL_DUMMYREADS, "Radeon 7500 (RV200)" },
    { 0x5158, RADEON_HAS_TCL | RADEON_HAS_HIERZ | RADEON_PLL_DUMMYREADS, "Radeon 7500 (RV200)" },
    { 0x5159, RADEON_PLL_DELAY, "Radeon 7000 / VE (RV100)" },
    { 0x515A, RADEON_PLL_DELAY, "Radeon 7000 / VE (RV100)" },
};

int radeon_find(void)
{
    int bus, dev;
    for (bus = 0; bus < 4; bus++)               /* (an AGP card sits behind the AGP bridge: bus 1) */
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0), cls = pci_read32(bus, dev, 0x08) >> 16;
            unsigned k;
            for (k = 0; k < sizeof chips / sizeof chips[0]; k++)
                if (chips[k].id == (uint16_t)(id >> 16))
                    break;
            if ((id & 0xFFFF) == RADEON_PCI_VENDOR && cls == 0x0300 && k < sizeof chips / sizeof chips[0]) {
                memset(&radeon, 0, sizeof radeon);
                radeon.bus = bus;
                radeon.dev = dev;
                radeon.device_id = (uint16_t)(id >> 16);
                radeon.flags = chips[k].flags;
                radeon.name = chips[k].name;
                radeon.revision = (uint8_t)pci_read32(bus, dev, 0x08);
                radeon.fb_phys = pci_read32(bus, dev, 0x10) & 0xFFFFFFF0u;
                radeon.mmio_phys = pci_read32(bus, dev, 0x18) & 0xFFFFFFF0u;
                return 0;
            }
        }
    return -1;
}

int radeon_map(void)
{
    if (!radeon.mmio_phys || !radeon.fb_phys)
        return -1;
    radeon.mmio = (volatile uint8_t *)sys_map_phys(radeon.mmio_phys, MMIO_BYTES);
    if (!radeon.mmio)
        return -1;
    radeon.vram_bytes = radeon_rd(RADEON_CONFIG_MEMSIZE);
    if (radeon.vram_bytes < (1u << 20) || radeon.vram_bytes > (128u << 20))
        radeon.vram_bytes = 32u << 20;
    /* (the engines' offsets count from the frame buffer's place in the chip's own address space: 0 as the P3's
     * BIOS leaves MC_FB_LOCATION) */
    radeon.fb_location = (radeon_rd(RADEON_MC_FB_LOCATION) & 0xFFFFu) << 16;
    radeon.fb = (volatile uint8_t *)sys_map_phys(radeon.fb_phys, radeon.vram_bytes);
    if (!radeon.fb) {
        radeon_unmap();
        return -1;
    }
    return 0;
}

void radeon_unmap(void)
{
    if (radeon.fb)
        sys_unmap_phys((void *)radeon.fb, radeon.vram_bytes);
    if (radeon.mmio)
        sys_unmap_phys((void *)radeon.mmio, MMIO_BYTES);
    radeon.fb = radeon.mmio = 0;
}

/* ---- VBE through the BIOS ---------------------------------------------------------------------------------- */

static uint8_t *vbuf;
static uint16_t vseg;

static int vbe(sys_rmregs *r)
{
    if (sys_rm_int(0x10, r) < 0)
        return -1;
    return (r->eax & 0xFFFF) == 0x004F ? 0 : -1;
}

static int vbe_info(void)
{
    sys_rmregs r;
    if (!vbuf && !(vbuf = (uint8_t *)sys_dos_alloc(1024, &vseg)))
        return -1;
    memset(vbuf, 0, 512);
    memcpy(vbuf, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00;
    r.es = vseg;
    r.edi = 0;
    if (vbe(&r) < 0 || memcmp(vbuf, "VESA", 4) != 0)
        return -1;
    return 0;
}

int radeon_list_modes(radeon_mode *out, int max)
{
    const uint16_t *p;
    uint16_t list[128];
    int n = 0, i, got = 0;
    if (vbe_info() < 0)
        return 0;
    p = (const uint16_t *)sys_real_ptr((uint16_t)(vbuf[0x10] | (vbuf[0x11] << 8)),
                                       (uint16_t)(vbuf[0x0E] | (vbuf[0x0F] << 8)));
    while (p && *p != 0xFFFF && n < 128)
        list[n++] = *p++;
    for (i = 0; i < n; i++) {
        sys_rmregs r;
        uint8_t *b = vbuf + 512;
        int mbpp, model;
        memset(b, 0, 256);
        memset(&r, 0, sizeof r);
        r.eax = 0x4F01;
        r.ecx = list[i];
        r.es = vseg;
        r.edi = 512;
        if (vbe(&r) < 0 || (b[0] & 0x91) != 0x91)      /* supported, graphics, a linear window */
            continue;
        mbpp = b[0x19];
        model = b[0x1B];
        if (mbpp == 16 && model == 6 && b[0x21] == 5)   /* 5 green bits: 15-bit colour */
            mbpp = 15;
        if (!((mbpp == 8 && model == 4) || ((mbpp == 15 || mbpp == 16 || mbpp == 32) && model == 6)))
            continue;
        if (out && got < max) {
            out[got].mode = list[i];
            out[got].width = (uint16_t)(b[0x12] | (b[0x13] << 8));
            out[got].height = (uint16_t)(b[0x14] | (b[0x15] << 8));
            out[got].bpp = (uint16_t)mbpp;
            /* (VBE 3.0's LinBytesPerScanLine when given, else BytesPerScanLine) */
            out[got].pitch_bytes = (uint16_t)((b[0x32] | (b[0x33] << 8)) ? (b[0x32] | (b[0x33] << 8))
                                                                         : (b[0x10] | (b[0x11] << 8)));
        }
        got++;
    }
    return got;
}

int radeon_find_mode(int w, int h, int bpp, radeon_mode *out)
{
    radeon_mode m[64];
    int n = radeon_list_modes(m, 64), i;
    for (i = 0; i < n && i < 64; i++)
        if (m[i].width == w && m[i].height == h && m[i].bpp == bpp) {
            *out = m[i];
            return 0;
        }
    return -1;
}

static int bytes_of(int bpp) { return bpp == 8 ? 1 : bpp == 32 ? 4 : 2; }

/* The 2D engine for the mode (radeon_accel.c's RADEONEngineInit and RADEONEngineRestore): a soft reset, the
 * surface, the widest scissors, the master control's defaults, the colour registers, every plane written, host
 * data little-endian. */
static void engine_init(int bpp, int pitch_bytes)
{
    uint32_t dt = bpp == 8 ? RADEON_DT_CI8 : bpp == 15 ? RADEON_DT_ARGB1555 : bpp == 16 ? RADEON_DT_RGB565
                : RADEON_DT_ARGB8888;
    uint32_t po = ((uint32_t)(pitch_bytes / 64) << 22) | (radeon.fb_location >> 10);
    radeon_engine_reset();
    radeon_sync(100000);
    radeon_wr(RADEON_DST_PITCH_OFFSET, po);
    radeon_wr(RADEON_SRC_PITCH_OFFSET, po);
    radeon_wr(RADEON_DP_DATATYPE, radeon_rd(RADEON_DP_DATATYPE) & ~RADEON_HOST_BIG_ENDIAN_EN);
    radeon_wr(RADEON_DEFAULT_SC_BOTTOM_RIGHT, RADEON_DEFAULT_SC_RIGHT_MAX | RADEON_DEFAULT_SC_BOTTOM_MAX);
    radeon_wr(RADEON_SC_TOP_LEFT, 0);
    radeon_wr(RADEON_SC_BOTTOM_RIGHT, RADEON_DEFAULT_SC_RIGHT_MAX | RADEON_DEFAULT_SC_BOTTOM_MAX);
    radeon_wr(RADEON_DP_GUI_MASTER_CNTL, RADEON_GMC_DST_PITCH_OFFSET_CNTL | dt << RADEON_GMC_DST_DATATYPE_SHIFT |
                                         RADEON_GMC_CLR_CMP_CNTL_DIS | RADEON_GMC_BRUSH_SOLID_COLOR |
                                         RADEON_GMC_SRC_DATATYPE_COLOR);
    radeon_wr(RADEON_DP_BRUSH_FRGD_CLR, 0xFFFFFFFFu);
    radeon_wr(RADEON_DP_BRUSH_BKGD_CLR, 0);
    radeon_wr(RADEON_DP_SRC_FRGD_CLR, 0xFFFFFFFFu);
    radeon_wr(RADEON_DP_SRC_BKGD_CLR, 0);
    radeon_wr(RADEON_DP_WRITE_MASK, 0xFFFFFFFFu);
    radeon_sync(100000);
}

#if defined(__GNUC__) && defined(__i386__)
static void wc_lock(void) { __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory"); }
#elif defined(__WATCOMC__) && defined(__386__)
static void wc_lock(void);
#pragma aux wc_lock = "lock or dword ptr [esp], 0" modify exact [];
#else
static void wc_lock(void) { }
#endif

void radeon_vram_written(uint32_t last)
{
    wc_lock();
    if (radeon.fb && last + 4u <= radeon.vram_bytes)
        (void)*(volatile uint32_t *)(radeon.fb + (last & ~3u));   /* (the last write's own dword read back) */
    if (radeon.mmio) {
        uint32_t t0 = 0, spins = 0;
        radeon_wr(RADEON_RB2D_DSTCACHE_CTLSTAT, radeon_rd(RADEON_RB2D_DSTCACHE_CTLSTAT) | RADEON_RB2D_DC_FLUSH_ALL);
        radeon_wr(RADEON_RB3D_DSTCACHE_CTLSTAT, radeon_rd(RADEON_RB3D_DSTCACHE_CTLSTAT) | RADEON_RB3D_DC_FLUSH_ALL);
        while ((radeon_rd(RADEON_RB3D_DSTCACHE_CTLSTAT) & RADEON_RB3D_DC_BUSY) ||
               (radeon_rd(RADEON_RB2D_DSTCACHE_CTLSTAT) & RADEON_RB2D_DC_BUSY))
            if ((++spins & 63u) == 0) {
                if (!t0)
                    t0 = sys_time_us();
                else if (sys_time_us() - t0 > 100000u)
                    break;
            }
    }
}

int radeon_set_mode(const radeon_mode *m)
{
    sys_rmregs r;
    if (radeon.mmio)
        radeon_sync(100000);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = 0x4000u | m->mode;                      /* (the linear window) */
    if (vbe(&r) < 0)
        return -1;
    radeon_mode_bpp = m->bpp;
    engine_init(m->bpp, m->pitch_bytes);
    return 0;
}

void radeon_text_mode(void)
{
    sys_rmregs r;
    if (radeon.mmio)
        radeon_sync(100000);
    memset(&r, 0, sizeof r);
    r.eax = 0x0003;
    sys_rm_int(0x10, &r);
}

/* ---- The display ----------------------------------------------------------------------------------------- */

int radeon_set_start(uint32_t byte_off)
{
    if (byte_off & 7u || byte_off >= radeon.vram_bytes)
        return -1;
    radeon_wr(RADEON_CRTC_OFFSET, byte_off);        /* (RADEONAdjustFrame's: bytes from DISPLAY_BASE_ADDR) */
    return 0;
}

static uint32_t dac_saved;
static int dac_changed;

void radeon_set_ramp(const uint8_t ramp[256])
{
    int i;
    if (!radeon.mmio)
        return;
    if (!dac_changed) {
        dac_saved = radeon_rd(RADEON_DAC_CNTL);
        dac_changed = 1;
    }
    radeon_wr(RADEON_DAC_CNTL, dac_saved | RADEON_DAC_8BIT_EN);
    radeon.mmio[RADEON_PALETTE_INDEX] = 0;
    for (i = 0; i < 256; i++)
        radeon_wr(RADEON_PALETTE_DATA, (uint32_t)ramp[i] * 0x010101u);
}

void radeon_ramp_off(void)
{
    int i;
    if (!radeon.mmio || !dac_changed)
        return;
    radeon.mmio[RADEON_PALETTE_INDEX] = 0;
    for (i = 0; i < 256; i++)
        radeon_wr(RADEON_PALETTE_DATA, (uint32_t)i * 0x010101u);
    radeon_wr(RADEON_DAC_CNTL, dac_saved);
    dac_changed = 0;
}

int radeon_set_pitch(int pitch_bytes)
{
    int px = pitch_bytes / bytes_of(radeon_mode_bpp ? radeon_mode_bpp : 16);
    if (px <= 0 || px % 8 || pitch_bytes % 64)
        return -1;
    radeon_wr(RADEON_CRTC_PITCH, (uint32_t)(px / 8));       /* (the width / 8, as the Rage 128's) */
    return 0;
}

int radeon_vsync_wait(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
#if defined(MGA_OW)
    if (timeout_us < 110000u)
        timeout_us = 110000u;                       /* (Watcom's clock is the BIOS tick: 55 ms steps, two at least) */
#endif
    while (sys_inb(0x3DA) & 0x08)
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    while (!(sys_inb(0x3DA) & 0x08))
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return 0;
}
