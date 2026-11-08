/* texstate.c - TMU sampling and combine state, preset functions, and the
 * gu* texture allocator (bump allocation over the reported TMU memory,
 * matching the retail runtime). */
#include "glide/mg.h"
#include "trace/trace.h"
#include "tex/texmgr.h"
#include "tex/texfmt.h"
#include <string.h>

GR_ENTRY(void, grTexFilterMode, (GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode,
                                 GrTextureFilterMode_t magfilter_mode))
{
    if (tmu == GR_TMU0) { tmu0.minf = minfilter_mode; tmu0.magf = magfilter_mode; }
}

GR_ENTRY(void, grTexClampMode, (GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode))
{
    if (tmu == GR_TMU0) { tmu0.clamp_s = s_clampmode; tmu0.clamp_t = t_clampmode; }
}

GR_ENTRY(void, grTexMipMapMode, (GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend))
{
    if (tmu == GR_TMU0) { tmu0.mipmap = mode; tmu0.lod_blend = lodBlend; }
}

GR_ENTRY(void, grTexLodBiasValue, (GrChipID_t tmu, float bias))
{
    if (tmu == GR_TMU0) tmu0.lod_bias = bias;
}

GR_ENTRY(void, grTexCombine, (GrChipID_t tmu, GrCombineFunction_t rgb_function, GrCombineFactor_t rgb_factor,
                              GrCombineFunction_t alpha_function, GrCombineFactor_t alpha_factor,
                              FxBool rgb_invert, FxBool alpha_invert))
{
    if (tmu != GR_TMU0)
        return;
    tmu0.rgb_func = rgb_function; tmu0.rgb_factor = rgb_factor;
    tmu0.alpha_func = alpha_function; tmu0.alpha_factor = alpha_factor;
    tmu0.rgb_invert = rgb_invert; tmu0.alpha_invert = alpha_invert;
    tmu0.combine_set = 1;
}

GR_ENTRY(void, grTexCombineFunction, (GrChipID_t tmu, GrTextureCombineFnc_t fnc))
{
    switch (fnc) {
    case GR_TEXTURECOMBINE_ZERO:
        grTexCombine(tmu, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_ZERO,
                     GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_ONE:
        grTexCombine(tmu, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_ZERO,
                     GR_COMBINE_FACTOR_NONE, FXTRUE, FXTRUE);
        break;
    case GR_TEXTURECOMBINE_OTHER:
        grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_FUNCTION_SCALE_OTHER,
                     GR_COMBINE_FACTOR_ONE, FXFALSE, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_ADD:
        grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE, FXFALSE, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_MULTIPLY:
        grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_FUNCTION_SCALE_OTHER,
                     GR_COMBINE_FACTOR_LOCAL_ALPHA, FXFALSE, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_SUBTRACT:
        grTexCombine(tmu, GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE, FXFALSE, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_DETAIL:
    case GR_TEXTURECOMBINE_DETAIL_OTHER:
    case GR_TEXTURECOMBINE_TRILINEAR_ODD:
    case GR_TEXTURECOMBINE_TRILINEAR_EVEN:
        mg_log(MG_LOG_DEBUG, "texcombine preset %d approximated as decal", (int)fnc);
        /* fall through */
    case GR_TEXTURECOMBINE_DECAL:
    default:
        grTexCombine(tmu, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL,
                     GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
        break;
    }
}

GR_ENTRY(void, guTexCombineFunction, (GrChipID_t tmu, GrTextureCombineFnc_t fnc))
{
    grTexCombineFunction(tmu, fnc);
}

GR_ENTRY(void, grTexDetailControl, (GrChipID_t tmu, int lod_bias, FxU8 detail_scale, float detail_max))
{
    MGA_UNUSED(tmu); MGA_UNUSED(lod_bias); MGA_UNUSED(detail_scale); MGA_UNUSED(detail_max);
    mg_stub_hit(MGA_API_grTexDetailControl);
}

GR_ENTRY(void, grTexMultibase, (GrChipID_t tmu, FxBool enable))
{
    MGA_UNUSED(tmu);
    if (enable)
        mg_stub_hit(MGA_API_grTexMultibase);
}

/* ---- Combine presets (Glide 2 utility layer) ---------------------------- */

GR_ENTRY(void, guColorCombineFunction, (GrColorCombineFnc_t fnc))
{
#define CC(f, fa, l, o, inv) grColorCombine(GR_COMBINE_FUNCTION_##f, GR_COMBINE_FACTOR_##fa, \
                                            GR_COMBINE_LOCAL_##l, GR_COMBINE_OTHER_##o, inv)
    switch (fnc) {
    case GR_COLORCOMBINE_ZERO: CC(ZERO, NONE, ITERATED, ITERATED, FXFALSE); break;
    case GR_COLORCOMBINE_CCRGB: CC(LOCAL, NONE, CONSTANT, NONE, FXFALSE); break;
    case GR_COLORCOMBINE_ITRGB:
    case GR_COLORCOMBINE_ITRGB_DELTA0: CC(LOCAL, NONE, ITERATED, NONE, FXFALSE); break;
    case GR_COLORCOMBINE_DECAL_TEXTURE: CC(SCALE_OTHER, ONE, NONE, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_CCRGB: CC(SCALE_OTHER, LOCAL, CONSTANT, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB:
    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0: CC(SCALE_OTHER, LOCAL, ITERATED, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_ADD_ALPHA:
        CC(SCALE_OTHER_ADD_LOCAL_ALPHA, LOCAL, ITERATED, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA: CC(SCALE_OTHER, LOCAL_ALPHA, NONE, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA_ADD_ITRGB:
        CC(SCALE_OTHER_ADD_LOCAL, LOCAL_ALPHA, ITERATED, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_TEXTURE_ADD_ITRGB: CC(SCALE_OTHER_ADD_LOCAL, ONE, ITERATED, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_TEXTURE_SUB_ITRGB: CC(SCALE_OTHER_MINUS_LOCAL, ONE, ITERATED, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_CCRGB_BLEND_ITRGB_ON_TEXALPHA:
        CC(SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL, TEXTURE_ALPHA, ITERATED, CONSTANT, FXFALSE); break;
    /* Measured from the retail runtime (t13): A = texel x alpha + iterated,
     * B = texel x iterated + alpha. */
    case GR_COLORCOMBINE_DIFF_SPEC_A: CC(SCALE_OTHER_ADD_LOCAL, LOCAL_ALPHA, ITERATED, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_DIFF_SPEC_B: CC(SCALE_OTHER_ADD_LOCAL_ALPHA, LOCAL, ITERATED, TEXTURE, FXFALSE); break;
    case GR_COLORCOMBINE_ONE: CC(ZERO, NONE, ITERATED, ITERATED, FXTRUE); break;
    default: break;
    }
    /* DELTA0: the iterated colour is flat, taken from grConstantColorValue4. */
    mg.st.delta0 = fnc == GR_COLORCOMBINE_ITRGB_DELTA0 || fnc == GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0;
#undef CC
}

GR_ENTRY(void, guAlphaSource, (GrAlphaSource_t mode))
{
    switch (mode) {
    case GR_ALPHASOURCE_CC_ALPHA:
        grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE, FXFALSE); break;
    case GR_ALPHASOURCE_ITERATED_ALPHA:
        grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                       GR_COMBINE_OTHER_NONE, FXFALSE); break;
    case GR_ALPHASOURCE_TEXTURE_ALPHA:
        grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                       GR_COMBINE_OTHER_TEXTURE, FXFALSE); break;
    case GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA:
        grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                       GR_COMBINE_OTHER_TEXTURE, FXFALSE); break;
    default: break;
    }
}

/* ---- gu* texture allocator -------------------------------------------- */
#define GU_MAX 512
static GrMipMapInfo gu_info[GU_MAX];
static GuNccTable gu_ncc[GU_MAX];
static int gu_count, gu_current = -1;
static uint32_t gu_next;

GR_ENTRY(void, guTexMemReset, (void))
{
    gu_count = 0;
    gu_next = 0;
    gu_current = -1;
}

GR_ENTRY(FxU32, guTexMemQueryAvail, (GrChipID_t tmu))
{
    MGA_UNUSED(tmu);
    return (FxU32)mg_config.report_tmu_mb * 1024u * 1024u - gu_next;
}

static void gu_fill(GrMipMapInfo *m, int width, int height, GrTextureFormat_t fmt, GrMipMapMode_t mm_mode,
                    GrLOD_t smallest_lod, GrLOD_t largest_lod, GrAspectRatio_t aspect,
                    GrTextureClampMode_t s_clamp, GrTextureClampMode_t t_clamp,
                    GrTextureFilterMode_t minf, GrTextureFilterMode_t magf)
{
    m->width = width; m->height = height; m->format = fmt; m->mipmap_mode = mm_mode;
    m->lod_min = smallest_lod; m->lod_max = largest_lod; m->aspect_ratio = aspect;
    m->s_clamp_mode = s_clamp; m->t_clamp_mode = t_clamp;
    m->minfilter_mode = minf; m->magfilter_mode = magf;
}

GR_ENTRY(GrMipMapId_t, guTexAllocateMemory, (GrChipID_t tmu, FxU8 odd_even_mask, int width, int height,
                                             GrTextureFormat_t fmt, GrMipMapMode_t mm_mode, GrLOD_t smallest_lod,
                                             GrLOD_t largest_lod, GrAspectRatio_t aspect,
                                             GrTextureClampMode_t s_clamp_mode, GrTextureClampMode_t t_clamp_mode,
                                             GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode,
                                             float lod_bias, FxBool trilinear))
{
    GrMipMapInfo *m;
    uint32_t need = tex_mem_required(smallest_lod, largest_lod, aspect, fmt, odd_even_mask);
    if (gu_count >= GU_MAX || gu_next + need > (uint32_t)mg_config.report_tmu_mb * 1024u * 1024u)
        return GR_NULL_MIPMAP_HANDLE;
    m = &gu_info[gu_count];
    memset(m, 0, sizeof *m);
    m->sst = 0;
    m->valid = FXTRUE;
    m->tmu = tmu;
    m->odd_even_mask = odd_even_mask;
    m->tmu_base_address = gu_next;
    m->trilinear = trilinear;
    m->lod_bias = (FxU32)(int)(lod_bias * 4.0f);
    gu_fill(m, width, height, fmt, mm_mode, smallest_lod, largest_lod, aspect, s_clamp_mode, t_clamp_mode,
            minfilter_mode, magfilter_mode);
    gu_next += need;
    return (GrMipMapId_t)gu_count++;
}

GR_ENTRY(FxBool, guTexChangeAttributes, (GrMipMapId_t mmid, int width, int height, GrTextureFormat_t fmt,
                                         GrMipMapMode_t mm_mode, GrLOD_t smallest_lod, GrLOD_t largest_lod,
                                         GrAspectRatio_t aspect, GrTextureClampMode_t s_clamp_mode,
                                         GrTextureClampMode_t t_clamp_mode, GrTextureFilterMode_t minFilterMode,
                                         GrTextureFilterMode_t magFilterMode))
{
    if ((int)mmid < 0 || (int)mmid >= gu_count)
        return FXFALSE;
    gu_fill(&gu_info[mmid], width, height, fmt, mm_mode, smallest_lod, largest_lod, aspect, s_clamp_mode,
            t_clamp_mode, minFilterMode, magFilterMode);
    return FXTRUE;
}

static void gu_texinfo(const GrMipMapInfo *m, GrTexInfo *ti, const void *data)
{
    ti->smallLod = m->lod_min;
    ti->largeLod = m->lod_max;
    ti->aspectRatio = m->aspect_ratio;
    ti->format = m->format;
    ti->data = (void *)data;
}

GR_ENTRY(void, guTexDownloadMipMap, (GrMipMapId_t mmid, const void *src, const GuNccTable *table))
{
    GrTexInfo ti;
    if ((int)mmid < 0 || (int)mmid >= gu_count)
        return;
    if (table)
        gu_ncc[mmid] = *table;
    gu_texinfo(&gu_info[mmid], &ti, src);
    grTexDownloadMipMap(gu_info[mmid].tmu, gu_info[mmid].tmu_base_address, gu_info[mmid].odd_even_mask, &ti);
}

GR_ENTRY(void, guTexDownloadMipMapLevel, (GrMipMapId_t mmid, GrLOD_t lod, const void **src))
{
    const GrMipMapInfo *m;
    if ((int)mmid < 0 || (int)mmid >= gu_count || !src)
        return;
    m = &gu_info[mmid];
    grTexDownloadMipMapLevel(m->tmu, m->tmu_base_address, lod, m->lod_max, m->aspect_ratio, m->format,
                             m->odd_even_mask, (void *)*src);
    *src = (const uint8_t *)*src + tex_level_data_bytes(lod, m->aspect_ratio, m->format);
}

GR_ENTRY(void, guTexSource, (GrMipMapId_t id))
{
    GrTexInfo ti;
    const GrMipMapInfo *m;
    if ((int)id < 0 || (int)id >= gu_count)
        return;
    m = &gu_info[id];
    gu_current = (int)id;
    grTexClampMode(m->tmu, m->s_clamp_mode, m->t_clamp_mode);
    grTexFilterMode(m->tmu, m->minfilter_mode, m->magfilter_mode);
    grTexMipMapMode(m->tmu, m->mipmap_mode, m->trilinear);
    grTexLodBiasValue(m->tmu, (float)(int)m->lod_bias / 4.0f);
    if (m->format == GR_TEXFMT_YIQ_422 || m->format == GR_TEXFMT_AYIQ_8422) {
        grTexDownloadTable(m->tmu, GR_TEXTABLE_NCC0, (void *)&gu_ncc[id]);
        grTexNCCTable(m->tmu, GR_NCCTABLE_NCC0);
    }
    gu_texinfo(m, &ti, NULL);
    grTexSource(m->tmu, m->tmu_base_address, m->odd_even_mask, &ti);
}

GR_ENTRY(GrMipMapId_t, guTexGetCurrentMipMap, (GrChipID_t tmu))
{
    MGA_UNUSED(tmu);
    return gu_current < 0 ? GR_NULL_MIPMAP_HANDLE : (GrMipMapId_t)gu_current;
}

GR_ENTRY(GrMipMapInfo *, guTexGetMipMapInfo, (GrMipMapId_t mmid))
{
    return ((int)mmid < 0 || (int)mmid >= gu_count) ? NULL : &gu_info[mmid];
}

GR_ENTRY(FxU16 *, guTexCreateColorMipMap, (void))
{
    mg_stub_hit(MGA_API_guTexCreateColorMipMap);
    return NULL;
}

/* Source sizes of gu mipmap downloads, for the call tracer. */
uint32_t tr_gu_src_bytes(GrMipMapId_t mmid)
{
    const GrMipMapInfo *m;
    uint32_t n = 0;
    int l;
    if ((int)mmid < 0 || (int)mmid >= gu_count)
        return 0;
    m = &gu_info[mmid];
    for (l = m->lod_max; l <= m->lod_min; l++)
        n += tex_level_data_bytes((GrLOD_t)l, m->aspect_ratio, m->format);
    return n;
}

uint32_t tr_gu_level_bytes(GrMipMapId_t mmid, GrLOD_t lod)
{
    if ((int)mmid < 0 || (int)mmid >= gu_count)
        return 0;
    return tex_level_data_bytes(lod, gu_info[mmid].aspect_ratio, gu_info[mmid].format);
}
