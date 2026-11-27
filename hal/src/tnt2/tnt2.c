/* tnt2.c - the Riva TNT2's register layer (docs/tnt2.md, T1): the chip found on PCI and mapped; its graphics engine,
 * FIFO and instance memory set up as X.org's xf86-video-nv sets up the NV4 family (nv_hw.c's NVLoadStateExt with no
 * display state: the BIOS's mode is left as it is); the command ring in video memory (nv_xaa.c's NVResetGraphics,
 * NVDmaWait, NVDmaKickoff, NVSync); the 2D operations: the target surface, the clip, solid fill, copies on the
 * target and from another surface, colour expansion and images from the CPU (nv_xaa.c's); the NV4's hardware
 * cursor (nv_cursor.c's, nv_hw.c's NVShowHideCursor) and the display start (NVSetStartAddress).
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
#include <stdlib.h>
#include <string.h>

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
    case 0x0110: return "GeForce2 MX";          /* (the NV11, docs/nv10.md: proven on bench E's Quadro2 MXR) */
    case 0x0111: return "GeForce2 MX 100/200";
    case 0x0112: return "GeForce2 Go";
    case 0x0113: return "Quadro2 MXR";
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
            tnt2.arch = (tnt2.device & 0x0FF0) == 0x0110 ? 10 : 4;
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
    if (tnt2.arch == 10) {
        /* The NV1x: its size in MB at PFB 0x10020C (nv_setup.c); the instance memory reversed over the top 512 KB
         * (NPRAMIN: instance offset X at size - 16 - X), so the layer's 128 KB goes below it. */
        tnt2.fb_size = PFB[0x020C / 4] & 0xFFF00000u;
        tnt2.fb_usable = tnt2.fb_size - (640u << 10);
        tnt2_fb = (volatile uint8_t *)sys_map_phys(tnt2.bar1, tnt2.fb_size);
        return tnt2_fb ? 0 : -1;
    }
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
static uint32_t ring_laps;                      /* times the CPU has gone round (tnt2_mark's positions) */
static const int ring_max = RING_DWORDS - 1;

/* The CPU's write-combining buffers out to memory: a locked instruction (warp.c's wc_drain). */
#if defined(__GNUC__) && defined(__i386__)
static void wc_out(void) { __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory"); }
#elif defined(__WATCOMC__) && defined(__386__)
void wc_out(void);
#pragma aux wc_out = "lock or dword ptr [esp], 0" modify exact [];
#else
static void wc_out(void) { }
#endif

static void write_put(int put)
{
    volatile uint8_t scratch = tnt2_fb[0];      /* (nv_local.h's WRITE_PUT: a read of video memory first, so the
                                                    ring's writes are out before the chip is told of them) */
    (void)scratch;
    wc_out();
    FIFO[0x0010] = (uint32_t)put << 2;
}

static int read_get(void)
{
    return (int)(FIFO[0x0011] >> 2);
}

/* The ring round where the chip stopped (a diagnostic): `before` dwords before its read pointer and `after` from it,
 * each line "ring N: value", to out. A header is (count << 18) | (subchannel << 13) | method. */
void tnt2_ring_dump(int before, int after, void (*out)(const char *line))
{
    static const char hex[] = "0123456789abcdef";
    char line[32];
    int get = read_get(), i, j;
    for (i = -before; i < after; i++) {
        int k = get + i;
        uint32_t v;
        if (k < 0) k += RING_DWORDS;
        if (k >= (int)RING_DWORDS) k -= RING_DWORDS;
        v = ring[k];
        memcpy(line, "ring ", 5);
        for (j = 0; j < 4; j++)
            line[5 + j] = (char)('0' + (k / (j == 0 ? 1000 : j == 1 ? 100 : j == 2 ? 10 : 1)) % 10);
        line[9] = i == 0 ? '*' : ' ';
        line[10] = ' ';
        for (j = 0; j < 8; j++)
            line[11 + j] = hex[(v >> (28 - 4 * j)) & 15u];
        line[19] = 0;
        out(line);
    }
}

static int trap_check(void);

static void ring_wait(int size)
{
    int get;
    uint32_t t0 = sys_time_us();
    size++;
    while (ring_free < size) {
        if (sys_time_us() - t0 > 20000u)
            trap_check();                               /* (a trapped chip resumed rather than written over) */
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
                ring_laps++;
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

/* The chip told of the commands written once 4 KB of them wait (a kick reads video memory first: not after every
 * small draw). */
static void kick_soon(void)
{
    int pending = ring_cur - ring_put;
    if (pending < 0)
        pending += ring_max;
    if (pending >= 1024)
        tnt2_kick();
}

/* A position in the command stream (laps round the ring, and the place in it), and whether the chip has done
 * everything written before it: its read pointer well past it (the margin taken to be more than the FIFO's cache
 * and PGRAPH's pipeline hold: an assumption, not a documented figure; docs/tnt2.md), or every command done. The chip is never more than a lap behind (ring_wait). */
#define MARK_MARGIN 1024

uint32_t tnt2_mark(void) { return ring_laps * RING_DWORDS + (uint32_t)ring_cur; }

static uint32_t chip_pos(void)
{
    uint32_t put_abs = ring_laps * RING_DWORDS + (uint32_t)ring_put;
    int get = read_get(), behind = ring_put - get;
    if (behind < 0)
        behind += ring_max;                     /* (the chip still in the lap before) */
    return put_abs - (uint32_t)behind;
}

int tnt2_mark_done(uint32_t mark)
{
    if ((int32_t)(tnt2_mark() - mark) >= (int32_t)(2 * RING_DWORDS))
        return 1;                               /* (two laps back: long done) */
    if ((int32_t)(chip_pos() - mark) >= MARK_MARGIN)
        return 1;
    return read_get() == ring_put && ring_cur == ring_put && PGRAPH[0x0700 / 4] == 0;
}

int tnt2_mark_wait(uint32_t mark, uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    if (tnt2_mark_done(mark))
        return 0;
    tnt2_kick();
    while (!tnt2_mark_done(mark))
        if (sys_time_us() - t0 > timeout_us)
            return -1;
    return 0;
}

void tnt2_kick(void)
{
    if (ring_cur != ring_put) {
        ring_put = ring_cur;
        write_put(ring_put);
    }
}

/* Asked by whoever waits for the chip (DOS-GL's swap polls it with a wait hook, SDL programs): commands written
 * but not yet told to the chip (kick_soon waits for 4 KB) go to it first, or it never gets there (Fifth Wheel
 * waited out the swap's 200 ms every frame on the P3). */
int tnt2_idle(void)
{
    if (ring_cur != ring_put)
        tnt2_kick();
    return read_get() == ring_put && PGRAPH[0x0700 / 4] == 0;
}

int tnt2_sync(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    tnt2_kick();
    while (read_get() != ring_put) {
        if (sys_time_us() - t0 > 20000u)
            trap_check();
        if (sys_time_us() - t0 > timeout_us)
            return -1;
    }
    while (PGRAPH[0x0700 / 4]) {                        /* PGRAPH_STATUS */
        if (sys_time_us() - t0 > 20000u)
            trap_check();
        if (sys_time_us() - t0 > timeout_us)
            return -1;
    }
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
#define RECT_EXPAND_ONE_COLOR_CLIP  0x0000C7ECu     /* clip, colour, size, point, then the data */
#define RECT_EXPAND_ONE_COLOR_DATA0 0x0000C800u
#define RECT_EXPAND_TWO_COLOR_CLIP  0x0000CBE4u     /* clip, colour 0, colour 1, size in, size out, point, data */
#define RECT_EXPAND_TWO_COLOR_DATA0 0x0000CC00u

static int cur_bpp, cur_rop = -1, three_d, stale_3d;  /* (stale_3d: a 2D operation since the 3D state went) */
static uint32_t tgt_off;
static int tgt_pitch;
static int clip_x0, clip_y0, clip_x1 = 0x7FFF, clip_y1 = 0x7FFF;   /* (applied in software: below) */

static void set_rop_copy(void)
{
    stale_3d = 1;
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
    stale_3d = 1;
    /* 32 bits: Y32 (0x0B, Mesa's surf2d_format for every 32-bit buffer), so fills write all 32 bits; the X
     * driver's X8R8G8B8 (6) leaves the top byte 0, which in a Z24S8 buffer is Z's (the P3, 2026-11-19). */
    if (bpp == 32) surface = 0x0000000B, other = 0x00000003;
    else if (bpp == 16) surface = 0x00000004, other = 0x00000001;
    else surface = 0x00000001, other = 0x00000003;
    ring_start(SURFACE_FORMAT, 4);
    NEXT(surface);
    NEXT((uint32_t)pitch_bytes | ((uint32_t)pitch_bytes << 16));   /* (the source's pitch, the destination's) */
    NEXT(off);
    NEXT(off);
    tgt_off = off;
    tgt_pitch = pitch_bytes;
    if (bpp != cur_bpp) {
        ring_start(PATTERN_FORMAT, 1); NEXT(other);
        ring_start(RECT_FORMAT, 1);    NEXT(other);
        if (!three_d) {                                 /* (subchannel 4 is the 3D surfaces' once 3D is on) */
            ring_start(LINE_FORMAT, 1);
            NEXT(other);
        }
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

void tnt2_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h)
{
    if (dx < clip_x0) { int d = clip_x0 - dx; sx += d, w -= d, dx = clip_x0; }
    if (dy < clip_y0) { int d = clip_y0 - dy; sy += d, h -= d, dy = clip_y0; }
    if (dx + w > clip_x1) w = clip_x1 - dx;
    if (dy + h > clip_y1) h = clip_y1 - dy;
    if (w <= 0 || h <= 0)
        return;
    set_rop_copy();
    ring_start(SURFACE_PITCH, 3);                       /* (the source's pitch and offset, as nv_xaa.c's image write) */
    NEXT((uint32_t)src_pitch_bytes | ((uint32_t)tgt_pitch << 16));
    NEXT(src_off);
    NEXT(tgt_off);
    ring_start(BLIT_POINT_SRC, 3);
    NEXT(((uint32_t)sy << 16) | (uint32_t)sx);
    NEXT(((uint32_t)dy << 16) | (uint32_t)dx);
    NEXT(((uint32_t)h << 16) | (uint32_t)w);
    ring_start(SURFACE_PITCH, 3);                       /* the target's again */
    NEXT((uint32_t)tgt_pitch | ((uint32_t)tgt_pitch << 16));
    NEXT(tgt_off);
    NEXT(tgt_off);
    tnt2_kick();
}

/* Colour expansion (nv_xaa.c's scanline CPU-to-screen colour expansion): the bits go in the ring, rows padded to
 * 32 pixels, the leftmost pixel in a dword's bit 0 (the driver's BIT_ORDER_IN_BYTE_LSBFIRST); the command's clip
 * trims the padding and is the layer's clip. Colours carry ones above the depth, as the driver's. Only the visible
 * part of each row is sent, so a rectangle may start anywhere. */
static uint32_t above_depth(void)
{
    return cur_bpp == 8 ? 0xFFFFFF00u : cur_bpp == 16 ? 0xFFFF0000u : 0xFF000000u;
}

void tnt2_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, unsigned flags)
{
    int vx0 = x > clip_x0 ? x : clip_x0, vy0 = y > clip_y0 ? y : clip_y0;
    int vx1 = x + w < clip_x1 ? x + w : clip_x1, vy1 = y + h < clip_y1 ? y + h : clip_y1;
    int vw, vh, bw, dwords, r, d, b;
    uint32_t data, m = above_depth();
    if (vx0 >= vx1 || vy0 >= vy1)
        return;
    vw = vx1 - vx0;
    vh = vy1 - vy0;
    bw = (vw + 31) & ~31;
    dwords = bw >> 5;
    set_rop_copy();
    if (flags & TNT2_MONO_TRANSPARENT) {
        ring_start(RECT_EXPAND_ONE_COLOR_CLIP, 5);
        NEXT(((uint32_t)vy0 << 16) | (uint32_t)vx0);
        NEXT(((uint32_t)vy1 << 16) | (uint32_t)vx1);
        NEXT(fg | m);
        NEXT(((uint32_t)vh << 16) | (uint32_t)bw);
        NEXT(((uint32_t)vy0 << 16) | (uint32_t)vx0);
        data = RECT_EXPAND_ONE_COLOR_DATA0;
    } else {
        ring_start(RECT_EXPAND_TWO_COLOR_CLIP, 7);
        NEXT(((uint32_t)vy0 << 16) | (uint32_t)vx0);
        NEXT(((uint32_t)vy1 << 16) | (uint32_t)vx1);
        NEXT(bg | m);
        NEXT(fg | m);
        NEXT(((uint32_t)vh << 16) | (uint32_t)bw);
        NEXT(((uint32_t)vh << 16) | (uint32_t)bw);
        NEXT(((uint32_t)vy0 << 16) | (uint32_t)vx0);
        data = RECT_EXPAND_TWO_COLOR_DATA0;
    }
    for (r = 0; r < vh; r++) {
        const uint8_t *row = bits + (long)(vy0 - y + r) * stride;
        int c0 = vx0 - x;                               /* the row's first visible pixel, in the caller's bits */
        ring_start(data, dwords);
        for (d = 0; d < dwords; d++) {
            uint32_t word = 0;
            for (b = 0; b < 32; b++) {
                int c = d * 32 + b;
                if (c >= vw)
                    break;
                c += c0;
                if ((row[c >> 3] >> (7 - (c & 7))) & 1)
                    word |= 1u << b;
            }
            NEXT(word);
        }
    }
    ring_start(BLIT_POINT_SRC, 1);                      /* (nv_xaa.c: "hardware bug workaround") */
    NEXT(0);
    tnt2_kick();
}

/* Images from the CPU (nv_xaa.c's scanline image write, a band at a time): rows copied into a scratch area of
 * video memory, then blitted from there to the target. The scratch is 64 KB after the ring, in the top 128 KB
 * the layer keeps; the engine finishes with it before it is filled again. */
#define SCRATCH_OFF   (tnt2.fb_usable + RING_DWORDS * 4u)
#define SCRATCH_BYTES 0x10000u

void tnt2_image(int x, int y, int w, int h, const void *px, int stride)
{
    int bpp_b = cur_bpp / 8, spitch = (w * bpp_b + 63) & ~63, band, r0, r, y0 = y, y1 = y + h;
    if (w <= 0 || h <= 0 || x >= clip_x1 || x + w <= clip_x0)
        return;
    if (y0 < clip_y0) y0 = clip_y0;
    if (y1 > clip_y1) y1 = clip_y1;
    band = (int)(SCRATCH_BYTES / (uint32_t)spitch);
    for (r0 = y0; r0 < y1; r0 += band) {
        int n = y1 - r0 < band ? y1 - r0 : band;
        tnt2_sync(1000000);
        for (r = 0; r < n; r++) {
            const uint8_t *src = (const uint8_t *)px + (long)(r0 - y + r) * stride;
            volatile uint8_t *dst = tnt2_fb + SCRATCH_OFF + (uint32_t)(r * spitch);
            int i;
            for (i = 0; i < w * bpp_b; i++)
                dst[i] = src[i];
        }
        tnt2_copy_from(SCRATCH_OFF, spitch, 0, 0, x, r0, w, n);
    }
}

/* ---- the NV4's cursor and the display start ---- */

/* The CRTC's registers through the chip's VGA window (PCIO, nv_setup.c's), its extended ones unlocked by CR1F. */
#define PCIO8 ((volatile uint8_t *)tnt2_regs + 0x601000)
static void crtc_w(int i, uint8_t v) { PCIO8[0x3D4] = (uint8_t)i; PCIO8[0x3D5] = v; }
static uint8_t crtc_r(int i) { PCIO8[0x3D4] = (uint8_t)i; return PCIO8[0x3D5]; }

/* The rows' length the display shows, in bytes (a multiple of 8): the CRTC's offset in 8-byte units, its low 8
 * bits in CR13 and bits 10-8 in CR19's bits 7-5 (nv_hw.c's), the rest of CR19 kept. */
int tnt2_set_pitch(int pitch_bytes)
{
    unsigned u = (unsigned)pitch_bytes >> 3;
    if (tnt2.arch == 10)
        return -1;                      /* (the NV11's CRTC: not written until it's proven safe, docs/nv10.md) */
    crtc_w(0x1F, 0x57);
    crtc_w(0x13, (uint8_t)(u & 0xFF));
    crtc_w(0x19, (uint8_t)((crtc_r(0x19) & 0x1F) | ((u >> 8) & 7) << 5));
    return 0;
}

/* 32 x 32 pixels of A1R5G5B5 in instance memory at dword 0x1E00 (nv_setup.c's NV4 CURSOR; CR30, CR31 and CR2F
 * point there: 0x00, 0xBC, 0x00, nv_hw.c's), alpha 0 transparent. Built from the caller's masks and colours as
 * nv_cursor.c's ConvertCursor1555. */
static uint8_t cursor_and[512], cursor_xor[512];
static uint32_t cursor_c0 = 0x8000, cursor_c1 = 0xFFFF;

static uint32_t to1555(uint32_t c)
{
    return 0x8000u | ((c >> 9) & 0x7C00u) | ((c >> 6) & 0x03E0u) | ((c >> 3) & 0x001Fu);
}

static void cursor_build(void)
{
    int y, x;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x += 2) {
            uint32_t pair = 0;
            int k;
            for (k = 0; k < 2; k++) {
                int i = y * 8 + ((x + k) >> 3), s = 7 - ((x + k) & 7);
                int a = (cursor_and[i] >> s) & 1, xo = (cursor_xor[i] >> s) & 1;
                uint32_t v = !a ? (xo ? cursor_c1 : cursor_c0) : xo ? cursor_c1 : 0u;   /* (no inversion: colour 1) */
                pair |= v << (16 * k);
            }
            PRAMIN[0x1E00 + (y * 32 + x) / 2] = pair;
        }
}

int tnt2_cursor_load(const uint8_t and_mask[512], const uint8_t xor_mask[512])
{
    int i;
    if (tnt2.arch == 10)
        return -1;                      /* (the NV1x's cursor needs CR30/CR31/CR2F behind CR1F: not yet, docs/nv10.md) */
    for (i = 0; i < 512; i++)
        cursor_and[i] = and_mask[i], cursor_xor[i] = xor_mask[i];
    cursor_build();
    crtc_w(0x1F, 0x57);
    crtc_w(0x30, 0x00);
    crtc_w(0x31, (uint8_t)(0xBC | (crtc_r(0x31) & 0x03)));     /* (the mode's double scan, bit 1, and shown, bit 0) */
    crtc_w(0x2F, 0x00);
    return 0;
}

void tnt2_cursor_colours(uint32_t c0, uint32_t c1)
{
    if (tnt2.arch == 10)
        return;
    cursor_c0 = to1555(c0);
    cursor_c1 = to1555(c1);
    cursor_build();
}

void tnt2_cursor_show(int on)
{
    if (tnt2.arch == 10)
        return;
    crtc_w(0x1F, 0x57);
    crtc_w(0x31, (uint8_t)((crtc_r(0x31) & 0xFE) | (on ? 1 : 0)));
}

/* The palette as a gamma ramp in direct-colour modes: the RAMDAC's general control (PRAMDAC 0x600) bits 4-5 send
 * each colour component through the VGA DAC, indexed by its value (nv_hw.c sets 0x30 for any depth above 8; X's
 * NVDACLoadPalette expands the 5- and 6-bit fields to 8 bits, so a 256-entry ramp serves every depth). The DAC's
 * entries are 8-bit when the general control's bit 20 is set (taken, as nv_hw.c's direct-colour modes have it),
 * else 6-bit. tnt2_ramp_off puts the general control back as it was. */
static uint32_t ramp_general;
static int ramp_on;

void tnt2_set_ramp(const uint8_t ramp[256])
{
    int i, shift;
    if (!ramp_on) {
        ramp_general = tnt2_regs[0x680600 / 4];
        ramp_on = 1;
    }
    tnt2_regs[0x680600 / 4] = ramp_general | 0x30u;
    shift = (ramp_general & 0x00100000u) ? 0 : 2;
    sys_outb(0x3C6, 0xFF);
    sys_outb(0x3C8, 0);
    for (i = 0; i < 256; i++) {
        uint8_t v = (uint8_t)(ramp[i] >> shift);
        sys_outb(0x3C9, v), sys_outb(0x3C9, v), sys_outb(0x3C9, v);
    }
}

void tnt2_ramp_off(void)
{
    if (ramp_on) {
        tnt2_regs[0x680600 / 4] = ramp_general;
        ramp_on = 0;
    }
}

/* (PRAMDAC 0x300 keeps 12 bits of each coordinate, signed: the P3's M64 read -5 back as 0xFFB.) */
void tnt2_cursor_move(int x, int y)
{
    if (tnt2.arch == 10)
        return;
    tnt2_regs[0x680300 / 4] = ((uint32_t)x & 0xFFFFu) | (((uint32_t)y & 0xFFFFu) << 16);
}

int tnt2_set_start(uint32_t byte_off)
{
    if (byte_off & 3u || byte_off >= tnt2.fb_size)
        return -1;
    tnt2_regs[0x600800 / 4] = byte_off;
    return 0;
}

int tnt2_vsync_wait(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
#if defined(MGA_OW)
    if (timeout_us < 110000u)
        timeout_us = 110000u;                   /* (Watcom's clock is the BIOS tick: 55 ms steps, two at least) */
#endif
    while (sys_inb(0x3DA) & 0x08)
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    while (!(sys_inb(0x3DA) & 0x08))
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return 0;
}

/* ---- 3D (T2): the 3D surfaces (class 0x53) and the textured triangle (0x54), as Mesa's classic nouveau driver
 * drives them (MIT; docs/tnt2.md lists what was taken). Objects as the X driver's 2D ones: instance words written
 * here (class, patch valid; the DMA instances in word 2: VRAM, instance 0x1200) and found through RAMHT, so no DMA
 * method is sent. Only methods the hardware itself handles are used: on the NV04 family the 3D surfaces' clip
 * methods (0x2F8, 0x2FC) trap to a driver (nouveau emulates them), and with no interrupt handler here a trap would
 * stall the engine; the 3D clip is written into PGRAPH instead (0x40053C-0x400548, as nouveau does). ---- */

#define SUBCH(s, m) (((uint32_t)(s) << 13) | (uint32_t)(m))
static uint32_t clip_now[4];                    /* (the 3D clip PGRAPH has: tnt2_3d_clip) */
static int tri_obj;                             /* (the object on subchannel 7: below) */
static void forget_3d(void);                    /* (the words cached as sent: the engine set up again) */
#define S3D 4                                   /* subchannel 4: the 3D surfaces (the line object's before) */
#define TRI 7                                   /* subchannel 7: the textured triangle (the scaled image's before) */
#define H_SURF3D 0x80000018u                    /* (RAMHT slots 8 and 9: the X driver's handles hash 0x80000010+i to i) */
#define H_TTRI   0x80000019u
#define H_MTRI   0x8000001Au                    /* (RAMHT slot 10) */
#define H_DMAVRAM 0x8000001Bu                   /* (RAMHT slot 11: the video memory's DMA object, the NV1x's 0x93) */
#define DUMMY_TEX_OFF (tnt2.fb_usable + RING_DWORDS * 4u + SCRATCH_BYTES)   /* 1x1 A8R8G8B8 white, 128-byte aligned */

uint32_t tnt2_tex_dummy_off(void) { return DUMMY_TEX_OFF; }

/* (Bring-up experiments: raw methods on the 3D subchannels, the layer's caching bypassed.) */
void tnt2_3d_raw(int sub3d, uint32_t method, uint32_t value)
{
    ring_start(SUBCH(sub3d ? S3D : TRI, method), 1);
    NEXT(value);
}
uint32_t tnt2_3d_handle(int sub3d) { return sub3d ? 0x80000018u : 0x80000019u; }

uint32_t tnt2_3d_trap(uint32_t *addr, uint32_t *data)
{
    if (addr) *addr = PGRAPH[0x0704 / 4];       /* PGRAPH_TRAPPED_ADDR: the method (subchannel, method) */
    if (data) *data = PGRAPH[0x0708 / 4];       /* PGRAPH_TRAPPED_DATA */
    return PGRAPH[0x0100 / 4];                  /* PGRAPH_INTR */
}


/* ---- Celsius: the NV1x's own 3D class (0x96; docs/nv10.md, the Quadro2 plan's Q3) ------------------------------
 * Set up as Mesa's NV11 driver set it up on bench E's Quadro2 under nouveau (captured 2026-11-27: the method/value
 * pairs below are its init and default state, in its order, MIT driver, facts only), with every DMA handle on the
 * video memory's DMA object (class 0x3D): DOS has no GART, so vertex buffers live in video memory too. */
#define H_CELSIUS 0x8000001Cu                   /* (RAMHT slot 12, instance 0x120D) */
static const struct { uint16_t m; uint32_t v; } celsius_init[] = {
    { 0x0100, 0x00000000 }, { 0x0200, 0x00000000 }, { 0x0204, 0x00000000 }, { 0x02C0, 0x07FF0800 }, { 0x02E0, 0x07FF0800 }, { 0x02C4, 0x00000000 },
    { 0x02E4, 0x00000000 }, { 0x02C8, 0x00000000 }, { 0x02E8, 0x00000000 }, { 0x02CC, 0x00000000 }, { 0x02EC, 0x00000000 }, { 0x02D0, 0x00000000 },
    { 0x02F0, 0x00000000 }, { 0x02D4, 0x00000000 }, { 0x02F4, 0x00000000 }, { 0x02D8, 0x00000000 }, { 0x02F8, 0x00000000 }, { 0x02DC, 0x00000000 },
    { 0x02FC, 0x00000000 }, { 0x0290, 0x00100001 }, { 0x03F4, 0x00000000 }, { 0x0100, 0x00000000 }, { 0x0120, 0x00000000 }, { 0x0124, 0x00000001 },
    { 0x0128, 0x00000002 }, { 0x0100, 0x00000000 }, { 0x0100, 0x00000000 }, { 0x02A4, 0x00000000 }, { 0x0300, 0x00000000 }, { 0x033C, 0x00000207 },
    { 0x0340, 0x00000000 }, { 0x0228, 0x00000000 }, { 0x022C, 0x00000000 }, { 0x0304, 0x00000000 }, { 0x0310, 0x00000001 }, { 0x0314, 0x00000000 },
    { 0x0320, 0x00000000 }, { 0x0328, 0x00000000 }, { 0x032C, 0x00000000 }, { 0x0344, 0x00000001 }, { 0x0348, 0x00000000 }, { 0x034C, 0x00000000 },
    { 0x0350, 0x00008006 }, { 0x0360, 0x000000FF }, { 0x0364, 0x00000207 }, { 0x0368, 0x00000000 }, { 0x036C, 0x000000FF }, { 0x0370, 0x00001E00 },
    { 0x0374, 0x00001E00 }, { 0x0378, 0x00001E00 }, { 0x037C, 0x00001D01 }, { 0x03A4, 0x00000000 }, { 0x02A4, 0x00000000 }, { 0x02A8, 0x00000000 },
    { 0x0294, 0x00000000 }, { 0x03B8, 0x00000000 }, { 0x03BC, 0x00000000 }, { 0x0330, 0x00000000 }, { 0x0334, 0x00000000 }, { 0x0338, 0x00000000 },
    { 0x0354, 0x00000201 }, { 0x035C, 0x00000000 }, { 0x030C, 0x00000000 }, { 0x0384, 0x00000000 }, { 0x0388, 0x00000000 }, { 0x03EC, 0x00000008 },
    { 0x0318, 0x00000000 }, { 0x031C, 0x00000000 }, { 0x0380, 0x00000008 }, { 0x0320, 0x00000000 }, { 0x038C, 0x00001B02 }, { 0x0390, 0x00001B02 },
    { 0x039C, 0x00000405 }, { 0x03A0, 0x00000901 }, { 0x0324, 0x00000000 }, { 0x0308, 0x00000000 }, { 0x03C0, 0x00000000 }, { 0x03C4, 0x00000000 },
    { 0x03C8, 0x00000000 }, { 0x03CC, 0x00000000 }, { 0x03D0, 0x00000000 }, { 0x03D4, 0x00000000 }, { 0x03D8, 0x00000000 }, { 0x03DC, 0x00000000 },
    { 0x03E0, 0x00000000 }, { 0x03E4, 0x00000000 }, { 0x0680, 0x3FC00000 }, { 0x0684, 0xBDB8AA0A }, { 0x0688, 0x00000000 }, { 0x0100, 0x00000000 },
    { 0x029C, 0x00000802 }, { 0x02A0, 0x00000002 }, { 0x03E8, 0x00000006 }, { 0x0358, 0x01010101 }, { 0x0C50, 0x3F800000 }, { 0x0C54, 0x00000000 },
    { 0x0C58, 0x00000000 }, { 0x0C5C, 0x3F800000 }, { 0x0C80, 0x00000000 }, { 0x0C84, 0x00000000 }, { 0x0C88, 0x00000000 }, { 0x0C30, 0x00000000 },
    { 0x0C34, 0x00000000 }, { 0x0C38, 0x3F800000 }, { 0x0CA0, 0x00000000 }, { 0x0CA4, 0x00000000 }, { 0x0CA8, 0x00000000 }, { 0x0CAC, 0x3F800000 },
    { 0x0CC8, 0x00000000 }, { 0x0CCC, 0x00000000 }, { 0x0CD0, 0x00000000 }, { 0x0CD4, 0x3F800000 }, { 0x0CE0, 0x00000000 }, { 0x0CEC, 0x00000001 },
    { 0x0394, 0x00000000 }, { 0x0398, 0x4B800000 }, { 0x0300, 0x00000000 }, { 0x033C, 0x00000207 }, { 0x0340, 0x00000000 }, { 0x034C, 0x00000000 },
    { 0x0304, 0x00000000 }, { 0x0350, 0x00008006 }, { 0x0344, 0x00000001 }, { 0x0348, 0x00000000 }, { 0x0358, 0x01010101 }, { 0x0298, 0x00000000 },
    { 0x0308, 0x00000000 }, { 0x039C, 0x00000405 }, { 0x03A0, 0x00000901 }, { 0x030C, 0x00000000 }, { 0x035C, 0x00000001 }, { 0x0354, 0x00000201 },
    { 0x0310, 0x00000001 }, { 0x0288, 0x20042000 }, { 0x028C, 0x20001400 }, { 0x0100, 0x00000000 }, { 0x0100, 0x00000000 }, { 0x0100, 0x00000000 },
    { 0x0100, 0x00000000 }, { 0x0100, 0x00000000 }, { 0x0100, 0x00000000 }, { 0x030C, 0x00000000 }, { 0x035C, 0x00000001 }, { 0x0354, 0x00000201 },
    { 0x029C, 0x00000800 }, { 0x02A0, 0x00000003 }, { 0x02A4, 0x00000000 }, { 0x02A8, 0x00000000 }, { 0x0680, 0x3FC00000 }, { 0x0684, 0xBDB851EC },
    { 0x0688, 0x00000000 }, { 0x0288, 0x20042000 }, { 0x028C, 0x20001400 }, { 0x03BC, 0x00000000 }, { 0x0314, 0x00000000 }, { 0x03A4, 0x00000000 },
    { 0x03B8, 0x00000000 }, { 0x0294, 0x00000000 }, { 0x0834, 0x00000000 }, { 0x0838, 0x00000000 }, { 0x083C, 0x00000000 }, { 0x0828, 0x00000000 },
    { 0x082C, 0x00000000 }, { 0x0830, 0x00000000 }, { 0x08B4, 0x00000000 }, { 0x08B8, 0x00000000 }, { 0x08BC, 0x00000000 }, { 0x08A8, 0x00000000 },
    { 0x08AC, 0x00000000 }, { 0x08B0, 0x00000000 }, { 0x0934, 0x00000000 }, { 0x0938, 0x00000000 }, { 0x093C, 0x00000000 }, { 0x0928, 0x00000000 },
    { 0x092C, 0x00000000 }, { 0x0930, 0x00000000 }, { 0x09B4, 0x00000000 }, { 0x09B8, 0x00000000 }, { 0x09BC, 0x00000000 }, { 0x09A8, 0x00000000 },
    { 0x09AC, 0x00000000 }, { 0x09B0, 0x00000000 }, { 0x0A34, 0x00000000 }, { 0x0A38, 0x00000000 }, { 0x0A3C, 0x00000000 }, { 0x0A28, 0x00000000 },
    { 0x0A2C, 0x00000000 }, { 0x0A30, 0x00000000 }, { 0x0AB4, 0x00000000 }, { 0x0AB8, 0x00000000 }, { 0x0ABC, 0x00000000 }, { 0x0AA8, 0x00000000 },
    { 0x0AAC, 0x00000000 }, { 0x0AB0, 0x00000000 }, { 0x0B34, 0x00000000 }, { 0x0B38, 0x00000000 }, { 0x0B3C, 0x00000000 }, { 0x0B28, 0x00000000 },
    { 0x0B2C, 0x00000000 }, { 0x0B30, 0x00000000 }, { 0x0BB4, 0x00000000 }, { 0x0BB8, 0x00000000 }, { 0x0BBC, 0x00000000 }, { 0x0BA8, 0x00000000 },
    { 0x0BAC, 0x00000000 }, { 0x0BB0, 0x00000000 }, { 0x0380, 0x00000008 }, { 0x0320, 0x00000000 }, { 0x0D40, 0x00000000 }, { 0x0D44, 0x00001503 },
    { 0x06C4, 0x00000000 }, { 0x06C8, 0x00000000 }, { 0x06CC, 0x00000000 }, { 0x03B4, 0x3F800000 }, { 0x06A0, 0x00000000 }, { 0x06A4, 0x3F800000 },
    { 0x06A8, 0x00000000 }, { 0x06AC, 0x00000000 }, { 0x06B0, 0x3F800000 }, { 0x06B4, 0x00000000 }, { 0x03EC, 0x00000008 }, { 0x031C, 0x00000000 },
    { 0x038C, 0x00001B02 }, { 0x0390, 0x00001B02 }, { 0x0324, 0x00000000 }, { 0x0330, 0x00000000 }, { 0x0334, 0x00000000 }, { 0x0338, 0x00000000 },
    { 0x0384, 0x00000000 }, { 0x0388, 0x00000000 }, { 0x0200, 0x02800000 }, { 0x0204, 0x01E00000 }, { 0x037C, 0x00001D01 }, { 0x032C, 0x00000000 },
    { 0x0364, 0x00000207 }, { 0x0368, 0x00000000 }, { 0x036C, 0x000000FF }, { 0x0360, 0x000000FF }, { 0x0370, 0x00001E00 }, { 0x0374, 0x00001E00 },
    { 0x0378, 0x00001E00 }, { 0x0260, 0x00000000 }, { 0x0268, 0x00000000 }, { 0x0270, 0x00000000 }, { 0x0278, 0x00000000 }, { 0x0280, 0x00000000 },
    { 0x0288, 0x20042000 }, { 0x028C, 0x20001400 }, { 0x0264, 0x00000000 }, { 0x026C, 0x00000000 }, { 0x0274, 0x00000000 }, { 0x027C, 0x00000000 },
    { 0x0284, 0x18000000 }, { 0x0288, 0x20042000 }, { 0x028C, 0x20001400 }, { 0x03C0, 0x00000000 }, { 0x03C4, 0x00000000 }, { 0x03C8, 0x00000000 },
    { 0x03CC, 0x00000000 }, { 0x03D0, 0x00000000 }, { 0x03D4, 0x00000000 }, { 0x03D8, 0x00000000 }, { 0x03DC, 0x00000000 }, { 0x03E0, 0x00000000 },
    { 0x03E4, 0x00000000 }, { 0x0228, 0x00000000 }, { 0x022C, 0x00000000 }, { 0x02C0, 0x0A7F0800 }, { 0x02E0, 0x09DF0800 }, { 0x0394, 0x00000000 },
    { 0x0398, 0x4A000000 }, { 0x02C0, 0x0A7F0800 }, { 0x02E0, 0x09DF0800 },
};

void tnt2_methods(int sub, uint32_t mthd, const uint32_t *v, int n)
{
    int i;
    ring_start(SUBCH(sub, mthd), n);
    for (i = 0; i < n; i++)
        NEXT(v[i]);
}

static void cel1(uint32_t mthd, uint32_t v)
{
    ring_start(SUBCH(TRI, mthd), 1);
    NEXT(v);
}

int tnt2_celsius_init(void)
{
    static const uint32_t h3[3] = { H_DMAVRAM, H_DMAVRAM, H_DMAVRAM };
    unsigned i;
    if (tnt2.arch != 10)
        return -1;
    /* (word 0 bit 22, the NV11's "new" object: without it the render target's depth may only be Z24S8, envytools'
     * hwtest MthdSurf3DFormat; DCEL0's Z16 with R5G6B5 trapped there, PGRAPH_INTR bit 20, on bench E) */
    PRAMIN[0x0834] = 0x01400096; PRAMIN[0x0835] = 0; PRAMIN[0x0836] = 0x12001200; PRAMIN[0x0837] = 0;
    PRAMIN[22] = H_DMAVRAM, PRAMIN[23] = 0x80011200;
    PRAMIN[24] = H_CELSIUS, PRAMIN[25] = 0x8001120D;
    cel1(0, H_CELSIUS);                                 /* (subchannel 7: the 0x94/0x95 objects' until now) */
    tri_obj = 0x96;
    cel1(0x180, H_DMAVRAM);                             /* DMA_NOTIFY */
    tnt2_methods(TRI, 0x184, h3, 3);                    /* DMA_TEXTURE0, DMA_TEXTURE1, DMA_VTXBUF */
    tnt2_methods(TRI, 0x194, h3, 2);                    /* DMA_COLOR, DMA_ZETA */
    for (i = 0; i < sizeof celsius_init / sizeof celsius_init[0]; i++)
        cel1(celsius_init[i].m, celsius_init[i].v);
    tnt2_kick();
    return tnt2_sync(1000000);
}

/* The render target: colour and depth offsets, the format (16 bits: R5G6B5 with Z16; 32: X8R8G8B8 with Z24S8),
 * the pitches, the window, and the viewport's translation into Celsius's 2048-offset space (Mesa's nv10_state_fb.c:
 * 6 no-ops before a target change on the NV11, "nv11 seems to get sad"). */
void tnt2_celsius_target(uint32_t colour, uint32_t zeta, int pitch_bytes, int w, int h, int bpp)
{
    int i;
    for (i = 0; i < 6; i++)
        cel1(0x100, 0);
    cel1(0x210, colour);
    cel1(0x214, zeta);
    cel1(0x208, bpp == 32 ? 0x105u : 0x113u);         /* TYPE linear | DEPTH Z24S8 0 / Z16 1 | COLOR X8R8G8B8 5 / R5G6B5 3 */
    cel1(0x20C, (uint32_t)pitch_bytes << 16 | (uint32_t)pitch_bytes);
    cel1(0x200, (uint32_t)w << 16);
    cel1(0x204, (uint32_t)h << 16);
}

/* (DCEL0's draw) n vertices of {r, g, b, x, y} floats at video memory offset vb, as GL_TRIANGLES: Mesa's software-
 * transform path's way (window coordinates, the projection the viewport's undoing, a batch from vertex 0). */
void tnt2_celsius_tris(uint32_t vb, int n, int w, int h)
{
    union { float f; uint32_t u; } c;
    uint32_t pm[16], fmt[8];
    int i;
    c.f = -2048.0f + w / 2.0f; cel1(0x6E8, c.u);
    c.f = -2048.0f + h / 2.0f; cel1(0x6EC, c.u);
    c.f = 32767.5f;            cel1(0x6F0, c.u);
    cel1(0x6F4, 0);
    for (i = 0; i < 16; i++)
        pm[i] = 0;
    c.f = 1.0f;                 pm[0] = c.u, pm[15] = c.u;
    c.f = -w / 2.0f;            pm[3] = c.u;
    c.f = -1.0f;                pm[5] = c.u;
    c.f = h / 2.0f;             pm[7] = c.u;
    c.f = -32767.5f;            pm[10] = c.u;
    tnt2_methods(TRI, 0x500, pm, 16);                   /* PROJECTION_MATRIX, row-major */
    fmt[0] = 0x1422;                                    /* position: stride 20, x and y, floats */
    fmt[1] = 0x1432;                                    /* colour: stride 20, r g b, floats */
    for (i = 2; i < 8; i++)
        fmt[i] = 0x0002;                                /* (the rest unused) */
    for (i = 0; i < 8; i++)
        cel1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    cel1(0xD00, vb + 12);                               /* VTXBUF_OFFSET(0): the position after the colour */
    cel1(0xD08, vb);                                    /* VTXBUF_OFFSET(1): the colour */
    cel1(0xCF0, 0);                                     /* VTXBUF_VALIDATE */
    cel1(0x13FC, 5);                                    /* BEGIN: GL_TRIANGLES + 1 */
    cel1(0x1400, (uint32_t)(n - 1) << 24);              /* a batch: n vertices from 0 */
    cel1(0x13FC, 0);                                    /* END */
    tnt2_kick();
}

int tnt2_3d_init(void)
{
    clip_now[0] = ~0u;                                  /* (the engine was set up again: PGRAPH's clip unknown) */
    forget_3d();
    /* The objects: 0x53 at instance 0x120A (PRAMIN dwords 0x828-0x82B), 0x54 at 0x120B (0x82C-0x82F). */
    {   /* (the NV1x's same classes, method for method, at 0x93/0x94/0x95: emulated on its Celsius pipeline) */
        uint32_t nv10 = tnt2.arch == 10 ? 0x40u : 0u;
        PRAMIN[0x0828] = 0x01000053 + nv10; PRAMIN[0x0829] = 0; PRAMIN[0x082A] = 0x12001200; PRAMIN[0x082B] = 0;
        PRAMIN[0x082C] = 0x01000054 + nv10; PRAMIN[0x082D] = 0; PRAMIN[0x082E] = 0x12001200; PRAMIN[0x082F] = 0;
        PRAMIN[0x0830] = 0x01000055 + nv10; PRAMIN[0x0831] = 0; PRAMIN[0x0832] = 0x12001200; PRAMIN[0x0833] = 0;
    }
    PRAMIN[16] = H_SURF3D; PRAMIN[17] = 0x8001120A;
    PRAMIN[18] = H_TTRI;   PRAMIN[19] = 0x8001120B;
    PRAMIN[20] = H_MTRI;   PRAMIN[21] = 0x8001120C;
    if (tnt2.arch == 10)
        PRAMIN[22] = H_DMAVRAM, PRAMIN[23] = 0x80011200;
    tnt2_fb[DUMMY_TEX_OFF + 0] = 0xFF; tnt2_fb[DUMMY_TEX_OFF + 1] = 0xFF;
    tnt2_fb[DUMMY_TEX_OFF + 2] = 0xFF; tnt2_fb[DUMMY_TEX_OFF + 3] = 0xFF;
    three_d = 1;
    ring_start(SUBCH(S3D, 0), 1); NEXT(H_SURF3D);
    if (tnt2.arch == 10) {                              /* (the NV1x's 0x93: its colour and depth memory by method) */
        ring_start(SUBCH(S3D, 0x184), 2);
        NEXT(H_DMAVRAM);
        NEXT(H_DMAVRAM);
    }
    ring_start(SUBCH(TRI, 0), 1); NEXT(H_MTRI);         /* the multitexture object's SURFACES first */
    ring_start(SUBCH(TRI, 0x18C), 1); NEXT(H_SURF3D);
    ring_start(SUBCH(TRI, 0), 1); NEXT(H_TTRI);
    ring_start(SUBCH(TRI, 0x18C), 1); NEXT(H_SURF3D);     /* SURFACES: the 3D surfaces object (in hardware) */
    tri_obj = 0x54;
    return tnt2_sync(200000) == 0 && PGRAPH[0x0100 / 4] == 0 ? 0 : -1;   /* (PGRAPH_INTR: nothing trapped) */
}

/* The colour buffer at color_off and the Z buffer at zeta_off, both pitch_bytes apart (a multiple of 64; the
 * Z buffer's bits a pixel are the colour's: 16-bit Z with 16-bit colour, 24-bit Z and 8 of stencil with 32), the
 * 3D clip w x h. */
static struct { uint32_t color, zeta; int pitch, bpp, w, h, set; } t3d;
static uint32_t st3d[6];
static int st3d_set;
static uint32_t mt3d[16];               /* the multitexture object's words, sent while it is the one in use */
static int mt3d_set, tri_obj;           /* tri_obj: 0x54 or 0x55, the object bound to subchannel 7 */

/* ---- Traps. With no interrupt handler, a method the chip traps on (PGRAPH_INTR set) stops the engine for good, and
 * the CPU then waits on a full ring. When a wait has gone 20 ms without progress the trap is said (the first one: the
 * registers, the words the layer last sent, the ring round the chip's read pointer, through tnt2_set_diag's output),
 * then acknowledged as nouveau's handler does (INTR written back, PGRAPH's FIFO access on again: the method dropped),
 * and the 3D state is sent again before the next triangle. A trap then costs its triangle, not the program. ---- */

static void (*diag_out)(const char *line);
static unsigned traps;

void tnt2_set_diag(void (*out)(const char *line)) { diag_out = out; }
unsigned tnt2_traps(void) { return traps; }

static char *hex8(char *p, uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    int j;
    *p++ = ' ';
    for (j = 0; j < 8; j++)
        *p++ = hex[(v >> (28 - 4 * j)) & 15u];
    return p;
}

static void diag_words(const char *label, const uint32_t *w, int n)
{
    char line[200], *p = line;
    int i;
    while (*label && p < line + 24)
        *p++ = *label++;
    for (i = 0; i < n && p < line + sizeof line - 10; i++)
        p = hex8(p, w[i]);
    *p = 0;
    diag_out(line);
}

static int trap_check(void)
{
    uint32_t intr = PGRAPH[0x0100 / 4];
    if (!intr)
        return 0;
    if (traps++ == 0 && diag_out) {
        uint32_t r[6], t[8];
        r[0] = intr, r[1] = PGRAPH[0x0108 / 4], r[2] = PGRAPH[0x0104 / 4];      /* INTR, NSOURCE, NSTATUS */
        r[3] = PGRAPH[0x0704 / 4], r[4] = PGRAPH[0x0708 / 4];                   /* TRAPPED_ADDR, TRAPPED_DATA */
        r[5] = (uint32_t)tri_obj;
        diag_words("tnt2-trap regs", r, 6);
        t[0] = t3d.color, t[1] = t3d.zeta, t[2] = (uint32_t)t3d.pitch, t[3] = (uint32_t)t3d.bpp;
        t[4] = (uint32_t)t3d.w, t[5] = (uint32_t)t3d.h, t[6] = (uint32_t)t3d.set, t[7] = (uint32_t)stale_3d;
        diag_words("tnt2-trap target", t, 8);
        diag_words("tnt2-trap clip", clip_now, 4);
        diag_words("tnt2-trap mt3d", mt3d, 16);
        diag_words("tnt2-trap st3d", st3d, 6);
        tnt2_ring_dump(64, 40, diag_out);
    }
    PGRAPH[0x0100 / 4] = intr;
    PGRAPH[0x0720 / 4] = 1;                             /* PGRAPH_FIFO: access on again */
    stale_3d = 1;
    return 1;
}

static void forget_3d(void)
{
    t3d.set = 0, st3d_set = 0, mt3d_set = 0;
}

static void send_target(int clip)
{
    ring_start(SUBCH(S3D, 0x30C), 2);                   /* OFFSET_COLOR, OFFSET_ZETA */
    NEXT(t3d.color);
    NEXT(t3d.zeta);
    ring_start(SUBCH(S3D, 0x300), 1);                   /* FORMAT: pitch-linear, R5G6B5 or X8R8G8B8 */
    NEXT(t3d.bpp == 32 ? 0x105u : 0x103u);
    ring_start(SUBCH(S3D, 0x308), 1);                   /* PITCH: zeta high, colour low */
    NEXT(((uint32_t)t3d.pitch << 16) | (uint32_t)t3d.pitch);
    if (clip)
        tnt2_3d_clip(0, 0, t3d.w, t3d.h);
}

/* The 3D clip (inclusive-exclusive), straight into PGRAPH with the engine idle (as nouveau emulates the surfaces'
 * clip methods), and only when it changes: a change costs a sync. */
static uint32_t clip_now[4] = { ~0u, ~0u, ~0u, ~0u };

void tnt2_3d_clip(int x0, int y0, int x1, int y1)
{
    uint32_t c[4];
    c[0] = (uint32_t)x0 & 0x3FFFF, c[1] = (uint32_t)y0 & 0x3FFFF, c[2] = (uint32_t)x1 & 0x3FFFF, c[3] = (uint32_t)y1 & 0x3FFFF;
    if (!memcmp(c, clip_now, sizeof c))
        return;
    if (tnt2.arch == 10) {
        /* (the NV1x's emulated 0x93 takes its clip by method, in hardware: nouveau has no software methods there;
         * PGRAPH's 2D clip registers aren't what clips its Celsius pipeline) */
        ring_start(SUBCH(S3D, 0x2F8), 2);
        NEXT((uint32_t)(x1 - x0) << 16 | ((uint32_t)x0 & 0xFFFFu));
        NEXT((uint32_t)(y1 - y0) << 16 | ((uint32_t)y0 & 0xFFFFu));
        memcpy(clip_now, c, sizeof c);
        return;
    }
    tnt2_sync(1000000);
    PGRAPH[0x053C / 4] = c[0];
    PGRAPH[0x0540 / 4] = c[1];
    PGRAPH[0x0544 / 4] = c[2];
    PGRAPH[0x0548 / 4] = c[3];
    memcpy(clip_now, c, sizeof c);
}

static void send_state(void)
{
    int i;
    if (tri_obj == 0x55) {              /* (Mesa's two bursts: 0x308-0x324, 0x32C-0x348; 0x328 not written) */
        ring_start(SUBCH(TRI, 0x308), 8);
        for (i = 0; i < 8; i++)
            NEXT(mt3d[i]);
        ring_start(SUBCH(TRI, 0x32C), 8);
        for (i = 8; i < 16; i++)
            NEXT(mt3d[i]);
        return;
    }
    ring_start(SUBCH(TRI, 0x300), 7);
    NEXT(0);
    for (i = 0; i < 6; i++)
        NEXT(st3d[i]);
}

/* Subchannel 7 to the triangle object wanted: a switch binds it and sends its state again (the safe rule: whether
 * the two objects keep their own state across a rebind is not known). */
static void use_obj(int obj)
{
    if (tri_obj == obj)
        return;
    ring_start(SUBCH(TRI, 0), 1);
    NEXT(obj == 0x55 ? H_MTRI : H_TTRI);
    tri_obj = obj;
    if (obj == 0x55 ? mt3d_set : st3d_set)
        send_state();
}

/* The 2D objects share PGRAPH's surface and state registers with the 3D ones: after any 2D operation a triangle
 * trapped (the P3: METHOD_CNT, INVALID_STATE at its first vertex) until the 3D state was sent again, which is why
 * Mesa sends it before every run of triangles. So the layer keeps both and sends them again when 2D has run. */
void tnt2_3d_target(uint32_t color_off, uint32_t zeta_off, int pitch_bytes, int bpp, int w, int h)
{
    /* The depth surface where the colour one is (no depth buffer, drawing to the buffer at 0: Glide's
     * grRenderBuffer(FRONT), a single-buffered context) is an invalid state to the chip: the next vertex trapped
     * (INVALID_STATE, Screamer Rally on the P3) and the engine stopped. Depth then goes just past the colour buffer,
     * never written (no depth writes without a depth buffer). */
    if (zeta_off == color_off)
        zeta_off = color_off + (uint32_t)pitch_bytes * (uint32_t)h;
    if (t3d.set && !stale_3d && t3d.color == color_off && t3d.zeta == zeta_off && t3d.pitch == pitch_bytes &&
        t3d.bpp == bpp && t3d.w == w && t3d.h == h)
        return;                                         /* (the same, and nothing 2D since: nothing to send) */
    t3d.color = color_off, t3d.zeta = zeta_off, t3d.pitch = pitch_bytes, t3d.bpp = bpp, t3d.w = w, t3d.h = h;
    t3d.set = 1;
    send_target(1);
    if (st3d_set)
        send_state();
    stale_3d = 0;
}

/* The triangle object's whole state block (Mesa sends it before every run of triangles): COLORKEY, the texture's
 * OFFSET, FORMAT and FILTER, BLEND, CONTROL, FOGCOLOR. */
void tnt2_3d_state(uint32_t tex_off, uint32_t format, uint32_t filter, uint32_t blend, uint32_t control,
                   uint32_t fogcolor)
{
    if (tri_obj != 0x54) {
        st3d_set = 0;                                   /* (sent at the switch below) */
        use_obj(0x54);
    }
    if (st3d_set && !stale_3d && st3d[0] == tex_off && st3d[1] == format && st3d[2] == filter && st3d[3] == blend &&
        st3d[4] == control && st3d[5] == fogcolor)
        return;                                         /* (the same, and nothing 2D since) */
    st3d[0] = tex_off, st3d[1] = format, st3d[2] = filter, st3d[3] = blend, st3d[4] = control, st3d[5] = fogcolor;
    st3d_set = 1;
    if (stale_3d && t3d.set) {
        send_target(0);
        stale_3d = 0;
    }
    send_state();
}

static uint32_t fbits(float f)
{
    union { float f; uint32_t u; } c;
    c.f = f;
    return c.u;
}

/* Three vertices into slots 0-2, then DRAWPRIMITIVE(0) = 0x102: (c, a, b), a rotation that keeps the winding and
 * makes c the flat-shading vertex (OpenGL's: the last), as Mesa's. */
void tnt2_tri(const tnt2_vertex *a, const tnt2_vertex *b, const tnt2_vertex *c)
{
    const tnt2_vertex *v[3];
    int i;
    v[0] = a, v[1] = b, v[2] = c;
    if (stale_3d) {
        if (t3d.set)
            send_target(0);
        if (st3d_set)
            send_state();
        stale_3d = 0;
    }
    ring_start(SUBCH(TRI, 0x400), 24);
    for (i = 0; i < 3; i++) {
        NEXT(fbits(v[i]->sx)); NEXT(fbits(v[i]->sy)); NEXT(fbits(v[i]->sz)); NEXT(fbits(v[i]->rhw));
        NEXT(v[i]->color); NEXT(v[i]->specular); NEXT(fbits(v[i]->tu)); NEXT(fbits(v[i]->tv));
    }
    ring_start(SUBCH(TRI, 0x600), 1);
    NEXT(0x102);
    kick_soon();
}

/* Texels written in place: the triangle objects' state sent again before the next draw, even unchanged (the
 * chip's texture cache keeps texels it read until its texture registers are written: t18 on the P3). */
void tnt2_3d_tex_written(void)
{
    stale_3d = 1;
}

void tnt2_3d_mt_state(const uint32_t w[16])
{
    if (tri_obj == 0x55 && mt3d_set && !stale_3d && !memcmp(w, mt3d, sizeof mt3d))
        return;
    memcpy(mt3d, w, sizeof mt3d);
    mt3d_set = 1;
    if (stale_3d && t3d.set) {
        send_target(0);
        stale_3d = 0;
        if (tri_obj == 0x55) {
            send_state();
            return;
        }
    }
    if (tri_obj != 0x55)
        use_obj(0x55);                                  /* (it sends the words) */
    else
        send_state();
}

static void stale_check(void)
{
    if (stale_3d) {
        if (t3d.set)
            send_target(0);
        if (tri_obj == 0x55 ? mt3d_set : st3d_set)
            send_state();
        stale_3d = 0;
    }
}

static void put_mt(const tnt2_vertex *v)
{
    NEXT(fbits(v->sx)); NEXT(fbits(v->sy)); NEXT(fbits(v->sz)); NEXT(fbits(v->rhw));
    NEXT(v->color); NEXT(v->specular); NEXT(fbits(v->tu)); NEXT(fbits(v->tv)); NEXT(fbits(v->tu1)); NEXT(fbits(v->tv1));
}

/* The multitexture object: TLMTVERTEX (10 dwords, 8 slots from 0x400), DRAWPRIMITIVE from 0x540. */
void tnt2_tri_mt(const tnt2_vertex *a, const tnt2_vertex *b, const tnt2_vertex *c)
{
    stale_check();
    ring_start(SUBCH(TRI, 0x400), 30);
    put_mt(a), put_mt(b), put_mt(c);
    ring_start(SUBCH(TRI, 0x540), 1);
    NEXT(0x102);
    kick_soon();
}

void tnt2_poly_mt(const tnt2_vertex *v, int n)
{
    int i;
    if (n < 3 || n > 8)
        return;
    stale_check();
    ring_start(SUBCH(TRI, 0x400), 10 * n);
    for (i = 0; i < n; i++)
        put_mt(&v[i]);
    ring_start(SUBCH(TRI, 0x540), (n - 1) / 2);
    for (i = 1; i + 1 < n; i += 2) {
        uint32_t w = (uint32_t)(i + 1) | (uint32_t)i << 8;
        if (i + 2 < n)
            w |= (uint32_t)(i + 2) << 12 | (uint32_t)(i + 1) << 20;
        NEXT(w);
    }
    kick_soon();
}

/* A polygon's n vertices (3 to 16) into slots 0 to n-1 at once, then its fan's triangles (0, i, i+1), two to each
 * DRAWPRIMITIVE word (indices 4 bits each; each triangle rotated as tnt2_tri's, so the winding is kept): each vertex
 * goes over the bus once. */
void tnt2_poly(const tnt2_vertex *v, int n)
{
    int i, words = (n - 1) / 2;
    if (n < 3 || n > 16)
        return;
    if (stale_3d) {
        if (t3d.set)
            send_target(0);
        if (st3d_set)
            send_state();
        stale_3d = 0;
    }
    ring_start(SUBCH(TRI, 0x400), 8 * n);
    for (i = 0; i < n; i++) {
        NEXT(fbits(v[i].sx)); NEXT(fbits(v[i].sy)); NEXT(fbits(v[i].sz)); NEXT(fbits(v[i].rhw));
        NEXT(v[i].color); NEXT(v[i].specular); NEXT(fbits(v[i].tu)); NEXT(fbits(v[i].tv));
    }
    ring_start(SUBCH(TRI, 0x600), words);
    for (i = 1; i + 1 < n; i += 2) {
        uint32_t w = (uint32_t)(i + 1) | 0u << 4 | (uint32_t)i << 8;           /* (i+1, 0, i) */
        if (i + 2 < n)
            w |= (uint32_t)(i + 2) << 12 | 0u << 16 | (uint32_t)(i + 1) << 20;  /* (i+2, 0, i+1) */
        NEXT(w);
    }
    kick_soon();
}

/* Textures are stored swizzled (Mesa's nv04_surface.c, get_swizzled_offset): in a level of w x h texels (powers
 * of two), with k = log2(min(w, h)), the low 2k bits of a texel's index interleave x and y (x's bit i at bit 2i, y's
 * at 2i + 1) and the longer side's higher bits sit above them. The levels follow one another from the base level,
 * each as large as it is (nouveau_texture.c: levels over 16 bytes start on 64 bytes, which powers of two do). */
uint32_t tnt2_tex_load(uint32_t off, int cpp, int log2w, int log2h, int levels, const void *const *level_texels)
{
    /* Each level put in its swizzled order in system memory (x's and y's bits from two tables: the longer side's
     * high bits sit above the interleaved ones, so the two only add), then written to video memory in one
     * sequential run, which write-combining turns into bursts (scattered 2-byte writes to it are slow). */
    static uint32_t sxt[2048], syt[2048];
    static uint8_t *tmp;
    static uint32_t tmp_bytes;
    int n, x, y, i;
    for (n = 0; n < levels; n++) {
        int lw = log2w > n ? log2w - n : 0, lh = log2h > n ? log2h - n : 0, w = 1 << lw, h = 1 << lh;
        int k = lw < lh ? lw : lh;
        uint32_t bytes = (uint32_t)(w * h * cpp);
        const uint8_t *src = (const uint8_t *)level_texels[n];
        volatile uint32_t *d32;
        const uint32_t *s32;
        if (bytes > tmp_bytes) {
            uint8_t *t = (uint8_t *)sys_alloc(bytes);  /* (its old contents not needed: the port's, not realloc) */
            if (!t)
                return off;
            if (tmp)
                sys_free(tmp);
            tmp = t, tmp_bytes = bytes;
        }
        for (x = 0; x < w; x++) {
            uint32_t v = (uint32_t)(x >> k) << (2 * k);
            for (i = 0; i < k; i++)
                v |= (((uint32_t)x >> i) & 1u) << (2 * i);
            sxt[x] = v;
        }
        for (y = 0; y < h; y++) {
            uint32_t v = (uint32_t)(y >> k) << (2 * k);
            for (i = 0; i < k; i++)
                v |= (((uint32_t)y >> i) & 1u) << (2 * i + 1);
            syt[y] = v;
        }
        if (cpp == 2) {
            uint16_t *t16 = (uint16_t *)tmp;
            const uint16_t *s16 = (const uint16_t *)src;
            for (y = 0; y < h; y++, s16 += w)
                for (x = 0; x < w; x++)
                    t16[sxt[x] | syt[y]] = s16[x];
        } else {
            uint32_t *t32 = (uint32_t *)tmp;
            const uint32_t *s = (const uint32_t *)src;
            for (y = 0; y < h; y++, s += w)
                for (x = 0; x < w; x++)
                    t32[sxt[x] | syt[y]] = s[x];
        }
        d32 = (volatile uint32_t *)(tnt2_fb + off);
        s32 = (const uint32_t *)tmp;
        for (i = 0; i < (int)(bytes / 4); i++)
            d32[i] = s32[i];
        if (bytes & 2)                                  /* (a 1x1 level of 2 bytes) */
            *(volatile uint16_t *)(tnt2_fb + off + bytes - 2) = *(const uint16_t *)(tmp + bytes - 2);
        off += bytes;
    }
    return off;
}

/* tnt2_tex_load's tables and buffer, as a function: a level's x and y to its swizzled index. */
static uint32_t sw_x[2048], sw_y[2048];

static void swizzle_tables(int lw, int lh)
{
    int k = lw < lh ? lw : lh, w = 1 << lw, h = 1 << lh, x, y, i;
    for (x = 0; x < w; x++) {
        uint32_t v = (uint32_t)(x >> k) << (2 * k);
        for (i = 0; i < k; i++)
            v |= (((uint32_t)x >> i) & 1u) << (2 * i);
        sw_x[x] = v;
    }
    for (y = 0; y < h; y++) {
        uint32_t v = (uint32_t)(y >> k) << (2 * k);
        for (i = 0; i < k; i++)
            v |= (((uint32_t)y >> i) & 1u) << (2 * i + 1);
        sw_y[y] = v;
    }
}

/* As tnt2_tex_load, from RGBA8 levels (r, g, b, a bytes), converted to the chip's format on the way in one pass:
 * TNT2_TEX_A8R8G8B8, _R5G6B5, _A1R5G5B5 (alpha from its top bit), _A4R4G4B4. */
uint32_t tnt2_tex_load_rgba8(uint32_t off, int fmt, int log2w, int log2h, int levels, const uint8_t *const *rgba)
{
    static uint8_t *tmp;
    static uint32_t tmp_bytes;
    int cpp = fmt == TNT2_TEX_A8R8G8B8 ? 4 : 2, n, x, y, i;
    for (n = 0; n < levels; n++) {
        int lw = log2w > n ? log2w - n : 0, lh = log2h > n ? log2h - n : 0, w = 1 << lw, h = 1 << lh;
        uint32_t bytes = (uint32_t)(w * h * cpp);
        const uint8_t *s = rgba[n];
        volatile uint32_t *d32;
        const uint32_t *s32;
        if (bytes > tmp_bytes) {
            uint8_t *t = (uint8_t *)sys_alloc(bytes);  /* (its old contents not needed: the port's, not realloc) */
            if (!t)
                return off;
            if (tmp)
                sys_free(tmp);
            tmp = t, tmp_bytes = bytes;
        }
        swizzle_tables(lw, lh);
        if (cpp == 4) {
            uint32_t *t32 = (uint32_t *)tmp;
            for (y = 0; y < h; y++) {
                uint32_t sy = sw_y[y];
                for (x = 0; x < w; x++, s += 4)
                    t32[sw_x[x] | sy] = (uint32_t)s[3] << 24 | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
            }
        } else {
            uint16_t *t16 = (uint16_t *)tmp;
            for (y = 0; y < h; y++) {
                uint32_t sy = sw_y[y];
                if (fmt == TNT2_TEX_R5G6B5)
                    for (x = 0; x < w; x++, s += 4)
                        t16[sw_x[x] | sy] = (uint16_t)((s[0] >> 3) << 11 | (s[1] >> 2) << 5 | (s[2] >> 3));
                else if (fmt == TNT2_TEX_A1R5G5B5)
                    for (x = 0; x < w; x++, s += 4)
                        t16[sw_x[x] | sy] = (uint16_t)((s[3] >> 7) << 15 | (s[0] >> 3) << 10 | (s[1] >> 3) << 5 | (s[2] >> 3));
                else
                    for (x = 0; x < w; x++, s += 4)
                        t16[sw_x[x] | sy] = (uint16_t)((s[3] >> 4) << 12 | (s[0] >> 4) << 8 | (s[1] >> 4) << 4 | (s[2] >> 4));
            }
        }
        d32 = (volatile uint32_t *)(tnt2_fb + off);
        s32 = (const uint32_t *)tmp;
        for (i = 0; i < (int)(bytes / 4); i++)
            d32[i] = s32[i];
        if (bytes & 2)
            *(volatile uint16_t *)(tnt2_fb + off + bytes - 2) = *(const uint16_t *)(tmp + bytes - 2);
        off += bytes;
    }
    return off;
}

/* A rectangle of a texture's base level from RGBA8 (the whole level, rows stride texels apart), written in place.
 * The swizzle keeps each aligned 4x4 block in 16 texels in a row: the rectangle's blocks are written whole from
 * the level (a block's texels outside the rectangle unchanged in it), each a whole 32-byte line or two, so
 * write-combining sends bursts (texel by texel in rows, a line took four partial writes). Textures under 4 texels
 * a side texel by texel. The engine done with the texture first: the caller's fence (tnt2_mark_wait). */
static uint32_t texel16(int fmt, const uint8_t *s)
{
    return fmt == TNT2_TEX_R5G6B5 ? (uint32_t)((s[0] >> 3) << 11 | (s[1] >> 2) << 5 | (s[2] >> 3))
         : fmt == TNT2_TEX_A1R5G5B5 ? (uint32_t)((s[3] >> 7) << 15 | (s[0] >> 3) << 10 | (s[1] >> 3) << 5 | (s[2] >> 3))
         : (uint32_t)((s[3] >> 4) << 12 | (s[0] >> 4) << 8 | (s[1] >> 4) << 4 | (s[2] >> 4));
}

void tnt2_tex_sub_rgba8(uint32_t off, int fmt, int log2w, int log2h, int x0, int y0, int w, int h,
                        const uint8_t *rgba, int stride)
{
    /* (a 4x4 block's texels in swizzled order: (0,0) (1,0) (0,1) (1,1) (2,0) (3,0) (2,1) (3,1), then rows 2-3) */
    static const uint8_t bx[16] = { 0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3 };
    static const uint8_t by[16] = { 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3 };
    int x, y, i;
    swizzle_tables(log2w, log2h);
    if (log2w < 2 || log2h < 2) {
        for (y = y0; y < y0 + h; y++) {
            const uint8_t *s = rgba + ((long)y * stride + x0) * 4;
            for (x = x0; x < x0 + w; x++, s += 4) {
                uint32_t t = sw_x[x] | sw_y[y];
                if (fmt == TNT2_TEX_A8R8G8B8)
                    ((volatile uint32_t *)(tnt2_fb + off))[t] =
                        (uint32_t)s[3] << 24 | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
                else
                    ((volatile uint16_t *)(tnt2_fb + off))[t] = (uint16_t)texel16(fmt, s);
            }
        }
        return;
    }
    /* (one loop a format: the conversion inlined, not chosen per texel) */
#define TNT2_SUB_LOOP(BODY)                                                                     \
    for (y = y0 & ~3; y < y0 + h; y += 4)                                                       \
        for (x = x0 & ~3; x < x0 + w; x += 4) {                                                 \
            const uint8_t *s = rgba + ((long)y * stride + x) * 4;                               \
            uint32_t t = sw_x[x] | sw_y[y];                                                     \
            BODY                                                                                \
        }
#define TNT2_Q(i) (s + (by[i] * stride + bx[i]) * 4)
#define TNT2_565(q) ((uint32_t)((q)[0] >> 3) << 11 | (uint32_t)((q)[1] >> 2) << 5 | (uint32_t)((q)[2] >> 3))
#define TNT2_1555(q) ((uint32_t)((q)[3] >> 7) << 15 | (uint32_t)((q)[0] >> 3) << 10 | (uint32_t)((q)[1] >> 3) << 5 | \
                      (uint32_t)((q)[2] >> 3))
#define TNT2_4444(q) ((uint32_t)((q)[3] >> 4) << 12 | (uint32_t)((q)[0] >> 4) << 8 | (uint32_t)((q)[1] >> 4) << 4 | \
                      (uint32_t)((q)[2] >> 4))
#define TNT2_PAIRS(CONV)                                                                        \
    {                                                                                           \
        volatile uint32_t *d = (volatile uint32_t *)(tnt2_fb + off + t * 2);                    \
        for (i = 0; i < 16; i += 2)                                                             \
            d[i >> 1] = CONV(TNT2_Q(i)) | CONV(TNT2_Q(i + 1)) << 16;                            \
    }
    switch (fmt) {
    case TNT2_TEX_A8R8G8B8:
        TNT2_SUB_LOOP({
            volatile uint32_t *d = (volatile uint32_t *)(tnt2_fb + off) + t;
            for (i = 0; i < 16; i++) {
                const uint8_t *q = TNT2_Q(i);
                d[i] = (uint32_t)q[3] << 24 | (uint32_t)q[0] << 16 | (uint32_t)q[1] << 8 | q[2];
            }
        })
        break;
    case TNT2_TEX_R5G6B5:
        TNT2_SUB_LOOP(TNT2_PAIRS(TNT2_565))
        break;
    case TNT2_TEX_A1R5G5B5:
        TNT2_SUB_LOOP(TNT2_PAIRS(TNT2_1555))
        break;
    default:
        TNT2_SUB_LOOP(TNT2_PAIRS(TNT2_4444))
        break;
    }
#undef TNT2_SUB_LOOP
#undef TNT2_Q
#undef TNT2_565
#undef TNT2_1555
#undef TNT2_4444
#undef TNT2_PAIRS
}

/* ---- the set-up (nv_hw.c's NVLoadStateExt for NV_ARCH_04, no display state) ---- */

/* The NV1x's transform pipe (its Celsius pipeline, under the emulated 0x94/0x95 too) loaded with nouveau's defaults
 * (gr/nv10.c's nv10_gr_load_pipe with nv10_gr_create_pipe's contents, MIT): PGRAPH 0x400F50 the pipe's address,
 * 0x400F54 its data, auto-incrementing; the transform modes 0x400F40/44 bypassed while the first part goes in. Without
 * it the bypass path's scale and offset rows are whatever the pipe held: NTRI0 drew nothing on bench E (no trap). And
 * the per-channel defaults nouveau writes into a new context (nv10.c's nv10_gr_context_new). */
static void pgraph_idle(void)
{
    uint32_t t0 = sys_time_us();
    while (PGRAPH[0x0700 / 4] != 0 && sys_time_us() - t0 < 100000u)
        ;
}

static void pipe_words(uint32_t addr, const uint32_t *w, int n)
{
    int i;
    PGRAPH[0x0F50 / 4] = addr;
    for (i = 0; i < n; i++)
        PGRAPH[0x0F54 / 4] = w ? w[i] : 0u;
}

static void nv10_pipe_init(void)
{
    static const uint32_t one4zero4[8] = { 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000, 0, 0, 0, 0 };
    static const uint32_t one3[3] = { 0x3F800000, 0x3F800000, 0x3F800000 };
    static const uint32_t eight = 0x00000008;
    static const uint32_t tail6400[25] = {          /* (0x6400's last 25 of 236; the first 211 are 0) */
        0x3F800000, 0x40000000, 0x40000000, 0x40000000, 0x40000000, 0, 0, 0x3F800000, 0, 0x3F000000, 0x3F000000,
        0, 0, 0, 0, 0x3F800000, 0, 0, 0, 0, 0, 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000 };
    static const uint32_t b6c00[12] = { 0, 0, 0, 0, 0xBF800000, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t b7000[76], b6800[188], xf0, xf1;
    int i;
    PGRAPH[0x0E88 / 4] = 0x08000000;                /* (the context defaults: CELSIUS_RASTER and the rest) */
    PGRAPH[0x0E9C / 4] = 0x4B7FFFFF;
    PGRAPH[0x0514 / 4] = 0x0001FFFF;                /* XY_LOGIC_MISC0 */
    PGRAPH[0x0E10 / 4] = 0x00001000;
    PGRAPH[0x0E14 / 4] = 0x00001000;
    PGRAPH[0x0E30 / 4] = 0x00080008;
    PGRAPH[0x0E34 / 4] = 0x00080008;
    PGRAPH[0x0148 / 4] = 0;                         /* CTX_USER: channel 0 */
    for (i = 0; i < 76; i++)                        /* (0x7000: 12 zeros, then 0x7149F2CA every fourth word 8 times) */
        b7000[i] = i >= 12 && i <= 40 && (i - 12) % 4 == 0 ? 0x7149F2CAu : 0u;
    for (i = 0; i < 188; i++)
        b6800[i] = i == 162 ? 0x3F800000u : 0u;
    pgraph_idle();
    xf0 = PGRAPH[0x0F40 / 4], xf1 = PGRAPH[0x0F44 / 4];
    PGRAPH[0x0F40 / 4] = 0x10000000;                /* (bypass while the first part goes in) */
    PGRAPH[0x0F44 / 4] = 0x00000000;
    pipe_words(0x64C0, one4zero4, 8);
    pipe_words(0x6AB0, one3, 3);
    pipe_words(0x6A80, NULL, 3);
    pipe_words(0x0040, &eight, 1);
    pipe_words(0x0200, NULL, 48);
    pgraph_idle();
    PGRAPH[0x0F40 / 4] = xf0;
    PGRAPH[0x0F44 / 4] = xf1;
    PGRAPH[0x0F50 / 4] = 0x6400;
    for (i = 0; i < 236; i++)
        PGRAPH[0x0F54 / 4] = i < 211 ? 0u : tail6400[i - 211];
    pipe_words(0x6800, b6800, 188);
    pipe_words(0x6C00, b6c00, 12);
    pipe_words(0x7000, b7000, 76);
    pipe_words(0x7400, NULL, 48);
    pipe_words(0x7800, NULL, 48);
    pipe_words(0x4400, NULL, 32);
    pipe_words(0x0000, NULL, 16);
    pipe_words(0x0040, NULL, 4);
    pgraph_idle();
}

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
    /* (the DMA object for video memory: class 0 on the NV4, as the X driver's; class 0x3D on the NV1x, whose emulated
     * 3D classes check it: textures 0x02 or 0x3D, the 3D surfaces 0x3D only, envytools' hardware tests) */
    PRAMIN[0x0800] = tnt2.arch == 10 ? 0x0000303D : 0x00003000;
    PRAMIN[0x0801] = tnt2.fb_size - 1;
    PRAMIN[0x0802] = 0x00000002;
    PRAMIN[0x0803] = 0x00000002;
    for (i = 0; i < 0x28 - 0x04; i++)
        obj[i] = 0;
    obj[0x04 - 4] = tnt2.arch == 10 ? 0x01008062 : 0x01008042;           /* 2D surfaces (NV4: 0x42, NV10: 0x62) */
    obj[0x06 - 4] = 0x12001200;
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

    if (tnt2.arch == 10) {
        /* PFB's tile regions off (nv_hw.c's NVLoadStateExt before NV40), then PGRAPH as nv_hw.c sets an NV_ARCH_10's:
         * the context control at 0x400144 (not the NV4's 0x400170, which is something else here), the state at
         * 0x400714, the surface register kept but for its own fields, the debug words, the tile regions copied, the
         * limits the memory's size. */
        for (i = 0; i < 8; i++) {
            PFB[(0x0240 + i * 0x10) / 4] = 0;
            PFB[(0x0244 + i * 0x10) / 4] = tnt2.fb_size - 1;
        }
        PGRAPH[0x0080 / 4] = 0xFFFFFFFF;
        PGRAPH[0x0080 / 4] = 0x00000000;
        PGRAPH[0x0140 / 4] = 0x00000000;
        PGRAPH[0x0100 / 4] = 0xFFFFFFFF;
        PGRAPH[0x0144 / 4] = 0x10010100;
        PGRAPH[0x0714 / 4] = 0xFFFFFFFF;
        PGRAPH[0x0720 / 4] = 0x00000001;
        PGRAPH[0x0710 / 4] &= 0x0007FF00;
        PGRAPH[0x0710 / 4] |= 0x00020100;
        PGRAPH[0x0084 / 4] = 0x00118700;
        PGRAPH[0x0088 / 4] = 0x24E00810;
        PGRAPH[0x008C / 4] = 0x55DE0030;
        for (i = 0; i < 32; i++)
            PGRAPH[0x0B00 / 4 + i] = PFB[0x0240 / 4 + i];
        PGRAPH[0x0640 / 4] = 0;
        PGRAPH[0x0644 / 4] = 0;
        PGRAPH[0x0684 / 4] = tnt2.fb_size - 1;
        PGRAPH[0x0688 / 4] = tnt2.fb_size - 1;
        PGRAPH[0x0810 / 4] = 0x00000000;
        PGRAPH[0x0608 / 4] = 0xFFFFFFFF;
        nv10_pipe_init();
    } else {
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
    }
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
