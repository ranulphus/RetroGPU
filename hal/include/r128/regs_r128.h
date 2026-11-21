/* regs_r128.h - the ATI Rage 128's registers this layer uses (docs/rage128.md, R1): offsets and bits as X.org's
 * xf86-video-r128 driver's r128_reg.h names them. That file's notice:
 *
 *   Copyright 1999, 2000 ATI Technologies Inc., Markham, Ontario, Precision Insight, Inc., Cedar Park, Texas, and
 *   VA Linux Systems Inc., Fremont, California. All Rights Reserved.
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
 *   PRECISION INSIGHT, VA LINUX SYSTEMS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 *   THE USE OR OTHER DEALINGS IN THE SOFTWARE. */
#ifndef R128_REGS_R128_H
#define R128_REGS_R128_H

#define R128_PCI_VENDOR             0x1002u

/* Configuration, clocks, reset */
#define R128_CLOCK_CNTL_INDEX       0x0008
#define     R128_PLL_WR_EN          (1u << 7)
#define R128_CLOCK_CNTL_DATA        0x000C
#define R128_CRTC_GEN_CNTL          0x0050
#define     R128_CRTC_CUR_EN        (1u << 16)
#define R128_CRTC_EXT_CNTL          0x0054
#define R128_CRTC_STATUS            0x005C
#define R128_CONFIG_MEMSIZE         0x00F8
#define R128_GEN_RESET_CNTL         0x00F0
#define     R128_SOFT_RESET_GUI     (1u << 0)
#define R128_PC_NGUI_CTLSTAT        0x0184
#define     R128_PC_FLUSH_ALL       0x00FFu
#define     R128_PC_BUSY            (1u << 31)
#define R128_MCLK_CNTL              0x0F        /* (a PLL register) */
#define     R128_FORCE_GCP          (1u << 16)
#define     R128_FORCE_PIPE3D_CP    (1u << 17)

/* The CRTC: the display start (bytes, a multiple of 8), the pitch (pixels / 8), the cursor */
#define R128_CRTC_OFFSET            0x0224
#define R128_CRTC_OFFSET_CNTL       0x0228
#define R128_CRTC_PITCH             0x022C
#define R128_CUR_OFFSET             0x0260
#define R128_CUR_HORZ_VERT_POSN     0x0264
#define R128_CUR_HORZ_VERT_OFF      0x0268
#define     R128_CUR_LOCK           (1u << 31)
#define R128_CUR_CLR0               0x026C
#define R128_CUR_CLR1               0x0270

/* The 2D engine */
#define R128_DST_PITCH_OFFSET       0x142C
#define R128_SRC_PITCH_OFFSET       0x1428
#define R128_SRC_Y_X                0x1434
#define R128_DST_Y_X                0x1438
#define R128_DST_HEIGHT_WIDTH       0x143C      /* (starts a copy or a host-data operation) */
#define R128_DP_GUI_MASTER_CNTL     0x146C
#define     R128_GMC_SRC_PITCH_OFFSET_CNTL (1u << 0)
#define     R128_GMC_DST_PITCH_OFFSET_CNTL (1u << 1)
#define     R128_GMC_BRUSH_SOLID_COLOR     (13u << 4)
#define     R128_GMC_BRUSH_NONE            (15u << 4)
#define     R128_GMC_DST_DATATYPE_SHIFT    8
#define     R128_GMC_SRC_DATATYPE_MONO_FG_BG (0u << 12)
#define     R128_GMC_SRC_DATATYPE_MONO_FG_LA (1u << 12)
#define     R128_GMC_SRC_DATATYPE_COLOR    (3u << 12)
#define     R128_GMC_BYTE_MSB_TO_LSB       (0u << 14)
#define     R128_DP_SRC_SOURCE_MEMORY      (2u << 24)
#define     R128_DP_SRC_SOURCE_HOST_DATA   (3u << 24)
#define     R128_GMC_CLR_CMP_CNTL_DIS      (1u << 28)
#define     R128_GMC_AUX_CLIP_DIS          (1u << 29)
#define     R128_ROP3_S                    0x00CC0000u
#define     R128_ROP3_P                    0x00F00000u
#define R128_DP_BRUSH_BKGD_CLR      0x1478
#define R128_DP_BRUSH_FRGD_CLR      0x147C
#define R128_DST_WIDTH_HEIGHT       0x1598      /* (starts a fill) */
#define R128_DP_SRC_FRGD_CLR        0x15D8
#define R128_DP_SRC_BKGD_CLR        0x15DC
#define R128_DST_BRES_ERR           0x1628
#define R128_DST_BRES_INC           0x162C
#define R128_DST_BRES_DEC           0x1630
#define R128_AUX_SC_CNTL            0x1660
#define R128_GUI_PROBE              0x16BC
#define R128_DP_CNTL                0x16C0
#define     R128_DST_X_LEFT_TO_RIGHT (1u << 0)
#define     R128_DST_Y_TOP_TO_BOTTOM (1u << 1)
#define R128_DP_DATATYPE            0x16C4
#define     R128_HOST_BIG_ENDIAN_EN (1u << 29)
#define R128_DP_WRITE_MASK          0x16CC
#define R128_DEFAULT_OFFSET         0x16E0
#define R128_DEFAULT_PITCH          0x16E4
#define R128_DEFAULT_SC_BOTTOM_RIGHT 0x16E8
#define     R128_DEFAULT_SC_RIGHT_MAX  (0x1FFFu << 0)
#define     R128_DEFAULT_SC_BOTTOM_MAX (0x1FFFu << 16)
#define R128_SC_TOP_LEFT            0x16EC
#define R128_SC_BOTTOM_RIGHT        0x16F0
#define R128_GUI_STAT               0x1740
#define     R128_GUI_FIFOCNT_MASK   0x0FFFu
#define     R128_GUI_ACTIVE         (1u << 31)
#define R128_HOST_DATA0             0x17C0      /* (to 0x17DC: eight in a row) */
#define R128_HOST_DATA_LAST         0x17E0
#define R128_SCALE_3D_CNTL          0x1A00

/* DP_GUI_MASTER_CNTL's destination data types (r128_reg.h's R128_DATATYPE_* >> 16) */
enum { R128_DT_CI8 = 2, R128_DT_ARGB1555 = 3, R128_DT_RGB565 = 4, R128_DT_RGB888 = 5, R128_DT_ARGB8888 = 6 };

#endif
