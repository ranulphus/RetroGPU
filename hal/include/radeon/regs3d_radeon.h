/* regs3d_radeon.h - the ATI Radeon's (R100 family's) 3D registers (docs/radeon.md, RD2): the registers the layer's
 * tri.c writes, each with its fields, as X.org's xf86-video-ati driver's radeon_reg.h has them (taken whole, a
 * register's block at a time; unsigned shifts). That file's notice is in regs_radeon.h. */
#ifndef RADEON_REGS3D_RADEON_H
#define RADEON_REGS3D_RADEON_H

#define RADEON_PP_BORDER_COLOR_0            0x1d40
#define RADEON_PP_BORDER_COLOR_1            0x1d44
#define RADEON_PP_BORDER_COLOR_2            0x1d48
#define RADEON_PP_CNTL                      0x1c38
#define     RADEON_STIPPLE_ENABLE        (1u << 0)
#define     RADEON_SCISSOR_ENABLE        (1u << 1)
#define     RADEON_PATTERN_ENABLE        (1u << 2)
#define     RADEON_SHADOW_ENABLE         (1u << 3)
#define     RADEON_TEX_ENABLE_MASK       (0xfu << 4)
#define     RADEON_TEX_0_ENABLE          (1u << 4)
#define     RADEON_TEX_1_ENABLE          (1u << 5)
#define     RADEON_TEX_2_ENABLE          (1u << 6)
#define     RADEON_TEX_3_ENABLE          (1u << 7)
#define     RADEON_TEX_BLEND_ENABLE_MASK (0xfu << 12)
#define     RADEON_TEX_BLEND_0_ENABLE    (1u << 12)
#define     RADEON_TEX_BLEND_1_ENABLE    (1u << 13)
#define     RADEON_TEX_BLEND_2_ENABLE    (1u << 14)
#define     RADEON_TEX_BLEND_3_ENABLE    (1u << 15)
#define     RADEON_PLANAR_YUV_ENABLE     (1u << 20)
#define     RADEON_SPECULAR_ENABLE       (1u << 21)
#define     RADEON_FOG_ENABLE            (1u << 22)
#define     RADEON_ALPHA_TEST_ENABLE     (1u << 23)
#define     RADEON_ANTI_ALIAS_NONE       (0u << 24)
#define     RADEON_ANTI_ALIAS_LINE       (1u << 24)
#define     RADEON_ANTI_ALIAS_POLY       (2u << 24)
#define     RADEON_ANTI_ALIAS_LINE_POLY  (3u << 24)
#define     RADEON_BUMP_MAP_ENABLE       (1u << 26)
#define     RADEON_BUMPED_MAP_T0         (0u << 27)
#define     RADEON_BUMPED_MAP_T1         (1u << 27)
#define     RADEON_BUMPED_MAP_T2         (2u << 27)
#define     RADEON_TEX_3D_ENABLE_0       (1u << 29)
#define     RADEON_TEX_3D_ENABLE_1       (1u << 30)
#define     RADEON_MC_ENABLE             (1u << 31)
#define RADEON_PP_FOG_COLOR                 0x1c18
#define     RADEON_FOG_COLOR_MASK        0x00ffffff
#define     RADEON_FOG_VERTEX            (0u << 24)
#define     RADEON_FOG_TABLE             (1u << 24)
#define     RADEON_FOG_USE_DEPTH         (0u << 25)
#define     RADEON_FOG_USE_DIFFUSE_ALPHA (2u << 25)
#define     RADEON_FOG_USE_SPEC_ALPHA    (3u << 25)
#define RADEON_PP_MISC                      0x1c14
#define     RADEON_REF_ALPHA_MASK        0x000000ff
#define     RADEON_ALPHA_TEST_FAIL       (0u << 8)
#define     RADEON_ALPHA_TEST_LESS       (1u << 8)
#define     RADEON_ALPHA_TEST_LEQUAL     (2u << 8)
#define     RADEON_ALPHA_TEST_EQUAL      (3u << 8)
#define     RADEON_ALPHA_TEST_GEQUAL     (4u << 8)
#define     RADEON_ALPHA_TEST_GREATER    (5u << 8)
#define     RADEON_ALPHA_TEST_NEQUAL     (6u << 8)
#define     RADEON_ALPHA_TEST_PASS       (7u << 8)
#define     RADEON_ALPHA_TEST_OP_MASK    (7u << 8)
#define     RADEON_CHROMA_FUNC_FAIL      (0u << 16)
#define     RADEON_CHROMA_FUNC_PASS      (1u << 16)
#define     RADEON_CHROMA_FUNC_NEQUAL    (2u << 16)
#define     RADEON_CHROMA_FUNC_EQUAL     (3u << 16)
#define     RADEON_CHROMA_KEY_NEAREST    (0u << 18)
#define     RADEON_CHROMA_KEY_ZERO       (1u << 18)
#define     RADEON_SHADOW_ID_AUTO_INC    (1u << 20)
#define     RADEON_SHADOW_FUNC_EQUAL     (0u << 21)
#define     RADEON_SHADOW_FUNC_NEQUAL    (1u << 21)
#define     RADEON_SHADOW_PASS_1         (0u << 22)
#define     RADEON_SHADOW_PASS_2         (1u << 22)
#define     RADEON_RIGHT_HAND_CUBE_D3D   (0u << 24)
#define     RADEON_RIGHT_HAND_CUBE_OGL   (1u << 24)
#define RADEON_PP_TXFILTER_0                0x1c54
#define RADEON_PP_TXFILTER_1                0x1c6c
#define RADEON_PP_TXFILTER_2                0x1c84
#define     RADEON_MAG_FILTER_NEAREST                   (0u << 0)
#define     RADEON_MAG_FILTER_LINEAR                    (1u << 0)
#define     RADEON_MAG_FILTER_MASK                      (1u << 0)
#define     RADEON_MIN_FILTER_NEAREST                   (0u << 1)
#define     RADEON_MIN_FILTER_LINEAR                    (1u << 1)
#define     RADEON_MIN_FILTER_NEAREST_MIP_NEAREST       (2u << 1)
#define     RADEON_MIN_FILTER_NEAREST_MIP_LINEAR        (3u << 1)
#define     RADEON_MIN_FILTER_LINEAR_MIP_NEAREST        (6u << 1)
#define     RADEON_MIN_FILTER_LINEAR_MIP_LINEAR         (7u << 1)
#define     RADEON_MIN_FILTER_ANISO_NEAREST             (8u << 1)
#define     RADEON_MIN_FILTER_ANISO_LINEAR              (9u << 1)
#define     RADEON_MIN_FILTER_ANISO_NEAREST_MIP_NEAREST (10u << 1)
#define     RADEON_MIN_FILTER_ANISO_NEAREST_MIP_LINEAR  (11u << 1)
#define     RADEON_MIN_FILTER_MASK                      (15u << 1)
#define     RADEON_MAX_ANISO_1_TO_1                     (0u << 5)
#define     RADEON_MAX_ANISO_2_TO_1                     (1u << 5)
#define     RADEON_MAX_ANISO_4_TO_1                     (2u << 5)
#define     RADEON_MAX_ANISO_8_TO_1                     (3u << 5)
#define     RADEON_MAX_ANISO_16_TO_1                    (4u << 5)
#define     RADEON_MAX_ANISO_MASK                       (7u << 5)
#define     RADEON_LOD_BIAS_MASK                        (0xffu << 8)
#define     RADEON_LOD_BIAS_SHIFT                       8
#define     RADEON_MAX_MIP_LEVEL_MASK                   (0x0fu << 16)
#define     RADEON_MAX_MIP_LEVEL_SHIFT                  16
#define     RADEON_YUV_TO_RGB                           (1u << 20)
#define     RADEON_YUV_TEMPERATURE_COOL                 (0u << 21)
#define     RADEON_YUV_TEMPERATURE_HOT                  (1u << 21)
#define     RADEON_YUV_TEMPERATURE_MASK                 (1u << 21)
#define     RADEON_WRAPEN_S                             (1u << 22)
#define     RADEON_CLAMP_S_WRAP                         (0u << 23)
#define     RADEON_CLAMP_S_MIRROR                       (1u << 23)
#define     RADEON_CLAMP_S_CLAMP_LAST                   (2u << 23)
#define     RADEON_CLAMP_S_MIRROR_CLAMP_LAST            (3u << 23)
#define     RADEON_CLAMP_S_CLAMP_BORDER                 (4u << 23)
#define     RADEON_CLAMP_S_MIRROR_CLAMP_BORDER          (5u << 23)
#define     RADEON_CLAMP_S_CLAMP_GL                     (6u << 23)
#define     RADEON_CLAMP_S_MIRROR_CLAMP_GL              (7u << 23)
#define     RADEON_CLAMP_S_MASK                         (7u << 23)
#define     RADEON_WRAPEN_T                             (1u << 26)
#define     RADEON_CLAMP_T_WRAP                         (0u << 27)
#define     RADEON_CLAMP_T_MIRROR                       (1u << 27)
#define     RADEON_CLAMP_T_CLAMP_LAST                   (2u << 27)
#define     RADEON_CLAMP_T_MIRROR_CLAMP_LAST            (3u << 27)
#define     RADEON_CLAMP_T_CLAMP_BORDER                 (4u << 27)
#define     RADEON_CLAMP_T_MIRROR_CLAMP_BORDER          (5u << 27)
#define     RADEON_CLAMP_T_CLAMP_GL                     (6u << 27)
#define     RADEON_CLAMP_T_MIRROR_CLAMP_GL              (7u << 27)
#define     RADEON_CLAMP_T_MASK                         (7u << 27)
#define     RADEON_BORDER_MODE_OGL                      (0u << 31)
#define     RADEON_BORDER_MODE_D3D                      (1u << 31)
#define RADEON_PP_TXFORMAT_0                0x1c58
#define RADEON_PP_TXFORMAT_1                0x1c70
#define RADEON_PP_TXFORMAT_2                0x1c88
#define     RADEON_TXFORMAT_I8                 (0u << 0)
#define     RADEON_TXFORMAT_AI88               (1u << 0)
#define     RADEON_TXFORMAT_RGB332             (2u << 0)
#define     RADEON_TXFORMAT_ARGB1555           (3u << 0)
#define     RADEON_TXFORMAT_RGB565             (4u << 0)
#define     RADEON_TXFORMAT_ARGB4444           (5u << 0)
#define     RADEON_TXFORMAT_ARGB8888           (6u << 0)
#define     RADEON_TXFORMAT_RGBA8888           (7u << 0)
#define     RADEON_TXFORMAT_Y8                 (8u << 0)
#define     RADEON_TXFORMAT_VYUY422            (10u << 0)
#define     RADEON_TXFORMAT_YVYU422            (11u << 0)
#define     RADEON_TXFORMAT_DXT1               (12u << 0)
#define     RADEON_TXFORMAT_DXT23              (14u << 0)
#define     RADEON_TXFORMAT_DXT45              (15u << 0)
#define     RADEON_TXFORMAT_FORMAT_MASK        (31u << 0)
#define     RADEON_TXFORMAT_FORMAT_SHIFT       0
#define     RADEON_TXFORMAT_APPLE_YUV_MODE     (1u << 5)
#define     RADEON_TXFORMAT_ALPHA_IN_MAP       (1u << 6)
#define     RADEON_TXFORMAT_NON_POWER2         (1u << 7)
#define     RADEON_TXFORMAT_WIDTH_MASK         (15u << 8)
#define     RADEON_TXFORMAT_WIDTH_SHIFT        8
#define     RADEON_TXFORMAT_HEIGHT_MASK        (15u << 12)
#define     RADEON_TXFORMAT_HEIGHT_SHIFT       12
#define     RADEON_TXFORMAT_F5_WIDTH_MASK      (15u << 16)
#define     RADEON_TXFORMAT_F5_WIDTH_SHIFT     16
#define     RADEON_TXFORMAT_F5_HEIGHT_MASK     (15u << 20)
#define     RADEON_TXFORMAT_F5_HEIGHT_SHIFT    20
#define     RADEON_TXFORMAT_ST_ROUTE_STQ0      (0u << 24)
#define     RADEON_TXFORMAT_ST_ROUTE_MASK      (3u << 24)
#define     RADEON_TXFORMAT_ST_ROUTE_STQ1      (1u << 24)
#define     RADEON_TXFORMAT_ST_ROUTE_STQ2      (2u << 24)
#define     RADEON_TXFORMAT_ENDIAN_NO_SWAP     (0u << 26)
#define     RADEON_TXFORMAT_ENDIAN_16BPP_SWAP  (1u << 26)
#define     RADEON_TXFORMAT_ENDIAN_32BPP_SWAP  (2u << 26)
#define     RADEON_TXFORMAT_ENDIAN_HALFDW_SWAP (3u << 26)
#define     RADEON_TXFORMAT_ALPHA_MASK_ENABLE  (1u << 28)
#define     RADEON_TXFORMAT_CHROMA_KEY_ENABLE  (1u << 29)
#define     RADEON_TXFORMAT_CUBIC_MAP_ENABLE   (1u << 30)
#define     RADEON_TXFORMAT_PERSPECTIVE_ENABLE (1u << 31)
#define RADEON_PP_TXOFFSET_0                0x1c5c
#define RADEON_PP_TXOFFSET_1                0x1c74
#define RADEON_PP_TXOFFSET_2                0x1c8c
#define     RADEON_TXO_ENDIAN_NO_SWAP     (0u << 0)
#define     RADEON_TXO_ENDIAN_BYTE_SWAP   (1u << 0)
#define     RADEON_TXO_ENDIAN_WORD_SWAP   (2u << 0)
#define     RADEON_TXO_ENDIAN_HALFDW_SWAP (3u << 0)
#define     RADEON_TXO_MACRO_LINEAR       (0u << 2)
#define     RADEON_TXO_MACRO_TILE         (1u << 2)
#define     RADEON_TXO_MICRO_LINEAR       (0u << 3)
#define     RADEON_TXO_MICRO_TILE_X2      (1u << 3)
#define     RADEON_TXO_MICRO_TILE_OPT     (2u << 3)
#define     RADEON_TXO_OFFSET_MASK        0xffffffe0
#define     RADEON_TXO_OFFSET_SHIFT       5
#define RADEON_PP_TXCBLEND_0                0x1c60
#define RADEON_PP_TXCBLEND_1                0x1c78
#define RADEON_PP_TXCBLEND_2                0x1c90
#define     RADEON_COLOR_ARG_A_SHIFT          0
#define     RADEON_COLOR_ARG_A_MASK           (0x1fu << 0)
#define     RADEON_COLOR_ARG_A_ZERO           (0u << 0)
#define     RADEON_COLOR_ARG_A_CURRENT_COLOR  (2u << 0)
#define     RADEON_COLOR_ARG_A_CURRENT_ALPHA  (3u << 0)
#define     RADEON_COLOR_ARG_A_DIFFUSE_COLOR  (4u << 0)
#define     RADEON_COLOR_ARG_A_DIFFUSE_ALPHA  (5u << 0)
#define     RADEON_COLOR_ARG_A_SPECULAR_COLOR (6u << 0)
#define     RADEON_COLOR_ARG_A_SPECULAR_ALPHA (7u << 0)
#define     RADEON_COLOR_ARG_A_TFACTOR_COLOR  (8u << 0)
#define     RADEON_COLOR_ARG_A_TFACTOR_ALPHA  (9u << 0)
#define     RADEON_COLOR_ARG_A_T0_COLOR       (10u << 0)
#define     RADEON_COLOR_ARG_A_T0_ALPHA       (11u << 0)
#define     RADEON_COLOR_ARG_A_T1_COLOR       (12u << 0)
#define     RADEON_COLOR_ARG_A_T1_ALPHA       (13u << 0)
#define     RADEON_COLOR_ARG_A_T2_COLOR       (14u << 0)
#define     RADEON_COLOR_ARG_A_T2_ALPHA       (15u << 0)
#define     RADEON_COLOR_ARG_A_T3_COLOR       (16u << 0)
#define     RADEON_COLOR_ARG_A_T3_ALPHA       (17u << 0)
#define     RADEON_COLOR_ARG_B_SHIFT          5
#define     RADEON_COLOR_ARG_B_MASK           (0x1fu << 5)
#define     RADEON_COLOR_ARG_B_ZERO           (0u << 5)
#define     RADEON_COLOR_ARG_B_CURRENT_COLOR  (2u << 5)
#define     RADEON_COLOR_ARG_B_CURRENT_ALPHA  (3u << 5)
#define     RADEON_COLOR_ARG_B_DIFFUSE_COLOR  (4u << 5)
#define     RADEON_COLOR_ARG_B_DIFFUSE_ALPHA  (5u << 5)
#define     RADEON_COLOR_ARG_B_SPECULAR_COLOR (6u << 5)
#define     RADEON_COLOR_ARG_B_SPECULAR_ALPHA (7u << 5)
#define     RADEON_COLOR_ARG_B_TFACTOR_COLOR  (8u << 5)
#define     RADEON_COLOR_ARG_B_TFACTOR_ALPHA  (9u << 5)
#define     RADEON_COLOR_ARG_B_T0_COLOR       (10u << 5)
#define     RADEON_COLOR_ARG_B_T0_ALPHA       (11u << 5)
#define     RADEON_COLOR_ARG_B_T1_COLOR       (12u << 5)
#define     RADEON_COLOR_ARG_B_T1_ALPHA       (13u << 5)
#define     RADEON_COLOR_ARG_B_T2_COLOR       (14u << 5)
#define     RADEON_COLOR_ARG_B_T2_ALPHA       (15u << 5)
#define     RADEON_COLOR_ARG_B_T3_COLOR       (16u << 5)
#define     RADEON_COLOR_ARG_B_T3_ALPHA       (17u << 5)
#define     RADEON_COLOR_ARG_C_SHIFT          10
#define     RADEON_COLOR_ARG_C_MASK           (0x1fu << 10)
#define     RADEON_COLOR_ARG_C_ZERO           (0u << 10)
#define     RADEON_COLOR_ARG_C_CURRENT_COLOR  (2u << 10)
#define     RADEON_COLOR_ARG_C_CURRENT_ALPHA  (3u << 10)
#define     RADEON_COLOR_ARG_C_DIFFUSE_COLOR  (4u << 10)
#define     RADEON_COLOR_ARG_C_DIFFUSE_ALPHA  (5u << 10)
#define     RADEON_COLOR_ARG_C_SPECULAR_COLOR (6u << 10)
#define     RADEON_COLOR_ARG_C_SPECULAR_ALPHA (7u << 10)
#define     RADEON_COLOR_ARG_C_TFACTOR_COLOR  (8u << 10)
#define     RADEON_COLOR_ARG_C_TFACTOR_ALPHA  (9u << 10)
#define     RADEON_COLOR_ARG_C_T0_COLOR       (10u << 10)
#define     RADEON_COLOR_ARG_C_T0_ALPHA       (11u << 10)
#define     RADEON_COLOR_ARG_C_T1_COLOR       (12u << 10)
#define     RADEON_COLOR_ARG_C_T1_ALPHA       (13u << 10)
#define     RADEON_COLOR_ARG_C_T2_COLOR       (14u << 10)
#define     RADEON_COLOR_ARG_C_T2_ALPHA       (15u << 10)
#define     RADEON_COLOR_ARG_C_T3_COLOR       (16u << 10)
#define     RADEON_COLOR_ARG_C_T3_ALPHA       (17u << 10)
#define     RADEON_COMP_ARG_A                 (1u << 15)
#define     RADEON_COMP_ARG_A_SHIFT           15
#define     RADEON_COMP_ARG_B                 (1u << 16)
#define     RADEON_COMP_ARG_B_SHIFT           16
#define     RADEON_COMP_ARG_C                 (1u << 17)
#define     RADEON_COMP_ARG_C_SHIFT           17
#define     RADEON_BLEND_CTL_MASK             (7u << 18)
#define     RADEON_BLEND_CTL_ADD              (0u << 18)
#define     RADEON_BLEND_CTL_SUBTRACT         (1u << 18)
#define     RADEON_BLEND_CTL_ADDSIGNED        (2u << 18)
#define     RADEON_BLEND_CTL_BLEND            (3u << 18)
#define     RADEON_BLEND_CTL_DOT3             (4u << 18)
#define     RADEON_SCALE_SHIFT                21
#define     RADEON_SCALE_MASK                 (3u << 21)
#define     RADEON_SCALE_1X                   (0u << 21)
#define     RADEON_SCALE_2X                   (1u << 21)
#define     RADEON_SCALE_4X                   (2u << 21)
#define     RADEON_CLAMP_TX                   (1u << 23)
#define     RADEON_T0_EQ_TCUR                 (1u << 24)
#define     RADEON_T1_EQ_TCUR                 (1u << 25)
#define     RADEON_T2_EQ_TCUR                 (1u << 26)
#define     RADEON_T3_EQ_TCUR                 (1u << 27)
#define     RADEON_COLOR_ARG_MASK             0x1f
#define     RADEON_COMP_ARG_SHIFT             15
#define RADEON_PP_TXABLEND_0                0x1c64
#define RADEON_PP_TXABLEND_1                0x1c7c
#define RADEON_PP_TXABLEND_2                0x1c94
#define     RADEON_ALPHA_ARG_A_SHIFT          0
#define     RADEON_ALPHA_ARG_A_MASK           (0xfu << 0)
#define     RADEON_ALPHA_ARG_A_ZERO           (0u << 0)
#define     RADEON_ALPHA_ARG_A_CURRENT_ALPHA  (1u << 0)
#define     RADEON_ALPHA_ARG_A_DIFFUSE_ALPHA  (2u << 0)
#define     RADEON_ALPHA_ARG_A_SPECULAR_ALPHA (3u << 0)
#define     RADEON_ALPHA_ARG_A_TFACTOR_ALPHA  (4u << 0)
#define     RADEON_ALPHA_ARG_A_T0_ALPHA       (5u << 0)
#define     RADEON_ALPHA_ARG_A_T1_ALPHA       (6u << 0)
#define     RADEON_ALPHA_ARG_A_T2_ALPHA       (7u << 0)
#define     RADEON_ALPHA_ARG_A_T3_ALPHA       (8u << 0)
#define     RADEON_ALPHA_ARG_B_SHIFT          4
#define     RADEON_ALPHA_ARG_B_MASK           (0xfu << 4)
#define     RADEON_ALPHA_ARG_B_ZERO           (0u << 4)
#define     RADEON_ALPHA_ARG_B_CURRENT_ALPHA  (1u << 4)
#define     RADEON_ALPHA_ARG_B_DIFFUSE_ALPHA  (2u << 4)
#define     RADEON_ALPHA_ARG_B_SPECULAR_ALPHA (3u << 4)
#define     RADEON_ALPHA_ARG_B_TFACTOR_ALPHA  (4u << 4)
#define     RADEON_ALPHA_ARG_B_T0_ALPHA       (5u << 4)
#define     RADEON_ALPHA_ARG_B_T1_ALPHA       (6u << 4)
#define     RADEON_ALPHA_ARG_B_T2_ALPHA       (7u << 4)
#define     RADEON_ALPHA_ARG_B_T3_ALPHA       (8u << 4)
#define     RADEON_ALPHA_ARG_C_SHIFT          8
#define     RADEON_ALPHA_ARG_C_MASK           (0xfu << 8)
#define     RADEON_ALPHA_ARG_C_ZERO           (0u << 8)
#define     RADEON_ALPHA_ARG_C_CURRENT_ALPHA  (1u << 8)
#define     RADEON_ALPHA_ARG_C_DIFFUSE_ALPHA  (2u << 8)
#define     RADEON_ALPHA_ARG_C_SPECULAR_ALPHA (3u << 8)
#define     RADEON_ALPHA_ARG_C_TFACTOR_ALPHA  (4u << 8)
#define     RADEON_ALPHA_ARG_C_T0_ALPHA       (5u << 8)
#define     RADEON_ALPHA_ARG_C_T1_ALPHA       (6u << 8)
#define     RADEON_ALPHA_ARG_C_T2_ALPHA       (7u << 8)
#define     RADEON_ALPHA_ARG_C_T3_ALPHA       (8u << 8)
#define     RADEON_DOT_ALPHA_DONT_REPLICATE   (1u << 9)
#define     RADEON_ALPHA_ARG_MASK             0xf
#define RADEON_PP_TFACTOR_0                 0x1c68
#define RADEON_PP_TFACTOR_1                 0x1c80
#define RADEON_PP_TFACTOR_2                 0x1c98
#define RADEON_RB3D_BLENDCNTL               0x1c20
#define     RADEON_COMB_FCN_MASK                    (3u << 12)
#define     RADEON_COMB_FCN_ADD_CLAMP               (0u << 12)
#define     RADEON_COMB_FCN_ADD_NOCLAMP             (1u << 12)
#define     RADEON_COMB_FCN_SUB_CLAMP               (2u << 12)
#define     RADEON_COMB_FCN_SUB_NOCLAMP             (3u << 12)
#define     RADEON_SRC_BLEND_GL_ZERO                (32u << 16)
#define     RADEON_SRC_BLEND_GL_ONE                 (33u << 16)
#define     RADEON_SRC_BLEND_GL_SRC_COLOR           (34u << 16)
#define     RADEON_SRC_BLEND_GL_ONE_MINUS_SRC_COLOR (35u << 16)
#define     RADEON_SRC_BLEND_GL_DST_COLOR           (36u << 16)
#define     RADEON_SRC_BLEND_GL_ONE_MINUS_DST_COLOR (37u << 16)
#define     RADEON_SRC_BLEND_GL_SRC_ALPHA           (38u << 16)
#define     RADEON_SRC_BLEND_GL_ONE_MINUS_SRC_ALPHA (39u << 16)
#define     RADEON_SRC_BLEND_GL_DST_ALPHA           (40u << 16)
#define     RADEON_SRC_BLEND_GL_ONE_MINUS_DST_ALPHA (41u << 16)
#define     RADEON_SRC_BLEND_GL_SRC_ALPHA_SATURATE  (42u << 16)
#define     RADEON_SRC_BLEND_MASK                   (63u << 16)
#define     RADEON_DST_BLEND_GL_ZERO                (32u << 24)
#define     RADEON_DST_BLEND_GL_ONE                 (33u << 24)
#define     RADEON_DST_BLEND_GL_SRC_COLOR           (34u << 24)
#define     RADEON_DST_BLEND_GL_ONE_MINUS_SRC_COLOR (35u << 24)
#define     RADEON_DST_BLEND_GL_DST_COLOR           (36u << 24)
#define     RADEON_DST_BLEND_GL_ONE_MINUS_DST_COLOR (37u << 24)
#define     RADEON_DST_BLEND_GL_SRC_ALPHA           (38u << 24)
#define     RADEON_DST_BLEND_GL_ONE_MINUS_SRC_ALPHA (39u << 24)
#define     RADEON_DST_BLEND_GL_DST_ALPHA           (40u << 24)
#define     RADEON_DST_BLEND_GL_ONE_MINUS_DST_ALPHA (41u << 24)
#define     RADEON_DST_BLEND_MASK                   (63u << 24)
#define RADEON_RB3D_CNTL                    0x1c3c
#define     RADEON_ALPHA_BLEND_ENABLE       (1u << 0)
#define     RADEON_PLANE_MASK_ENABLE        (1u << 1)
#define     RADEON_DITHER_ENABLE            (1u << 2)
#define     RADEON_ROUND_ENABLE             (1u << 3)
#define     RADEON_SCALE_DITHER_ENABLE      (1u << 4)
#define     RADEON_DITHER_INIT              (1u << 5)
#define     RADEON_ROP_ENABLE               (1u << 6)
#define     RADEON_STENCIL_ENABLE           (1u << 7)
#define     RADEON_Z_ENABLE                 (1u << 8)
#define     RADEON_DEPTH_XZ_OFFEST_ENABLE   (1u << 9)
#define     RADEON_COLOR_FORMAT_ARGB1555    (3u << 10)
#define     RADEON_COLOR_FORMAT_RGB565      (4u << 10)
#define     RADEON_COLOR_FORMAT_ARGB8888    (6u << 10)
#define     RADEON_COLOR_FORMAT_RGB332      (7u << 10)
#define     RADEON_COLOR_FORMAT_Y8          (8u << 10)
#define     RADEON_COLOR_FORMAT_RGB8        (9u << 10)
#define     RADEON_COLOR_FORMAT_YUV422_VYUY (11u << 10)
#define     RADEON_COLOR_FORMAT_YUV422_YVYU (12u << 10)
#define     RADEON_COLOR_FORMAT_aYUV444     (14u << 10)
#define     RADEON_COLOR_FORMAT_ARGB4444    (15u << 10)
#define     RADEON_CLRCMP_FLIP_ENABLE       (1u << 14)
#define RADEON_RB3D_COLOROFFSET             0x1c40
#define     RADEON_COLOROFFSET_MASK      0xfffffff0
#define RADEON_RB3D_COLORPITCH              0x1c48
#define     RADEON_COLORPITCH_MASK         0x000001ff8
#define     RADEON_COLOR_TILE_ENABLE       (1u << 16)
#define     RADEON_COLOR_MICROTILE_ENABLE  (1u << 17)
#define     RADEON_COLOR_ENDIAN_NO_SWAP    (0u << 18)
#define     RADEON_COLOR_ENDIAN_WORD_SWAP  (1u << 18)
#define     RADEON_COLOR_ENDIAN_DWORD_SWAP (2u << 18)
#define RADEON_RB3D_DEPTHOFFSET             0x1c24
#define RADEON_RB3D_DEPTHPITCH              0x1c28
#define     RADEON_DEPTHPITCH_MASK         0x00001ff8
#define     RADEON_DEPTH_ENDIAN_NO_SWAP    (0u << 18)
#define     RADEON_DEPTH_ENDIAN_WORD_SWAP  (1u << 18)
#define     RADEON_DEPTH_ENDIAN_DWORD_SWAP (2u << 18)
#define RADEON_RB3D_PLANEMASK               0x1d84
#define RADEON_RB3D_ROPCNTL                 0x1d80
#define     RADEON_ROP_MASK              (15u << 8)
#define     RADEON_ROP_CLEAR             (0u << 8)
#define     RADEON_ROP_NOR               (1u << 8)
#define     RADEON_ROP_AND_INVERTED      (2u << 8)
#define     RADEON_ROP_COPY_INVERTED     (3u << 8)
#define     RADEON_ROP_AND_REVERSE       (4u << 8)
#define     RADEON_ROP_INVERT            (5u << 8)
#define     RADEON_ROP_XOR               (6u << 8)
#define     RADEON_ROP_NAND              (7u << 8)
#define     RADEON_ROP_AND               (8u << 8)
#define     RADEON_ROP_EQUIV             (9u << 8)
#define     RADEON_ROP_NOOP              (10u << 8)
#define     RADEON_ROP_OR_INVERTED       (11u << 8)
#define     RADEON_ROP_COPY              (12u << 8)
#define     RADEON_ROP_OR_REVERSE        (13u << 8)
#define     RADEON_ROP_OR                (14u << 8)
#define     RADEON_ROP_SET               (15u << 8)
#define RADEON_RB3D_STENCILREFMASK          0x1d7c
#define     RADEON_STENCIL_REF_SHIFT       0
#define     RADEON_STENCIL_REF_MASK        (0xffu << 0)
#define     RADEON_STENCIL_MASK_SHIFT      16
#define     RADEON_STENCIL_VALUE_MASK      (0xffu << 16)
#define     RADEON_STENCIL_WRITEMASK_SHIFT 24
#define     RADEON_STENCIL_WRITE_MASK      (0xffu << 24)
#define RADEON_RB3D_ZSTENCILCNTL            0x1c2c
#define     RADEON_DEPTH_FORMAT_MASK          (0xfu << 0)
#define     RADEON_DEPTH_FORMAT_16BIT_INT_Z   (0u << 0)
#define     RADEON_DEPTH_FORMAT_24BIT_INT_Z   (2u << 0)
#define     RADEON_DEPTH_FORMAT_24BIT_FLOAT_Z (3u << 0)
#define     RADEON_DEPTH_FORMAT_32BIT_INT_Z   (4u << 0)
#define     RADEON_DEPTH_FORMAT_32BIT_FLOAT_Z (5u << 0)
#define     RADEON_DEPTH_FORMAT_16BIT_FLOAT_W (7u << 0)
#define     RADEON_DEPTH_FORMAT_24BIT_FLOAT_W (9u << 0)
#define     RADEON_DEPTH_FORMAT_32BIT_FLOAT_W (11u << 0)
#define     RADEON_Z_TEST_NEVER               (0u << 4)
#define     RADEON_Z_TEST_LESS                (1u << 4)
#define     RADEON_Z_TEST_LEQUAL              (2u << 4)
#define     RADEON_Z_TEST_EQUAL               (3u << 4)
#define     RADEON_Z_TEST_GEQUAL              (4u << 4)
#define     RADEON_Z_TEST_GREATER             (5u << 4)
#define     RADEON_Z_TEST_NEQUAL              (6u << 4)
#define     RADEON_Z_TEST_ALWAYS              (7u << 4)
#define     RADEON_Z_TEST_MASK                (7u << 4)
#define     RADEON_STENCIL_TEST_NEVER         (0u << 12)
#define     RADEON_STENCIL_TEST_LESS          (1u << 12)
#define     RADEON_STENCIL_TEST_LEQUAL        (2u << 12)
#define     RADEON_STENCIL_TEST_EQUAL         (3u << 12)
#define     RADEON_STENCIL_TEST_GEQUAL        (4u << 12)
#define     RADEON_STENCIL_TEST_GREATER       (5u << 12)
#define     RADEON_STENCIL_TEST_NEQUAL        (6u << 12)
#define     RADEON_STENCIL_TEST_ALWAYS        (7u << 12)
#define     RADEON_STENCIL_TEST_MASK          (0x7u << 12)
#define     RADEON_STENCIL_FAIL_KEEP          (0u << 16)
#define     RADEON_STENCIL_FAIL_ZERO          (1u << 16)
#define     RADEON_STENCIL_FAIL_REPLACE       (2u << 16)
#define     RADEON_STENCIL_FAIL_INC           (3u << 16)
#define     RADEON_STENCIL_FAIL_DEC           (4u << 16)
#define     RADEON_STENCIL_FAIL_INVERT        (5u << 16)
#define     RADEON_STENCIL_FAIL_MASK          (0x7u << 16)
#define     RADEON_STENCIL_ZPASS_KEEP         (0u << 20)
#define     RADEON_STENCIL_ZPASS_ZERO         (1u << 20)
#define     RADEON_STENCIL_ZPASS_REPLACE      (2u << 20)
#define     RADEON_STENCIL_ZPASS_INC          (3u << 20)
#define     RADEON_STENCIL_ZPASS_DEC          (4u << 20)
#define     RADEON_STENCIL_ZPASS_INVERT       (5u << 20)
#define     RADEON_STENCIL_ZPASS_MASK         (0x7u << 20)
#define     RADEON_STENCIL_ZFAIL_KEEP         (0u << 24)
#define     RADEON_STENCIL_ZFAIL_ZERO         (1u << 24)
#define     RADEON_STENCIL_ZFAIL_REPLACE      (2u << 24)
#define     RADEON_STENCIL_ZFAIL_INC          (3u << 24)
#define     RADEON_STENCIL_ZFAIL_DEC          (4u << 24)
#define     RADEON_STENCIL_ZFAIL_INVERT       (5u << 24)
#define     RADEON_STENCIL_ZFAIL_MASK         (0x7u << 24)
#define     RADEON_Z_COMPRESSION_ENABLE       (1u << 28)
#define     RADEON_FORCE_Z_DIRTY              (1u << 29)
#define     RADEON_Z_WRITE_ENABLE             (1u << 30)
#define RADEON_RE_MISC                      0x26c4
#define     RADEON_STIPPLE_COORD_MASK       0x1f
#define     RADEON_STIPPLE_X_OFFSET_SHIFT   0
#define     RADEON_STIPPLE_X_OFFSET_MASK    (0x1fu << 0)
#define     RADEON_STIPPLE_Y_OFFSET_SHIFT   8
#define     RADEON_STIPPLE_Y_OFFSET_MASK    (0x1fu << 8)
#define     RADEON_STIPPLE_LITTLE_BIT_ORDER (0u << 16)
#define     RADEON_STIPPLE_BIG_BIT_ORDER    (1u << 16)
#define RADEON_RE_SOLID_COLOR               0x1c1c
#define RADEON_RE_TOP_LEFT                  0x26c0
#define     RADEON_RE_LEFT_SHIFT         0
#define     RADEON_RE_TOP_SHIFT          16
#define RADEON_RE_WIDTH_HEIGHT              0x1c44
#define     RADEON_RE_WIDTH_SHIFT        0
#define     RADEON_RE_HEIGHT_SHIFT       16
#define RADEON_SE_CNTL                      0x1c4c
#define     RADEON_FFACE_CULL_CW          (0u << 0)
#define     RADEON_FFACE_CULL_CCW         (1u << 0)
#define     RADEON_FFACE_CULL_DIR_MASK    (1u << 0)
#define     RADEON_BFACE_CULL             (0u << 1)
#define     RADEON_BFACE_SOLID            (3u << 1)
#define     RADEON_FFACE_CULL             (0u << 3)
#define     RADEON_FFACE_SOLID            (3u << 3)
#define     RADEON_FFACE_CULL_MASK        (3u << 3)
#define     RADEON_BADVTX_CULL_DISABLE    (1u << 5)
#define     RADEON_FLAT_SHADE_VTX_0       (0u << 6)
#define     RADEON_FLAT_SHADE_VTX_1       (1u << 6)
#define     RADEON_FLAT_SHADE_VTX_2       (2u << 6)
#define     RADEON_FLAT_SHADE_VTX_LAST    (3u << 6)
#define     RADEON_DIFFUSE_SHADE_SOLID    (0u << 8)
#define     RADEON_DIFFUSE_SHADE_FLAT     (1u << 8)
#define     RADEON_DIFFUSE_SHADE_GOURAUD  (2u << 8)
#define     RADEON_DIFFUSE_SHADE_MASK     (3u << 8)
#define     RADEON_ALPHA_SHADE_SOLID      (0u << 10)
#define     RADEON_ALPHA_SHADE_FLAT       (1u << 10)
#define     RADEON_ALPHA_SHADE_GOURAUD    (2u << 10)
#define     RADEON_ALPHA_SHADE_MASK       (3u << 10)
#define     RADEON_SPECULAR_SHADE_SOLID   (0u << 12)
#define     RADEON_SPECULAR_SHADE_FLAT    (1u << 12)
#define     RADEON_SPECULAR_SHADE_GOURAUD (2u << 12)
#define     RADEON_SPECULAR_SHADE_MASK    (3u << 12)
#define     RADEON_FOG_SHADE_SOLID        (0u << 14)
#define     RADEON_FOG_SHADE_FLAT         (1u << 14)
#define     RADEON_FOG_SHADE_GOURAUD      (2u << 14)
#define     RADEON_FOG_SHADE_MASK         (3u << 14)
#define     RADEON_ZBIAS_ENABLE_POINT     (1u << 16)
#define     RADEON_ZBIAS_ENABLE_LINE      (1u << 17)
#define     RADEON_ZBIAS_ENABLE_TRI       (1u << 18)
#define     RADEON_WIDELINE_ENABLE        (1u << 20)
#define     RADEON_VPORT_XY_XFORM_ENABLE  (1u << 24)
#define     RADEON_VPORT_Z_XFORM_ENABLE   (1u << 25)
#define     RADEON_VTX_PIX_CENTER_D3D     (0u << 27)
#define     RADEON_VTX_PIX_CENTER_OGL     (1u << 27)
#define     RADEON_ROUND_MODE_TRUNC       (0u << 28)
#define     RADEON_ROUND_MODE_ROUND       (1u << 28)
#define     RADEON_ROUND_MODE_ROUND_EVEN  (2u << 28)
#define     RADEON_ROUND_MODE_ROUND_ODD   (3u << 28)
#define     RADEON_ROUND_PREC_16TH_PIX    (0u << 30)
#define     RADEON_ROUND_PREC_8TH_PIX     (1u << 30)
#define     RADEON_ROUND_PREC_4TH_PIX     (2u << 30)
#define     RADEON_ROUND_PREC_HALF_PIX    (3u << 30)
#define RADEON_SE_CNTL_STATUS               0x2140
#define     RADEON_VC_NO_SWAP            (0u << 0)
#define     RADEON_VC_16BIT_SWAP         (1u << 0)
#define     RADEON_VC_32BIT_SWAP         (2u << 0)
#define     RADEON_VC_HALF_DWORD_SWAP    (3u << 0)
#define     RADEON_TCL_BYPASS            (1u << 8)
#define RADEON_SE_COORD_FMT                 0x1c50
#define     RADEON_VTX_XY_PRE_MULT_1_OVER_W0  (1u << 0)
#define     RADEON_VTX_Z_PRE_MULT_1_OVER_W0   (1u << 1)
#define     RADEON_VTX_ST0_NONPARAMETRIC      (1u << 8)
#define     RADEON_VTX_ST1_NONPARAMETRIC      (1u << 9)
#define     RADEON_VTX_ST2_NONPARAMETRIC      (1u << 10)
#define     RADEON_VTX_ST3_NONPARAMETRIC      (1u << 11)
#define     RADEON_VTX_W0_NORMALIZE           (1u << 12)
#define     RADEON_VTX_W0_IS_NOT_1_OVER_W0    (1u << 16)
#define     RADEON_VTX_ST0_PRE_MULT_1_OVER_W0 (1u << 17)
#define     RADEON_VTX_ST1_PRE_MULT_1_OVER_W0 (1u << 19)
#define     RADEON_VTX_ST2_PRE_MULT_1_OVER_W0 (1u << 21)
#define     RADEON_VTX_ST3_PRE_MULT_1_OVER_W0 (1u << 23)
#define     RADEON_TEX1_W_ROUTING_USE_W0      (0u << 26)
#define     RADEON_TEX1_W_ROUTING_USE_Q1      (1u << 26)
#define RADEON_SE_VTX_FMT                   0x2080
#define     RADEON_SE_VTX_FMT_XY         0x00000000
#define     RADEON_SE_VTX_FMT_W0         0x00000001
#define     RADEON_SE_VTX_FMT_FPCOLOR    0x00000002
#define     RADEON_SE_VTX_FMT_FPALPHA    0x00000004
#define     RADEON_SE_VTX_FMT_PKCOLOR    0x00000008
#define     RADEON_SE_VTX_FMT_FPSPEC     0x00000010
#define     RADEON_SE_VTX_FMT_FPFOG      0x00000020
#define     RADEON_SE_VTX_FMT_PKSPEC     0x00000040
#define     RADEON_SE_VTX_FMT_ST0        0x00000080
#define     RADEON_SE_VTX_FMT_ST1        0x00000100
#define     RADEON_SE_VTX_FMT_Q1         0x00000200
#define     RADEON_SE_VTX_FMT_ST2        0x00000400
#define     RADEON_SE_VTX_FMT_Q2         0x00000800
#define     RADEON_SE_VTX_FMT_ST3        0x00001000
#define     RADEON_SE_VTX_FMT_Q3         0x00002000
#define     RADEON_SE_VTX_FMT_Q0         0x00004000
#define     RADEON_SE_VTX_FMT_BLND_WEIGHT_CNT_MASK  0x00038000
#define     RADEON_SE_VTX_FMT_N0         0x00040000
#define     RADEON_SE_VTX_FMT_XY1        0x08000000
#define     RADEON_SE_VTX_FMT_Z1         0x10000000
#define     RADEON_SE_VTX_FMT_W1         0x20000000
#define     RADEON_SE_VTX_FMT_N1         0x40000000
#define     RADEON_SE_VTX_FMT_Z          0x80000000
#define RADEON_SE_VF_CNTL                             0x2084
#define     RADEON_VF_PRIM_TYPE_POINT_LIST         1
#define     RADEON_VF_PRIM_TYPE_LINE_LIST          2
#define     RADEON_VF_PRIM_TYPE_LINE_STRIP         3
#define     RADEON_VF_PRIM_TYPE_TRIANGLE_LIST      4
#define     RADEON_VF_PRIM_TYPE_TRIANGLE_FAN       5
#define     RADEON_VF_PRIM_TYPE_TRIANGLE_STRIP     6
#define     RADEON_VF_PRIM_TYPE_TRIANGLE_FLAG      7
#define     RADEON_VF_PRIM_TYPE_RECTANGLE_LIST     8
#define     RADEON_VF_PRIM_TYPE_POINT_LIST_3       9
#define     RADEON_VF_PRIM_TYPE_LINE_LIST_3        10
#define     RADEON_VF_PRIM_TYPE_SPIRIT_LIST        11
#define     RADEON_VF_PRIM_TYPE_LINE_LOOP          12
#define     RADEON_VF_PRIM_TYPE_QUAD_LIST          13
#define     RADEON_VF_PRIM_TYPE_QUAD_STRIP         14
#define     RADEON_VF_PRIM_TYPE_POLYGON            15
#define     RADEON_VF_PRIM_WALK_STATE              (0u << 4)
#define     RADEON_VF_PRIM_WALK_INDEX              (1u << 4)
#define     RADEON_VF_PRIM_WALK_LIST               (2u << 4)
#define     RADEON_VF_PRIM_WALK_DATA               (3u << 4)
#define     RADEON_VF_COLOR_ORDER_RGBA             (1u << 6)
#define     RADEON_VF_RADEON_MODE                  (1u << 8)
#define     RADEON_VF_TCL_OUTPUT_CTL_ENA           (1u << 9)
#define     RADEON_VF_PROG_STREAM_ENA              (1u << 10)
#define     RADEON_VF_INDEX_SIZE_SHIFT             11
#define     RADEON_VF_NUM_VERTICES_SHIFT           16
#define RADEON_SE_PORT_DATA0            0x2000

#define RADEON_AUX_SC_CNTL                  0x1660
#define     RADEON_ZBLOCK16                 (1u << 15)      /* (RB3D_CNTL: the DRM's radeon_drv.h) */

#endif
