/* texprobe.c - black-box probe of a Glide runtime's texture-memory
 * arithmetic. Writes C:\OUT\TEXMEM.TXT with one line per query:
 *   range <min> <max>
 *   calc <fmt> <aspect> <large> <small> <bytes>
 *   req <evenOdd> <fmt> <aspect> <large> <small> <bytes>
 *   avail <bytes>
 *   alloc <w> <h> <fmt> <aspect> <large> <small> <evenOdd> -> <id> avail <bytes>
 * Used to make MGA-Glide's texfmt.c match the retail behaviour exactly. */
#include "hx.h"
#include "glbind.h"
#include <stdio.h>

static const int fmts[] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14 };

int main(int argc, char **argv)
{
    char err[128];
    int missing, f, a, lg, sm, eo;
    FILE *out;
    GrHwConfiguration hw;
    GrTexInfo ti;
    hx_init(argc, argv, "texprobe");
    if (!glbind_load(hx_args.glide ? hx_args.glide : "C:\\TEST\\GLIDE2X.OVL", &missing, err, sizeof err)) {
        hx_test("load", 0, "%s", err);
        hx_done(HX_INIT_FAILED);
    }
    out = fopen("C:\\OUT\\TEXMEM.TXT", "w");
    gl.grGlideInit();
    gl.grSstQueryHardware(&hw);
    gl.grSstSelect(0);
    gl.grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    fprintf(out, "range %lu %lu\n", (unsigned long)gl.grTexMinAddress(0), (unsigned long)gl.grTexMaxAddress(0));
    for (f = 0; f < (int)(sizeof fmts / sizeof fmts[0]); f++)
        for (a = 0; a <= 6; a++)
            for (lg = 0; lg <= 8; lg++)
                for (sm = lg; sm <= 8; sm++) {
                    fprintf(out, "calc %d %d %d %d %lu\n", fmts[f], a, lg, sm,
                            (unsigned long)gl.grTexCalcMemRequired(sm, lg, a, fmts[f]));
                    ti.smallLod = sm; ti.largeLod = lg; ti.aspectRatio = a; ti.format = fmts[f]; ti.data = 0;
                    for (eo = 1; eo <= 3; eo++)
                        fprintf(out, "req %d %d %d %d %d %lu\n", eo, fmts[f], a, lg, sm,
                                (unsigned long)gl.grTexTextureMemRequired(eo, &ti));
                }
    gl.guTexMemReset();
    fprintf(out, "avail %lu\n", (unsigned long)gl.guTexMemQueryAvail(0));
    {
        static const int cfg[][7] = {
            { 256, 256, 10, 3, 0, 8, 3 }, { 64, 64, 5, 3, 2, 2, 3 }, { 64, 32, 12, 2, 2, 8, 3 },
            { 8, 8, 10, 3, 5, 5, 3 }, { 1, 1, 10, 3, 8, 8, 3 }, { 256, 32, 5, 0, 0, 8, 3 },
            { 32, 256, 10, 6, 0, 8, 3 }, { 16, 16, 0, 3, 4, 8, 3 }, { 128, 128, 10, 3, 1, 1, 1 },
            { 128, 128, 10, 3, 1, 1, 2 },
        };
        int i;
        for (i = 0; i < (int)(sizeof cfg / sizeof cfg[0]); i++) {
            GrMipMapId_t id = gl.guTexAllocateMemory(0, (FxU8)cfg[i][6], cfg[i][0], cfg[i][1], cfg[i][2],
                                                     GR_MIPMAP_NEAREST, cfg[i][5], cfg[i][4], cfg[i][3],
                                                     GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP,
                                                     GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR,
                                                     0.0f, FXFALSE);
            fprintf(out, "alloc %d %d %d %d %d %d %d -> %ld avail %lu\n", cfg[i][0], cfg[i][1], cfg[i][2],
                    cfg[i][3], cfg[i][4], cfg[i][5], cfg[i][6], (long)id, (unsigned long)gl.guTexMemQueryAvail(0));
        }
    }
    fclose(out);
    gl.grGlideShutdown();
    hx_test("probe", 1, "written");
    hx_done(0);
    return 0;
}
