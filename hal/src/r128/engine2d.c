/* engine2d.c - the Rage 128's 2D engine by MMIO, the CCE off (docs/rage128.md, R1): fills, copies within and between
 * surfaces, colour expansion from the CPU, images from the CPU. The sequences are X.org's xf86-video-r128's
 * (r128_exa.c's R128PrepareSolid/R128Solid and R128DoPrepareCopy/R128Copy; r128_accel.c's R128EngineReset,
 * R128WaitForFifo and R128WaitForIdle; MIT). Every operation is trimmed to the clip here, in software, as the ViRGE
 * layer's are, so the engine's scissors stay at their widest. */
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/sys.h"
#include <string.h>

/* A drawing-engine register: by MMIO, or a type-0 packet while the CCE runs (it ignores MMIO writes there). */
static void eng_wr(uint32_t reg, uint32_t v)
{
    r128_3d_stale = 1;
    if (r128_cce_running())
        r128_cce_reg(reg, v);
    else
        r128_wr(reg, v);
}

static uint32_t t_off, t_dt;
static int t_pitch, t_bpp, t_bytes;
static int c_x0, c_y0, c_x1 = 0x7FFF, c_y1 = 0x7FFF;

/* Room for n register writes in the 64-entry FIFO, bounded (a stuck engine shows at the next sync). */
static void fifo_wait(int n)
{
    uint32_t t0 = sys_time_us();
    if (r128_cce_running())
        return;                         /* (the CCE's own flush waits for room) */
    while ((int)(r128_rd(R128_GUI_STAT) & R128_GUI_FIFOCNT_MASK) < n && sys_time_us() - t0 < 200000u)
        ;
}

int r128_idle(void)
{
    if (r128.mmio && r128_cce_running()) {
        uint32_t st;
        r128_cce_flush();
        st = r128_rd(R128_PM4_STAT);
        return (st & R128_PM4_FIFOCNT_MASK) >= 192 && !(st & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE));
    }
    return r128.mmio && (r128_rd(R128_GUI_STAT) & R128_GUI_FIFOCNT_MASK) >= 64 &&
           !(r128_rd(R128_GUI_STAT) & R128_GUI_ACTIVE);
}

/* The FIFO drained, the engine idle, then its pixel cache flushed to memory (R128WaitForIdle, R128EngineFlush). */
int r128_sync(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    if (!r128.mmio)
        return 0;
    if (r128_cce_running())
        return r128_cce_idle_wait(timeout_us);
    while ((r128_rd(R128_GUI_STAT) & R128_GUI_FIFOCNT_MASK) < 64 || (r128_rd(R128_GUI_STAT) & R128_GUI_ACTIVE))
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    r128_wr(R128_PC_NGUI_CTLSTAT, r128_rd(R128_PC_NGUI_CTLSTAT) | R128_PC_FLUSH_ALL);
    while (r128_rd(R128_PC_NGUI_CTLSTAT) & R128_PC_BUSY)
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return 0;
}

void r128_engine_reset(void)
{
    uint32_t index, mclk, gen;
    uint32_t t0 = sys_time_us();
    r128_wr(R128_PC_NGUI_CTLSTAT, r128_rd(R128_PC_NGUI_CTLSTAT) | R128_PC_FLUSH_ALL);
    while ((r128_rd(R128_PC_NGUI_CTLSTAT) & R128_PC_BUSY) && sys_time_us() - t0 < 100000u)
        ;
    index = r128_rd(R128_CLOCK_CNTL_INDEX);
    mclk = r128_pll_rd(R128_MCLK_CNTL);
    r128_pll_wr(R128_MCLK_CNTL, mclk | R128_FORCE_GCP | R128_FORCE_PIPE3D_CP);
    gen = r128_rd(R128_GEN_RESET_CNTL);
    r128_wr(R128_GEN_RESET_CNTL, gen | R128_SOFT_RESET_GUI);
    (void)r128_rd(R128_GEN_RESET_CNTL);
    r128_wr(R128_GEN_RESET_CNTL, gen & ~R128_SOFT_RESET_GUI);
    (void)r128_rd(R128_GEN_RESET_CNTL);
    r128_pll_wr(R128_MCLK_CNTL, mclk);
    r128_wr(R128_CLOCK_CNTL_INDEX, index);
    r128_wr(R128_GEN_RESET_CNTL, gen);
    r128_cce_forget();                  /* (the CCE stops at an engine reset: the DRM's r128_do_engine_reset) */
}

void r128_set_target(uint32_t off, int pitch_bytes, int bpp)
{
    t_off = off;
    t_pitch = pitch_bytes;
    t_bpp = bpp;
    t_bytes = bpp == 8 ? 1 : bpp == 32 ? 4 : 2;
    t_dt = bpp == 8 ? R128_DT_CI8 : bpp == 15 ? R128_DT_ARGB1555 : bpp == 16 ? R128_DT_RGB565 : R128_DT_ARGB8888;
}

void r128_set_clip(int x0, int y0, int x1, int y1)
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

/* A surface's pitch-and-offset word (r128_exa.c's R128GetOffsetPitch: the pitch in 8-pixel units at bit 21, the
 * offset in 32-byte units), the offset's part below 32 bytes moved into x (*xadd). */
static uint32_t pitch_offset(uint32_t off, int pitch_bytes, int *xadd)
{
    *xadd = (int)((off & 31u) / (uint32_t)t_bytes);
    return ((uint32_t)(pitch_bytes / t_bytes / 8) << 21) | (off >> 5);
}

static uint32_t yx(int x, int y) { return ((uint32_t)y << 16) | ((uint32_t)x & 0xFFFFu); }

static uint32_t master(uint32_t extra)
{
    return R128_GMC_DST_PITCH_OFFSET_CNTL | t_dt << R128_GMC_DST_DATATYPE_SHIFT | R128_GMC_CLR_CMP_CNTL_DIS |
           R128_GMC_AUX_CLIP_DIS | extra;
}

static uint32_t pixel(uint32_t v) { return t_bpp == 8 ? v & 0xFFu : t_bpp == 32 ? v : v & 0xFFFFu; }

void r128_fill(int x, int y, int w, int h, uint32_t value)
{
    int dx, dy, xa;
    uint32_t po;
    if (w <= 0 || h <= 0 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    po = pitch_offset(t_off, t_pitch, &xa);
    fifo_wait(6);
    eng_wr(R128_DP_GUI_MASTER_CNTL, master(R128_GMC_BRUSH_SOLID_COLOR | R128_GMC_SRC_DATATYPE_COLOR | R128_ROP3_P));
    eng_wr(R128_DP_BRUSH_FRGD_CLR, pixel(value));
    eng_wr(R128_DP_CNTL, R128_DST_X_LEFT_TO_RIGHT | R128_DST_Y_TOP_TO_BOTTOM);
    eng_wr(R128_DST_PITCH_OFFSET, po);
    eng_wr(R128_DST_Y_X, yx(x + xa, y));
    eng_wr(R128_DST_WIDTH_HEIGHT, ((uint32_t)w << 16) | (uint32_t)h);
}

/* A copy from (sx, sy) of src to (x, y) of the target, already trimmed; the direction chosen so an overlap reads
 * before it writes (R128Copy: a right-to-left or bottom-to-top copy starts at its far corner). */
static void blt(uint32_t src_off, int src_pitch, int sx, int sy, int x, int y, int w, int h)
{
    int same = src_off == t_off && src_pitch == t_pitch, xs, xd;
    uint32_t cntl = 0, spo = pitch_offset(src_off, src_pitch, &xs), dpo = pitch_offset(t_off, t_pitch, &xd);
    sx += xs, x += xd;
    if (!same || sx >= x)
        cntl |= R128_DST_X_LEFT_TO_RIGHT;
    else
        sx += w - 1, x += w - 1;
    if (!same || sy >= y)
        cntl |= R128_DST_Y_TOP_TO_BOTTOM;
    else
        sy += h - 1, y += h - 1;
    fifo_wait(7);
    eng_wr(R128_DP_GUI_MASTER_CNTL, master(R128_GMC_SRC_PITCH_OFFSET_CNTL | R128_GMC_BRUSH_NONE |
                                            R128_GMC_SRC_DATATYPE_COLOR | R128_ROP3_S | R128_DP_SRC_SOURCE_MEMORY));
    eng_wr(R128_DP_CNTL, cntl);
    eng_wr(R128_DST_PITCH_OFFSET, dpo);
    eng_wr(R128_SRC_PITCH_OFFSET, spo);
    eng_wr(R128_SRC_Y_X, yx(sx, sy));
    eng_wr(R128_DST_Y_X, yx(x, y));
    eng_wr(R128_DST_HEIGHT_WIDTH, ((uint32_t)h << 16) | (uint32_t)w);
}

void r128_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    int mx, my;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || !trim(&dx, &dy, &w, &h, &mx, &my))
        return;
    blt(t_off, t_pitch, sx + mx, sy + my, dx, dy, w, h);
}

void r128_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h)
{
    int mx, my;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || !trim(&dx, &dy, &w, &h, &mx, &my))
        return;
    blt(src_off, src_pitch_bytes, sx + mx, sy + my, dx, dy, w, h);
}

/* Pixels from the CPU written into VRAM through the window, the engine idle first (as r128_exa.c's upload). */
void r128_image(int x, int y, int w, int h, const void *px, int stride)
{
    int dx, dy, j;
    const uint8_t *p = (const uint8_t *)px;
    if (w <= 0 || h <= 0 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    r128_sync(1000000);
    p += (long)dy * stride + (long)dx * t_bytes;
    for (j = 0; j < h; j++, p += stride) {
        volatile uint8_t *d = r128.fb + t_off + (uint32_t)(y + j) * (uint32_t)t_pitch + (uint32_t)x * (uint32_t)t_bytes;
        int i;
        for (i = 0; i < w * t_bytes; i++)
            d[i] = p[i];
    }
}

static uint32_t rowbuf[2048 / 32 + 1];

/* Colour expansion from host data: each row's bits from bit dx, the leftmost pixel in bit 7 of the first byte
 * (BYTE_MSB_TO_LSB), padded with clear bits to whole dwords and drawn at the padded width with clear bits left
 * alone (MONO_FG_LA): the same pixels whether or not the engine pads rows itself. Opaque: the background filled
 * first. */
void r128_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, unsigned flags)
{
    int dx, dy, j, i, dwords, xa, n = 0, total;
    uint32_t po;
    if (w <= 0 || h <= 0 || w > 2048 || !trim(&x, &y, &w, &h, &dx, &dy))
        return;
    if (!(flags & R128_MONO_TRANSPARENT))
        r128_fill(x, y, w, h, bg);
    dwords = (w + 31) / 32;
    total = dwords * h;
    po = pitch_offset(t_off, t_pitch, &xa);
    fifo_wait(6);
    eng_wr(R128_DP_GUI_MASTER_CNTL, master(R128_GMC_BRUSH_NONE | R128_GMC_SRC_DATATYPE_MONO_FG_LA |
                                            R128_GMC_BYTE_MSB_TO_LSB | R128_ROP3_S | R128_DP_SRC_SOURCE_HOST_DATA));
    eng_wr(R128_DP_SRC_FRGD_CLR, pixel(fg));
    eng_wr(R128_DP_CNTL, R128_DST_X_LEFT_TO_RIGHT | R128_DST_Y_TOP_TO_BOTTOM);
    eng_wr(R128_DST_PITCH_OFFSET, po);
    eng_wr(R128_DST_Y_X, yx(x + xa, y));
    eng_wr(R128_DST_HEIGHT_WIDTH, ((uint32_t)h << 16) | (uint32_t)(dwords * 32));
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
            eng_wr(n == total - 1 ? R128_HOST_DATA_LAST : R128_HOST_DATA0 + (uint32_t)(n & 7) * 4u, rowbuf[i]);
        }
    }
}
