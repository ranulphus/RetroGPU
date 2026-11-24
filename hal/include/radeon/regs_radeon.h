/* regs_radeon.h - the ATI Radeon's (R100 family's) registers this layer uses (docs/radeon.md, RD1): offsets and bits
 * as X.org's xf86-video-ati driver's radeon_reg.h names them. That file's notice:
 *
 *   Copyright 2000 ATI Technologies Inc., Markham, Ontario, and VA Linux Systems Inc., Fremont, California.
 *   All Rights Reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 *   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 *   on the rights to use, copy, modify, merge, publish, distribute, sub license, and/or sell copies of the Software,
 *   and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice (including the next paragraph) shall be included in all
 *   copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 *   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL ATI,
 *   VA LINUX SYSTEMS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *   OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *   DEALINGS IN THE SOFTWARE. */
#ifndef RADEON_REGS_RADEON_H
#define RADEON_REGS_RADEON_H

#define RADEON_PCI_VENDOR           0x1002u

/* Configuration, clocks, reset, status */
#define RADEON_CLOCK_CNTL_INDEX     0x0008
#define     RADEON_PLL_WR_EN        (1u << 7)
#define RADEON_CLOCK_CNTL_DATA      0x000C
#define RADEON_CRTC_GEN_CNTL        0x0050
#define     RADEON_CRTC_CUR_EN      (1u << 16)
#define RADEON_CRTC_EXT_CNTL        0x0054
#define RADEON_DAC_CNTL             0x0058
#define     RADEON_DAC_8BIT_EN      (1u << 8)
#define RADEON_CRTC_STATUS          0x005C
#define RADEON_PALETTE_INDEX        0x00B0
#define RADEON_PALETTE_DATA         0x00B4
#define RADEON_RBBM_SOFT_RESET      0x00F0
#define     RADEON_SOFT_RESET_CP    (1u << 0)
#define     RADEON_SOFT_RESET_HI    (1u << 1)
#define     RADEON_SOFT_RESET_SE    (1u << 2)
#define     RADEON_SOFT_RESET_RE    (1u << 3)
#define     RADEON_SOFT_RESET_PP    (1u << 4)
#define     RADEON_SOFT_RESET_E2    (1u << 5)
#define     RADEON_SOFT_RESET_RB    (1u << 6)
#define RADEON_CONFIG_MEMSIZE       0x00F8
#define RADEON_HOST_PATH_CNTL       0x0130
#define     RADEON_HDP_SOFT_RESET   (1u << 26)
#define RADEON_MC_FB_LOCATION       0x0148      /* (the frame buffer in the chip's own address space: start >> 16 in
                                                 * the low word, end >> 16 in the high) */
#define RADEON_RBBM_STATUS          0x0E40
#define     RADEON_RBBM_FIFOCNT_MASK 0x007Fu
#define     RADEON_RBBM_ACTIVE      (1u << 31)
#define RADEON_RB3D_DSTCACHE_CTLSTAT 0x325C
#define     RADEON_RB3D_DC_FLUSH_ALL 0xFu
#define     RADEON_RB3D_DC_BUSY     (1u << 31)
#define RADEON_RB2D_DSTCACHE_CTLSTAT 0x342C     /* (the 2D engine's destination cache, written directly) */
#define     RADEON_RB2D_DC_FLUSH_ALL 0xFu
#define     RADEON_RB2D_DC_BUSY     (1u << 31)

/* The CRTC: the display start (bytes, a multiple of 8, from DISPLAY_BASE_ADDR), the pitch (pixels / 8), the cursor */
#define RADEON_CRTC_OFFSET          0x0224
#define RADEON_CRTC_OFFSET_CNTL     0x0228
#define     RADEON_CRTC_GUI_TRIG_OFFSET (1u << 30)  /* (R) the last start written not yet shown */
#define RADEON_CRTC_PITCH           0x022C
#define RADEON_DISPLAY_BASE_ADDR    0x023C
#define RADEON_CUR_OFFSET           0x0260
#define RADEON_CUR_HORZ_VERT_POSN   0x0264
#define RADEON_CUR_HORZ_VERT_OFF    0x0268
#define     RADEON_CUR_LOCK         (1u << 31)
#define RADEON_CUR_CLR0             0x026C
#define RADEON_CUR_CLR1             0x0270

/* The 2D engine (the Rage 128's registers at the Rage 128's offsets) */
#define RADEON_SRC_PITCH_OFFSET     0x1428
#define RADEON_DST_PITCH_OFFSET     0x142C      /* (the pitch in 64-byte units at bit 22, the offset in 1 KB units) */
#define RADEON_SRC_Y_X              0x1434
#define RADEON_DST_Y_X              0x1438
#define RADEON_DST_HEIGHT_WIDTH     0x143C      /* (starts a copy or a host-data operation) */
#define RADEON_DP_GUI_MASTER_CNTL   0x146C
#define     RADEON_GMC_SRC_PITCH_OFFSET_CNTL (1u << 0)
#define     RADEON_GMC_DST_PITCH_OFFSET_CNTL (1u << 1)
#define     RADEON_GMC_BRUSH_SOLID_COLOR     (13u << 4)
#define     RADEON_GMC_BRUSH_NONE            (15u << 4)
#define     RADEON_GMC_DST_DATATYPE_SHIFT    8
#define     RADEON_GMC_SRC_DATATYPE_MONO_FG_BG (0u << 12)
#define     RADEON_GMC_SRC_DATATYPE_MONO_FG_LA (1u << 12)
#define     RADEON_GMC_SRC_DATATYPE_COLOR    (3u << 12)
#define     RADEON_GMC_BYTE_MSB_TO_LSB       (0u << 14)
#define     RADEON_DP_SRC_SOURCE_MEMORY      (2u << 24)
#define     RADEON_DP_SRC_SOURCE_HOST_DATA   (3u << 24)
#define     RADEON_GMC_CLR_CMP_CNTL_DIS      (1u << 28)
#define     RADEON_GMC_WR_MSK_DIS            (1u << 30)
#define     RADEON_ROP3_S                    0x00CC0000u
#define     RADEON_ROP3_P                    0x00F00000u
#define RADEON_DP_BRUSH_BKGD_CLR    0x1478
#define RADEON_DP_BRUSH_FRGD_CLR    0x147C
#define RADEON_DST_WIDTH_HEIGHT     0x1598      /* (starts a fill) */
#define RADEON_DP_SRC_FRGD_CLR      0x15D8
#define RADEON_DP_SRC_BKGD_CLR      0x15DC
#define RADEON_DP_CNTL              0x16C0
#define     RADEON_DST_X_LEFT_TO_RIGHT (1u << 0)
#define     RADEON_DST_Y_TOP_TO_BOTTOM (1u << 1)
#define RADEON_DP_DATATYPE          0x16C4
#define     RADEON_HOST_BIG_ENDIAN_EN (1u << 29)
#define RADEON_DP_WRITE_MASK        0x16CC
#define RADEON_DEFAULT_SC_BOTTOM_RIGHT 0x16E8
#define     RADEON_DEFAULT_SC_RIGHT_MAX  (0x1FFFu << 0)
#define     RADEON_DEFAULT_SC_BOTTOM_MAX (0x1FFFu << 16)
#define RADEON_SC_TOP_LEFT          0x16EC
#define RADEON_SC_BOTTOM_RIGHT      0x16F0
#define RADEON_DSTCACHE_CTLSTAT      0x1714      /* (the same cache's control in the engine's queue: in order with
                                                 * the drawing) */
#define RADEON_WAIT_UNTIL           0x1720      /* (the queue stalled until the events named) */
#define     RADEON_WAIT_DMA_GUI_IDLE    (1u << 9)
#define     RADEON_WAIT_2D_IDLECLEAN    (1u << 16)
#define     RADEON_WAIT_3D_IDLECLEAN    (1u << 17)
#define     RADEON_WAIT_HOST_IDLECLEAN  (1u << 18)
#define RADEON_HOST_DATA0           0x17C0      /* (to 0x17DC: eight in a row) */
#define RADEON_HOST_DATA_LAST       0x17E0

/* DP_GUI_MASTER_CNTL's destination data types (radeon_reg.h's RADEON_GMC_DST_* >> 8) */
enum { RADEON_DT_CI8 = 2, RADEON_DT_ARGB1555 = 3, RADEON_DT_RGB565 = 4, RADEON_DT_ARGB8888 = 6 };

#endif
