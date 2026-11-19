/* eval.c - Glide's colour and alpha combine units evaluated at a vertex, for any back end (moved from the Matrox
 * back end's combine.c: the plans' CS_SOFT, CS_SOFT_FACTOR, AS_SOFT forms and the specular's local colour). */
#include "glide/mg.h"
#include "combine/eval.h"

/* ---- The Voodoo combine units, evaluated at a vertex ----------------------
 * out = f(func, factor, local, other), each value 0..255, clamped, then
 * optionally inverted. Glide's "local alpha" and "other alpha" are the
 * alpha combine's local and other selections. Interpolating per-vertex
 * results reproduces the per-pixel Voodoo result whenever the expression
 * is affine in the iterated values (no product of two iterated terms). */

static float clampf(float v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static float depth_local(const GrVertex *v) { return clampf((v->ooz + (float)mg.st.depth_bias) / 256.0f); }

static float alpha_sel_local(const GrVertex *v)
{
    switch (mg.st.ac_local) {
    case GR_COMBINE_LOCAL_CONSTANT: return (float)(mg_color_to_argb(mg.st.constant_color) >> 24);
    case GR_COMBINE_LOCAL_DEPTH: return depth_local(v);
    default: return clampf(v->a);
    }
}

static float alpha_sel_other(const GrVertex *v)
{
    return mg.st.ac_other == GR_COMBINE_OTHER_CONSTANT ? (float)(mg_color_to_argb(mg.st.constant_color) >> 24)
                                                       : clampf(v->a);           /* texture never reaches here */
}

static void color_sel(const GrVertex *v, int sel_const, float out[3])
{
    if (sel_const) {
        uint32_t c = mg_color_to_argb(mg.st.constant_color);
        out[0] = (float)((c >> 16) & 0xFF); out[1] = (float)((c >> 8) & 0xFF); out[2] = (float)(c & 0xFF);
    } else {
        out[0] = clampf(v->r); out[1] = clampf(v->g); out[2] = clampf(v->b);
    }
}

static float factor_val(GrCombineFactor_t f, float local, float a_local, float a_other)
{
    float x;
    switch (f & 7) {
    case GR_COMBINE_FACTOR_LOCAL: x = local; break;
    case GR_COMBINE_FACTOR_OTHER_ALPHA: x = a_other; break;
    case GR_COMBINE_FACTOR_LOCAL_ALPHA: x = a_local; break;
    case GR_COMBINE_FACTOR_ZERO: x = 0; break;
    default: x = 0; break;              /* texture terms: not evaluated here */
    }
    return (f & 8) ? 255.0f - x : x;
}

static float unit(GrCombineFunction_t fn, float f, float L, float O, float a_local, FxBool inv)
{
    float r;
    f /= 255.0f;
    switch (fn) {
    case GR_COMBINE_FUNCTION_LOCAL: r = L; break;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA: r = a_local; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER: r = f * O; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: r = f * O + L; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA: r = f * O + a_local; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL: r = f * (O - L); break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: r = f * (O - L) + L; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA: r = f * (O - L) + a_local; break;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL: r = L - f * L; break;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA: r = a_local - f * L; break;
    default: r = 0; break;
    }
    r = clampf(r);
    return inv ? 255.0f - r : r;
}

void mg_cc_vertex(const GrVertex *v, int factor_only, float rgb[3])
{
    const mg_state *s = &mg.st;
    float L[3], O[3], al = alpha_sel_local(v), ao = alpha_sel_other(v);
    int i;
    if (s->cc_local == GR_COMBINE_LOCAL_DEPTH)
        L[0] = L[1] = L[2] = depth_local(v);
    else
        color_sel(v, s->cc_local == GR_COMBINE_LOCAL_CONSTANT, L);
    color_sel(v, s->cc_other == GR_COMBINE_OTHER_CONSTANT, O);
    for (i = 0; i < 3; i++) {
        float f = factor_val(s->cc_factor, L[i], al, ao);
        rgb[i] = factor_only ? f : unit(s->cc_func, f, L[i], O[i], al, s->cc_invert);
    }
}

void mg_cc_local_vertex(const GrVertex *v, int local_alpha, float rgb[3])
{
    const mg_state *s = &mg.st;
    if (local_alpha) {
        rgb[0] = rgb[1] = rgb[2] = alpha_sel_local(v);
    } else if (s->cc_local == GR_COMBINE_LOCAL_DEPTH) {
        rgb[0] = rgb[1] = rgb[2] = depth_local(v);
    } else {
        color_sel(v, s->cc_local == GR_COMBINE_LOCAL_CONSTANT, rgb);
    }
}

float mg_ac_vertex(const GrVertex *v, int factor_only)
{
    const mg_state *s = &mg.st;
    float L = alpha_sel_local(v), O = alpha_sel_other(v);
    float f = factor_val(s->ac_factor, L, L, O);
    return factor_only ? f : unit(s->ac_func, f, L, O, L, s->ac_invert);
}
