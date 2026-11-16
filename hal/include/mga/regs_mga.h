/* regs_mga.h - Matrox MGA drawing-engine and CRTC register map.
 *
 * Offsets and bit fields from the MGA-G100 (Feb 1998), MGA-G200 (Nov 1998)
 * and MGA-G400 (Jun 1999) specifications, cross-checked against the X.org
 * xf86-video-mga driver (MIT) and 86Box's vid_mga.c model. */
#ifndef MGA_REGS_H
#define MGA_REGS_H

/* ---- Drawing registers (MMIO aperture, BAR1) ---------------------------- */
#define MGAREG_DWGCTL       0x1C00
#define MGAREG_MACCESS      0x1C04
#define MGAREG_MCTLWTST     0x1C08
#define MGAREG_ZORG         0x1C0C
#define MGAREG_PAT0         0x1C10
#define MGAREG_PAT1         0x1C14
#define MGAREG_PLNWT        0x1C1C
#define MGAREG_MEMRDBK      0x1E44      /* G200+: memory read-back timing (the G450 set-up after a reset: pci.c) */
#define MGAREG_BCOL         0x1C20
#define MGAREG_FCOL         0x1C24
#define MGAREG_SRC0         0x1C30
#define MGAREG_XYSTRT       0x1C40
#define MGAREG_XYEND        0x1C44
#define MGAREG_SHIFT        0x1C50
#define MGAREG_DMAPAD       0x1C54
#define MGAREG_SGN          0x1C58
#define MGAREG_LEN          0x1C5C
#define MGAREG_AR0          0x1C60
#define MGAREG_AR1          0x1C64
#define MGAREG_AR2          0x1C68
#define MGAREG_AR3          0x1C6C
#define MGAREG_AR4          0x1C70
#define MGAREG_AR5          0x1C74
#define MGAREG_AR6          0x1C78
#define MGAREG_CXBNDRY      0x1C80
#define MGAREG_FXBNDRY      0x1C84
#define MGAREG_YDSTLEN      0x1C88
#define MGAREG_PITCH        0x1C8C
#define MGAREG_YDST         0x1C90
#define MGAREG_YDSTORG      0x1C94
#define MGAREG_YTOP         0x1C98
#define MGAREG_YBOT         0x1C9C
#define MGAREG_CXLEFT       0x1CA0
#define MGAREG_CXRIGHT      0x1CA4
#define MGAREG_FXLEFT       0x1CA8
#define MGAREG_FXRIGHT      0x1CAC
#define MGAREG_XDST         0x1CB0
#define MGAREG_DR0          0x1CC0
#define MGAREG_FOGSTART     0x1CC4
#define MGAREG_DR2          0x1CC8
#define MGAREG_DR3          0x1CCC
#define MGAREG_DR4          0x1CD0
#define MGAREG_FOGXINC      0x1CD4
#define MGAREG_DR6          0x1CD8
#define MGAREG_DR7          0x1CDC
#define MGAREG_DR8          0x1CE0
#define MGAREG_FOGYINC      0x1CE4
#define MGAREG_DR10         0x1CE8
#define MGAREG_DR11         0x1CEC
#define MGAREG_DR12         0x1CF0
#define MGAREG_FOGCOL       0x1CF4
#define MGAREG_DR14         0x1CF8
#define MGAREG_DR15         0x1CFC
#define MGAREG_EXEC         0x0100      /* add to a drawing register to start the engine */
#define MGAREG_DMAWIN       0x0000      /* 0000h-1BFFh: ILOAD data (pseudo-DMA window) */
#define MGA_DMAWIN_SIZE     0x1C00

#define MGAREG_FIFOSTATUS   0x1E10
#define MGAREG_STATUS       0x1E14
#define MGAREG_ICLEAR       0x1E18
#define MGAREG_IEN          0x1E1C
#define MGAREG_VCOUNT       0x1E20
#define MGAREG_RST          0x1E40
#define MGAREG_OPMODE       0x1E54
#define OPMODE_DMAMOD_BLIT  (1u << 2)   /* DMA window writes are ILOAD data */
#define OPMODE_DMAMOD_VERTEX (3u << 2)  /* DMA window writes are vertices for the WARP (G200/G400) */
#define OPMODE_DMAMOD_MASK  (3u << 2)
#define MGAREG_PRIMADDRESS  0x1E58
#define MGAREG_PRIMEND      0x1E5C

#define MGAREG_TMR0         0x2C00      /* TMR0..TMR8 at +4 each */
#define MGAREG_TMR(n)       (0x2C00 + 4 * (n))
#define MGAREG_TEXORG       0x2C24
#define MGAREG_TEXWIDTH     0x2C28
#define MGAREG_TEXHEIGHT    0x2C2C
#define MGAREG_TEXCTL       0x2C30
#define MGAREG_TEXTRANS     0x2C34
#define MGAREG_TEXTRANSHIGH 0x2C38      /* G200+ */
#define MGAREG_TEXCTL2      0x2C3C      /* G200+ */
#define MGAREG_SECADDRESS   0x2C40
#define MGAREG_SECEND       0x2C44
#define MGAREG_SOFTRAP      0x2C48
#define MGAREG_DWGSYNC      0x2C4C      /* G200+ */
#define MGAREG_DR0_Z32LSB   0x2C50
#define MGAREG_DR0_Z32MSB   0x2C54
#define MGAREG_TEXFILTER    0x2C58
#define MGAREG_DR2_Z32LSB   0x2C60
#define MGAREG_DR2_Z32MSB   0x2C64
#define MGAREG_DR3_Z32LSB   0x2C68
#define MGAREG_DR3_Z32MSB   0x2C6C
#define MGAREG_ALPHASTART   0x2C70
#define MGAREG_ALPHAXINC    0x2C74
#define MGAREG_ALPHAYINC    0x2C78
#define MGAREG_ALPHACTRL    0x2C7C
#define MGAREG_SPECRSTART   0x2C80      /* G200+ specular start/xinc/yinc R,G,B */
#define MGAREG_SPECRXINC    0x2C84
#define MGAREG_SPECRYINC    0x2C88
#define MGAREG_SPECGSTART   0x2C8C
#define MGAREG_SPECGXINC    0x2C90
#define MGAREG_SPECGYINC    0x2C94
#define MGAREG_SPECBSTART   0x2C98
#define MGAREG_SPECBXINC    0x2C9C
#define MGAREG_SPECBYINC    0x2CA0
#define MGAREG_TEXORG1      0x2CA4      /* G200+ mip origins */
#define MGAREG_TEXORG2      0x2CA8
#define MGAREG_TEXORG3      0x2CAC
#define MGAREG_TEXORG4      0x2CB0
#define MGAREG_SRCORG       0x2CB4
#define MGAREG_DSTORG       0x2CB8

/* VGA/CRTC through MMIO. */
#define MGAREG_SEQ_INDEX    0x1FC4
#define MGAREG_SEQ_DATA     0x1FC5
#define MGAREG_CRTC_INDEX   0x1FD4
#define MGAREG_CRTC_DATA    0x1FD5
#define MGAREG_INSTS1       0x1FDA
#define MGAREG_CRTCEXT_INDEX 0x1FDE
#define MGAREG_CRTCEXT_DATA 0x1FDF
/* RAMDAC. */
#define MGAREG_PALWTADD     0x3C00      /* also the index of the indirect registers (X_DATAREG) */
#define MGAREG_PALDATA      0x3C01
#define MGAREG_PIXRDMSK     0x3C02
#define MGAREG_PALRDADD     0x3C03
#define MGAREG_X_DATAREG    0x3C0A
#define MGAREG_CURPOSXL     0x3C0C      /* XL, XH, YL, YH: the cursor's bottom right corner + 1 */
/* Indirect DAC registers. */
#define XREG_XCURADDL       0x04        /* the cursor image's address, bits 17:10 */
#define XREG_XCURADDH       0x05        /* bits 22:18 (G100), 23:18 (G200, G400) */
#define XREG_XCURCTRL       0x06
#define XCURCTRL_OFF        0x00
#define XCURCTRL_XGA        0x02        /* planes 1, 0: 00 colour 0, 01 colour 1, 10 transparent, 11 inverted */
#define XREG_XCURCOL0R      0x08        /* G, B follow; colour 1 at 0Ch, colour 2 at 10h */
#define XREG_XCURCOL1R      0x0C
/* CRTC: the display start. */
#define CRTC_STARTADD_HI    0x0C        /* startadd<15:8> */
#define CRTC_STARTADD_LO    0x0D        /* startadd<7:0> */
#define CRTCEXT0            0x00        /* <3:0> startadd<19:16>, <6> startadd<20>; <5:4>, <7> kept */

/* ---- DWGCTL ----------------------------------------------------------- */
#define DWG_OPCOD_LINE_OPEN     0x0
#define DWG_OPCOD_TRAP          0x4
#define DWG_OPCOD_TEXTURE_TRAP  0x6
#define DWG_OPCOD_BITBLT        0x8
#define DWG_OPCOD_ILOAD         0x9
#define DWG_OPCOD_IDUMP         0xA
#define DWG_OPCOD_FBITBLT       0xC     /* the Millennium's: Reserved on the G100, G200 and G400 */
#define DWG_ATYPE_RPL           (0u << 4)
#define DWG_ATYPE_RSTR          (1u << 4)
#define DWG_ATYPE_ZI            (3u << 4)
#define DWG_ATYPE_BLK           (4u << 4)
#define DWG_ATYPE_I             (7u << 4)
#define DWG_LINEAR              (1u << 7)
#define DWG_ZMODE_NOZCMP        (0u << 8)
#define DWG_ZMODE_ZE            (2u << 8)
#define DWG_ZMODE_ZNE           (3u << 8)
#define DWG_ZMODE_ZLT           (4u << 8)
#define DWG_ZMODE_ZLTE          (5u << 8)
#define DWG_ZMODE_ZGT           (6u << 8)
#define DWG_ZMODE_ZGTE          (7u << 8)
#define DWG_SOLID               (1u << 11)
#define DWG_ARZERO              (1u << 12)
#define DWG_SGNZERO             (1u << 13)
#define DWG_SHFTZERO            (1u << 14)
#define DWG_BOP(x)              ((uint32_t)(x) << 16)
#define DWG_BOP_COPY            DWG_BOP(0xC)
#define DWG_TRANS(x)            ((uint32_t)(x) << 20)
#define DWG_BLTMOD_BMONOLEF     (0x0u << 25)
#define DWG_BLTMOD_BFCOL        (0x2u << 25)
#define DWG_BLTMOD_BMONOWF      (0x4u << 25)    /* one-bit source, the first pixel in bit 7 of each byte */
#define DWG_BLTMOD_BU32RGB      (0x7u << 25)
#define DWG_PATTERN             (1u << 29)
#define DWG_TRANSC              (1u << 30)
#define DWG_CLIPDIS             (1u << 31)

/* ---- WARP setup engine (G200/G400; G400 specification 3-260..3-278) ------- */
#define MGAREG_WIADDR       0x1DC0      /* WARP0's instruction address and mode (FIFO); the G200's start */
#define MGAREG_WFLAG        0x1DC4
#define MGAREG_WGETMSB      0x1DC8
#define MGAREG_WVRTXSZ      0x1DCC      /* registers a vertex, minus one; primsz <13:8> */
#define MGAREG_WACCEPTSEQ   0x1DD4      /* G400 (reserved on the G200) */
#define MGAREG_WIADDR2      0x1DD8      /* G400: both WARPs */
#define MGAREG_WFLAG1       0x1DE0      /* G400: WARP1's flags */
#define MGAREG_WIADDRNB2    0x1E00      /* WIADDR2 bypassing the FIFO */
#define MGAREG_WIADDRNB1    0x1E04      /* WARP1's instruction address (read only) */
#define MGAREG_WIADDRNB     0x1E60      /* WARP0's instruction address (read back) */
#define MGAREG_WIMEMADDR    0x1E68      /* instruction memory word (0-255) for WIMEMDATA */
#define MGAREG_WCODEADDR    0x1E6C
#define MGAREG_WMISC        0x1E70
#define MGAREG_AGP_PLL      0x1E4C      /* G200-AGP: agp2xpllen <0>, needed for AGP 2x (G200 specification 3-30) */
#define MGAREG_WIMEMDATA    0x2000      /* WARP0's instruction memory: two dwords a 64-bit word */
#define MGAREG_WIMEMDATA1   0x2100      /* G400: WARP1's */
#define MGAREG_WR(n)        (0x2D00 + 4u * (n))   /* WR0-WR63 */
#define WMODE_SUSPEND       0u
#define WMODE_RESUME        1u
#define WMODE_JUMP          2u
#define WMODE_START         3u
#define WMISC_UCODECACHE    (1u << 0)   /* fetch the microcode from memory (with WMISC_MASTER) */
#define WMISC_MASTER        (1u << 1)
#define WMISC_CACHEFLUSH    (1u << 3)
#define STATUS_WBUSY        (1u << 18)  /* also while the WARP waits for vertices */
#define STATUS_WBUSY1       (1u << 19)

/* ---- MACCESS ------------------------------------------------------------- */
#define MACCESS_PW8             0x0u
#define MACCESS_PW16            0x1u
#define MACCESS_PW32            0x2u
#define MACCESS_PW24            0x3u
#define MACCESS_ZW32            (1u << 3)
#define MACCESS_MEMRESET        (1u << 15)
#define MACCESS_FOGEN           (1u << 26)
#define MACCESS_TLUTLOAD        (1u << 29)
#define MACCESS_NODITHER        (1u << 30)
#define MACCESS_DIT555          (1u << 31)

/* ---- SGN ------------------------------------------------------------------ */
#define SGN_SDYDXL              (1u << 0)
#define SGN_SCANLEFT            (1u << 0)
#define SGN_SDXL                (1u << 1)
#define SGN_SDY                 (1u << 2)
#define SGN_SDXR                (1u << 5)

/* ---- STATUS ------------------------------------------------------------- */
#define STATUS_VSYNCSTS         (1u << 3)
#define STATUS_DWGENGSTS        (1u << 16)

/* ---- TEXCTL -------------------------------------------------------------- */
#define TEXCTL_TW4              0x0u
#define TEXCTL_TW8              0x1u
#define TEXCTL_TW15             0x2u
#define TEXCTL_TW16             0x3u
#define TEXCTL_TW12             0x4u
#define TEXCTL_TW32             0x6u      /* G200+ */
#define TEXCTL_TW422            0xAu      /* G200+ */
#define TEXCTL_PALSEL(x)        ((uint32_t)(x) << 4)
#define TEXCTL_TPITCHLIN        (1u << 8)
#define TEXCTL_TPITCHEXT(x)     ((uint32_t)(x) << 9)
#define TEXCTL_NPCEN            (1u << 21)
#define TEXCTL_AZEROEXTEND      (1u << 23)
#define TEXCTL_DECALCKEY        (1u << 24)
#define TEXCTL_TAKEY            (1u << 25)
#define TEXCTL_TAMASK           (1u << 26)
#define TEXCTL_CLAMPV           (1u << 27)
#define TEXCTL_CLAMPU           (1u << 28)
#define TEXCTL_TMODULATE        (1u << 29)
#define TEXCTL_STRANS           (1u << 30)
#define TEXCTL_ITRANS           (1u << 31)

/* TEXCTL2 (G200+). */
#define TEXCTL2_DECALBLEND      (1u << 0)
#define TEXCTL2_IDECAL          (1u << 1)
#define TEXCTL2_DECALDIS        (1u << 2)
#define TEXCTL2_SPECEN          (1u << 6)
#define TEXCTL2_DUALTEX         (1u << 7)    /* G400: two texture maps and both combiner stages */
#define TEXCTL2_MAP1            (1u << 31)   /* G400: the next writes reach map 1 only (tmap0dis) */
/* G400 texture combiner (TDUALSTAGE0/1, one copy each; docs/g400-dual-texture.md). */
#define MGAREG_TEXBORDERCOL     0x2C5C
#define MGAREG_TDUALSTAGE0      0x2CF8
#define MGAREG_TDUALSTAGE1      0x2CFC

/* TEXWIDTH / TEXHEIGHT: tw[5:0], rfw[14:9], twmask[28:18]. */
#define TEXWH(log2, rf, mask)   ((uint32_t)(log2) | ((uint32_t)((rf) & 63) << 9) | ((uint32_t)(mask) << 18))

/* TEXFILTER: minfilter[3:0], magfilter[7:4] (G200+ codes). */
#define TEXFILTER_NRST          0x0u
#define TEXFILTER_BILIN         0x2u
#define TEXFILTER_CNST          0x3u
#define TEXFILTER_MM1S          0x8u      /* G200+: nearest level, point */
#define TEXFILTER_MM2S          0x9u      /* nearest level, bilinear */
#define TEXFILTER_MM4S          0xAu      /* two levels, point */
#define TEXFILTER_MM8S          0xCu      /* two levels, bilinear */
#define TEXFILTER_FILTERALPHA   (1u << 20)
#define TEXFILTER_FTHRES(x)     ((uint32_t)(x) << 21)
#define TEXFILTER_MAPNB(x)      ((uint32_t)(x) << 29)
#define TEXFILTER_MIN(x)        ((uint32_t)(x))
#define TEXFILTER_MAG(x)        ((uint32_t)(x) << 4)

/* ALPHACTRL. */
#define ALPHACTRL_SRC(x)        ((uint32_t)(x))
#define ALPHACTRL_DST(x)        ((uint32_t)(x) << 4)
#define ALPHACTRL_ASTIPPLE      (1u << 11)
#define ALPHACTRL_ATEN          (1u << 12)
#define ALPHACTRL_ATMODE(x)     ((uint32_t)(x) << 13)
#define ALPHACTRL_ATREF(x)      ((uint32_t)(x) << 16)
#define ALPHACTRL_ALPHASEL(x)   ((uint32_t)(x) << 24)
#define ALPHASEL_TEXTURE        0u
#define ALPHASEL_DIFFUSE        1u
#define ALPHASEL_MODULATED      2u
/* Blend factor codes (G200+). G100 requires the reserved field = 0x54. */
#define BLEND_ZERO              0x0u
#define BLEND_ONE               0x1u
#define BLEND_DST_COLOR         0x2u
#define BLEND_ONE_MINUS_DST_COLOR 0x3u
#define BLEND_SRC_ALPHA         0x4u
#define BLEND_ONE_MINUS_SRC_ALPHA 0x5u
#define BLEND_DST_ALPHA         0x6u
#define BLEND_ONE_MINUS_DST_ALPHA 0x7u
#define BLEND_SRC_ALPHA_SATURATE 0x8u
#define BLEND_SRC_COLOR         0x2u      /* as destination factor */
#define BLEND_ONE_MINUS_SRC_COLOR 0x3u
#define ALPHACTRL_G100_FIXED    0x54u

#endif
