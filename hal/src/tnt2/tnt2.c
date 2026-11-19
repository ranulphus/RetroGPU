/* tnt2.c - the Riva TNT2's register layer (docs/tnt2.md, T1): the chip found on PCI and mapped; its graphics engine,
 * FIFO and instance memory set up as X.org's xf86-video-nv sets up the NV4 family (nv_hw.c's NVLoadStateExt with no
 * display state: the BIOS's mode is left as it is); the command ring in video memory (nv_xaa.c's NVResetGraphics,
 * NVDmaWait, NVDmaKickoff, NVSync); and the first 2D operations: the target surface, the clip, solid fill and
 * screen-to-screen copy (nv_xaa.c's).
 *
 * The set-up's register values, the objects and the ring's protocol follow xf86-video-nv 2.1.23, under this notice:
 *
 * Copyright (c) 1993-2003 NVIDIA, Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "tnt2/tnt2.h"
#include "mga/hal.h"
#include "mga/sys.h"

tnt2_chip tnt2;
volatile uint32_t *tnt2_regs;
volatile uint8_t *tnt2_fb;

/* The units, as dword pointers into BAR0 (nv_setup.c's). */
#define PMC     (tnt2_regs + 0x000000 / 4)
#define PFIFO   (tnt2_regs + 0x002000 / 4)
#define PTIMER  (tnt2_regs + 0x009000 / 4)
#define PFB     (tnt2_regs + 0x100000 / 4)
#define PGRAPH  (tnt2_regs + 0x400000 / 4)
#define PRAMIN  (tnt2_regs + 0x710000 / 4)
#define FIFO    (tnt2_regs + 0x800000 / 4)  /* channel 0's user area: PUT at 0x40, GET at 0x44 (bytes) */

static const char *chip_name(uint16_t id)
{
    switch (id) {
    case 0x0020: return "Riva TNT";
    case 0x0028: return "Riva TNT2";
    case 0x0029: return "Riva TNT2 Ultra";
    case 0x002C: return "Vanta";
    case 0x002D: return "Riva TNT2 M64";
    case 0x00A0: return "Aladdin TNT2";
    default:     return NULL;
    }
}

int tnt2_find(void)
{
    int bus, dev;
    for (bus = 0; bus < 4; bus++)               /* (an AGP card sits behind the AGP bridge, usually bus 1) */
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = mga_pci_read32(bus, dev, 0, 0);
            if ((id & 0xFFFF) != 0x10DE || !chip_name((uint16_t)(id >> 16)))
                continue;
            tnt2.bus = bus;
            tnt2.dev = dev;
            tnt2.device = (uint16_t)(id >> 16);
            tnt2.revision = (uint8_t)mga_pci_read32(bus, dev, 0, 0x08);
            tnt2.bar0 = mga_pci_read32(bus, dev, 0, 0x10) & ~0xFu;
            tnt2.bar1 = mga_pci_read32(bus, dev, 0, 0x14) & ~0xFu;
            tnt2.name = chip_name(tnt2.device);
            return 0;
        }
    return -1;
}

int tnt2_map(void)
{
    uint32_t boot;
    tnt2_regs = (volatile uint32_t *)sys_map_phys(tnt2.bar0, 0x01000000u);
    if (!tnt2_regs)
        return -1;
    tnt2.boot0 = PMC[0];
    /* The memory's size as nv_setup.c reads an NV4's PFB_BOOT_0 (confirmed on the P3's M64: 32 MB, docs/tnt2.md). */
    boot = PFB[0];
    if (boot & 0x100)
        tnt2.fb_size = (((boot >> 12) & 0x0F) * 2 + 2) << 20;
    else {
        static const uint32_t mb[4] = { 32, 4, 8, 16 };
        tnt2.fb_size = mb[boot & 3] << 20;
    }
    tnt2.fb_usable = tnt2.fb_size - (128u << 10);   /* (nv_driver.c's FbUsableSize before NV40) */
    tnt2_fb = (volatile uint8_t *)sys_map_phys(tnt2.bar1, tnt2.fb_size);
    return tnt2_fb ? 0 : -1;
}

void tnt2_unmap(void)
{
    if (tnt2_fb)
        sys_unmap_phys(tnt2_fb, tnt2.fb_size);
    if (tnt2_regs)
        sys_unmap_phys(tnt2_regs, 0x01000000u);
    tnt2_fb = NULL;
    tnt2_regs = NULL;
}

/* ---- the command ring (nv_xaa.c) -------------------------------------------- */

/* There is a hardware race with command buffers in video memory: you can't jump to the place of your put offset.
 * Put goes SKIPS dwords past the jump, with no-ops between (nv_xaa.c's note). */
#define SKIPS 8
#define RING_DWORDS 8192u                       /* 32 KB at fb_usable */

static volatile uint32_t *ring;
static int ring_put, ring_cur, ring_free;
static const int ring_max = RING_DWORDS - 1;

static void write_put(int put)
{
    volatile uint8_t scratch = tnt2_fb[0];      /* (nv_local.h's WRITE_PUT: a read of video memory first, so the
                                                    ring's writes are out before the chip is told of them) */
    (void)scratch;
    __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory");
    FIFO[0x0010] = (uint32_t)put << 2;
}

static int read_get(void)
{
    return (int)(FIFO[0x0011] >> 2);
}

static void ring_wait(int size)
{
    int get;
    uint32_t t0 = sys_time_us();
    size++;
    while (ring_free < size) {
        get = read_get();
        if (ring_put >= get) {
            ring_free = ring_max - ring_cur;
            if (ring_free < size) {
                ring[ring_cur++] = 0x20000000u;         /* a jump to the ring's start */
                if (get <= SKIPS) {
                    if (ring_put <= SKIPS)              /* (the corner case: it will be idle) */
                        write_put(SKIPS + 1);
                    while ((get = read_get()) <= SKIPS && sys_time_us() - t0 < 1000000u)
                        ;
                }
                write_put(SKIPS);
                ring_cur = ring_put = SKIPS;
                ring_free = get - (SKIPS + 1);
            }
        } else
            ring_free = get - ring_cur - 1;
        if (sys_time_us() - t0 > 1000000u)              /* (a stuck chip: give up rather than hang the PC) */
            return;
    }
}

static void ring_start(uint32_t method, int size)
{
    if (ring_free <= size)
        ring_wait(size);
    ring[ring_cur++] = ((uint32_t)size << 18) | method;
    ring_free -= size + 1;
}

#define NEXT(v) (ring[ring_cur++] = (uint32_t)(v))

void tnt2_kick(void)
{
    if (ring_cur != ring_put) {
        ring_put = ring_cur;
        write_put(ring_put);
    }
}

int tnt2_sync(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    tnt2_kick();
    while (read_get() != ring_put)
        if (sys_time_us() - t0 > timeout_us)
            return -1;
    while (PGRAPH[0x0700 / 4])                          /* PGRAPH_STATUS */
        if (sys_time_us() - t0 > timeout_us)
            return -1;
    return 0;
}

/* ---- the objects' methods (nv_dma.h): subchannel N's methods at N * 0x2000 ---- */
#define SURFACE_FORMAT      0x00000300u     /* subchannel 0: the 2D surfaces */
#define SURFACE_PITCH       0x00000304u
#define SURFACE_OFFSET_SRC  0x00000308u
#define SURFACE_OFFSET_DST  0x0000030Cu
#define ROP_SET             0x00002300u     /* 1: the ROP */
#define PATTERN_FORMAT      0x00004300u     /* 2: the pattern */
#define PATTERN_COLOR_0     0x00004310u
#define CLIP_POINT          0x00006300u     /* 3: the clip */
#define CLIP_SIZE           0x00006304u
#define LINE_FORMAT         0x00008300u     /* 4: lines */
#define BLIT_POINT_SRC      0x0000A300u     /* 5: the screen-to-screen blit */
#define RECT_FORMAT         0x0000C300u     /* 6: the GDI rectangles and text */
#define RECT_SOLID_COLOR    0x0000C3FCu
#define RECT_SOLID_RECTS0   0x0000C400u

static int cur_bpp, cur_rop = -1;
static int clip_x0, clip_y0, clip_x1 = 0x7FFF, clip_y1 = 0x7FFF;   /* (applied in software: below) */

static void set_rop_copy(void)
{
    if (cur_rop == 0xCC)
        return;
    ring_start(PATTERN_COLOR_0, 4);                     /* (nv_xaa.c's NVSetRopSolid: the pattern all ones) */
    NEXT(~0u); NEXT(~0u); NEXT(~0u); NEXT(~0u);
    ring_start(ROP_SET, 1);
    NEXT(0xCC);                                         /* GXcopy (NVCopyROP) */
    cur_rop = 0xCC;
}

void tnt2_set_target(uint32_t off, int pitch_bytes, int bpp)
{
    uint32_t surface, other;
    if (bpp == 32) surface = 0x00000006, other = 0x00000003;        /* (nv_dma.h's DEPTH24 formats) */
    else if (bpp == 16) surface = 0x00000004, other = 0x00000001;
    else surface = 0x00000001, other = 0x00000003;
    ring_start(SURFACE_FORMAT, 4);
    NEXT(surface);
    NEXT((uint32_t)pitch_bytes | ((uint32_t)pitch_bytes << 16));
    NEXT(off);
    NEXT(off);
    if (bpp != cur_bpp) {
        ring_start(PATTERN_FORMAT, 1); NEXT(other);
        ring_start(RECT_FORMAT, 1);    NEXT(other);
        ring_start(LINE_FORMAT, 1);    NEXT(other);
        cur_bpp = bpp;
    }
}

/* The solid rectangles and the blit ignore the clip object (the P3, 2026-11-19: a fill drew outside it; X.org's
 * driver clips only colour expansion and pattern fills in hardware), so fills and copies are clipped here; the clip
 * object is set too, for the operations that take it. */
void tnt2_set_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = x0, clip_y0 = y0, clip_x1 = x1, clip_y1 = y1;
    ring_start(CLIP_POINT, 2);
    NEXT(((uint32_t)y0 << 16) | (uint32_t)(x0 & 0xFFFF));
    NEXT(((uint32_t)(y1 - y0) << 16) | (uint32_t)((x1 - x0) & 0xFFFF));
}

void tnt2_fill(int x, int y, int w, int h, uint32_t value)
{
    if (x < clip_x0) w -= clip_x0 - x, x = clip_x0;
    if (y < clip_y0) h -= clip_y0 - y, y = clip_y0;
    if (x + w > clip_x1) w = clip_x1 - x;
    if (y + h > clip_y1) h = clip_y1 - y;
    if (w <= 0 || h <= 0)
        return;
    set_rop_copy();
    ring_start(RECT_SOLID_COLOR, 1);
    NEXT(value);
    ring_start(RECT_SOLID_RECTS0, 2);
    NEXT(((uint32_t)x << 16) | (uint32_t)y);
    NEXT(((uint32_t)w << 16) | (uint32_t)h);
    if (w * h >= 512)
        tnt2_kick();
}

void tnt2_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    /* The destination clipped; the source moves with it. */
    if (dx < clip_x0) { int d = clip_x0 - dx; sx += d, w -= d, dx = clip_x0; }
    if (dy < clip_y0) { int d = clip_y0 - dy; sy += d, h -= d, dy = clip_y0; }
    if (dx + w > clip_x1) w = clip_x1 - dx;
    if (dy + h > clip_y1) h = clip_y1 - dy;
    if (w <= 0 || h <= 0)
        return;
    set_rop_copy();
    ring_start(BLIT_POINT_SRC, 3);
    NEXT(((uint32_t)sy << 16) | (uint32_t)sx);
    NEXT(((uint32_t)dy << 16) | (uint32_t)dx);
    NEXT(((uint32_t)h << 16) | (uint32_t)w);
    if (w * h >= 512)
        tnt2_kick();
}

/* ---- the set-up (nv_hw.c's NVLoadStateExt for NV_ARCH_04, no display state) ---- */

int tnt2_accel_init(void)
{
    static const uint32_t ramht[16] = {        /* the hash table: 8 handles, each to its object's instance */
        0x80000010, 0x80011201, 0x80000011, 0x80011202, 0x80000012, 0x80011203, 0x80000013, 0x80011204,
        0x80000014, 0x80011205, 0x80000015, 0x80011206, 0x80000016, 0x80011207, 0x80000017, 0x80011208 };
    /* The objects at instance 0x1201-0x1209 (PRAMIN dwords 0x0804-0x0827): 0x42 2D surfaces, 0x43 ROP, 0x44 pattern,
     * 0x19 clip, 0x5C line, 0x5F image blit, 0x4A GDI rectangles and text, 0x77 scaled image, and the DMA object
     * the ring is read through (0x3002: video memory from fb_usable, 32 KB). */
    uint32_t obj[0x28 - 0x04];
    int i;
    uint32_t t0;

    PMC[0x0140 / 4] = 0x00000000;
    PMC[0x0200 / 4] = 0xFFFF00FF;
    PMC[0x0200 / 4] = 0xFFFFFFFF;               /* every unit on (PGRAPH among them: off after the BIOS) */

    /* PTIMER: nv_hw.c writes PTIMER[0x0200] and so on through a dword pointer, so at 0x9800, 0x9840, 0x9500, 0x9400;
     * the NV4's timer registers are 0x9200 (numerator), 0x9210 (denominator), 0x9140 (interrupt enable) and 0x9100
     * (interrupts), which these values fit: written there. */
    PTIMER[0x0200 / 4] = 0x00000008;
    PTIMER[0x0210 / 4] = 0x00000003;
    PTIMER[0x0140 / 4] = 0x00000000;
    PTIMER[0x0100 / 4] = 0xFFFFFFFF;

    for (i = 0; i < 16; i++)
        PRAMIN[i] = ramht[i];
    PRAMIN[0x0800] = 0x00003000;
    PRAMIN[0x0801] = tnt2.fb_size - 1;
    PRAMIN[0x0802] = 0x00000002;
    PRAMIN[0x0803] = 0x00000002;
    for (i = 0; i < 0x28 - 0x04; i++)
        obj[i] = 0;
    obj[0x04 - 4] = 0x01008042; obj[0x06 - 4] = 0x12001200;               /* 2D surfaces (NV4: 0x42) */
    obj[0x08 - 4] = 0x01008043;                                           /* ROP */
    obj[0x0C - 4] = 0x01008044; obj[0x0D - 4] = 0x00000002;               /* pattern */
    obj[0x10 - 4] = 0x01008019;                                           /* clip */
    obj[0x14 - 4] = 0x0100A05C;                                           /* line */
    obj[0x18 - 4] = 0x0100805F; obj[0x1A - 4] = 0x12001200;               /* image blit (no vsync wait) */
    obj[0x1C - 4] = 0x0100804A; obj[0x1D - 4] = 0x00000002;               /* GDI rectangles and text */
    obj[0x20 - 4] = 0x01018077; obj[0x22 - 4] = 0x12001200;               /* scaled image */
    obj[0x24 - 4] = 0x00003002; obj[0x25 - 4] = 0x00007FFF;               /* the DMA object: the ring */
    obj[0x26 - 4] = tnt2.fb_usable | 0x00000002; obj[0x27 - 4] = 0x00000002;
    for (i = 0; i < 0x28 - 0x04; i++)
        PRAMIN[0x0804 + i] = obj[i];
    if (tnt2.device == 0x0020) {                        /* (the first TNT only) */
        PRAMIN[0x0824] |= 0x00020000;
        PRAMIN[0x0826] += tnt2.bar1;
    }

    /* PGRAPH, before NV10. */
    PGRAPH[0x0080 / 4] = 0x000001FF;
    PGRAPH[0x0080 / 4] = 0x1230C000;
    PGRAPH[0x0084 / 4] = 0x72111101;
    PGRAPH[0x0088 / 4] = 0x11D5F071;
    PGRAPH[0x008C / 4] = 0x0004FF31;
    PGRAPH[0x008C / 4] = 0x4004FF31;
    PGRAPH[0x0140 / 4] = 0x00000000;
    PGRAPH[0x0100 / 4] = 0xFFFFFFFF;
    PGRAPH[0x0170 / 4] = 0x10010100;
    PGRAPH[0x0710 / 4] = 0xFFFFFFFF;
    PGRAPH[0x0720 / 4] = 0x00000001;
    PGRAPH[0x0810 / 4] = 0x00000000;
    PGRAPH[0x0608 / 4] = 0xFFFFFFFF;
    PGRAPH[0x053C / 4] = 0;
    PGRAPH[0x0540 / 4] = 0;
    PGRAPH[0x0544 / 4] = 0x00007FFF;
    PGRAPH[0x0548 / 4] = 0x00007FFF;

    /* PFIFO: channel 0 in DMA mode, reading the ring through the DMA object (instance 0x1209). */
    PFIFO[0x0140] = 0x00000000;
    PFIFO[0x0141] = 0x00000001;
    PFIFO[0x0480] = 0x00000000;
    PFIFO[0x0494] = 0x00000000;
    PFIFO[0x0481] = 0x00000100;
    PFIFO[0x0490] = 0x00000000;
    PFIFO[0x0491] = 0x00000000;
    PFIFO[0x048B] = 0x00001209;
    PFIFO[0x0400] = 0x00000000;
    PFIFO[0x0414] = 0x00000000;
    PFIFO[0x0084] = 0x03000100;
    PFIFO[0x0085] = 0x00000110;
    PFIFO[0x0086] = 0x00000112;
    PFIFO[0x0143] = 0x0000FFFF;
    PFIFO[0x0496] = 0x0000FFFF;
    PFIFO[0x0050] = 0x00000000;
    PFIFO[0x0040] = 0xFFFFFFFF;
    PFIFO[0x0415] = 0x00000001;
    PFIFO[0x048C] = 0x00000000;
    PFIFO[0x04A0] = 0x00000000;
    PFIFO[0x0489] = 0x000F0078;
    PFIFO[0x0488] = 0x00000001;
    PFIFO[0x0480] = 0x00000001;
    PFIFO[0x0494] = 0x00000001;
    PFIFO[0x0495] = 0x00000001;
    PFIFO[0x0140] = 0x00000001;

    /* The ring (nv_xaa.c's NVResetGraphics): no-ops, then the eight objects bound to subchannels 0-7. */
    ring = (volatile uint32_t *)(tnt2_fb + tnt2.fb_usable);
    for (i = 0; i < SKIPS; i++)
        ring[i] = 0;
    for (i = 0; i < 8; i++) {
        ring[SKIPS + 2 * i] = 0x00040000u | ((uint32_t)i << 13);   /* a method 0 (bind) on subchannel i */
        ring[SKIPS + 2 * i + 1] = 0x80000010u + (uint32_t)i;
    }
    ring_put = 0;
    ring_cur = 16 + SKIPS;
    ring_free = ring_max - ring_cur;
    cur_bpp = 0;
    cur_rop = -1;
    tnt2_kick();
    t0 = sys_time_us();
    while (read_get() != ring_put)
        if (sys_time_us() - t0 > 200000u)
            return -1;
    return 0;
}
