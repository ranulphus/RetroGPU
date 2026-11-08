/* ct.c - conformance-test driver: CONFORM.EXE <test> [--glide=PATH]. */
#include "ct.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ct_name = "t";
static int ct_frame;
static uint16_t ct_px[CT_W * CT_H];
static uint8_t ct_rgb[CT_W * CT_H * 3];

int ct_open(int nbuffers, int naux)
{
    GrHwConfiguration hw;
    gl.grGlideInit();
    if (!gl.grSstQueryHardware(&hw) || hw.num_sst < 1) {
        hx_test("open", 0, "no hardware");
        return -1;
    }
    gl.grSstSelect(0);
    if (!gl.grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                         GR_ORIGIN_UPPER_LEFT, nbuffers, naux)) {
        hx_test("open", 0, "grSstWinOpen failed");
        return -1;
    }
    return 0;
}

void ct_close(void)
{
    gl.grGlideShutdown();
}

static void save(void)
{
    char name[40];
    int i;
    for (i = 0; i < CT_W * CT_H; i++) {
        uint16_t p = ct_px[i];
        ct_rgb[i * 3 + 0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
        ct_rgb[i * 3 + 1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
        ct_rgb[i * 3 + 2] = (uint8_t)((p & 31) * 255 / 31);
    }
    snprintf(name, sizeof name, "%s_%d", ct_name, ct_frame++);
    hx_save_ppm(name, CT_W, CT_H, ct_rgb);
}

void ct_capture(GrBuffer_t buffer)
{
    gl.grSstIdle();
    if (!gl.grLfbReadRegion(buffer, 0, 0, CT_W, CT_H, CT_W * 2, ct_px))
        hx_test("readregion", 0, "grLfbReadRegion failed");
    save();
}

void ct_capture_depth(void)
{
    int i;
    gl.grSstIdle();
    if (!gl.grLfbReadRegion(GR_BUFFER_AUXBUFFER, 0, 0, CT_W, CT_H, CT_W * 2, ct_px))
        hx_test("readregion-aux", 0, "grLfbReadRegion(aux) failed");
    /* Show depth as grey (top 5 bits), repacked as 565 for save(). */
    for (i = 0; i < CT_W * CT_H; i++) {
        uint16_t d = (uint16_t)(ct_px[i] >> 11);
        ct_px[i] = (uint16_t)((d << 11) | (d << 6) | d);
    }
    save();
}

void ct_vtx(GrVertex *v, float x, float y, float r, float g, float b, float a)
{
    memset(v, 0, sizeof *v);
    v->x = x; v->y = y; v->r = r; v->g = g; v->b = b; v->a = a;
    v->ooz = 65535.0f; v->oow = 1.0f;
    v->tmuvtx[0].oow = 1.0f;
}

void ct_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb)
{
    GrVertex a, b, c;
    float r = (float)((rgb >> 16) & 0xFF), g = (float)((rgb >> 8) & 0xFF), bl = (float)(rgb & 0xFF);
    ct_vtx(&a, x0, y0, r, g, bl, 255);
    ct_vtx(&b, x1, y1, r, g, bl, 255);
    ct_vtx(&c, x2, y2, r, g, bl, 255);
    gl.grDrawTriangle(&a, &b, &c);
}

int main(int argc, char **argv)
{
    char err[128] = "";
    int missing, i;
    const char *which = NULL;
    const char *path;
    le_module *m;
    for (i = 1; i < argc; i++)
        if (argv[i][0] != '-') {
            which = argv[i];
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof argv[0]);
            argc--;
            break;
        }
    hx_init(argc, argv, "conform");
    if (!which) {
        hx_log("usage: CONFORM <test>");
        hx_done(HX_BAD_ARGS);
    }
    path = hx_args.glide ? hx_args.glide : "C:\\TEST\\GLIDE2X.OVL";
    m = glbind_load(path, &missing, err, sizeof err);
    if (!m) {
        hx_test("load", 0, "%s: %s", path, err);
        hx_done(HX_INIT_FAILED);
    }
    {
        static const ct_test *const suites[] = { ct_tests, ct_tex_tests, ct_alpha_tests, ct_fog_tests, ct_lfb_tests, ct_combine_tests, ct_bench_tests, NULL };
        int si;
        for (si = 0; suites[si]; si++)
            for (i = 0; suites[si][i].name; i++) {
                if (!strcmp(suites[si][i].name, which)) {
                    ct_name = suites[si][i].name;
                    hx_log("HX-STAT test=%s what=\"%s\"", ct_name, suites[si][i].what);
                    suites[si][i].fn();
                    hx_done(0);
                }
            }
    }
    hx_log("unknown test %s", which);
    hx_done(HX_BAD_ARGS);
    return 0;
}
