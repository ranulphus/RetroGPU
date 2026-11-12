/* regs_virge.h - S3 ViRGE family registers used by RetroGPU's ViRGE/DX layer
 * (docs/virge.md). From S3's "ViRGE Integrated 3D Accelerator" data book
 * (August 1996), sections 15, 18, 19 and 22; the ViRGE/DX (86C375) shares
 * its S3d engine. Offsets are from the new MMIO base, the chip's 64 MB
 * window + 16 MB (BAR0 + 0x1000000). */
#ifndef VIRGE_REGS_VIRGE_H
#define VIRGE_REGS_VIRGE_H

#define VIRGE_MMIO_OFFSET   0x1000000u  /* new MMIO, from the window's base */
#define VIRGE_MMIO_SIZE     0x10000u
#define VIRGE_IMAGE_PORT    0x0000u     /* image data transfers, 32 KB (dword writes) */
#define VIRGE_IMAGE_PORT_SZ 0x8000u

/* Subsystem status (read) and advanced function control. */
#define VIRGE_SUBSYS_STAT   0x8504u
#define   VIRGE_STAT_FIFO_SHIFT 8       /* bits 12-8: S3d FIFO slots free, 0-16 */
#define   VIRGE_STAT_FIFO_MASK  0x1Fu
#define   VIRGE_STAT_IDLE       (1u << 13) /* 1: the S3d engine is idle */
#define VIRGE_FIFO_SLOTS    16
#define VIRGE_ADVFUNC_CNTL  0x850Cu

/* The colour pattern (8x8) and the BitBLT/rectangle fill group. */
#define VIRGE_COLOR_PATTERN 0xA100u
#define VIRGE_BLT_SRC_BASE  0xA4D4u     /* bits 21-3: the source's pixel 0,0, quadword aligned */
#define VIRGE_BLT_DEST_BASE 0xA4D8u
#define VIRGE_BLT_CLIP_LR   0xA4DCu     /* left 26-16, right 10-0, inclusive */
#define VIRGE_BLT_CLIP_TB   0xA4E0u     /* top 26-16, bottom 10-0, inclusive */
#define VIRGE_BLT_STRIDE    0xA4E4u     /* destination 27-16, source 11-0: bytes, multiples of 8 */
#define VIRGE_BLT_MONO_PAT0 0xA4E8u
#define VIRGE_BLT_MONO_PAT1 0xA4ECu
#define VIRGE_BLT_PAT_BG    0xA4F0u
#define VIRGE_BLT_PAT_FG    0xA4F4u     /* also the colour of rectangle fills */
#define VIRGE_BLT_SRC_BG    0xA4F8u     /* mono image transfers: a clear bit */
#define VIRGE_BLT_SRC_FG    0xA4FCu     /* mono image transfers: a set bit; transparent colour compare */
#define VIRGE_BLT_CMD       0xA500u
#define VIRGE_BLT_WH        0xA504u     /* width - 1 in 26-16, height (lines) in 10-0 */
#define VIRGE_BLT_SRC_XY    0xA508u     /* x 26-16, y 10-0 */
#define VIRGE_BLT_DEST_XY   0xA50Cu

/* The 2D command set register (BitBLT group; the line and polygon groups share its layout). */
#define VIRGE_CMD_AE            (1u << 0)   /* autoexecute */
#define VIRGE_CMD_HC            (1u << 1)   /* hardware clipping */
#define VIRGE_CMD_FMT_8         (0u << 2)
#define VIRGE_CMD_FMT_16        (1u << 2)   /* RGB1555 or RGB565: the 2D engine copies either */
#define VIRGE_CMD_FMT_24        (2u << 2)   /* packed RGB888 */
#define VIRGE_CMD_DE            (1u << 5)   /* draw enable */
#define VIRGE_CMD_MS            (1u << 6)   /* mono source */
#define VIRGE_CMD_IDS           (1u << 7)   /* source from the image transfer port (the CPU) */
#define VIRGE_CMD_MP            (1u << 8)   /* mono pattern (required for rectangle fills) */
#define VIRGE_CMD_TP            (1u << 9)   /* transparent */
#define VIRGE_CMD_ITA_BYTE      (0u << 10)  /* image rows byte, word or dword aligned */
#define VIRGE_CMD_ITA_WORD      (1u << 10)
#define VIRGE_CMD_ITA_DWORD     (2u << 10)
#define VIRGE_CMD_ROP(r)        ((uint32_t)(r) << 17)
#define VIRGE_CMD_XP            (1u << 25)  /* left to right */
#define VIRGE_CMD_YP            (1u << 26)  /* top to bottom */
#define VIRGE_CMD_BITBLT        (0u << 27)
#define VIRGE_CMD_RECT_FILL     (2u << 27)
#define VIRGE_CMD_LINE          (3u << 27)
#define VIRGE_CMD_POLY_FILL     (5u << 27)
#define VIRGE_CMD_NOP           (15u << 27)
#define VIRGE_CMD_3D            (1u << 31)

#define VIRGE_ROP_SRCCOPY   0xCC
#define VIRGE_ROP_PATCOPY   0xF0

/* Extended CRTC registers (index at 3D4h, data at 3D5h, after the unlock). */
#define VIRGE_CR_CHIP_ID_HI 0x2D
#define VIRGE_CR_CHIP_ID_LO 0x2E
#define VIRGE_CR_REVISION   0x2F
#define VIRGE_CR_LOCK1      0x38        /* 48h unlocks CR2D-CR3F */
#define VIRGE_CR_LOCK2      0x39        /* A5h unlocks CR40 and up */
#define VIRGE_CR_MMIO       0x53        /* bits 4-3: 01 new MMIO, 11 old and new */
#define VIRGE_CR_LAW_CTL    0x58        /* bit 4 linear addressing; bits 1-0 window size (3: 4 MB) */
#define VIRGE_CR_LAW_HI     0x59        /* window position bits 31-24 (follows PCI BAR0) */
#define VIRGE_CR_LAW_LO     0x5A        /* bits 23-16 */
#define VIRGE_CR_EXT_MISC   0x66        /* bit 0: enhanced functions */

#endif
