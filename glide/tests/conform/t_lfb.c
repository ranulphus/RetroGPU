/* LFB conformance test t18: write modes, colour formats, origins, write
 * regions, pixel-pipeline writes (combine, blend, chroma, depth), colour +
 * depth writes and read locks. */
#include "ct.h"
#include <stdio.h>
#include <string.h>

static uint32_t rowbuf[640 * 64];

static uint32_t pat(int x, int y)
{
    int r = (x * 255) / 639, g = ((y % 60) * 255) / 59, b = ((x / 16 + y / 8) & 1) ? 230 : 40;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint16_t to565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F));
}

static uint16_t to1555(uint32_t c, int a)
{
    return (uint16_t)((a ? 0x8000 : 0) | ((c >> 9) & 0x7C00) | ((c >> 6) & 0x03E0) | ((c >> 3) & 0x1F));
}

/* Write a band through a lock, pixels x in [20,620) of rows [y0,y0+h). */
static void band(GrLfbWriteMode_t mode, GrOriginLocation_t org, FxBool pipe, int y0, int h, int alpha_ramp)
{
    GrLfbInfo_t info;
    int x, y;
    info.size = sizeof info;
    if (!gl.grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, mode, org, pipe, &info)) {
        hx_test("lock", 0, "mode %d", (int)mode);
        return;
    }
    for (y = y0; y < y0 + h; y++) {
        int ly = org == GR_ORIGIN_LOWER_LEFT ? 479 - y : y;
        uint8_t *row = (uint8_t *)info.lfbPtr + (uint32_t)ly * info.strideInBytes;
        for (x = 20; x < 620; x++) {
            uint32_t c = pat(x, y);
            int a = alpha_ramp ? (x - 20) * 255 / 599 : 255;
            switch (mode) {
            case GR_LFBWRITEMODE_565: ((uint16_t *)row)[x] = to565(c); break;
            case GR_LFBWRITEMODE_555: ((uint16_t *)row)[x] = (uint16_t)(to1555(c, 0) & 0x7FFF); break;
            case GR_LFBWRITEMODE_1555: ((uint16_t *)row)[x] = to1555(c, a >= 128); break;
            case GR_LFBWRITEMODE_888: ((uint32_t *)row)[x] = c & 0xFFFFFF; break;
            case GR_LFBWRITEMODE_8888: ((uint32_t *)row)[x] = (c & 0xFFFFFF) | ((uint32_t)a << 24); break;
            case GR_LFBWRITEMODE_565_DEPTH:
                ((uint32_t *)row)[x] = to565(c) | ((uint32_t)((x - 20) * 65535 / 599) << 16);
                break;
            default: break;
            }
        }
    }
    gl.grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
}

static void flat_combine(void)
{
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
}

static void quad(float x0, float y0, float x1, float y1, float z, uint32_t rgb)
{
    GrVertex v[4];
    float r = (float)((rgb >> 16) & 255), g = (float)((rgb >> 8) & 255), b = (float)(rgb & 255);
    ct_vtx(&v[0], x0, y0, r, g, b, 255); ct_vtx(&v[1], x1, y0, r, g, b, 255);
    ct_vtx(&v[2], x1, y1, r, g, b, 255); ct_vtx(&v[3], x0, y1, r, g, b, 255);
    v[0].ooz = v[1].ooz = v[2].ooz = v[3].ooz = z;
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[3]);
}

static void t18(void)
{
    int x, y;
    if (ct_open(2, 1) < 0) return;
    flat_combine();

    /* Frame 0: plain writes. */
    gl.grBufferClear(0x00404040, 0, 0xFFFF);
    band(GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, 0, 58, 0);
    band(GR_LFBWRITEMODE_555, GR_ORIGIN_UPPER_LEFT, FXFALSE, 60, 58, 0);
    band(GR_LFBWRITEMODE_1555, GR_ORIGIN_UPPER_LEFT, FXFALSE, 120, 58, 1);
    band(GR_LFBWRITEMODE_888, GR_ORIGIN_UPPER_LEFT, FXFALSE, 180, 58, 0);
    gl.grLfbWriteColorFormat(GR_COLORFORMAT_ABGR);
    band(GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT, FXFALSE, 240, 58, 0);
    gl.grLfbWriteColorFormat(GR_COLORFORMAT_ARGB);
    band(GR_LFBWRITEMODE_565, GR_ORIGIN_LOWER_LEFT, FXFALSE, 300, 58, 0);
    for (y = 0; y < 58; y++)
        for (x = 0; x < 300; x++) {
            ((uint16_t *)rowbuf)[y * 300 + x] = to565(pat(x + 20, y) ^ 0x00FF00u);
        }
    gl.grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 20, 360, GR_LFB_SRC_FMT_565, 300, 58, 600, rowbuf);
    for (y = 0; y < 58; y++)
        for (x = 0; x < 300; x++)
            ((uint16_t *)rowbuf)[y * 300 + x] = to1555(pat(x + 320, y) ^ 0xFF0000u, 1);
    gl.grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 320, 360, GR_LFB_SRC_FMT_1555, 300, 58, 600, rowbuf);
    for (y = 0; y < 58; y++)
        for (x = 0; x < 600; x++)
            rowbuf[y * 600 + x] = pat(x + 20, y) ^ 0x0000FFu;
    gl.grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 20, 420, GR_LFB_SRC_FMT_8888, 600, 58, 2400, rowbuf);
    ct_capture(GR_BUFFER_BACKBUFFER);

    /* Frame 1: pixel-pipeline writes. */
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);       /* before the clear, or depth is not cleared */
    gl.grBufferClear(0x00202040, 0, 0xFFFF);
    gl.grDepthBufferFunction(GR_CMP_LESS);
    gl.grDepthMask(FXTRUE);
    quad(200, 0, 440, 480, 10000.0f, 0x808080);
    /* A: does the colour combine apply to pipeline writes? (constant red) */
    gl.grConstantColorValue(0xFFFF0000u);
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    band(GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXTRUE, 20, 90, 0);
    flat_combine();
    /* B: blending with per-pixel alpha. */
    gl.grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    band(GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT, FXTRUE, 130, 90, 1);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    /* C: chroma key on the dark checker squares. */
    gl.grChromakeyValue(0xFF000000u);
    gl.grChromakeyMode(GR_CHROMAKEY_ENABLE);
    for (y = 240; y < 330; y += 1) {
        GrLfbInfo_t info;
        info.size = sizeof info;
        if (y == 240 && gl.grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565,
                                     GR_ORIGIN_UPPER_LEFT, FXTRUE, &info)) {
            int yy;
            for (yy = 240; yy < 330; yy++) {
                uint16_t *row = (uint16_t *)((uint8_t *)info.lfbPtr + (uint32_t)yy * info.strideInBytes);
                for (x = 20; x < 620; x++)
                    row[x] = ((x / 16 + yy / 8) & 1) ? to565(pat(x, yy)) : 0;
            }
            gl.grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        }
    }
    gl.grChromakeyMode(GR_CHROMAKEY_DISABLE);
    /* D: constant depth against the quad (depth 10000): 20000 fails over it. */
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    gl.grDepthBufferFunction(GR_CMP_LESS);
    gl.grLfbConstantDepth(20000);
    band(GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXTRUE, 350, 90, 0);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    ct_capture(GR_BUFFER_BACKBUFFER);

    /* Frame 2: colour + depth writes, then a depth-tested quad; read locks
     * copied elsewhere through write regions. */
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    gl.grBufferClear(0x00102030, 0, 0xFFFF);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    band(GR_LFBWRITEMODE_565_DEPTH, GR_ORIGIN_UPPER_LEFT, FXFALSE, 20, 100, 0);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    gl.grDepthBufferFunction(GR_CMP_LESS);
    quad(0, 50, 640, 90, 32768.0f, 0xE0E0E0);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    {
        GrLfbInfo_t info;
        int org;
        for (org = 0; org < 2; org++) {
            info.size = sizeof info;
            if (!gl.grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY,
                              org ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT, FXFALSE, &info)) {
                hx_test("readlock", 0, "origin %d", org);
                continue;
            }
            /* Rows 20..119 (upper-left numbering) -> 64 rows copied below. */
            for (y = 0; y < 64; y++) {
                int sy = org ? 479 - (20 + y) : 20 + y;
                memcpy((uint16_t *)rowbuf + y * 300,
                       (const uint8_t *)info.lfbPtr + (uint32_t)sy * info.strideInBytes + (org ? 640 : 40), 600);
            }
            gl.grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER);
            gl.grLfbWriteRegion(GR_BUFFER_BACKBUFFER, org ? 330 : 20, 150, GR_LFB_SRC_FMT_565, 300, 64, 600, rowbuf);
        }
        /* The depth written above, read back and shown as grey. */
        info.size = sizeof info;
        if (gl.grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_AUXBUFFER, GR_LFBWRITEMODE_ANY, GR_ORIGIN_UPPER_LEFT,
                         FXFALSE, &info)) {
            for (y = 0; y < 64; y++) {
                const uint16_t *zr = (const uint16_t *)((const uint8_t *)info.lfbPtr +
                                                        (uint32_t)(30 + y) * info.strideInBytes);
                for (x = 0; x < 600; x++) {
                    uint32_t d = zr[x + 20] >> 8;
                    ((uint16_t *)rowbuf)[y * 600 + x] = to565(0xFF000000u | (d << 16) | (d << 8) | d);
                }
            }
            gl.grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_AUXBUFFER);
            gl.grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 20, 240, GR_LFB_SRC_FMT_565, 600, 64, 1200, rowbuf);
        } else
            hx_test("readlock-aux", 0, "failed");
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

const ct_test ct_lfb_tests[] = {
    { "t18", t18, "LFB" },
    { NULL, NULL, NULL }
};
