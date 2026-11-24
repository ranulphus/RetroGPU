/* engine2d.c - the Radeon's 2D engine by register writes (docs/radeon.md, RD1): fills, copies within and between
 * surfaces, colour expansion from the CPU, images from the CPU. The Rage 128 layer's engine2d.c, from which it is
 * made: the drawing registers are the Rage 128's at the same offsets; the status, the reset, the cache's flush and
 * the pitch-and-offset word are the Radeon's (X.org's xf86-video-ati: radeon_accel.c's RADEONWaitForFifoFunction,
 * RADEONEngineFlush and RADEONEngineReset, radeon_exa_funcs.c's solid and copy; MIT). Every operation is trimmed to
 * the clip here, in software, so the engine's scissors stay at their widest. */
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "mga/sys.h"
#include <string.h>

/* A drawing-engine register (by the command processor's packets once it runs: RD3). */
static void eng_wr(uint32_t reg, uint32_t v)
{
    if (radeon_cp_running())
        radeon_cp_reg(reg, v);
    else
        radeon_wr(reg, v);
}

static uint32_t t_off, t_dt;
static int t_pitch, t_bpp, t_bytes;
static int c_x0, c_y0, c_x1 = 0x7FFF, c_y1 = 0x7FFF;

/* Room for n register writes in the 64-entry FIFO, bounded (a stuck engine shows at the next sync). */
static void fifo_wait(int n)
{
    uint32_t t0;
    if (radeon_cp_running())            /* (the ring has its own room) */
        return;
    t0 = sys_time_us();
    while ((int)(radeon_rd(RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK) < n && sys_time_us() - t0 < 200000u)
        ;
}

int radeon_idle(void)
{
    uint32_t st;
    if (!radeon.mmio)
        return 0;
    st = radeon_rd(RADEON_RBBM_STATUS);
    return (st & RADEON_RBBM_FIFOCNT_MASK) >= 64 && !(st & RADEON_RBBM_ACTIVE);
}

/* The destination caches out to memory, bounded: the 3D one (RADEONEngineFlush) and the 2D engine's own. */
static int cache_flush(uint32_t t0, uint32_t timeout_us)
{
    radeon_wr(RADEON_RB2D_DSTCACHE_CTLSTAT, radeon_rd(RADEON_RB2D_DSTCACHE_CTLSTAT) | RADEON_RB2D_DC_FLUSH_ALL);
    while (radeon_rd(RADEON_RB2D_DSTCACHE_CTLSTAT) & RADEON_RB2D_DC_BUSY)
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    radeon_wr(RADEON_RB3D_DSTCACHE_CTLSTAT, radeon_rd(RADEON_RB3D_DSTCACHE_CTLSTAT) | RADEON_RB3D_DC_FLUSH_ALL);
    while (radeon_rd(RADEON_RB3D_DSTCACHE_CTLSTAT) & RADEON_RB3D_DC_BUSY)
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return 0;
}

/* Before an operation's registers change the 2D state (radeon_accelfuncs.c, at every set-up): the 2D destination
 * cache flushed in the queue, and the queue held until the 2D engine is idle and clean. Without it a few pixels of
 * the operation before, still in the cache, came out wrong when the surface or the data type changed under them
 * (DTWOD on the P3: 2 to 9 stray pixels a case, the same ones each run). */
static void settle(void)
{
    radeon_2d_begin();                  /* (after 3D drawing: that engine idle and clean first) */
    fifo_wait(2);
    eng_wr(RADEON_DSTCACHE_CTLSTAT, RADEON_RB2D_DC_FLUSH_ALL);
    eng_wr(RADEON_WAIT_UNTIL, RADEON_WAIT_2D_IDLECLEAN | RADEON_WAIT_DMA_GUI_IDLE);
}

/* The FIFO drained, the engine idle, then its cache flushed to memory (RADEONWaitForIdleMMIO). */
int radeon_sync(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    if (!radeon.mmio)
        return 0;
    if (radeon_cp_running())            /* (the caches purged in the stream, the ring fetched, the engine idle) */
        return radeon_cp_idle_wait(timeout_us);
    while (!radeon_idle())
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return cache_flush(t0, timeout_us);
}

/* RADEONEngineReset for an R100-family chip, without its reads and writes of the PLL's MCLK_CNTL (it writes back
 * what it read: nothing changes, and every PLL access wants the RV100's 5 ms): the engines' soft reset bits set and
 * cleared (the host interface's among them the first time), the cache flushed, the engines' again and the host data
 * path's by HOST_PATH_CNTL ("soft resetting HDP through RBBM_SOFT_RESET can cause some unexpected behaviour on
 * some machines"). Nothing of the memory controller's. */
void radeon_engine_reset(void)
{
    const uint32_t eng = RADEON_SOFT_RESET_CP | RADEON_SOFT_RESET_SE | RADEON_SOFT_RESET_RE | RADEON_SOFT_RESET_PP |
                         RADEON_SOFT_RESET_E2 | RADEON_SOFT_RESET_RB;
    uint32_t rbbm, host;
    rbbm = radeon_rd(RADEON_RBBM_SOFT_RESET);
    radeon_wr(RADEON_RBBM_SOFT_RESET, rbbm | eng | RADEON_SOFT_RESET_HI);
    (void)radeon_rd(RADEON_RBBM_SOFT_RESET);
    radeon_wr(RADEON_RBBM_SOFT_RESET, rbbm & ~(eng | RADEON_SOFT_RESET_HI));
    (void)radeon_rd(RADEON_RBBM_SOFT_RESET);
    radeon_wr(RADEON_RBBM_SOFT_RESET, rbbm);
    (void)radeon_rd(RADEON_RBBM_SOFT_RESET);
    cache_flush(sys_time_us(), 100000u);
    host = radeon_rd(RADEON_HOST_PATH_CNTL);
    rbbm = radeon_rd(RADEON_RBBM_SOFT_RESET);
    radeon_wr(RADEON_RBBM_SOFT_RESET, rbbm | eng);
    (void)radeon_rd(RADEON_RBBM_SOFT_RESET);
    radeon_wr(RADEON_RBBM_SOFT_RESET, rbbm & ~eng);
    (void)radeon_rd(RADEON_RBBM_SOFT_RESET);
    radeon_wr(RADEON_HOST_PATH_CNTL, host | RADEON_HDP_SOFT_RESET);
    (void)radeon_rd(RADEON_HOST_PATH_CNTL);
    radeon_wr(RADEON_HOST_PATH_CNTL, host);
    radeon_wr(RADEON_RBBM_SOFT_RESET, rbbm);
}

void radeon_set_target(uint32_t off, int pitch_bytes, int bpp)
{
    t_off = off;
    t_pitch = pitch_bytes;
    t_bpp = bpp;
    t_bytes = bpp == 8 ? 1 : bpp == 32 ? 4 : 2;
    t_dt = bpp == 8 ? RADEON_DT_CI8 : bpp == 15 ? RADEON_DT_ARGB1555 : bpp == 16 ? RADEON_DT_RGB565
         : RADEON_DT_ARGB8888;
}

void radeon_set_clip(int x0, int y0, int x1, int y1)
{
    c_x0 = x0, c_y0 = y0, c_x1 = x1, c_y1 = y1;
}

/* The rectangle at (*x, *y), *w by *h, trimmed to the clip: 0 when nothing is left; *dx and *dy say how far its
 * corner moved. */
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

/* A surface's pitch-and-offset word (radeon_exa.c's RADEONGetOffsetPitch: the pitch in 64-byte units at bit 22, the
 * offset, in the chip's own address space, in 1 KB units), the offset's part below 1 KB moved into x (*xadd). */
static uint32_t pitch_offset(uint32_t off, int pitch_bytes, int *xadd)
{
    *xadd = (int)((off & 1023u) / (uint32_t)t_bytes);
    return ((uint32_t)(pitch_bytes / 64) << 22) | ((radeon.fb_location + off) >> 10);
}

static uint32_t yx(int x, int y) { return ((uint32_t)y << 16) | ((uint32_t)x & 0xFFFFu); }

static uint32_t master(uint32_t extra)
{
    return RADEON_GMC_DST_PITCH_OFFSET_CNTL | t_dt << RADEON_GMC_DST_DATATYPE_SHIFT | RADEON_GMC_CLR_CMP_CNTL_DIS |
           extra;
}

static uint32_t pixel(uint32_t v) { return t_bpp == 8 ? v & 0xFFu : t_bpp == 32 ? v : v & 0xFFFFu; }

void radeon_fill(int x, int y, int w, int h, uint32_t value)
{
    int dx, dy, xa;
    uint32_t po;
    if (w <= 0 || h <= 0 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    po = pitch_offset(t_off, t_pitch, &xa);
    settle();
    fifo_wait(6);
    eng_wr(RADEON_DP_GUI_MASTER_CNTL, master(RADEON_GMC_BRUSH_SOLID_COLOR | RADEON_GMC_SRC_DATATYPE_COLOR | RADEON_ROP3_P));
    eng_wr(RADEON_DP_BRUSH_FRGD_CLR, pixel(value));
    eng_wr(RADEON_DP_CNTL, RADEON_DST_X_LEFT_TO_RIGHT | RADEON_DST_Y_TOP_TO_BOTTOM);
    eng_wr(RADEON_DST_PITCH_OFFSET, po);
    eng_wr(RADEON_DST_Y_X, yx(x + xa, y));
    eng_wr(RADEON_DST_WIDTH_HEIGHT, ((uint32_t)w << 16) | (uint32_t)h);
}

/* A copy from (sx, sy) of src to (x, y) of the target, already trimmed; the direction chosen so an overlap reads
 * before it writes (RADEONCopy: a right-to-left or bottom-to-top copy starts at its far corner). */
static void blt(uint32_t src_off, int src_pitch, int sx, int sy, int x, int y, int w, int h)
{
    int same = src_off == t_off && src_pitch == t_pitch, xs, xd;
    uint32_t cntl = 0, spo = pitch_offset(src_off, src_pitch, &xs), dpo = pitch_offset(t_off, t_pitch, &xd);
    sx += xs, x += xd;
    if (!same || sx >= x)
        cntl |= RADEON_DST_X_LEFT_TO_RIGHT;
    else
        sx += w - 1, x += w - 1;
    if (!same || sy >= y)
        cntl |= RADEON_DST_Y_TOP_TO_BOTTOM;
    else
        sy += h - 1, y += h - 1;
    settle();
    fifo_wait(7);
    eng_wr(RADEON_DP_GUI_MASTER_CNTL, master(RADEON_GMC_SRC_PITCH_OFFSET_CNTL | RADEON_GMC_BRUSH_NONE |
                                            RADEON_GMC_SRC_DATATYPE_COLOR | RADEON_ROP3_S | RADEON_DP_SRC_SOURCE_MEMORY));
    eng_wr(RADEON_DP_CNTL, cntl);
    eng_wr(RADEON_DST_PITCH_OFFSET, dpo);
    eng_wr(RADEON_SRC_PITCH_OFFSET, spo);
    eng_wr(RADEON_SRC_Y_X, yx(sx, sy));
    eng_wr(RADEON_DST_Y_X, yx(x, y));
    eng_wr(RADEON_DST_HEIGHT_WIDTH, ((uint32_t)h << 16) | (uint32_t)w);
}

void radeon_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    int mx, my;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || !trim(&dx, &dy, &w, &h, &mx, &my))
        return;
    blt(t_off, t_pitch, sx + mx, sy + my, dx, dy, w, h);
}

void radeon_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h)
{
    int mx, my;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || !trim(&dx, &dy, &w, &h, &mx, &my))
        return;
    blt(src_off, src_pitch_bytes, sx + mx, sy + my, dx, dy, w, h);
}

/* Pixels from the CPU written into VRAM through the window, the engine idle first (as radeon_exa.c's upload). */
void radeon_image(int x, int y, int w, int h, const void *px, int stride)
{
    int dx, dy, j;
    const uint8_t *p = (const uint8_t *)px;
    if (w <= 0 || h <= 0 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    radeon_sync(1000000);
    p += (long)dy * stride + (long)dx * t_bytes;
    for (j = 0; j < h; j++, p += stride) {
        volatile uint8_t *d = radeon.fb + t_off + (uint32_t)(y + j) * (uint32_t)t_pitch + (uint32_t)x * (uint32_t)t_bytes;
        int i;
        for (i = 0; i < w * t_bytes; i++)
            d[i] = p[i];
    }
    radeon_vram_written(t_off + (uint32_t)(y + h - 1) * (uint32_t)t_pitch + (uint32_t)(x + w - 1) * (uint32_t)t_bytes);
}

static uint32_t rowbuf[2048 / 32 + 1];

/* Colour expansion from host data: each row's bits from bit dx, the leftmost pixel in bit 7 of the first byte
 * (BYTE_MSB_TO_LSB), padded with clear bits to whole dwords and drawn at the padded width with clear bits left
 * alone (MONO_FG_LA): the same pixels whether or not the engine pads rows itself. Opaque: the background filled
 * first. */
void radeon_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, unsigned flags)
{
    int dx, dy, j, i, dwords, xa, n = 0, total;
    uint32_t po;
    if (w <= 0 || h <= 0 || w > 2048 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    if (!(flags & RADEON_MONO_TRANSPARENT))
        radeon_fill(x, y, w, h, bg);
    dwords = (w + 31) / 32;
    total = dwords * h;
    po = pitch_offset(t_off, t_pitch, &xa);
    settle();
    fifo_wait(6);
    eng_wr(RADEON_DP_GUI_MASTER_CNTL, master(RADEON_GMC_BRUSH_NONE | RADEON_GMC_SRC_DATATYPE_MONO_FG_LA |
                                            RADEON_GMC_BYTE_MSB_TO_LSB | RADEON_ROP3_S | RADEON_DP_SRC_SOURCE_HOST_DATA));
    eng_wr(RADEON_DP_SRC_FRGD_CLR, pixel(fg));
    eng_wr(RADEON_DP_CNTL, RADEON_DST_X_LEFT_TO_RIGHT | RADEON_DST_Y_TOP_TO_BOTTOM);
    eng_wr(RADEON_DST_PITCH_OFFSET, po);
    eng_wr(RADEON_DST_Y_X, yx(x + xa, y));
    eng_wr(RADEON_DST_HEIGHT_WIDTH, ((uint32_t)h << 16) | (uint32_t)(dwords * 32));
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
        for (i = 0; i < dwords; i++, n++) {
            if ((n & 7) == 0)
                fifo_wait(8);
            eng_wr(n == total - 1 ? RADEON_HOST_DATA_LAST : RADEON_HOST_DATA0 + (uint32_t)(n & 7) * 4u, rowbuf[i]);
        }
    }
}

