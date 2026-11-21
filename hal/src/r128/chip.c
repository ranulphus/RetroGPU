/* chip.c - the ATI Rage 128: finding it on PCI, its registers and framebuffer, modes through its VBE 2.0 BIOS with
 * the linear window, the display start and pitch, and the 2D engine's set-up after a mode set (docs/rage128.md,
 * R1; the sequences after X.org's xf86-video-r128, r128_accel.c's R128EngineInit and r128_driver.c's
 * R128AdjustFrame, MIT). */
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/sys.h"
#include <string.h>

r128_chip r128;
int r128_mode_bpp;

static uint32_t pci_read32(int bus, int dev, int reg)
{
    sys_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | (uint32_t)(reg & 0xFC));
    return sys_inl(0xCFC);
}

uint32_t r128_rd(uint32_t off) { return *(volatile uint32_t *)(r128.mmio + off); }
void r128_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(r128.mmio + off) = v; }

/* The PLL registers through the clock index (r128_reg.h's INPLL and OUTPLL: the index's low 6 bits, PLL_WR_EN to
 * write). */
uint32_t r128_pll_rd(int index)
{
    r128.mmio[R128_CLOCK_CNTL_INDEX] = (uint8_t)(index & 0x3F);
    return r128_rd(R128_CLOCK_CNTL_DATA);
}

void r128_pll_wr(int index, uint32_t v)
{
    r128.mmio[R128_CLOCK_CNTL_INDEX] = (uint8_t)((index & 0x3F) | R128_PLL_WR_EN);
    r128_wr(R128_CLOCK_CNTL_DATA, v);
}

int r128_find(void)
{
    int bus, dev;
    for (bus = 0; bus < 4; bus++)               /* (an AGP card sits behind the AGP bridge: bus 1) */
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0), cls = pci_read32(bus, dev, 0x08) >> 16;
            if ((id & 0xFFFF) == R128_PCI_VENDOR && cls == 0x0300) {
                memset(&r128, 0, sizeof r128);
                r128.bus = bus;
                r128.dev = dev;
                r128.device_id = (uint16_t)(id >> 16);
                r128.revision = (uint8_t)pci_read32(bus, dev, 0x08);
                r128.fb_phys = pci_read32(bus, dev, 0x10) & 0xFFFFFFF0u;
                r128.mmio_phys = pci_read32(bus, dev, 0x18) & 0xFFFFFFF0u;
                return 0;
            }
        }
    return -1;
}

int r128_map(void)
{
    if (!r128.mmio_phys || !r128.fb_phys)
        return -1;
    r128.mmio = (volatile uint8_t *)sys_map_phys(r128.mmio_phys, 0x4000u);
    if (!r128.mmio)
        return -1;
    r128.vram_bytes = r128_rd(R128_CONFIG_MEMSIZE);
    if (r128.vram_bytes < (1u << 20) || r128.vram_bytes > (64u << 20))
        r128.vram_bytes = 16u << 20;
    r128.fb = (volatile uint8_t *)sys_map_phys(r128.fb_phys, r128.vram_bytes);
    if (!r128.fb) {
        r128_unmap();
        return -1;
    }
    return 0;
}

void r128_unmap(void)
{
    if (r128.fb)
        sys_unmap_phys((void *)r128.fb, r128.vram_bytes);
    if (r128.mmio)
        sys_unmap_phys((void *)r128.mmio, 0x4000u);
    r128.fb = r128.mmio = 0;
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

int r128_list_modes(r128_mode *out, int max)
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

int r128_find_mode(int w, int h, int bpp, r128_mode *out)
{
    r128_mode m[64];
    int n = r128_list_modes(m, 64), i;
    for (i = 0; i < n && i < 64; i++)
        if (m[i].width == w && m[i].height == h && m[i].bpp == bpp) {
            *out = m[i];
            return 0;
        }
    return -1;
}

static int bytes_of(int bpp) { return bpp == 8 ? 1 : bpp == 32 ? 4 : 2; }

/* The 2D engine for the mode (r128_accel.c's R128EngineInit): the 3D scaler off, a soft reset, the default
 * surface and scissors, the master control's defaults, the Bresenham and colour registers cleared, every plane
 * written, host data little-endian. */
static void engine_init(int bpp, int pitch_bytes)
{
    uint32_t dt = bpp == 8 ? R128_DT_CI8 : bpp == 15 ? R128_DT_ARGB1555 : bpp == 16 ? R128_DT_RGB565
                : R128_DT_ARGB8888;
    r128_wr(R128_SCALE_3D_CNTL, 0);
    r128_engine_reset();
    r128_sync(100000);
    r128_wr(R128_DEFAULT_OFFSET, 0);
    r128_wr(R128_DEFAULT_PITCH, (uint32_t)(pitch_bytes / bytes_of(bpp) / 8));
    r128_wr(R128_AUX_SC_CNTL, 0);
    r128_wr(R128_DEFAULT_SC_BOTTOM_RIGHT, R128_DEFAULT_SC_RIGHT_MAX | R128_DEFAULT_SC_BOTTOM_MAX);
    r128_wr(R128_SC_TOP_LEFT, 0);
    r128_wr(R128_SC_BOTTOM_RIGHT, R128_DEFAULT_SC_RIGHT_MAX | R128_DEFAULT_SC_BOTTOM_MAX);
    r128_wr(R128_DP_GUI_MASTER_CNTL, dt << R128_GMC_DST_DATATYPE_SHIFT | R128_GMC_CLR_CMP_CNTL_DIS |
                                     R128_GMC_AUX_CLIP_DIS | R128_GMC_BRUSH_SOLID_COLOR |
                                     R128_GMC_SRC_DATATYPE_COLOR);
    r128_wr(R128_DST_BRES_ERR, 0);
    r128_wr(R128_DST_BRES_INC, 0);
    r128_wr(R128_DST_BRES_DEC, 0);
    r128_wr(R128_DP_BRUSH_FRGD_CLR, 0xFFFFFFFFu);
    r128_wr(R128_DP_BRUSH_BKGD_CLR, 0);
    r128_wr(R128_DP_SRC_FRGD_CLR, 0xFFFFFFFFu);
    r128_wr(R128_DP_SRC_BKGD_CLR, 0);
    r128_wr(R128_DP_WRITE_MASK, 0xFFFFFFFFu);
    r128_wr(R128_DP_DATATYPE, r128_rd(R128_DP_DATATYPE) & ~R128_HOST_BIG_ENDIAN_EN);
    r128_sync(100000);
}

#if defined(__GNUC__) && defined(__i386__)
static void wc_lock(void) { __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory"); }
#elif defined(__WATCOMC__) && defined(__386__)
static void wc_lock(void);
#pragma aux wc_lock = "lock or dword ptr [esp], 0" modify exact [];
#else
static void wc_lock(void) { }
#endif

void r128_vram_written(uint32_t last)
{
    wc_lock();
    if (r128.fb && last + 4u <= r128.vram_bytes)
        (void)*(volatile uint32_t *)(r128.fb + (last & ~3u));   /* (the last write's own dword read back) */
}

int r128_set_mode(const r128_mode *m)
{
    sys_rmregs r;
    if (r128.mmio) {
        r128_sync(100000);
        r128_cce_stop();                            /* (the caller starts it again for the new mode) */
    }
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = 0x4000u | m->mode;                      /* (the linear window) */
    if (vbe(&r) < 0)
        return -1;
    r128_mode_bpp = m->bpp;
    engine_init(m->bpp, m->pitch_bytes);
    return 0;
}

void r128_text_mode(void)
{
    sys_rmregs r;
    if (r128.mmio) {
        r128_sync(100000);
        r128_cce_stop();
    }
    memset(&r, 0, sizeof r);
    r.eax = 0x0003;
    sys_rm_int(0x10, &r);
}

/* ---- The display ----------------------------------------------------------------------------------------- */

int r128_set_start(uint32_t byte_off)
{
    if (byte_off & 7u || byte_off >= r128.vram_bytes)
        return -1;
    r128_wr(R128_CRTC_OFFSET, byte_off);            /* (R128AdjustFrame's: bytes, the low 3 bits 0) */
    return 0;
}

static uint32_t dac_saved;
static int dac_changed;

void r128_set_ramp(const uint8_t ramp[256])
{
    int i;
    if (!r128.mmio)
        return;
    if (!dac_changed) {
        dac_saved = r128_rd(R128_DAC_CNTL);
        dac_changed = 1;
    }
    r128_wr(R128_DAC_CNTL, dac_saved | R128_DAC_8BIT_EN);
    r128.mmio[R128_PALETTE_INDEX] = 0;
    for (i = 0; i < 256; i++)
        r128_wr(R128_PALETTE_DATA, (uint32_t)ramp[i] * 0x010101u);
}

void r128_ramp_off(void)
{
    int i;
    if (!r128.mmio || !dac_changed)
        return;
    r128.mmio[R128_PALETTE_INDEX] = 0;
    for (i = 0; i < 256; i++)
        r128_wr(R128_PALETTE_DATA, (uint32_t)i * 0x010101u);
    r128_wr(R128_DAC_CNTL, dac_saved);
    dac_changed = 0;
}

int r128_set_pitch(int pitch_bytes)
{
    int px = pitch_bytes / bytes_of(r128_mode_bpp ? r128_mode_bpp : 16);
    if (px <= 0 || px % 8)
        return -1;
    r128_wr(R128_CRTC_PITCH, (uint32_t)(px / 8));   /* (r128_crtc.c's crtc_pitch: the width / 8) */
    if (r128_cce_running())
        r128_cce_reg(R128_DEFAULT_PITCH, (uint32_t)(px / 8));
    else
        r128_wr(R128_DEFAULT_PITCH, (uint32_t)(px / 8));
    return 0;
}

int r128_vsync_wait(uint32_t timeout_us)
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
