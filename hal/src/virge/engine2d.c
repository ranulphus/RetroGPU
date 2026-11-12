/* engine2d.c - the ViRGE/DX's S3d engine, 2D (docs/virge.md, V1): fills,
 * copies within and between surfaces, colour expansion and images from the
 * CPU, from the BitBLT/rectangle fill register group (data book 15.4.4 and
 * 19.3). Its coordinates are 11-bit and unsigned, so every operation is
 * trimmed to the clip here, in software, and the engine's own clipping is
 * left off; CPU data for a trimmed image is repacked to fit. */
#include "virge/virge.h"
#include "virge/regs_virge.h"
#include "mga/sys.h"
#include <string.h>

#define WR(off, v) (*(volatile uint32_t *)(virge.mmio + (off)) = (uint32_t)(v))
#define RD(off)    (*(volatile uint32_t *)(virge.mmio + (off)))

static uint32_t t_off, t_fmt;
static int t_pitch, t_bpp, t_bytes;
static int c_x0, c_y0, c_x1, c_y1;

static int slots_free(void)
{
    return (int)((RD(VIRGE_SUBSYS_STAT) >> VIRGE_STAT_FIFO_SHIFT) & VIRGE_STAT_FIFO_MASK);
}

/* Room for n register writes in the S3d FIFO. Bounded: a stuck engine
 * should not hang the machine (the caller's next sync reports it). */
static void fifo_wait(int n)
{
    uint32_t t0 = sys_time_us();
    while (slots_free() < n && sys_time_us() - t0 < 200000)
        ;
}

int virge_sync(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    if (!virge.mmio)
        return 0;
    for (;;) {
        uint32_t s = RD(VIRGE_SUBSYS_STAT);
        if ((s & VIRGE_STAT_IDLE) && ((s >> VIRGE_STAT_FIFO_SHIFT) & VIRGE_STAT_FIFO_MASK) >= VIRGE_FIFO_SLOTS)
            return 0;
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    }
}

void virge_set_target(uint32_t off, int pitch_bytes, int bpp)
{
    t_off = off;
    t_pitch = pitch_bytes;
    t_bpp = bpp;
    t_bytes = bpp == 24 ? 3 : bpp == 16 ? 2 : 1;
    t_fmt = bpp == 24 ? VIRGE_CMD_FMT_24 : bpp == 16 ? VIRGE_CMD_FMT_16 : VIRGE_CMD_FMT_8;
}

void virge_set_clip(int x0, int y0, int x1, int y1)
{
    c_x0 = x0, c_y0 = y0, c_x1 = x1, c_y1 = y1;
}

/* The rectangle at (*x, *y), *w by *h, trimmed to the clip: 0 when nothing
 * is left; *dx and *dy say how far its corner moved. */
static int trim(int *x, int *y, int *w, int *h, int *dx, int *dy)
{
    int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
    if (x0 < c_x0) x0 = c_x0;
    if (y0 < c_y0) y0 = c_y0;
    if (x1 > c_x1) x1 = c_x1;
    if (y1 > c_y1) y1 = c_y1;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    *dx = x0 - *x, *dy = y0 - *y;
    *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
    return 1;
}

static uint32_t xy(int x, int y) { return ((uint32_t)x << 16) | (uint32_t)y; }

static uint32_t colour(uint32_t v)
{
    v &= t_bpp == 24 ? 0xFFFFFFu : t_bpp == 16 ? 0xFFFFu : 0xFFu;
    return t_bpp == 8 ? v | (v << 8) : v;       /* (8 bits: both data bytes, as SRC_FG_CLR asks) */
}

void virge_fill(int x, int y, int w, int h, uint32_t value)
{
    int dx, dy;
    if (w <= 0 || h <= 0 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    fifo_wait(6);
    WR(VIRGE_BLT_DEST_BASE, t_off);
    WR(VIRGE_BLT_STRIDE, ((uint32_t)t_pitch << 16) | (uint32_t)t_pitch);
    WR(VIRGE_BLT_PAT_FG, colour(value));
    WR(VIRGE_BLT_WH, xy(w - 1, h));
    WR(VIRGE_BLT_DEST_XY, xy(x, y));
    WR(VIRGE_BLT_CMD, VIRGE_CMD_RECT_FILL | VIRGE_CMD_ROP(VIRGE_ROP_PATCOPY) | VIRGE_CMD_XP | VIRGE_CMD_YP |
                      VIRGE_CMD_MP | VIRGE_CMD_DE | t_fmt);
}

/* A copy from (sx, sy) of src to (x, y) of the target, already trimmed;
 * the direction chosen so an overlap reads before it writes (table 15-2). */
static void blt(uint32_t src_off, int src_pitch, int sx, int sy, int x, int y, int w, int h)
{
    uint32_t cmd = VIRGE_CMD_BITBLT | VIRGE_CMD_ROP(VIRGE_ROP_SRCCOPY) | VIRGE_CMD_DE | t_fmt;
    int same = src_off == t_off && src_pitch == t_pitch;
    if (!same || sx >= x)
        cmd |= VIRGE_CMD_XP;
    else
        sx += w - 1, x += w - 1;
    if (!same || sy >= y)
        cmd |= VIRGE_CMD_YP;
    else
        sy += h - 1, y += h - 1;
    fifo_wait(7);
    WR(VIRGE_BLT_SRC_BASE, src_off);
    WR(VIRGE_BLT_DEST_BASE, t_off);
    WR(VIRGE_BLT_STRIDE, ((uint32_t)t_pitch << 16) | (uint32_t)src_pitch);
    WR(VIRGE_BLT_WH, xy(w - 1, h));
    WR(VIRGE_BLT_SRC_XY, xy(sx, sy));
    WR(VIRGE_BLT_DEST_XY, xy(x, y));
    WR(VIRGE_BLT_CMD, cmd);
}

void virge_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    int mx, my;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || !trim(&dx, &dy, &w, &h, &mx, &my))
        return;
    blt(t_off, t_pitch, sx + mx, sy + my, dx, dy, w, h);
}

void virge_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h)
{
    int mx, my;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || !trim(&dx, &dy, &w, &h, &mx, &my))
        return;
    blt(src_off, src_pitch_bytes, sx + mx, sy + my, dx, dy, w, h);
}

/* Dwords to the image transfer port, room in the FIFO first (image data
 * goes through it, data book 15.3). */
static void push(const uint32_t *d, int n)
{
    int i = 0;
    while (i < n) {
        int k = n - i < 8 ? n - i : 8;
        fifo_wait(k);
        while (k--)
            WR(VIRGE_IMAGE_PORT, d[i++]);
    }
}

static uint32_t rowbuf[(2048 * 3 + 3) / 4 + 1];

void virge_image(int x, int y, int w, int h, const void *px, int stride)
{
    int dx, dy, j, row_bytes, dwords;
    const uint8_t *p = (const uint8_t *)px;
    if (w <= 0 || h <= 0 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    row_bytes = w * t_bytes;
    dwords = (row_bytes + 3) / 4;
    fifo_wait(5);
    WR(VIRGE_BLT_DEST_BASE, t_off);
    WR(VIRGE_BLT_STRIDE, ((uint32_t)t_pitch << 16) | (uint32_t)t_pitch);
    WR(VIRGE_BLT_WH, xy(w - 1, h));
    WR(VIRGE_BLT_DEST_XY, xy(x, y));
    WR(VIRGE_BLT_CMD, VIRGE_CMD_BITBLT | VIRGE_CMD_ROP(VIRGE_ROP_SRCCOPY) | VIRGE_CMD_XP | VIRGE_CMD_YP |
                      VIRGE_CMD_IDS | VIRGE_CMD_ITA_DWORD | VIRGE_CMD_DE | t_fmt);
    p += (long)dy * stride + (long)dx * t_bytes;
    for (j = 0; j < h; j++, p += stride) {
        rowbuf[dwords - 1] = 0;
        memcpy(rowbuf, p, (size_t)row_bytes);
        push(rowbuf, dwords);
    }
}

void virge_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg,
                unsigned flags)
{
    int dx, dy, j, i, dwords;
    uint32_t cmd = VIRGE_CMD_BITBLT | VIRGE_CMD_ROP(VIRGE_ROP_SRCCOPY) | VIRGE_CMD_XP | VIRGE_CMD_YP |
                   VIRGE_CMD_IDS | VIRGE_CMD_MS | VIRGE_CMD_ITA_DWORD | VIRGE_CMD_DE | t_fmt;
    if (w <= 0 || h <= 0 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    if (flags & VIRGE_MONO_TRANSPARENT)
        cmd |= VIRGE_CMD_TP;
    dwords = (w + 31) / 32;
    fifo_wait(7);
    WR(VIRGE_BLT_DEST_BASE, t_off);
    WR(VIRGE_BLT_STRIDE, ((uint32_t)t_pitch << 16) | (uint32_t)t_pitch);
    WR(VIRGE_BLT_SRC_FG, colour(fg));
    WR(VIRGE_BLT_SRC_BG, colour(bg));
    WR(VIRGE_BLT_WH, xy(w - 1, h));
    WR(VIRGE_BLT_DEST_XY, xy(x, y));
    WR(VIRGE_BLT_CMD, cmd);
    /* Each row from bit dx of the source row, the leftmost pixel in bit 7 of
     * the first byte, packed into dwords as the bytes appear in memory. */
    for (j = 0; j < h; j++) {
        const uint8_t *src = bits + (long)(dy + j) * stride;
        uint8_t *o = (uint8_t *)rowbuf;
        memset(rowbuf, 0, (size_t)dwords * 4);
        if ((dx & 7) == 0)
            memcpy(o, src + dx / 8, (size_t)((w + 7) / 8));
        else
            for (i = 0; i < w; i++) {
                int b = dx + i;
                if (src[b >> 3] & (0x80 >> (b & 7)))
                    o[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
            }
        if (w & 7)                                  /* (bits past the row's end clear) */
            o[(w - 1) >> 3] &= (uint8_t)(0xFF00 >> (w & 7));
        push(rowbuf, dwords);
    }
}
