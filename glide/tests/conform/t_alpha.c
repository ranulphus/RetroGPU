/* Alpha conformance tests: t14 blending, t15 alpha test, t16 chroma key,
 * t22 gamma (display path). */
#include "ct.h"
#include <string.h>
#include <stdio.h>

static uint16_t tb[64 * 64];

static void combine_tex_decal(void)
{
    gl.grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                      GR_COMBINE_OTHER_TEXTURE, FXFALSE);
}

static void background(void)
{
    GrVertex a, b, c, d;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    ct_vtx(&a, 0, 0, 255, 0, 0, 255); ct_vtx(&b, 640, 0, 0, 255, 0, 255);
    ct_vtx(&c, 640, 480, 0, 0, 255, 255); ct_vtx(&d, 0, 480, 255, 255, 0, 255);
    gl.grDrawTriangle(&a, &b, &c);
    gl.grDrawTriangle(&a, &c, &d);
}

static void quad(float x0, float y0, float x1, float y1, float a0, float a1)
{
    GrVertex v[4];
    ct_vtx(&v[0], x0, y0, 255, 255, 255, a0); v[0].tmuvtx[0].sow = 0; v[0].tmuvtx[0].tow = 0;
    ct_vtx(&v[1], x1, y0, 255, 255, 255, a1); v[1].tmuvtx[0].sow = 256; v[1].tmuvtx[0].tow = 0;
    ct_vtx(&v[2], x1, y1, 255, 255, 255, a1); v[2].tmuvtx[0].sow = 256; v[2].tmuvtx[0].tow = 256;
    ct_vtx(&v[3], x0, y1, 255, 255, 255, a0); v[3].tmuvtx[0].sow = 0; v[3].tmuvtx[0].tow = 256;
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[3]);
}

/* 64x64 texture in 'fmt' with an alpha ramp along x and a colour pattern. */
static void tex_alpha(GrTextureFormat_t fmt, FxU32 addr)
{
    GrTexInfo ti;
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int a = x * 4 + 2, r = ((x / 8 + y / 8) & 1) ? 255 : 40, g = y * 4, b = 255 - y * 4;
            if (fmt == GR_TEXFMT_ARGB_4444)
                tb[y * 64 + x] = (uint16_t)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
            else
                tb[y * 64 + x] = (uint16_t)(((a >= 128) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
        }
    ti.smallLod = GR_LOD_64; ti.largeLod = GR_LOD_64; ti.aspectRatio = GR_ASPECT_1x1; ti.format = fmt; ti.data = tb;
    gl.grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
}

/* t14: blending. Frame 0 standard translucency, frame 1 other factors. */
static void t14(void)
{
    if (ct_open(2, 1) < 0) return;
    gl.grBufferClear(0, 0, 0);
    background();
    gl.grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    /* Constant alpha, untextured. */
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grConstantColorValue(0x80000000u);
    quad(20, 20, 300, 220, 0, 0);
    /* Iterated alpha ramp, untextured. */
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    quad(340, 20, 620, 220, 0, 255);
    /* Texture alpha (4444) and 1-bit alpha (1555). */
    combine_tex_decal();
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                      GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    tex_alpha(GR_TEXFMT_ARGB_4444, 0);
    quad(20, 260, 300, 460, 255, 255);
    tex_alpha(GR_TEXFMT_ARGB_1555, 0x10000);
    quad(340, 260, 620, 460, 255, 255);
    ct_capture(GR_BUFFER_BACKBUFFER);
    /* Frame 1: additive, inverse and multiplicative factors. */
    gl.grBufferClear(0, 0, 0);
    background();
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grConstantColorValue(0x60406080u);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
    quad(20, 20, 300, 220, 0, 0);
    gl.grAlphaBlendFunction(GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    quad(340, 20, 620, 220, 0, 0);
    gl.grAlphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    quad(20, 260, 300, 460, 0, 0);
    gl.grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
    quad(340, 260, 620, 460, 0, 0);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t15: alpha test on texture alpha and on iterated alpha. */
static void t15(void)
{
    static const GrCmpFnc_t fn[] = { GR_CMP_LESS, GR_CMP_EQUAL, GR_CMP_GREATER, GR_CMP_GEQUAL };
    int i;
    if (ct_open(2, 1) < 0) return;
    gl.grBufferClear(0, 0, 0);
    background();
    combine_tex_decal();
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                      GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    tex_alpha(GR_TEXFMT_ARGB_4444, 0);
    for (i = 0; i < 4; i++) {
        gl.grAlphaTestFunction(fn[i]);
        gl.grAlphaTestReferenceValue((GrAlpha_t)(i == 1 ? 0x88 : 0x80));
        quad(10.0f + i * 157, 20, 150.0f + i * 157, 200, 255, 255);
    }
    tex_alpha(GR_TEXFMT_ARGB_1555, 0x10000);
    gl.grAlphaTestFunction(GR_CMP_GREATER);
    gl.grAlphaTestReferenceValue(128);
    quad(10, 240, 300, 460, 255, 255);
    gl.grAlphaTestFunction(GR_CMP_ALWAYS);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t16: chroma key on palettised and 565 textures, and untextured. */
static void t16(void)
{
    static uint8_t p8[64 * 64];
    static GuTexPalette pal;
    GrTexInfo ti;
    int x, y;
    if (ct_open(2, 1) < 0) return;
    for (x = 0; x < 256; x++)
        pal.data[x] = x == 7 ? 0x00FF00FFu : ((uint32_t)x << 16) | ((uint32_t)(x * 3 & 0xFF) << 8) | (uint32_t)(255 - x);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            p8[y * 64 + x] = (uint8_t)(((x / 8 + y / 8) & 1) ? 7 : (x * 4));
            tb[y * 64 + x] = ((x / 8 + y / 8) & 1) ? 0xF81F : (uint16_t)((x >> 1) << 11 | (y << 5));
        }
    gl.grBufferClear(0, 0, 0);
    background();
    combine_tex_decal();
    gl.grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
    gl.grChromakeyValue(0x00FF00FF);
    gl.grChromakeyMode(GR_CHROMAKEY_ENABLE);
    ti.smallLod = GR_LOD_64; ti.largeLod = GR_LOD_64; ti.aspectRatio = GR_ASPECT_1x1; ti.format = GR_TEXFMT_P_8;
    ti.data = p8;
    gl.grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    quad(20, 20, 300, 220, 255, 255);
    ti.format = GR_TEXFMT_RGB_565;
    ti.data = tb;
    gl.grTexDownloadMipMap(GR_TMU0, 0x10000, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0x10000, GR_MIPMAPLEVELMASK_BOTH, &ti);
    quad(340, 20, 620, 220, 255, 255);
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grConstantColorValue(0x00FF00FF);
    quad(20, 260, 300, 460, 255, 255);
    gl.grConstantColorValue(0x0000FF00);
    quad(340, 260, 620, 460, 255, 255);
    gl.grChromakeyMode(GR_CHROMAKEY_DISABLE);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* Flat bars at every 5-bit level (mid-step, so truncation and rounding
 * agree) in grey, red, green and blue rows. Dithering is off: the Voodoo
 * and Matrox dither patterns differ, and gamma amplifies a one-step
 * difference near black far beyond any sensible tolerance. */
static void gamma_bars(void)
{
    GrVertex a, b, c, d;
    int i, row;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    gl.grDitherMode(GR_DITHER_DISABLE);
    for (row = 0; row < 4; row++)
        for (i = 0; i < 32; i++) {
            float v = (float)(i * 8 + 4), x0 = (float)(i * 20), x1 = x0 + 20.0f;
            float y0 = (float)(row * 120), y1 = y0 + 120.0f;
            float r = (row == 0 || row == 1) ? v : 0, g = (row == 0 || row == 2) ? v : 0;
            float bl = (row == 0 || row == 3) ? v : 0;
            ct_vtx(&a, x0, y0, r, g, bl, 255); ct_vtx(&b, x1, y0, r, g, bl, 255);
            ct_vtx(&c, x1, y1, r, g, bl, 255); ct_vtx(&d, x0, y1, r, g, bl, 255);
            gl.grDrawTriangle(&a, &b, &c);
            gl.grDrawTriangle(&a, &c, &d);
        }
    gl.grDitherMode(GR_DITHER_4x4);
}

/* t22: gamma is a display-path effect: capture the screen, not the LFB. */
static void t22(void)
{
    static const float gammas[] = { 1.0f, 1.7f, 2.2f };
    int i;
    char name[16];
    if (ct_open(2, 1) < 0) return;
    for (i = -1; i < 3; i++) {
        gl.grBufferClear(0, 0, 0);
        gamma_bars();
        if (i >= 0)     /* frame t22_d: the runtime's default ramp */
            gl.grGammaCorrectionValue(gammas[i]);
        gl.grBufferSwap(1);
        gl.grSstIdle();
        {   /* let the display refresh a few times before capturing */
            int f;
            for (f = 0; f < 6; f++) {
                while (gl.grSstVRetraceOn()) ;
                while (!gl.grSstVRetraceOn()) ;
            }
        }
        if (i < 0) snprintf(name, sizeof name, "t22_d");
        else snprintf(name, sizeof name, "t22_%d", i);
        hx_snap_screen(name);
    }
    ct_close();
}

const ct_test ct_alpha_tests[] = {
    { "t14", t14, "blending" },
    { "t15", t15, "alpha test" },
    { "t16", t16, "chroma key" },
    { "t22", t22, "gamma" },
    { NULL, NULL, NULL }
};
