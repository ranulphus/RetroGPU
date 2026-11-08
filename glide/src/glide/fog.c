/* fog.c - Voodoo fog on the Matrox fog iterator (PRD §7.5).
 *
 * The Voodoo blends towards the fog colour by a factor f (0..255) taken
 * per pixel from iterated alpha, iterated Z (its top 8 bits) or a 64-entry
 * table indexed by the pixel's W-buffer code and interpolated between
 * entries. The Matrox iterates one linear fog factor per trapezoid (255 =
 * no fog). Iterated modes are exact; table fog uses exact per-vertex
 * values, and triangles whose fog changes a lot are drawn in bands with a
 * local linear fit (draw.c). */
#include "glide/mg.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

int mg_fog_source(void)
{
    int src = (int)(mg.st.fog_mode & 0xFF);
    /* Voodoo Graphics has no Z fog: the retail runtime draws such
     * triangles unfogged (t17), whatever the fog table holds. */
    if (src == GR_FOG_WITH_ITERATED_Z && !mg_config.report_voodoo2)
        return GR_FOG_DISABLE;
    return src;
}

/* Table fog at a given 1/w: index = top 6 bits of the W code, the next 8
 * bits blend towards the following entry. */
double mg_fog_table_at(double oow)
{
    unsigned code = mg_wcode_from_oow(oow);
    unsigned idx = code >> 10, frac = (code >> 2) & 0xFF;
    double f0, f1;
    if (idx > 63)
        idx = 63;
    f0 = mg.st.fog_table[idx];
    f1 = idx < 63 ? mg.st.fog_table[idx + 1] : f0;
    return f0 + (f1 - f0) * (double)frac / 256.0;
}

/* Fog factor (0 = none, 255 = all fog colour) at a vertex. */
double mg_fog_vertex(const GrVertex *v)
{
    double f;
    switch (mg_fog_source()) {
    case GR_FOG_WITH_ITERATED_ALPHA:
        f = v->a;
        break;
    case GR_FOG_WITH_ITERATED_Z:
        f = (v->ooz + mg.st.depth_bias) / 256.0;
        break;
    case GR_FOG_WITH_TABLE:
        f = mg_fog_table_at(v->oow);
        break;
    default:
        return 0;
    }
    return f < 0 ? 0 : (f > 255 ? 255 : f);
}

/* FOGCOL for the current mode: ADD2 drops the fog colour term. */
void mg_fog_emit_color(void)
{
    uint32_t c = (mg.st.fog_mode & GR_FOG_ADD2) ? 0 : (mg_color_to_argb(mg.st.fog_color) & 0xFFFFFF);
    if (mg.fogcol_valid && c == mg.fogcol_hw)
        return;
    fifo_reserve(1);
    MGA_WR32(MGAREG_FOGCOL, c);
    mg.fogcol_hw = c;
    mg.fogcol_valid = 1;
}

/* ---- Utility fog tables ------------------------------------------------ */

GR_ENTRY(float, guFogTableIndexToW, (int i))
{
    /* Entry i covers W codes with exponent i/4 and top mantissa bits i%4. */
    return (float)((double)(1u << (3 + (i >> 2))) / (double)(8 - (i & 3)));
}

/* Retail computes in single precision: 1 - e^-x rounds to exactly 1.0
 * (255) once e^-x drops below half a float ulp of 1. */
static GrFog_t fog_byte(double f)
{
    volatile float ff = (float)f;
    if (ff < 0.0f) ff = 0.0f;
    if (ff > 1.0f) ff = 1.0f;
    return (GrFog_t)(ff * 255.0f);
}

GR_ENTRY(void, guFogGenerateExp, (GrFog_t fogtable[], float density))
{
    int i;
    FPU_ENTER();
    for (i = 0; i < GR_FOG_TABLE_SIZE; i++)
        fogtable[i] = fog_byte(1.0 - mg_exp(-(double)density * guFogTableIndexToW(i)));
    FPU_LEAVE();
}

GR_ENTRY(void, guFogGenerateExp2, (GrFog_t fogtable[], float density))
{
    int i;
    FPU_ENTER();
    for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        double d = (double)density * guFogTableIndexToW(i);
        fogtable[i] = fog_byte(1.0 - mg_exp(-d * d));
    }
    FPU_LEAVE();
}

GR_ENTRY(void, guFogGenerateLinear, (GrFog_t fogtable[], float nearZ, float farZ))
{
    int i;
    FPU_ENTER();
    for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        double w = guFogTableIndexToW(i);
        fogtable[i] = farZ > nearZ ? fog_byte((w - nearZ) / ((double)farZ - nearZ)) : (GrFog_t)(w >= farZ ? 255 : 0);
    }
    FPU_LEAVE();
}
