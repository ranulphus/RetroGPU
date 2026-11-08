/* state.c - the Glide state mirror and its translation to MGA registers. */
#include "glide/mg.h"
#include "tex/texmgr.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include <string.h>

void mg_state_defaults(void)
{
    mg_state *s = &mg.st;
    int i;
    memset(s, 0, sizeof *s);
    s->cc_func = GR_COMBINE_FUNCTION_SCALE_OTHER; s->cc_factor = GR_COMBINE_FACTOR_ONE;
    s->cc_local = GR_COMBINE_LOCAL_ITERATED; s->cc_other = GR_COMBINE_OTHER_ITERATED;
    s->ac_func = GR_COMBINE_FUNCTION_SCALE_OTHER; s->ac_factor = GR_COMBINE_FACTOR_ONE;
    s->ac_local = GR_COMBINE_LOCAL_ITERATED; s->ac_other = GR_COMBINE_OTHER_ITERATED;
    s->constant_color = 0xFFFFFFFFu;
    s->blend_src = GR_BLEND_ONE; s->blend_dst = GR_BLEND_ZERO;
    s->blend_asrc = GR_BLEND_ONE; s->blend_adst = GR_BLEND_ZERO;
    s->alpha_test_func = GR_CMP_ALWAYS;
    s->depth_mode = GR_DEPTHBUFFER_DISABLE;
    s->depth_func = GR_CMP_LESS;
    s->depth_mask = FXFALSE;
    s->cull = GR_CULL_DISABLE;
    s->dither = GR_DITHER_2x2;
    s->color_mask_rgb = FXTRUE; s->color_mask_a = FXTRUE;
    s->clip_x0 = 0; s->clip_y0 = 0; s->clip_x1 = 640; s->clip_y1 = 480;
    s->origin = GR_ORIGIN_UPPER_LEFT;
    s->fog_mode = GR_FOG_DISABLE;
    s->chroma_mode = GR_CHROMAKEY_DISABLE;
    for (i = 0; i < GR_FOG_TABLE_SIZE; i++)
        s->fog_table[i] = 0;
    mg.dirty = ~0u;
}

/* GrColor_t in the format chosen at grSstWinOpen -> 0xAARRGGBB. */
uint32_t mg_color_to_argb(GrColor_t c)
{
    switch (mg.color_format) {
    case GR_COLORFORMAT_ABGR:
        return (c & 0xFF00FF00u) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
    case GR_COLORFORMAT_RGBA:
        return (c >> 8) | (c << 24);
    case GR_COLORFORMAT_BGRA:
        return ((c & 0xFF) << 24) | ((c >> 24) & 0xFF) | ((c >> 8) & 0xFF00) | ((c << 8) & 0xFF0000);
    default:
        return c;
    }
}

/* Inverse of mg_color_to_argb (each mapping is its own inverse except
 * RGBA, a rotation). */
GrColor_t mg_argb_to_color(uint32_t c)
{
    switch (mg.color_format) {
    case GR_COLORFORMAT_ABGR:
        return (c & 0xFF00FF00u) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
    case GR_COLORFORMAT_RGBA:
        return (c << 8) | (c >> 24);
    case GR_COLORFORMAT_BGRA:
        return ((c & 0xFF) << 24) | ((c >> 24) & 0xFF) | ((c >> 8) & 0xFF00) | ((c << 8) & 0xFF0000);
    default:
        return c;
    }
}

uint32_t mg_argb_to_565(uint32_t argb)
{
    return ((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) | ((argb >> 3) & 0x001F);
}

uint32_t mg_buffer_offset(GrBuffer_t b)
{
    switch (b) {
    case GR_BUFFER_FRONTBUFFER: return mg.buf_off[mg.front];
    case GR_BUFFER_AUXBUFFER: return mg.aux_off;
    default: return mg.buf_off[mg.back];
    }
}

void mg_target_for(GrBuffer_t b)
{
    mga_target t;
    t.color_off = mg_buffer_offset(b);
    t.z_off = mg.aux_off;
    t.pitch_px = mg.pitch_px;
    t.bpp = mg.bpp;
    t.zbits = mg.zbits;
    engine_set_target(&t);
}

void mg_validate(void)
{
    if (!mg.open)
        return;
    if (mg.dirty & MG_DIRTY_TARGET)
        mg_target_for(mg.render_buffer);
    if (mg.dirty & (MG_DIRTY_TARGET | MG_DIRTY_CLIP)) {
        int y0 = mg.st.clip_y0, y1 = mg.st.clip_y1;
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
            y0 = mg.height - mg.st.clip_y1;
            y1 = mg.height - mg.st.clip_y0;
        }
        engine_set_clip(mg_hx(mg.st.clip_x0), mg_hy(y0), mg_hx(mg.st.clip_x1), mg_hy(y1));
    }
    engine_set_maccess_flags((mg.st.dither == GR_DITHER_DISABLE ? MACCESS_NODITHER : 0) |
                             (mg.fog_on ? MACCESS_FOGEN : 0));
    mg.dirty = 0;
}

/* ---- State entry points ------------------------------------------------ */

GR_ENTRY(void, grColorCombine, (GrCombineFunction_t function, GrCombineFactor_t factor,
                                GrCombineLocal_t local, GrCombineOther_t other, FxBool invert))
{
    mg.st.delta0 = FXFALSE;         /* the gu DELTA0 presets set it again afterwards */
    mg.st.cc_func = function; mg.st.cc_factor = factor; mg.st.cc_local = local;
    mg.st.cc_other = other; mg.st.cc_invert = invert;
}

GR_ENTRY(void, grAlphaCombine, (GrCombineFunction_t function, GrCombineFactor_t factor,
                                GrCombineLocal_t local, GrCombineOther_t other, FxBool invert))
{
    mg.st.ac_func = function; mg.st.ac_factor = factor; mg.st.ac_local = local;
    mg.st.ac_other = other; mg.st.ac_invert = invert;
}

GR_ENTRY(void, grConstantColorValue, (GrColor_t value)) { mg.st.constant_color = value; }

GR_ENTRY(void, grAlphaBlendFunction, (GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df,
                                      GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df))
{
    mg.st.blend_src = rgb_sf; mg.st.blend_dst = rgb_df;
    mg.st.blend_asrc = alpha_sf; mg.st.blend_adst = alpha_df;
}

GR_ENTRY(void, grAlphaTestFunction, (GrCmpFnc_t function)) { mg.st.alpha_test_func = function; }
GR_ENTRY(void, grAlphaTestReferenceValue, (GrAlpha_t value)) { mg.st.alpha_test_ref = value; }

GR_ENTRY(void, grDepthBufferMode, (GrDepthBufferMode_t mode)) { mg.st.depth_mode = mode; }
GR_ENTRY(void, grDepthBufferFunction, (GrCmpFnc_t function)) { mg.st.depth_func = function; }
GR_ENTRY(void, grDepthMask, (FxBool mask)) { mg.st.depth_mask = mask; }
GR_ENTRY(void, grDepthBiasLevel, (FxI16 level)) { mg.st.depth_bias = level; }

GR_ENTRY(void, grCullMode, (GrCullMode_t mode)) { mg.st.cull = mode; }

GR_ENTRY(void, grDitherMode, (GrDitherMode_t mode))
{
    mg.st.dither = mode;
    mg.dirty |= MG_DIRTY_RASTER;
}

GR_ENTRY(void, grColorMask, (FxBool rgb, FxBool a))
{
    mg.st.color_mask_rgb = rgb;
    mg.st.color_mask_a = a;
}

GR_ENTRY(void, grClipWindow, (FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy))
{
    mg.st.clip_x0 = (int)minx; mg.st.clip_y0 = (int)miny;
    mg.st.clip_x1 = (int)maxx; mg.st.clip_y1 = (int)maxy;
    if (mg.st.clip_x1 > mg.width) mg.st.clip_x1 = mg.width;
    if (mg.st.clip_y1 > mg.height) mg.st.clip_y1 = mg.height;
    mg.dirty |= MG_DIRTY_CLIP;
}

GR_ENTRY(void, grFogMode, (GrFogMode_t mode)) { mg.st.fog_mode = mode; }
GR_ENTRY(void, grFogColorValue, (GrColor_t fogcolor)) { mg.st.fog_color = fogcolor; }
GR_ENTRY(void, grFogTable, (const GrFog_t ft[GR_FOG_TABLE_SIZE]))
{
    memcpy(mg.st.fog_table, ft, GR_FOG_TABLE_SIZE);
}

GR_ENTRY(void, grChromakeyMode, (GrChromakeyMode_t mode)) { mg.st.chroma_mode = mode; }
GR_ENTRY(void, grChromakeyValue, (GrColor_t value)) { mg.st.chroma_value = value; }

GR_ENTRY(void, grHints, (GrHint_t hintType, FxU32 hintMask))
{
    if (hintType == GR_HINT_STWHINT)
        mg.st.stw_hint = hintMask;
}

/* GrState is opaque to games: it holds the core state and TMU0's source,
 * sampling and combine settings. */
typedef struct {
    mg_state  st;
    tex_saved tex;
} mg_saved_state;

typedef char mg_saved_state_fits[sizeof(mg_saved_state) <= sizeof(GrState) ? 1 : -1];

GR_ENTRY(void, grGlideGetState, (GrState *state))
{
    mg_saved_state *sv = (mg_saved_state *)state;
    memset(state, 0, sizeof *state);
    sv->st = mg.st;
    tex_save(&sv->tex);
}

GR_ENTRY(void, grGlideSetState, (const GrState *state))
{
    const mg_saved_state *sv = (const mg_saved_state *)state;
    mg.st = sv->st;
    tex_restore(&sv->tex);
    mg.dirty = ~0u;
}
