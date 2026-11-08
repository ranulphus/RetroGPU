/* t23: throughput benchmark (reported, not compared). Draws batches of
 * equal triangles for at least a second per case and reports triangles
 * and pixels per second. In 86Box the numbers describe the emulator; on
 * the bench (Loop B) they are the PRD §9 performance figures. */
#include "ct.h"
#include <stdio.h>
#include <string.h>

static volatile const uint32_t *const bios_ticks = (volatile const uint32_t *)0x46C;   /* 18.2 Hz */
static uint16_t btex[64 * 64];

static void bench_case(const char *name, float size, int textured, int blend)
{
    GrVertex v[3];
    uint32_t t0, t1, n = 0, ticks;
    float x = 20.0f, y = 20.0f;
    int i;
    if (textured) {
        gl.grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
        gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    } else {
        gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_NONE, FXFALSE);
    }
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    if (blend)
        gl.grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    else
        gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    gl.grSstIdle();
    t0 = *bios_ticks;
    while (*bios_ticks == t0)
        ;
    t0 = *bios_ticks;
    do {
        for (i = 0; i < 64; i++) {
            ct_vtx(&v[0], x, y, 255, 64, 64, 160);
            ct_vtx(&v[1], x + size, y, 64, 255, 64, 160);
            ct_vtx(&v[2], x, y + size, 64, 64, 255, 160);
            v[0].tmuvtx[0].sow = 0;   v[0].tmuvtx[0].tow = 0;
            v[1].tmuvtx[0].sow = 256; v[1].tmuvtx[0].tow = 0;
            v[2].tmuvtx[0].sow = 0;   v[2].tmuvtx[0].tow = 256;
            gl.grDrawTriangle(&v[0], &v[1], &v[2]);
            x += 7.0f;
            if (x + size > 620.0f) { x = 20.0f; y += 5.0f; }
            if (y + size > 460.0f) y = 20.0f;
        }
        n += 64;
        t1 = *bios_ticks;
    } while (t1 - t0 < 18);
    gl.grSstIdle();
    ticks = *bios_ticks - t0;
    {
        double secs = ticks / 18.2065, tps = n / secs, pix = tps * size * size * 0.5;
        hx_stat("bench case=%s size=%d tris=%u secs=%.2f tris_per_s=%.0f mpix_per_s=%.2f", name, (int)size, n,
                secs, tps, pix / 1e6);
    }
}

static void t23(void)
{
    GrTexInfo ti;
    int i;
    if (ct_open(2, 1) < 0) return;
    for (i = 0; i < 64 * 64; i++)
        btex[i] = (uint16_t)((((i & 63) / 8 + (i / 64) / 8) & 1) ? 0xFFFF : 0x39E7);
    ti.smallLod = GR_LOD_64; ti.largeLod = GR_LOD_64; ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_RGB_565; ti.data = btex;
    gl.grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    gl.grBufferClear(0, 0, 0);
    bench_case("flat-gouraud", 4.0f, 0, 0);
    bench_case("flat-gouraud", 16.0f, 0, 0);
    bench_case("flat-gouraud", 64.0f, 0, 0);
    bench_case("textured", 16.0f, 1, 0);
    bench_case("textured", 64.0f, 1, 0);
    bench_case("textured-blend", 64.0f, 1, 1);
    ct_close();
}

const ct_test ct_bench_tests[] = {
    { "t23", t23, "throughput benchmark" },
    { NULL, NULL, NULL }
};
