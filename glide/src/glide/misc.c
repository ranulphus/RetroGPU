/* misc.c - small Tier-2 entry points: float constant colour, effect reset,
 * triangle statistics, the splash screen, polygon clipping, byte swaps. */
#include "glide/mg.h"

static uint32_t chan(float v)
{
    return v <= 0.0f ? 0u : (v >= 255.0f ? 255u : (uint32_t)(v + 0.5f));
}

/* The colour the DELTA0 combine presets use as a flat "iterated" colour
 * (black until set, as the retail runtime shows in t13). */
GR_ENTRY(void, grConstantColorValue4, (float a, float r, float g, float b))
{
    FPU_ENTER();
    mg.st.delta0_argb = (chan(a) << 24) | (chan(r) << 16) | (chan(g) << 8) | chan(b);
    FPU_LEAVE();
}

GR_ENTRY(void, grDisableAllEffects, (void))
{
    mg.st.blend_src = GR_BLEND_ONE;  mg.st.blend_dst = GR_BLEND_ZERO;
    mg.st.blend_asrc = GR_BLEND_ONE; mg.st.blend_adst = GR_BLEND_ZERO;
    mg.st.alpha_test_func = GR_CMP_ALWAYS;
    mg.st.chroma_mode = GR_CHROMAKEY_DISABLE;
    mg.st.depth_mode = GR_DEPTHBUFFER_DISABLE;
    mg.st.fog_mode = GR_FOG_DISABLE;
}

GR_ENTRY(void, grAlphaControlsITRGBLighting, (FxBool enable))
{
    /* Texture alpha's top bit choosing iterated vs constant local colour
     * has no Matrox equivalent; the flag is kept for GrState and the
     * combine plan treats local colour as iterated. */
    if (enable && !mg.st.alpha_itrgb)
        mg_log(MG_LOG_WARN, "MGA-DEGRADE alpha-controls-itrgb approx");
    mg.st.alpha_itrgb = enable;
}

GR_ENTRY(void, grResetTriStats, (void))
{
    mg_stats.tri_processed = mg_stats.tri_drawn = 0;
}

GR_ENTRY(void, grTriStats, (FxU32 *trisProcessed, FxU32 *trisDrawn))
{
    if (trisProcessed) *trisProcessed = mg_stats.tri_processed;
    if (trisDrawn) *trisDrawn = mg_stats.tri_drawn;
}

/* The 3dfx splash animation is 3dfx's artwork; MGA-Glide draws nothing. */
GR_ENTRY(void, grSplash, (float x, float y, float width, float height, FxU32 frame))
{
    MGA_UNUSED(x); MGA_UNUSED(y); MGA_UNUSED(width); MGA_UNUSED(height); MGA_UNUSED(frame);
}

GR_ENTRY(FxU16, guEndianSwapBytes, (FxU16 value))
{
    return (FxU16)((value >> 8) | (value << 8));
}

GR_ENTRY(FxU32, guEndianSwapWords, (FxU32 value))
{
    return (value >> 16) | (value << 16);
}

/* ---- Tier 3: hardware controls with nothing to do on the Matrox --------- */

/* Pass-through control (activate / deactivate the 3D card's output). The
 * Matrox is the only display, so there is nothing to switch; report
 * success so games carry on. */
GR_ENTRY(FxBool, grSstControl, (FxU32 code))
{
    mg_log(MG_LOG_DEBUG, "grSstControl(%u): single display, ignored", code);
    return FXTRUE;
}

/* Pixel counters are not available: report zeros rather than garbage. */
GR_ENTRY(void, grSstPerfStats, (GrSstPerfStats_t *pStats))
{
    if (pStats)
        pStats->pixelsIn = pStats->chromaFail = pStats->zFuncFail = pStats->aFuncFail = pStats->pixelsOut = 0;
}

GR_ENTRY(void, grSstResetPerfStats, (void)) { }
GR_ENTRY(void, grCheckForRoom, (FxI32 n)) { MGA_UNUSED(n); }
GR_ENTRY(void, grGlideShamelessPlug, (const FxBool on)) { MGA_UNUSED(on); }

GR_ENTRY(void, grSstConfigPipeline, (GrChipID_t chip, FxU32 reg, FxU32 value))
{
    mg_log(MG_LOG_WARN, "grSstConfigPipeline(%d, %x, %x): Voodoo register write ignored", (int)chip, reg, value);
}

GR_ENTRY(void, grSstVidMode, (FxU32 whichSst, void *vidTimings))
{
    MGA_UNUSED(whichSst);
    if (vidTimings)
        mg_log(MG_LOG_WARN, "grSstVidMode: custom video timings ignored");
}
