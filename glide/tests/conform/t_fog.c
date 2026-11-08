/* Fog conformance test t17: iterated-alpha, iterated-Z and table fog,
 * untextured and textured, the gu fog-table generators, and the ADD2 /
 * MULT2 multipass modes. */
#include "ct.h"
#include <stdio.h>
#include <string.h>

static uint16_t ftex[64 * 64];

static void fog_texture(void)
{
    GrTexInfo ti;
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int c = ((x / 8 + y / 8) & 1);
            ftex[y * 64 + x] = (uint16_t)(c ? 0xFFE0 : (((x * 31 / 63) << 11) | ((y * 63 / 63) << 5) | 0x10));
        }
    ti.smallLod = GR_LOD_64; ti.largeLod = GR_LOD_64; ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_RGB_565; ti.data = ftex;
    gl.grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
}

static void combine(int textured)
{
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    if (textured) {
        gl.grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
        gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    } else {
        gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_NONE, FXFALSE);
    }
}

/* Axis-aligned quad; per-corner alpha, ooz and oow (left, right); s,t span
 * 'rep' texture widths. */
static void quad(float x0, float y0, float x1, float y1, float a0, float a1, float z0, float z1,
                 float w0, float w1, float rep)
{
    GrVertex v[4];
    float s = 256.0f * rep;
    ct_vtx(&v[0], x0, y0, 255, 40, 40, a0);
    ct_vtx(&v[1], x1, y0, 40, 255, 40, a1);
    ct_vtx(&v[2], x1, y1, 40, 40, 255, a1);
    ct_vtx(&v[3], x0, y1, 255, 255, 40, a0);
    v[0].ooz = v[3].ooz = z0; v[1].ooz = v[2].ooz = z1;
    v[0].oow = v[3].oow = w0; v[1].oow = v[2].oow = w1;
    v[0].tmuvtx[0].sow = 0;     v[0].tmuvtx[0].tow = 0;
    v[1].tmuvtx[0].sow = s * w1; v[1].tmuvtx[0].tow = 0;
    v[2].tmuvtx[0].sow = s * w1; v[2].tmuvtx[0].tow = 256.0f * w1;
    v[3].tmuvtx[0].sow = 0;     v[3].tmuvtx[0].tow = 256.0f * w0;
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[3]);
}

/* A perspective floor from y=ytop (1/w = far) to y=479 (1/w = 1): 1/w is
 * linear in screen space, texture coordinates perspective-correct. */
static void floor_quad(float x0, float x1, float ytop, float far_oow, int textured)
{
    GrVertex v[4];
    int i;
    ct_vtx(&v[0], x0, ytop, 200, 200, 200, 255);
    ct_vtx(&v[1], x1, ytop, 200, 200, 200, 255);
    ct_vtx(&v[2], x1, 479.0f, 200, 200, 200, 255);
    ct_vtx(&v[3], x0, 479.0f, 200, 200, 200, 255);
    v[0].oow = v[1].oow = far_oow;
    v[2].oow = v[3].oow = 0.1f;
    for (i = 0; i < 4; i++) {
        float u = (i == 1 || i == 2) ? 256.0f * 2 : 0.0f, t = (i < 2) ? 256.0f * 6 : 0.0f;
        v[i].tmuvtx[0].oow = v[i].oow;
        v[i].tmuvtx[0].sow = u * v[i].oow;
        v[i].tmuvtx[0].tow = t * v[i].oow;
    }
    combine(textured);
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[3]);
}

static void log_table(const char *what, const GrFog_t *t)
{
    char hex[2 * GR_FOG_TABLE_SIZE + 1];
    int i;
    for (i = 0; i < GR_FOG_TABLE_SIZE; i++)
        snprintf(hex + 2 * i, 3, "%02x", t[i]);
    hx_stat("fogtable=%s %s", what, hex);
}

static void t17(void)
{
    GrFog_t tab[GR_FOG_TABLE_SIZE];
    int i;
    if (ct_open(2, 1) < 0) return;
    fog_texture();
    gl.grFogColorValue(0xFF4080C0u);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);

    /* Generators (compared through the logged values) and index-to-W. */
    gl.guFogGenerateExp(tab, 0.002f);   log_table("exp", tab);
    gl.guFogGenerateExp2(tab, 0.001f);  log_table("exp2", tab);
    gl.guFogGenerateLinear(tab, 10.0f, 3000.0f); log_table("lin", tab);
    for (i = 0; i < GR_FOG_TABLE_SIZE; i += 9)
        hx_stat("fogw i=%d w1000=%ld", i, (long)(gl.guFogTableIndexToW(i) * 1000.0f));

    /* Frame 0: iterated alpha and iterated Z, untextured and textured. */
    gl.grBufferClear(0, 0, 0);
    gl.grFogMode(GR_FOG_WITH_ITERATED_ALPHA);
    combine(0); quad(20, 20, 620, 110, 0, 255, 0, 0, 1, 1, 1);
    combine(1); quad(20, 130, 620, 220, 0, 255, 0, 0, 1, 1, 4);
    /* Iterated-Z fog, with a depth buffer (always-pass compare) so Z is
     * certainly iterated; Voodoo Graphics draws it unfogged. */
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    gl.grDepthBufferFunction(GR_CMP_ALWAYS);
    /* A half-strength table shows whether Z fog falls back to table fog. */
    memset(tab, 128, sizeof tab);
    gl.grFogTable(tab);
    gl.grFogMode(GR_FOG_WITH_ITERATED_Z);
    combine(0); quad(20, 250, 620, 340, 255, 255, 0, 65535, 1, 1, 1);
    combine(1); quad(20, 360, 620, 450, 255, 255, 0, 65535, 1, 1, 4);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    ct_capture(GR_BUFFER_BACKBUFFER);

    /* Frame 1: table fog on perspective floors (exp table untextured on the
     * left, linear table textured on the right). */
    gl.grBufferClear(0x00302010, 0, 0);
    gl.grFogMode(GR_FOG_WITH_TABLE);
    gl.guFogGenerateExp(tab, 0.02f);
    gl.grFogTable(tab);
    floor_quad(0, 319, 60, 1.0f / 4000.0f, 0);
    gl.guFogGenerateLinear(tab, 10.0f, 300.0f);
    gl.grFogTable(tab);
    floor_quad(320, 639, 60, 1.0f / 4000.0f, 1);
    ct_capture(GR_BUFFER_BACKBUFFER);

    /* Frame 2: multipass halves: ADD2 keeps (1-f) * colour, MULT2 keeps
     * f * fog colour; iterated-alpha source; plus a table-fog step ramp. */
    gl.grBufferClear(0, 0, 0);
    gl.grFogMode(GR_FOG_WITH_ITERATED_ALPHA | GR_FOG_ADD2);
    combine(0); quad(20, 20, 620, 110, 0, 255, 0, 0, 1, 1, 1);
    combine(1); quad(20, 130, 620, 220, 0, 255, 0, 0, 1, 1, 4);
    gl.grFogMode(GR_FOG_WITH_ITERATED_ALPHA | GR_FOG_MULT2);
    combine(0); quad(20, 250, 620, 340, 0, 255, 0, 0, 1, 1, 1);
    combine(1); quad(20, 360, 620, 450, 0, 255, 0, 0, 1, 1, 4);
    ct_capture(GR_BUFFER_BACKBUFFER);

    gl.grFogMode(GR_FOG_DISABLE);
    ct_close();
}

const ct_test ct_fog_tests[] = {
    { "t17", t17, "fog" },
    { NULL, NULL, NULL }
};
