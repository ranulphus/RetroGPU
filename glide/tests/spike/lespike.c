/* lespike.c - loader spike S4/S5 (docs/loader.md).
 *
 * Loads GLIDE2X.OVL with leload, reports what it found, queries the
 * hardware, and with --draw clears the screen and draws one flat triangle,
 * reading the result back with grLfbReadRegion. Run against a retail OVL
 * on the emulated Voodoo, and against MGA-Glide on the G100. */
#include "hx.h"
#include "glbind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int want_draw, want_init;

static void opts(int *argc, char **argv)
{
    int i, j = 1;
    for (i = 1; i < *argc; i++) {
        if (!strcmp(argv[i], "--draw")) want_draw = 1;
        else if (!strcmp(argv[i], "--init")) want_init = 1;
        else argv[j++] = argv[i];
    }
    *argc = j;
}

static void vtx(GrVertex *v, float x, float y, float r, float g, float b)
{
    memset(v, 0, sizeof *v);
    v->x = x; v->y = y; v->r = r; v->g = g; v->b = b; v->a = 255.0f;
    v->ooz = 65535.0f; v->oow = 1.0f;
}

static void draw_test(void)
{
    GrVertex a, b, c;
    static uint16_t px[640 * 480];
    static uint8_t rgb[640 * 480 * 3];
    FxBool ok;
    int i, run;
    gl.grSstSelect(0);
    ok = gl.grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                         GR_ORIGIN_UPPER_LEFT, 2, 1);
    hx_test("winopen", ok != 0, "grSstWinOpen=%d", (int)ok);
    if (!ok)
        return;
    for (run = 0; run < 3; run++) {
        gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_NONE, FXFALSE);
        gl.grBufferClear(0x00203060, 0, 0xFFFF);
        vtx(&a, 100.0f, 80.0f, 255, 0, 0);
        vtx(&b, 540.0f, 120.0f, 0, 255, 0);
        vtx(&c, 300.0f, 420.0f, 0, 0, 255);
        gl.grDrawTriangle(&a, &b, &c);
        gl.grBufferSwap(1);
    }
    gl.grSstIdle();
    ok = gl.grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 0, 0, 640, 480, 640 * 2, px);
    hx_test("readregion", ok != 0, "grLfbReadRegion=%d", (int)ok);
    for (i = 0; i < 640 * 480; i++) {
        uint16_t p = px[i];
        rgb[i * 3 + 0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((p & 31) * 255 / 31);
    }
    hx_save_ppm("readback", 640, 480, rgb);
    hx_snap_screen("screen");
    gl.grGlideShutdown();
}

int main(int argc, char **argv)
{
    char err[128] = "", ver[80] = "";
    le_module *m;
    int missing;
    GrHwConfiguration hw;
    const char *path;

    opts(&argc, argv);
    hx_init(argc, argv, "lespike");
    path = hx_args.glide ? hx_args.glide : "C:\\TEST\\GLIDE2X.OVL";
    m = glbind_load(path, &missing, err, sizeof err);
    hx_test("load", m != NULL, "%s %s", path, m ? "" : err);
    if (!m)
        hx_done(HX_INIT_FAILED);
    hx_log("HX-STAT module=%s flags=%lx objects=%d fixups=%d missing=%d entry=%p",
           le_module_name(m), (unsigned long)le_module_flags(m), le_object_count(m),
           le_fixup_count(m), missing, le_entry(m));
    if (want_init && le_entry(m)) {
        int (*init)(void *, unsigned) = (int (*)(void *, unsigned))le_entry(m);
        hx_log("HX-STAT init-return=%d", init(NULL, 0));
    }
    if (gl.grGlideGetVersion) {
        gl.grGlideGetVersion(ver);
        hx_log("HX-STAT version=\"%s\"", ver);
    }
    memset(&hw, 0, sizeof hw);
    gl.grGlideInit();                 /* Glide 2.x queries after init */
    if (gl.grSstQueryHardware) {
        FxBool found = gl.grSstQueryHardware(&hw);
        hx_test("query", found != 0, "num_sst=%d type=%d fbRam=%d nTexelfx=%d tmuRam=%d",
                hw.num_sst, hw.SSTs[0].type, hw.SSTs[0].sstBoard.VoodooConfig.fbRam,
                hw.SSTs[0].sstBoard.VoodooConfig.nTexelfx,
                hw.SSTs[0].sstBoard.VoodooConfig.tmuConfig[0].tmuRam);
    }
    if (want_draw)
        draw_test();
    hx_done(0);
    return 0;
}
