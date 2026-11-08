/* Combine conformance test t13: a grid of colour-combine states (every gu
 * preset plus raw forms), and alpha sources under blending. Cells the G100
 * cannot reproduce are listed in the manifest as approximate, which makes
 * the result a combine-coverage table. */
#include "ct.h"
#include <string.h>

static uint16_t ctex[64 * 64];

static void combine_texture(void)
{
    GrTexInfo ti;
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int a = (x * 15) / 63, r = ((x / 8 + y / 8) & 1) ? 15 : 4, g = (y * 15) / 63, b = 15 - (x * 15) / 63;
            ctex[y * 64 + x] = (uint16_t)((a << 12) | (r << 8) | (g << 4) | b);
        }
    ti.smallLod = GR_LOD_64; ti.largeLod = GR_LOD_64; ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_ARGB_4444; ti.data = ctex;
    gl.grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
}

/* Cell quad: colour gradient across, alpha gradient down. */
static void cell(int i)
{
    GrVertex v[4];
    float x = 10.0f + (i % 6) * 105, y = 10.0f + (i / 6) * 117;
    ct_vtx(&v[0], x, y, 250, 30, 30, 20);
    ct_vtx(&v[1], x + 100, y, 30, 250, 30, 20);
    ct_vtx(&v[2], x + 100, y + 110, 30, 30, 250, 240);
    ct_vtx(&v[3], x, y + 110, 200, 200, 30, 240);
    v[0].tmuvtx[0].sow = 0;   v[0].tmuvtx[0].tow = 0;
    v[1].tmuvtx[0].sow = 256; v[1].tmuvtx[0].tow = 0;
    v[2].tmuvtx[0].sow = 256; v[2].tmuvtx[0].tow = 256;
    v[3].tmuvtx[0].sow = 0;   v[3].tmuvtx[0].tow = 256;
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[3]);
}

#define CC(f, fa, l, o, inv) gl.grColorCombine(GR_COMBINE_FUNCTION_##f, GR_COMBINE_FACTOR_##fa, \
                                               GR_COMBINE_LOCAL_##l, GR_COMBINE_OTHER_##o, inv)
#define AC(f, fa, l, o, inv) gl.grAlphaCombine(GR_COMBINE_FUNCTION_##f, GR_COMBINE_FACTOR_##fa, \
                                               GR_COMBINE_LOCAL_##l, GR_COMBINE_OTHER_##o, inv)

static void t13(void)
{
    int i;
    if (ct_open(2, 1) < 0) return;
    combine_texture();
    gl.grConstantColorValue(0xC04080E0u);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    gl.grBufferClear(0x00181818, 0, 0);
    /* Frame 0: colour combine. Cells 0..16: gu presets. */
    AC(LOCAL, NONE, ITERATED, NONE, FXFALSE);
    for (i = 0; i <= GR_COLORCOMBINE_ONE; i++) {
        gl.guColorCombineFunction((GrColorCombineFnc_t)i);
        cell(i);
    }
    CC(SCALE_OTHER, LOCAL_ALPHA, CONSTANT, ITERATED, FXFALSE);     /* 17: iterated x constant alpha */
    AC(LOCAL, NONE, CONSTANT, NONE, FXFALSE);
    cell(17);
    AC(LOCAL, NONE, ITERATED, ITERATED, FXFALSE);
    CC(SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL, OTHER_ALPHA, CONSTANT, ITERATED, FXFALSE);  /* 18: lerp by iterated alpha */
    cell(18);
    CC(SCALE_MINUS_LOCAL_ADD_LOCAL, ONE_MINUS_LOCAL_ALPHA, ITERATED, NONE, FXFALSE); /* 19: iterated x alpha */
    cell(19);
    CC(LOCAL, NONE, ITERATED, NONE, FXTRUE);                        /* 20: inverted iterated */
    cell(20);
    CC(SCALE_OTHER, ONE_MINUS_LOCAL, CONSTANT, TEXTURE, FXFALSE);  /* 21: texel x (1 - constant) */
    cell(21);
    AC(LOCAL, NONE, CONSTANT, NONE, FXFALSE);
    CC(SCALE_OTHER, LOCAL_ALPHA, CONSTANT, TEXTURE, FXFALSE);      /* 22: texel x constant alpha */
    cell(22);
    AC(LOCAL, NONE, ITERATED, NONE, FXFALSE);
    CC(LOCAL_ALPHA, NONE, ITERATED, NONE, FXFALSE);                 /* 23: iterated alpha as grey */
    cell(23);
    ct_capture(GR_BUFFER_BACKBUFFER);

    /* Frame 1: alpha sources blended over stripes. */
    gl.grBufferClear(0x00181818, 0, 0);
    CC(LOCAL, NONE, CONSTANT, NONE, FXFALSE);
    gl.grConstantColorValue(0xFFE0E0E0u);
    for (i = 0; i < 24; i++) {
        float x = 10.0f + (i % 6) * 105, y = 10.0f + (i / 6) * 117;
        ct_tri(x, y + 50, x + 100, y + 50, x + 50, y + 60, 0x3060C0);
        ct_tri(x, y + 20, x + 100, y + 20, x + 50, y + 30, 0xC06030);
    }
    gl.guColorCombineFunction(GR_COLORCOMBINE_DECAL_TEXTURE);
    gl.grConstantColorValue(0x80FFFFFFu);
    gl.grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    for (i = 0; i < 4; i++) {
        gl.guAlphaSource((GrAlphaSource_t)i);
        cell(i);
    }
    AC(SCALE_OTHER, LOCAL_ALPHA, CONSTANT, TEXTURE, FXFALSE);      /* 4: texture alpha x constant alpha */
    cell(4);
    AC(SCALE_OTHER, ONE_MINUS_LOCAL_ALPHA, ITERATED, TEXTURE, FXFALSE);  /* 5: texture x (1 - iterated) */
    cell(5);
    AC(LOCAL, NONE, ITERATED, NONE, FXTRUE);                        /* 6: 1 - iterated */
    cell(6);
    AC(SCALE_OTHER, LOCAL_ALPHA, CONSTANT, ITERATED, FXFALSE);     /* 7: iterated x constant */
    cell(7);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

const ct_test ct_combine_tests[] = {
    { "t13", t13, "combine grid" },
    { NULL, NULL, NULL }
};
