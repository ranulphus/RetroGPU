/* Untextured conformance tests t01-t09 and t21. */
#include "ct.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* t01: initialisation, hardware query, window open/close. */
static void t01(void)
{
    GrHwConfiguration hw;
    char ver[80] = "";
    gl.grGlideGetVersion(ver);
    hx_test("version", ver[0] != 0, "\"%s\"", ver);
    gl.grGlideInit();
    hx_test("query", gl.grSstQueryHardware(&hw) && hw.num_sst >= 1, "num_sst=%d", hw.num_sst);
    gl.grSstSelect(0);
    hx_test("open", gl.grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                                    GR_ORIGIN_UPPER_LEFT, 2, 1) != 0, "");
    hx_test("size", gl.grSstScreenWidth() == 640 && gl.grSstScreenHeight() == 480, "%lux%lu",
            (unsigned long)gl.grSstScreenWidth(), (unsigned long)gl.grSstScreenHeight());
    gl.grBufferClear(0x00336699, 0, 0);
    ct_capture(GR_BUFFER_BACKBUFFER);
    gl.grSstWinClose();
    hx_test("reopen", gl.grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                                      GR_ORIGIN_UPPER_LEFT, 2, 1) != 0, "");
    ct_close();
}

/* t02: clears - full, clip-window bounded, colour formats, depth. */
static void t02(void)
{
    if (ct_open(2, 1) < 0) return;
    gl.grBufferClear(0x00FF8040, 0, 0);
    ct_capture(GR_BUFFER_BACKBUFFER);
    gl.grClipWindow(100, 50, 400, 300);
    gl.grBufferClear(0x0000FF00, 0, 0);
    gl.grClipWindow(0, 0, 640, 480);
    ct_capture(GR_BUFFER_BACKBUFFER);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    gl.grDepthMask(FXTRUE);
    gl.grBufferClear(0x00000000, 0, 0x8000);
    ct_capture_depth();
    gl.grColorMask(FXFALSE, FXFALSE);
    gl.grBufferClear(0x00FFFFFF, 0, 0x1234);
    gl.grColorMask(FXTRUE, FXFALSE);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_capture_depth();
    ct_close();
}

/* t03: swaps - front/back contents, render to front, triple buffering. */
static void t03(void)
{
    if (ct_open(2, 1) < 0) return;
    gl.grBufferClear(0x00FF0000, 0, 0);
    gl.grBufferSwap(1);
    gl.grBufferClear(0x000000FF, 0, 0);
    ct_capture(GR_BUFFER_FRONTBUFFER);      /* red */
    ct_capture(GR_BUFFER_BACKBUFFER);       /* blue */
    gl.grBufferSwap(0);
    ct_capture(GR_BUFFER_FRONTBUFFER);      /* blue */
    gl.grRenderBuffer(GR_BUFFER_FRONTBUFFER);
    ct_tri(100, 100, 300, 120, 150, 300, 0x00FFFF00);
    gl.grRenderBuffer(GR_BUFFER_BACKBUFFER);
    ct_capture(GR_BUFFER_FRONTBUFFER);
    ct_close();
    if (ct_open(3, 1) < 0) return;
    gl.grBufferClear(0x00FF0000, 0, 0); gl.grBufferSwap(1);
    gl.grBufferClear(0x0000FF00, 0, 0); gl.grBufferSwap(1);
    gl.grBufferClear(0x000000FF, 0, 0); gl.grBufferSwap(1);
    ct_capture(GR_BUFFER_FRONTBUFFER);      /* blue */
    ct_close();
}

/* t04: flat triangles, fill rule, slivers and tiny triangles. */
static void t04(void)
{
    int i;
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grBufferClear(0, 0, 0);
    /* Two triangles sharing an edge (a quad) plus a fan. */
    ct_tri(20, 20, 200, 20, 20, 150, 0x00FF0000);
    ct_tri(200, 20, 200, 150, 20, 150, 0x0000FF00);
    for (i = 0; i < 12; i++) {
        float a0 = i * 0.5236f, a1 = (i + 1) * 0.5236f;
        ct_tri(420, 120, 420 + 100 * (float)cos(a0), 120 + 100 * (float)sin(a0),
               420 + 100 * (float)cos(a1), 120 + 100 * (float)sin(a1),
               (i & 1) ? 0x00FFFFFF : 0x00808080);
    }
    /* Sub-pixel positions: vertices on pixel centres and between them. */
    ct_tri(30.5f, 200.5f, 130.5f, 200.5f, 30.5f, 300.5f, 0x000000FF);
    ct_tri(150.25f, 200.75f, 250.125f, 210.0625f, 170.875f, 320.3125f, 0x00FF00FF);
    /* Slivers and tiny triangles. */
    ct_tri(300, 200, 600, 205, 300, 202, 0x0000FFFF);
    ct_tri(300, 250, 300.5f, 250, 300, 250.5f, 0x00FFFF00);
    ct_tri(310, 250, 312, 250, 310, 252, 0x00FFFF00);
    for (i = 0; i < 20; i++)
        ct_tri(320.0f + i * 14, 300.0f + i * 0.3f, 330.0f + i * 14, 300.0f, 325.0f + i * 14, 400.0f - i * 3,
               0x00406080 + (uint32_t)i * 0x00050A0F);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t05: Gouraud shading and dithering. */
static void t05(void)
{
    GrVertex a, b, c, d;
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grBufferClear(0, 0, 0);
    ct_vtx(&a, 20, 20, 255, 0, 0, 255);
    ct_vtx(&b, 300, 40, 0, 255, 0, 255);
    ct_vtx(&c, 60, 300, 0, 0, 255, 255);
    gl.grDrawTriangle(&a, &b, &c);
    /* A slow gradient that shows the dither pattern. */
    ct_vtx(&a, 340, 20, 0, 0, 0, 255);
    ct_vtx(&b, 620, 20, 64, 64, 64, 255);
    ct_vtx(&c, 620, 200, 64, 64, 64, 255);
    ct_vtx(&d, 340, 200, 0, 0, 0, 255);
    gl.grDrawTriangle(&a, &b, &c);
    gl.grDrawTriangle(&a, &c, &d);
    gl.grDitherMode(GR_DITHER_DISABLE);
    ct_vtx(&a, 340, 240, 0, 0, 0, 255);
    ct_vtx(&b, 620, 240, 64, 64, 64, 255);
    ct_vtx(&c, 620, 420, 64, 64, 64, 255);
    ct_vtx(&d, 340, 420, 0, 0, 0, 255);
    gl.grDrawTriangle(&a, &b, &c);
    gl.grDrawTriangle(&a, &c, &d);
    gl.grDitherMode(GR_DITHER_2x2);
    /* Constant colour through the combine unit. */
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grConstantColorValue(0x0080C0FF);
    ct_tri(40, 340, 300, 330, 200, 460, 0);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

static void ztri(float x0, float y0, float z0, float x1, float y1, float z1,
                 float x2, float y2, float z2, uint32_t rgb)
{
    GrVertex a, b, c;
    float r = (float)((rgb >> 16) & 0xFF), g = (float)((rgb >> 8) & 0xFF), bl = (float)(rgb & 0xFF);
    ct_vtx(&a, x0, y0, r, g, bl, 255); a.ooz = z0; a.oow = z0 / 65535.0f;
    ct_vtx(&b, x1, y1, r, g, bl, 255); b.ooz = z1; b.oow = z1 / 65535.0f;
    ct_vtx(&c, x2, y2, r, g, bl, 255); c.ooz = z2; c.oow = z2 / 65535.0f;
    gl.grDrawTriangle(&a, &b, &c);
}

/* t06: Z buffer - interpenetrating triangles, compare functions, mask, bias. */
static void t06(void)
{
    static const GrCmpFnc_t fns[] = { GR_CMP_NEVER, GR_CMP_LESS, GR_CMP_EQUAL, GR_CMP_LEQUAL,
                                      GR_CMP_GREATER, GR_CMP_NOTEQUAL, GR_CMP_GEQUAL, GR_CMP_ALWAYS };
    int i;
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    gl.grDepthMask(FXTRUE);
    gl.grDepthBufferFunction(GR_CMP_GREATER);          /* ooz: larger is nearer */
    gl.grBufferClear(0, 0, 0);
    ztri(40, 40, 60000, 400, 60, 5000, 60, 300, 30000, 0x00FF0000);
    ztri(60, 60, 5000, 420, 40, 60000, 300, 320, 30000, 0x0000FF00);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_capture_depth();
    /* Each compare function against a mid-depth plane. */
    gl.grDepthBufferFunction(GR_CMP_ALWAYS);
    gl.grBufferClear(0, 0, 0);
    ztri(0, 0, 32768, 640, 0, 32768, 0, 480, 32768, 0x00404040);
    ztri(640, 0, 32768, 640, 480, 32768, 0, 480, 32768, 0x00404040);
    for (i = 0; i < 8; i++) {
        float x = 20.0f + i * 76;
        hx_log("HX-STEP t06 compare %d", i);
        gl.grDepthBufferFunction(fns[i]);
        ztri(x, 20, 20000, x + 60, 20, 45000, x, 200, 45000, 0x00FFFFFF);
        ztri(x + 60, 20, 45000, x + 60, 200, 32768, x, 200, 45000, 0x0000FFFF);
    }
    hx_log("HX-STEP t06 mask");
    /* Depth mask off: colour drawn, depth unchanged. */
    gl.grDepthBufferFunction(GR_CMP_ALWAYS);
    gl.grDepthMask(FXFALSE);
    ztri(40, 260, 65000, 300, 260, 65000, 40, 460, 65000, 0x00FF00FF);
    gl.grDepthMask(FXTRUE);
    gl.grDepthBufferFunction(GR_CMP_GREATER);
    ztri(40, 260, 40000, 300, 460, 40000, 40, 460, 40000, 0x00FFFF00);
    hx_log("HX-STEP t06 bias");
    /* Depth bias. */
    gl.grDepthBiasLevel(8000);
    ztri(340, 260, 30000, 600, 260, 30000, 340, 460, 30000, 0x0000FF00);
    gl.grDepthBiasLevel(0);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t07: W buffer across a wide range of w. */
static void t07(void)
{
    int i;
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    gl.grDepthMask(FXTRUE);
    gl.grDepthBufferFunction(GR_CMP_LESS);
    gl.grBufferClear(0, 0, GR_WDEPTHVALUE_FARTHEST);
    for (i = 0; i < 10; i++) {
        GrVertex a, b, c;
        float w0 = 1.0f + i * i * 700.0f, w1 = 1.0f + (9 - i) * (9 - i) * 700.0f;
        float y = 20.0f + i * 44;
        ct_vtx(&a, 20, y, 255, 32.0f * (i % 8), 0, 255); a.oow = 1.0f / w0;
        ct_vtx(&b, 620, y + 10, 255, 32.0f * (i % 8), 0, 255); b.oow = 1.0f / w1;
        ct_vtx(&c, 320, y + 60, 255, 32.0f * (i % 8), 0, 255); c.oow = 1.0f / ((w0 + w1) / 2);
        gl.grDrawTriangle(&a, &b, &c);
        ct_vtx(&a, 20, y + 30, 0, 32.0f * (i % 8), 255, 255); a.oow = 1.0f / (w0 * 1.01f);
        ct_vtx(&b, 620, y + 20, 0, 32.0f * (i % 8), 255, 255); b.oow = 1.0f / (w1 * 0.99f);
        ct_vtx(&c, 320, y - 20, 0, 32.0f * (i % 8), 255, 255); c.oow = 1.0f / ((w0 + w1) / 2);
        gl.grDrawTriangle(&a, &b, &c);
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t08: culling modes against both orientations. */
static void t08(void)
{
    static const GrCullMode_t modes[] = { GR_CULL_DISABLE, GR_CULL_NEGATIVE, GR_CULL_POSITIVE };
    int m;
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grBufferClear(0, 0, 0);
    for (m = 0; m < 3; m++) {
        float y = 20.0f + m * 150;
        gl.grCullMode(modes[m]);
        ct_tri(40, y, 240, y, 40, y + 120, 0x00FF0000);           /* clockwise on screen */
        ct_tri(300, y, 300, y + 120, 500, y, 0x0000FF00);          /* counter-clockwise */
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t09: clip window and guDrawTriangleWithClip. */
static void t09(void)
{
    GrVertex a, b, c;
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grBufferClear(0x00202020, 0, 0);
    gl.grClipWindow(100, 80, 540, 400);
    ct_tri(0, 0, 640, 40, 200, 480, 0x00FF8000);
    gl.grClipWindow(300, 200, 301, 480);
    ct_tri(250, 150, 400, 150, 300, 400, 0x0000FF00);
    gl.grClipWindow(0, 0, 640, 480);
    ct_vtx(&a, -100, 300, 255, 0, 0, 255);
    ct_vtx(&b, 700, 350, 0, 255, 0, 255);
    ct_vtx(&c, 320, 600, 0, 0, 255, 255);
    gl.grClipWindow(10, 250, 630, 470);
    gl.guDrawTriangleWithClip(&a, &b, &c);
    gl.grClipWindow(0, 0, 640, 480);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t21: points, lines, polygons and antialiased variants. */
static void t21(void)
{
    GrVertex v[6];
    int i;
    int ilist[5] = { 0, 1, 2, 3, 4 };
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grBufferClear(0, 0, 0);
    for (i = 0; i < 40; i++) {
        ct_vtx(&v[0], 20.0f + i * 3.5f, 20.0f + i * 1.25f, 255, 255, 255, 255);
        gl.grDrawPoint(&v[0]);
    }
    for (i = 0; i < 16; i++) {
        float a = i * 0.3927f;
        ct_vtx(&v[0], 400, 120, 255, 255, 0, 255);
        ct_vtx(&v[1], 400 + 100 * (float)cos(a), 120 + 100 * (float)sin(a), 0, 255, 255, 255);
        gl.grDrawLine(&v[0], &v[1]);
    }
    for (i = 0; i < 5; i++) {
        float a = i * 1.2566f;
        ct_vtx(&v[i], 150 + 90 * (float)cos(a), 330 + 90 * (float)sin(a),
               (float)(i * 60), 255.0f - i * 50, 128, 255);
    }
    gl.grDrawPolygonVertexList(5, v);
    for (i = 0; i < 5; i++)
        v[i].x += 300;
    gl.grDrawPolygon(5, ilist, v);
    gl.grAADrawLine(&v[0], &v[2]);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* x01: a fault with the window open. The runtime's fault hook logs
 * MGL-EXC, restores text mode and chains to DOS/4GW, which ends the
 * program; the harness then checks the video mode (HX-VMODE). */
void ct_bad_selector(void);
#pragma aux ct_bad_selector = "mov ax, 0x1235" "mov es, ax" modify [eax];

static void x01(void)
{
    if (ct_open(2, 1) < 0) return;
    gl.grBufferClear(0x00FF0000, 0, 0);
    gl.grBufferSwap(0);
    hx_log("forcing #GP");
    ct_bad_selector();
    hx_log("still running after #GP");
}

/* x02: exit to DOS with the window open and no grGlideShutdown. */
static void x02(void)
{
    if (ct_open(2, 1) < 0) return;
    gl.grBufferClear(0x0000FF00, 0, 0);
    gl.grBufferSwap(0);
    hx_log("exiting without shutdown");
    exit(3);
}

const ct_test ct_tests[] = {
    { "x01", x01, "fault with the window open" },
    { "x02", x02, "exit without grGlideShutdown" },
    { "t01", t01, "init, query, open, close" },
    { "t02", t02, "clears" },
    { "t03", t03, "swaps and render buffers" },
    { "t04", t04, "flat triangles and fill rule" },
    { "t05", t05, "Gouraud and dither" },
    { "t06", t06, "Z buffer" },
    { "t07", t07, "W buffer" },
    { "t08", t08, "culling" },
    { "t09", t09, "clip window" },
    { "t21", t21, "points, lines, polygons" },
    { NULL, NULL, NULL }
};
