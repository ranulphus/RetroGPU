/* words.h - the NV4/NV5 textured triangle's state words (classes 0x54 and 0x55), as Mesa's classic nouveau driver
 * and DOS-GL's TNT2 back end use them (docs/tnt2.md; nv04_3d.xml.h). For drawing back ends on the TNT2 layer. */
#ifndef TNT2_WORDS_H
#define TNT2_WORDS_H

/* FORMAT */
#define TNT2_F_DMA_A      0x00000001u
#define TNT2_F_ORIGIN     0x000000A0u     /* zero- and first-order-hold origins at the texel's corner (Mesa's) */
#define TNT2_F_A1R5G5B5   0x00000200u
#define TNT2_F_X1R5G5B5   0x00000300u
#define TNT2_F_A4R4G4B4   0x00000400u
#define TNT2_F_R5G6B5     0x00000500u
#define TNT2_F_A8R8G8B8   0x00000600u
#define TNT2_F_X8R8G8B8   0x00000700u
/* The 1x1 white texel (tnt2_tex_dummy_off): A8R8G8B8, one level, repeat; nearest. */
#define TNT2_DUMMY_FORMAT 0x110016A1u
#define TNT2_DUMMY_FILTER 0x11001010u

/* BLEND (0x54; 0x55's without the texture map field) */
#define TNT2_B_MSB        0x00000020u
#define TNT2_B_GOURAUD    0x00000080u
#define TNT2_B_PERSP      0x00000100u
#define TNT2_B_SPECULAR   0x00001000u
#define TNT2_B_FOG        0x00010000u
#define TNT2_B_BLEND      0x00100000u
#define TNT2_B_SRC(f)     ((uint32_t)(f) << 24)
#define TNT2_B_DST(f)     ((uint32_t)(f) << 28)
/* 0x54's texture map (BLEND bits 3-0) */
enum { TNT2_TB_DECAL = 1, TNT2_TB_MODULATE = 2, TNT2_TB_DECALALPHA = 3, TNT2_TB_MODULATEALPHA = 4, TNT2_TB_ADD = 8 };
/* Blend factors (Direct3D's D3DBLEND) */
enum { TNT2_BF_ZERO = 1, TNT2_BF_ONE, TNT2_BF_SRC_COLOR, TNT2_BF_INV_SRC_COLOR, TNT2_BF_SRC_ALPHA,
       TNT2_BF_INV_SRC_ALPHA, TNT2_BF_DST_ALPHA, TNT2_BF_INV_DST_ALPHA, TNT2_BF_DST_COLOR, TNT2_BF_INV_DST_COLOR,
       TNT2_BF_SRC_ALPHA_SAT };

/* CONTROL (0x54; 0x55's CONTROL0) */
#define TNT2_C_ALPHA_REF(r) ((uint32_t)(r) & 0xFFu)
#define TNT2_C_ALPHA_FUNC(f) ((uint32_t)(f) << 8)     /* Direct3D's D3DCMP: 1 never .. 8 always */
#define TNT2_C_ALPHA_ON   0x00001000u
#define TNT2_C_CORNER     0x00002000u
#define TNT2_C_Z_ON       0x00004000u
#define TNT2_C_Z_FUNC(f)  ((uint32_t)(f) << 16)
#define TNT2_C_CULL_NONE  0x00100000u
#define TNT2_C_DITHER     0x00400000u
#define TNT2_C_Z_WRITE    0x01000000u
#define TNT2_C_Z_FIXED    0x40000000u
#define TNT2_C_Z_FLOAT    0x80000000u     /* (never used by Mesa; for Glide's W-buffer on the P3, with or */
#define TNT2_C_Z_PERSP    0x00800000u     /*  without Z_PERSPECTIVE, further from the Voodoo than linear Z16) */
enum { TNT2_CMP_NEVER = 1, TNT2_CMP_LESS, TNT2_CMP_EQUAL, TNT2_CMP_LEQUAL, TNT2_CMP_GREATER, TNT2_CMP_NOTEQUAL,
       TNT2_CMP_GEQUAL, TNT2_CMP_ALWAYS };

#endif
