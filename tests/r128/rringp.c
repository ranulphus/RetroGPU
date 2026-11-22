/* rringp.c - RRINGP: whether the Rage 128's CCE fetches a bus-master ring (192BM) from its own VRAM, and at which
 * address (docs/rage128.md, R4's speed). Each try loads the microcode, starts 192BM with PM4_BUFFER_OFFSET at one
 * candidate address for a 64 KB ring at VRAM offset RING, writes NOPs and one type-0 packet (DP_BRUSH_FRGD_CLR = a
 * marker) into the ring through the framebuffer window, gives the chip the write index, and reads back the read
 * index, PM4_STAT, PM4_BUFFER_ADDR and the register. The tries: the VRAM offset itself; the same with bus mastering
 * on (BUS_CNTL and the PCI command register); the framebuffer's PCI address plus the offset; the offset plus
 * R128_AGP_OFFSET (32 MB: the DRM's AGP space, expected to fail). After each the CCE is stopped and the engine
 * reset.
 *
 *   RRINGP
 *
 * Lines: RRINGP-REG (the bus and aperture registers), RRINGP-TRY (each try's readings). */
#include "hx.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>

#define RING_BYTES 0x10000u

static uint32_t pci_rd(int reg)
{
    sys_outl(0xCF8, 0x80000000u | ((uint32_t)r128.bus << 16) | ((uint32_t)r128.dev << 11) | (uint32_t)(reg & 0xFC));
    return sys_inl(0xCFC);
}

static void pci_wr(int reg, uint32_t v)
{
    sys_outl(0xCF8, 0x80000000u | ((uint32_t)r128.bus << 16) | ((uint32_t)r128.dev << 11) | (uint32_t)(reg & 0xFC));
    sys_outl(0xCFC, v);
}

/* The PCI-to-PCI bridge (class 0604) on bus 0 whose secondary bus is the card's: its device number, or -1. */
static int bridge_dev = -1;

static uint32_t cfg_rd(int bus, int dev, int reg)
{
    sys_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | (uint32_t)(reg & 0xFC));
    return sys_inl(0xCFC);
}

static void cfg_wr(int bus, int dev, int reg, uint32_t v)
{
    sys_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | (uint32_t)(reg & 0xFC));
    sys_outl(0xCFC, v);
}

static void find_bridge(void)
{
    int d;
    for (d = 0; d < 32; d++) {
        uint32_t id = cfg_rd(0, d, 0), cls = cfg_rd(0, d, 8) >> 16;
        if (id == 0xFFFFFFFFu || cls != 0x0604)
            continue;
        if ((int)(cfg_rd(0, d, 0x18) >> 8 & 0xFF) == r128.bus) {
            bridge_dev = d;
            hx_log("RRINGP-BRIDGE dev=%d id=%08lx cmd=%08lx buses=%08lx bridge_ctl=%08lx", d, (unsigned long)id,
                   (unsigned long)cfg_rd(0, d, 4), (unsigned long)cfg_rd(0, d, 0x18), (unsigned long)cfg_rd(0, d, 0x3C));
        }
    }
}

static int try_one(const char *name, uint32_t ring_off, uint32_t addr, int bm, volatile uint32_t *cpu)
{
    volatile uint32_t *ring = cpu ? cpu : (volatile uint32_t *)(r128.fb + ring_off);
    uint32_t marker = 0x5A000000u | (addr >> 8 & 0xFFFFFFu), rp0, rp, st, ba, reg, t0, bus = r128_rd(R128_BUS_CNTL);
    uint32_t cmd = pci_rd(0x04);
    int i, ok;
    for (i = 0; i < (int)(RING_BYTES / 4) + 32; i++)
        ring[i] = R128_CCE_PACKET2;
    ring[8] = R128_CCE_PACKET0 | (R128_DP_BRUSH_FRGD_CLR >> 2);
    ring[9] = marker;
    if (!cpu)
        r128_vram_written(ring_off + RING_BYTES + 124u);
    __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory");
    if (bm) {
        r128_wr(R128_BUS_CNTL, bus & ~0x40u);           /* (BUS_MASTER_DIS off) */
        pci_wr(0x04, cmd | 4u);                          /* (bus master) */
    }
    r128_wr(R128_PM4_MICROCODE_ADDR, 0);
    for (i = 0; i < 256; i++) {
        r128_wr(R128_PM4_MICROCODE_DATAH, r128_cce_microcode[i * 2]);
        r128_wr(R128_PM4_MICROCODE_DATAL, r128_cce_microcode[i * 2 + 1]);
    }
    r128_wr(R128_PM4_BUFFER_OFFSET, addr);
    r128_wr(R128_PM4_BUFFER_DL_WPTR, 0);
    r128_wr(R128_PM4_BUFFER_DL_RPTR, 0);
    r128_wr(R128_PM4_BUFFER_WM_CNTL, (16u / 4u) << 0 | (8u / 4u) << 8 | (8u / 4u) << 16 | (128u / 64u) << 24);
    r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_192BM | 13u | R128_PM4_BUFFER_CNTL_NOUPDATE);    /* (64 KB: 2^13 qwords) */
    (void)r128_rd(R128_PM4_BUFFER_ADDR);
    r128_wr(R128_PM4_MICRO_CNTL, R128_PM4_MICRO_FREERUN);
    sys_delay_us(1000);
    hx_log("RRINGP-SET %-8s cntl=%08lx offset=%08lx micro=%08lx wptr=%08lx stat=%08lx wm=%08lx bus=%08lx", name,
           (unsigned long)r128_rd(R128_PM4_BUFFER_CNTL), (unsigned long)r128_rd(R128_PM4_BUFFER_OFFSET),
           (unsigned long)r128_rd(R128_PM4_MICRO_CNTL), (unsigned long)r128_rd(R128_PM4_BUFFER_DL_WPTR),
           (unsigned long)r128_rd(R128_PM4_STAT), (unsigned long)r128_rd(R128_PM4_BUFFER_WM_CNTL),
           (unsigned long)r128_rd(R128_BUS_CNTL));
    rp0 = r128_rd(R128_PM4_BUFFER_DL_RPTR);
    r128_wr(R128_PM4_BUFFER_DL_WPTR, 16);
    t0 = sys_time_us();
    do
        rp = r128_rd(R128_PM4_BUFFER_DL_RPTR);
    while (rp != 16 && sys_time_us() - t0 < 50000u);
    sys_delay_us(2000);
    st = r128_rd(R128_PM4_STAT), ba = r128_rd(R128_PM4_BUFFER_ADDR), reg = r128_rd(R128_DP_BRUSH_FRGD_CLR);
    ok = rp == 16 && reg == marker;
    hx_log("RRINGP-TRY %-8s ring=%06lx addr=%08lx bm=%d rptr0=%lx rptr=%lx stat=%08lx bufaddr=%08lx reg=%08lx want=%08lx %s",
           name, (unsigned long)ring_off, (unsigned long)addr, bm, (unsigned long)rp0, (unsigned long)rp,
           (unsigned long)st, (unsigned long)ba, (unsigned long)reg, (unsigned long)marker, ok ? "FETCHED" : "no");
    /* Stopped, the engine reset, bus mastering as it was. */
    r128_wr(R128_PM4_MICRO_CNTL, 0);
    r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE);
    r128_engine_reset();
    r128_wr(R128_BUS_CNTL, bus);
    pci_wr(0x04, cmd);
    hx_test(name, 1, "%s", ok ? "the ring fetched" : "not fetched");
    return ok;
}

int main(int argc, char **argv)
{
    r128_mode m;
    (void)argc;
    hx_init(1, argv, "rringp");
    if (r128_find() < 0 || r128_map() < 0 || r128_find_mode(640, 480, 16, &m) < 0 || r128_set_mode(&m) < 0) {
        hx_test("start", 0, "no Rage 128 or mode");
        hx_done(1);
    }
    find_bridge();
    hx_log("RRINGP-REG bus_cntl=%08lx agp_base=%08lx agp_cntl=%08lx agp_aper_off=%08lx aper0=%08lx aper1=%08lx "
           "aper_size=%08lx pci_cmd=%08lx fb_phys=%08lx", (unsigned long)r128_rd(R128_BUS_CNTL),
           (unsigned long)r128_rd(0x0170), (unsigned long)r128_rd(0x0174), (unsigned long)r128_rd(0x0178),
           (unsigned long)r128_rd(0x0100), (unsigned long)r128_rd(0x0104), (unsigned long)r128_rd(0x0108),
           (unsigned long)pci_rd(0x04), (unsigned long)r128.fb_phys);
    if (r128_cce_init(NULL) == 0) {             /* (the baseline: PIO's state when started) */
        sys_delay_us(1000);
        hx_log("RRINGP-SET pio      cntl=%08lx micro=%08lx stat=%08lx", (unsigned long)r128_rd(R128_PM4_BUFFER_CNTL),
               (unsigned long)r128_rd(R128_PM4_MICRO_CNTL), (unsigned long)r128_rd(R128_PM4_STAT));
        r128_cce_stop();
    }
    {
        /* The ring in conventional memory (linear = physical), its pages (and the mirror's) in a PCI GART table
         * there too, PCI_GART_PAGE at the table: the ring at VM 32 MB (R128_AGP_OFFSET), the DRM's PCI way. */
        uint16_t seg;
        uint8_t *blk = (uint8_t *)sys_dos_alloc(0x40000u, &seg);  /* (256 KB: the ring 64 KB-aligned, the table's 32 KB
                                                                     32 KB-aligned after it and its mirror page) */
        if (!blk)
            hx_test("sysgart", 0, "no conventional memory");
        else {
            uint32_t lin = (uint32_t)seg << 4, al = (lin + 0xFFFFu) & ~0xFFFFu, tbl = (al + RING_BYTES + 4096u + 0x7FFFu) & ~0x7FFFu;
            volatile uint32_t *t = (volatile uint32_t *)(blk + (tbl - lin));
            int k;
            for (k = 0; k < 8192; k++)            /* (all of the DRM's 32 KB table: the ring's pages, then the ring again) */
                t[k] = k <= (int)(RING_BYTES / 4096u) ? al + (uint32_t)k * 4096u : al;
            uint32_t gart0 = r128_rd(0x017C), chunk = r128_rd(0x0A18);
            (void)cfg_wr;
            r128_wr(0x017C, tbl);               /* (PCI_GART_PAGE; bit 0 clear: the PCI GART, not the AGP one) */
            hx_log("RRINGP-GART ring=%08lx table=%08lx (gart page was %08lx, bm_chunk_0 %08lx)", (unsigned long)al,
                   (unsigned long)tbl, (unsigned long)gart0, (unsigned long)chunk);
            /* X.org's R128DRIPciInit for AGP boards: the bus master's pointer, PM4 and other reads forced to PCI. */
            r128_wr(0x0A18, chunk | 7u << 21);
            /* X.org's R128DRIPciInit for AGP boards: the bus master's pointer, PM4 and other reads forced to PCI.
             * (The other ways tried, a ring at a VRAM offset among them, never fetched; the VRAM ones wedged the
             * chip's bus mastering until a power cycle, so they're gone: docs/rage128.md.) */
            r128_wr(0x0A18, chunk | 7u << 21);
            try_one("syspci", 0, 0x02000000u, 1, (volatile uint32_t *)(blk + (al - lin)));
            r128_wr(0x0A18, chunk);
            r128_wr(0x017C, gart0);
            sys_dos_free(blk);
        }
    }
    r128_text_mode();
    hx_done(0);
}
