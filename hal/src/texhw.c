/* texhw.c - sampler registers and coordinate conditioning for TEXTURE_TRAP. */
#include "mga/tex.h"
#include "mga/hal.h"
#include "mga/fp.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

static void emit_sampler(const mga_texstate *t, uint32_t texctl2_extra, int map1_only);

void tex_emit(const mga_texstate *t)
{
    emit_sampler(t, 0, 0);
}

void tex_emit_dual(const mga_texstate *t0, const mga_texstate *t1)
{
    emit_sampler(t0, TEXCTL2_DUALTEX, 0);               /* both maps */
    emit_sampler(t1, TEXCTL2_DUALTEX, 1);               /* then map 1 alone */
}

void tex_emit_map1(const mga_texstate *t1)
{
    emit_sampler(t1, TEXCTL2_DUALTEX, 1);               /* tex_emit_dual's second half */
}

void tex_emit_combiner(uint32_t stage0, uint32_t stage1)
{
    fifo_reserve(2);
    MGA_WR32(MGAREG_TDUALSTAGE0, stage0);
    MGA_WR32(MGAREG_TDUALSTAGE1, stage1);
}

static uint32_t texctl_for(const mga_texstate *t)
{
    uint32_t texctl = t->hwfmt | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT((uint32_t)t->pitch & 0x7FF);
    /* takey=1, tamask=0: texel alpha never keys; tamask=1, takey=0: alpha 0
     * is transparent. */
    texctl |= t->key_alpha0 ? (TEXCTL_STRANS | TEXCTL_TAMASK) : TEXCTL_TAKEY;
    if (t->clamp_u) texctl |= TEXCTL_CLAMPU;
    if (t->clamp_v) texctl |= TEXCTL_CLAMPV;
    if (t->modulate) texctl |= TEXCTL_TMODULATE;
    return texctl;
}

void tex_emit_change(const mga_texstate *t, const mga_texstate *prev)
{
    const uint32_t texctl = texctl_for(t);
    if (t->mip_n > 1 || prev->mip_n > 1 || t->bilinear != prev->bilinear || t->texctl2 != prev->texctl2) {
        tex_emit(t);
        return;
    }
    fifo_reserve(2);
    if (t->org != prev->org)
        MGA_WR32(MGAREG_TEXORG, t->org);
    if (texctl != texctl_for(prev))
        MGA_WR32(MGAREG_TEXCTL, texctl);
}

/* One map's sampler. For map 1 the first TEXCTL2 write carries the map1 bit,
 * which takes effect from the next write (the specification's tmap0dis), so
 * that write reaches both maps: both get dualtex, as they must. The last
 * write clears the bit, still reaching map 1 only, and broadcast resumes. */
static void emit_sampler(const mga_texstate *t, uint32_t texctl2_extra, int map1_only)
{
    static const uint32_t orgreg[4] = { MGAREG_TEXORG1, MGAREG_TEXORG2, MGAREG_TEXORG3, MGAREG_TEXORG4 };
    const uint32_t texctl = texctl_for(t);
    uint32_t filt = t->bilinear ? TEXFILTER_BILIN : TEXFILTER_NRST;
    int k;
    fifo_reserve(9);
    if (map1_only)
        MGA_WR32(MGAREG_TEXCTL2, t->texctl2 | texctl2_extra | TEXCTL2_MAP1);
    MGA_WR32(MGAREG_TEXORG, t->org);
    MGA_WR32(MGAREG_TEXCTL, texctl);
    if (mga.has_texctl2 && !map1_only)
        MGA_WR32(MGAREG_TEXCTL2, t->texctl2 | texctl2_extra);
    if (t->mip_n > 1) {
        /* G200 window: levels 1..n-1 at TEXORG1..; the chip picks the level
         * per pixel. */
        for (k = 1; k < t->mip_n; k++)
            MGA_WR32(orgreg[k - 1], t->mip_org[k]);
        MGA_WR32(MGAREG_TEXFILTER,
                 TEXFILTER_MIN(t->trilinear ? (t->bilinear ? TEXFILTER_MM8S : TEXFILTER_MM4S)
                                            : (t->bilinear ? TEXFILTER_MM2S : TEXFILTER_MM1S)) |
                 TEXFILTER_MAG(filt) | TEXFILTER_FILTERALPHA | TEXFILTER_FTHRES(0x10) |
                 TEXFILTER_MAPNB((uint32_t)(t->mip_n - 1)));
    } else
        MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(filt) | TEXFILTER_MAG(filt));
    MGA_WR32(MGAREG_TEXTRANS, 0x0000FFFFu);
    if (map1_only) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_TEXCTL2, t->texctl2 | texctl2_extra);
    }
}

static void adjust(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t, int map1);

void tex_adjust_coords(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t)
{
    adjust(a, b, c, t, 0);
}

void tex_adjust_coords1(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t)
{
    adjust(a, b, c, t, 1);
}

static void adjust(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t, int map1)
{
    mga_svtx *v[3];
    int i, axis;
    v[0] = a; v[1] = b; v[2] = c;
    for (axis = 0; axis < 2; axis++) {
        int wrap = axis ? !t->clamp_v : !t->clamp_u;
        double shift = 0, half = 0;
        if (t->bilinear && !mga.texel_centre) {   /* GL's texel centres on a chip that filters from corners */
            half = 0.5 / (double)(1 << (axis ? t->h_log2 : t->w_log2));
            shift -= half;
        }
        if (wrap) {
            /* The smallest coordinate only matters here. With every q
             * positive it is found by cross-multiplying (exact: a product
             * of two floats fits a double), then one divide: the same value
             * as dividing all three. */
            double mn = 1e30;
            if (v[0]->q > 0 && v[1]->q > 0 && v[2]->q > 0) {
                int best = -1;
                for (i = 0; i < 3; i++) {
                    double si = axis ? (map1 ? v[i]->t1 : v[i]->t) : (map1 ? v[i]->s1 : v[i]->s);
                    if (si != si)
                        continue;       /* (a NaN never compares smaller) */
                    if (best < 0 || si * (double)v[best]->q <
                        (double)(axis ? (map1 ? v[best]->t1 : v[best]->t) : (map1 ? v[best]->s1 : v[best]->s)) *
                        (double)v[i]->q)
                        best = i;
                }
                if (best >= 0) {
                    double u = (double)(axis ? (map1 ? v[best]->t1 : v[best]->t) : (map1 ? v[best]->s1 : v[best]->s)) /
                               (double)v[best]->q;
                    if (u < mn) mn = u;
                }
            } else
                for (i = 0; i < 3; i++) {
                    double q = v[i]->q > 0 ? v[i]->q : 1e-9;
                    double u = (axis ? (map1 ? v[i]->t1 : v[i]->t) : (map1 ? v[i]->s1 : v[i]->s)) / q;
                    if (u < mn) mn = u;
                }
            if (t->bilinear)
                mn -= half;
            shift -= mga_floor(mn);         /* minimum into [0, 1): whole periods only */
        }
        for (i = 0; i < 3; i++) {
            float *p = axis ? (map1 ? &v[i]->t1 : &v[i]->t) : (map1 ? &v[i]->s1 : &v[i]->s);
            *p += (float)(shift * v[i]->q);     /* also when shift is 0: a NaN q makes it NaN */
        }
    }
}
