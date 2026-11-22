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

/* The 3D context (X.org's r128_reg.h; Mesa 7.x's r128 driver's values: r128_state.c's r128DDInitState) */
#define R128_PC_GUI_CTLSTAT         0x1748
#define     R128_PC_FLUSH_GUI       (3u << 0)
#define R128_SETUP_CNTL             0x1BC4
#define     R128_COLOR_GOURAUD      (4u << 3)
#define     R128_PRIM_TYPE_TRI      (0u << 7)
#define     R128_TEXTURE_ST_MULT_W  (0u << 9)
#define     R128_STARTING_VERTEX_1  (1u << 14)
#define     R128_ENDING_VERTEX_3    (3u << 16)
#define     R128_SU_POLY_LINE_NOT_LAST (1u << 18)
#define     R128_SUB_PIX_4BITS      (1u << 19)
#define R128_WINDOW_XY_OFFSET       0x1BCC
#define R128_DST_PITCH_OFFSET_C     0x1C80      /* (the context's 12 registers from here, in a row) */
#define R128_DP_GUI_MASTER_CNTL_C   0x1C84
#define     R128_GMC_DST_CLIPPING   (1u << 3)
#define     R128_GMC_3D_FCN_EN      (1u << 27)
#define     R128_GMC_WR_MSK_DIS     (1u << 30)
#define R128_Z_STEN_CNTL_C          0x1C98
#define     R128_Z_PIX_WIDTH_16     (0u << 1)
#define     R128_Z_PIX_WIDTH_24     (1u << 1)
#define     R128_Z_PIX_WIDTH_32     (2u << 1)
#define     R128_Z_TEST_SHIFT       4           /* 0 never, 1 <, 2 <=, 3 =, 4 >=, 5 >, 6 !=, 7 always */
#define     R128_STENCIL_TEST_ALWAYS (7u << 12)
#define     R128_STENCIL_TEST_SHIFT 12          /* the Z test's codes */
#define     R128_STENCIL_S_FAIL_SHIFT 16        /* 0 keep, 1 zero, 2 replace, 3 increment, 4 decrement, 5 invert */
#define     R128_STENCIL_ZPASS_SHIFT 20
#define     R128_STENCIL_ZFAIL_SHIFT 24
#define R128_TEX_CNTL_C             0x1C9C
#define     R128_Z_ENABLE           (1u << 0)
#define     R128_Z_WRITE_ENABLE     (1u << 1)
#define     R128_TEXMAP_ENABLE      (1u << 4)
#define     R128_STENCIL_ENABLE     (1u << 3)
#define     R128_SEC_TEXMAP_ENABLE  (1u << 5)
#define     R128_FOG_ENABLE         (1u << 7)
#define     R128_DITHER_ENABLE      (1u << 8)
#define     R128_ALPHA_ENABLE       (1u << 9)
#define     R128_ALPHA_TEST_ENABLE  (1u << 10)
#define     R128_SPEC_LIGHT_ENABLE  (1u << 11)   /* (the vertices' specular RGB added after the combine) */
#define     R128_TEX_CACHE_FLUSH    (1u << 23)
#define     R128_LOD_BIAS_SHIFT     24
#define R128_MISC_3D_STATE_CNTL_REG 0x1CA0
#define     R128_MISC_SCALE_3D_TEXMAP_SHADE (2u << 8)
#define     R128_MISC_SCALE_PIX_REPLICATE   (1u << 10)
#define     R128_FOG_TABLE          (1u << 14)
#define     R128_ALPHA_BLEND_SRC_SHIFT 16
#define     R128_ALPHA_BLEND_DST_SHIFT 20
#define     R128_ALPHA_TEST_SHIFT   24
enum { R128_BLEND_ZERO, R128_BLEND_ONE, R128_BLEND_SRCCOLOR, R128_BLEND_INVSRCCOLOR, R128_BLEND_SRCALPHA,
       R128_BLEND_INVSRCALPHA, R128_BLEND_DSTALPHA, R128_BLEND_INVDSTALPHA, R128_BLEND_DSTCOLOR, R128_BLEND_INVDSTCOLOR,
       R128_BLEND_SAT };
#define     R128_REF_ALPHA_MASK     0xFFu
#define R128_FOG_COLOR_C            0x1CAC
#define R128_PRIM_TEX_CNTL_C        0x1CB0     /* (and SEC_TEX_CNTL_C's: the same fields for the second unit) */
#define     R128_MIN_BLEND_SHIFT    1          /* 0 nearest, 1 linear, 2 mip nearest, 3 mip linear, 4 linear mip */
#define     R128_MAG_BLEND_SHIFT    4          /*   nearest, 5 linear mip linear; mag: 0 nearest, 1 linear */
#define     R128_MIP_MAP_DISABLE    (1u << 7)
#define     R128_TEX_CLAMP_S_SHIFT  8          /* 0 wrap, 1 mirror, 2 clamp (to the edge), 3 border colour */
#define     R128_TEX_CLAMP_T_SHIFT  11
#define     R128_TEX_DATATYPE_SHIFT 16
enum { R128_TEX_ARGB1555 = 3, R128_TEX_RGB565 = 4, R128_TEX_ARGB8888 = 6, R128_TEX_ARGB4444 = 15 };
#define     R128_SEC_SELECT_SEC_ST  (1u << 0)   /* (SEC_TEX_CNTL_C: the second unit reads s2, t2) */
#define R128_PRIM_TEXTURE_COMBINE_CNTL_C 0x1CB4 /* (and SEC_TEX_COMBINE_CNTL_C) */
enum { R128_COMB_DIS, R128_COMB_COPY, R128_COMB_COPY_INP, R128_COMB_MODULATE, R128_COMB_MODULATE2X,
       R128_COMB_MODULATE4X, R128_COMB_ADD, R128_COMB_ADD_SIGNED, R128_COMB_BLEND_VERTEX, R128_COMB_BLEND_TEXTURE,
       R128_COMB_BLEND_CONST, R128_COMB_BLEND_PREMULT, R128_COMB_BLEND_PREV, R128_COMB_BLEND_PREMULT_INV,
       R128_COMB_ADD_SIGNED2X, R128_COMB_BLEND_CONST_COLOR };
#define     R128_COLOR_FACTOR_SHIFT 4          /* 0 constant colour, 1 its inverse, 4 the texel, 5 its inverse, */
#define     R128_COMB_FCN_MSB       (1u << 8)   /*   6 the texel's alpha, 7 its inverse, 8 the previous colour */
#define     R128_INPUT_FACTOR_SHIFT 10         /* 2 constant colour, 3 its alpha, 4 interpolated colour, 5 its */
#define     R128_COMB_ALPHA_SHIFT   14         /*   alpha, 8 the previous colour, 9 its alpha */
#define     R128_ALPHA_FACTOR_SHIFT 18         /* 6 the texel's alpha, 7 its inverse */
#define     R128_INP_FACTOR_A_SHIFT 25         /* 1 constant alpha, 2 interpolated alpha, 4 the previous alpha */
#define R128_TEX_SIZE_PITCH_C       0x1CB8
#define R128_SEC_TEX_CNTL_C         0x1D00
#define     R128_SCALE_TEX_CACHE_SPLIT (1u << 9)   /* (SCALE_3D_CNTL: the texture cache shared by two units) */
#define R128_CONSTANT_COLOR_C       0x1D34
#define R128_STEN_REF_MASK_C        0x1D40
#define     R128_STEN_MASK_SHIFT    16          /* (the reference in bits 7-0) */
#define     R128_STEN_WRITE_MASK_SHIFT 24
#define R128_PLANE_3D_MASK_C        0x1D44
#define     R128_FRONT_DIR_CCW      (1u << 0)
#define     R128_BACKFACE_SOLID     (3u << 1)
#define     R128_FRONTFACE_SOLID    (3u << 3)
#define     R128_FPU_COLOR_GOURAUD  (2u << 5)
#define     R128_FPU_SUB_PIX_4BITS  (1u << 7)
#define     R128_FPU_MODE_3D        (1u << 8)
#define     R128_TRAP_BITS_DISABLE  (1u << 9)
#define     R128_FLAT_SHADE_VERTEX_OGL (1u << 14)
#define     R128_SCALE_DITHER_TABLE (1u << 1)
#define     R128_DITHER_INIT_RESET  (1u << 3)
#define     R128_SCALE_3D_TEXMAP_SHADE (2u << 6)
#define     R128_SCALE_PIX_REPLICATE (1u << 8)
#define     R128_TEX_MAP_ALPHA_IN_TEXTURE (1u << 30)
#define     R128_TEX_CACHE_LINE_SIZE_4QW (1u << 31)
#define R128_3D_RNDR_GEN_PRIM       0x00002500u /* (a type-3 opcode: the vertices inline) */
#define     R128_VC_FRMT_RHW        0x001u
#define     R128_VC_FRMT_DIFFUSE_ARGB 0x008u
#define     R128_VC_FRMT_SPEC_FRGB  0x040u
#define     R128_VC_FRMT_S_T        0x080u
#define     R128_VC_FRMT_S2_T2      0x100u
#define     R128_VC_PRIM_TRI_LIST   0x004u
#define     R128_VC_PRIM_TRI_FAN    0x005u
#define     R128_VC_WALK_RING       0x030u
#define     R128_VC_NUM_SHIFT       16

/* The CCE (Linux's r128_drv.h) */
#define R128_BUS_CNTL               0x0030      /* (bit 6: BUS_MASTER_DIS) */
#define     R128_BUS_MASTER_DIS     (1u << 6)
#define R128_PCI_GART_PAGE          0x017C      /* (31:12 the table's page; bit 0 PCI_GART_DIS) */
#define R128_BM_CHUNK_0_VAL         0x0A18
#define     R128_BM_FORCE_TO_PCI    (7u << 21)  /* (pointer, PM4 reads, everything else: X.org's for AGP boards) */
#define R128_DAC_CNTL               0x0058
#define     R128_DAC_8BIT_EN        (1u << 8)
#define R128_PALETTE_INDEX          0x00B0      /* (an 8-bit write: the entry; PALETTE_DATA then moves on) */
#define R128_PALETTE_DATA           0x00B4      /* (0x00RRGGBB) */
#define R128_PM4_BUFFER_OFFSET      0x0700
#define R128_PM4_BUFFER_WM_CNTL     0x0708
#define R128_PM4_BUFFER_CNTL        0x0704
#define     R128_PM4_NONPM4         (0u << 28)
#define     R128_PM4_192PIO         (1u << 28)
#define     R128_PM4_192BM          (2u << 28)
#define     R128_PM4_BUFFER_CNTL_NOUPDATE (1u << 27)
#define R128_PM4_BUFFER_DL_RPTR     0x0710
#define R128_PM4_BUFFER_DL_WPTR     0x0714
#define     R128_PM4_BUFFER_DL_DONE (1u << 31)
#define R128_PM4_VC_FPU_SETUP       0x071C
#define R128_PM4_STAT               0x07B8
#define     R128_PM4_FIFOCNT_MASK   0x0FFFu
#define     R128_PM4_BUSY           (1u << 16)
#define     R128_PM4_GUI_ACTIVE     (1u << 31)
#define R128_PM4_MICROCODE_ADDR     0x07D4
#define R128_PM4_MICROCODE_DATAH    0x07DC
#define R128_PM4_MICROCODE_DATAL    0x07E0
#define R128_PM4_BUFFER_ADDR        0x07F0
#define R128_PM4_MICRO_CNTL         0x07FC
#define     R128_PM4_MICRO_FREERUN  (1u << 30)
#define R128_PM4_FIFO_DATA_EVEN     0x1000
#define R128_PM4_FIFO_DATA_ODD      0x1004
#define R128_CCE_PACKET0            0x00000000u   /* | (count - 1) << 16 | reg >> 2 */
#define R128_CCE_PACKET2            0x80000000u   /* (a no-op: pads an odd count) */
#define R128_CCE_PACKET3            0xC0000000u   /* | opcode | (count - 1) << 16 */

/* DP_GUI_MASTER_CNTL's destination data types (r128_reg.h's R128_DATATYPE_* >> 16) */
enum { R128_DT_CI8 = 2, R128_DT_ARGB1555 = 3, R128_DT_RGB565 = 4, R128_DT_RGB888 = 5, R128_DT_ARGB8888 = 6 };

#endif
