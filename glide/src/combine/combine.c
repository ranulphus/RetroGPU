/* combine.c - see combine.h. */
#include "glide/mg.h"
#include "combine/combine.h"
#include "tex/texmgr.h"
#include "mga/regs_mga.h"

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

void mg_combine_vertex(const GrVertex *v, int csrc, float rgb[3])
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
        rgb[i] = csrc == CS_SOFT_FACTOR ? f : unit(s->cc_func, f, L[i], O[i], al, s->cc_invert);
    }
}

void mg_combine_vertex_spec(const GrVertex *v, int spec, float rgb[3])
{
    const mg_state *s = &mg.st;
    if (spec == SPEC_LOCAL_ALPHA) {
        rgb[0] = rgb[1] = rgb[2] = alpha_sel_local(v);
    } else if (s->cc_local == GR_COMBINE_LOCAL_DEPTH) {
        rgb[0] = rgb[1] = rgb[2] = depth_local(v);
    } else {
        color_sel(v, s->cc_local == GR_COMBINE_LOCAL_CONSTANT, rgb);
    }
}

float mg_combine_vertex_alpha(const GrVertex *v, int asrc)
{
    const mg_state *s = &mg.st;
    float L = alpha_sel_local(v), O = alpha_sel_other(v);
    float f = factor_val(s->ac_factor, L, L, O);
    return asrc == AS_MODULATED_SOFT ? f : unit(s->ac_func, f, L, O, L, s->ac_invert);
}

static int func_uses_other(GrCombineFunction_t fn)
{
    return fn >= GR_COMBINE_FUNCTION_SCALE_OTHER && fn <= GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA;
}

static int func_uses_local(GrCombineFunction_t fn)
{
    return fn == GR_COMBINE_FUNCTION_LOCAL || fn == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL ||
           (fn >= GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL && fn <= GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL) ||
           fn == GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL || fn == GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA;
}

static int func_uses_factor(GrCombineFunction_t fn)
{
    return fn >= GR_COMBINE_FUNCTION_SCALE_OTHER;
}

static int factor_is_texture(GrCombineFactor_t f, int other_is_texture_alpha)
{
    int b = f & 7;
    return b == GR_COMBINE_FACTOR_TEXTURE_ALPHA || b == GR_COMBINE_FACTOR_TEXTURE_RGB ||
           (b == GR_COMBINE_FACTOR_OTHER_ALPHA && other_is_texture_alpha);
}

/* Does the factor vary with the iterated values? */
static int factor_iterated(GrCombineFactor_t f, int local_iter, int alocal_iter, int aother_iter)
{
    switch (f & 7) {
    case GR_COMBINE_FACTOR_LOCAL: return local_iter;
    case GR_COMBINE_FACTOR_OTHER_ALPHA: return aother_iter;
    case GR_COMBINE_FACTOR_LOCAL_ALPHA: return alocal_iter;
    default: return 0;
    }
}

void mg_combine_plan(mg_plan *p)
{
    const mg_state *s = &mg.st;
    int tex_zero = 0, uses_tex, alocal_iter = s->ac_local == GR_COMBINE_LOCAL_ITERATED;
    int aother_iter = s->ac_other == GR_COMBINE_OTHER_ITERATED, local_iter = s->cc_local == GR_COMBINE_LOCAL_ITERATED;
    p->textured = 0;
    p->modulate = 0;
    p->color_src = CS_ITERATED;
    p->tex_white = 0;
    p->approx = 0;
    p->quadratic = 0;
    p->spec = SPEC_NONE;

    /* What TMU0 hands to the colour unit. */
    switch (tmu0.rgb_func) {
    case GR_COMBINE_FUNCTION_LOCAL:
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        break;
    case GR_COMBINE_FUNCTION_ZERO:
        if (tmu0.rgb_invert) p->tex_white = 1; else tex_zero = 1;
        break;
    default:
        p->approx = 1;            /* two-TMU forms: TMU1 is absent, use the local texel */
        break;
    }

    uses_tex = (func_uses_other(s->cc_func) && s->cc_other == GR_COMBINE_OTHER_TEXTURE) ||
               (func_uses_factor(s->cc_func) &&
                factor_is_texture(s->cc_factor, s->ac_other == GR_COMBINE_OTHER_TEXTURE));
    if (!uses_tex) {
        /* Evaluated per vertex: exact unless two iterated terms multiply. */
        int other_iter = func_uses_other(s->cc_func) && s->cc_other == GR_COMBINE_OTHER_ITERATED;
        int lterm_iter = func_uses_local(s->cc_func) && local_iter && s->cc_func != GR_COMBINE_FUNCTION_LOCAL &&
                         s->cc_func != GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL;
        p->color_src = CS_SOFT;
        if (func_uses_factor(s->cc_func) && factor_iterated(s->cc_factor, local_iter, alocal_iter, aother_iter) &&
            (other_iter || lterm_iter))
            p->quadratic = 1;
        p->tex_white = 0;
        return;
    }
    /* The texture is involved: the Matrox offers texel, or texel x colour. */
    if (tex_zero && s->cc_other == GR_COMBINE_OTHER_TEXTURE && s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER) {
        p->color_src = CS_ZERO;
        return;
    }
    p->textured = 1;
    if (s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER && s->cc_other == GR_COMBINE_OTHER_TEXTURE && !s->cc_invert) {
        if (s->cc_factor == GR_COMBINE_FACTOR_ONE)
            return;                                        /* decal */
        if (s->cc_factor == GR_COMBINE_FACTOR_ZERO) {
            p->textured = 0;
            p->color_src = CS_ZERO;
            return;
        }
        if (!factor_is_texture(s->cc_factor, 1)) {
            p->modulate = 1;                               /* texel x per-vertex factor */
            p->color_src = CS_SOFT_FACTOR;
            return;
        }
    }
    /* G200 and later: texel x factor + local colour (or local alpha) is
     * the modulate plus the specular add. */
    if (mga.has_specular && s->cc_other == GR_COMBINE_OTHER_TEXTURE && !s->cc_invert && !tex_zero &&
        (s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL ||
         s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA) &&
        !factor_is_texture(s->cc_factor, 1)) {
        p->spec = s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL ? SPEC_LOCAL : SPEC_LOCAL_ALPHA;
        if (s->cc_factor != GR_COMBINE_FACTOR_ONE) {
            p->modulate = 1;
            p->color_src = CS_SOFT_FACTOR;
        }
        return;
    }
    /* Additive, subtractive and texture-alpha forms: the nearest the G100
     * can do is the texel scaled by the factor where that is per-vertex. */
    p->approx = 1;
    if (func_uses_factor(s->cc_func) && !factor_is_texture(s->cc_factor, 1) && s->cc_factor != GR_COMBINE_FACTOR_ONE) {
        p->modulate = 1;
        p->color_src = CS_SOFT_FACTOR;
    }
}

/* Log each distinct approximated combine state once. */
void mg_note_approx(void)
{
    static uint32_t seen[64];
    static int nseen;
    const mg_state *s = &mg.st;
    uint32_t h = ((uint32_t)s->cc_func << 24) ^ ((uint32_t)s->cc_factor << 16) ^ ((uint32_t)s->cc_local << 12) ^
                 ((uint32_t)s->cc_other << 8) ^ ((uint32_t)s->cc_invert << 7) ^ ((uint32_t)tmu0.rgb_func << 3) ^
                 (uint32_t)tmu0.rgb_invert;
    int i;
    for (i = 0; i < nseen; i++)
        if (seen[i] == h)
            return;
    if (nseen < 64)
        seen[nseen++] = h;
    mg_line("MGA-DEGRADE cc=%d,%d,%d,%d,%d tc=%d,%d,%d approx", s->cc_func, s->cc_factor, s->cc_local,
            s->cc_other, s->cc_invert, tmu0.rgb_func, tmu0.rgb_factor, tmu0.rgb_invert);
}

/* Feature census: log each distinct draw-state combination once, so the
 * features a game actually uses can be read off a run's serial log. */
void mg_census(void)
{
    static uint32_t seen[256];
    static int nseen;
    const mg_state *s = &mg.st;
    uint32_t v[12], h = 2166136261u;
    int i;
    v[0] = (uint32_t)s->cc_func | ((uint32_t)s->cc_factor << 8) | ((uint32_t)s->cc_local << 16) | ((uint32_t)s->cc_other << 24);
    v[1] = (uint32_t)s->ac_func | ((uint32_t)s->ac_factor << 8) | ((uint32_t)s->ac_local << 16) | ((uint32_t)s->ac_other << 24);
    v[2] = (uint32_t)s->blend_src | ((uint32_t)s->blend_dst << 8) | ((uint32_t)s->blend_asrc << 16) | ((uint32_t)s->blend_adst << 24);
    v[3] = (uint32_t)s->alpha_test_func | ((uint32_t)s->alpha_test_ref << 8);
    v[4] = (uint32_t)s->chroma_mode;
    v[5] = (uint32_t)s->fog_mode;
    v[6] = (uint32_t)s->depth_mode | ((uint32_t)s->depth_func << 8) | ((uint32_t)s->depth_mask << 16);
    v[7] = (uint32_t)tmu0.rgb_func | ((uint32_t)tmu0.rgb_factor << 8) | ((uint32_t)tmu0.alpha_func << 16) | ((uint32_t)tmu0.alpha_factor << 24);
    v[8] = (uint32_t)tmu0.fmt | ((uint32_t)tmu0.minf << 8) | ((uint32_t)tmu0.magf << 12) | ((uint32_t)tmu0.mipmap << 16);
    v[9] = (uint32_t)s->cc_invert | ((uint32_t)s->ac_invert << 1) | ((uint32_t)tmu0.rgb_invert << 2) | ((uint32_t)tmu0.alpha_invert << 3);
    v[10] = (uint32_t)s->cull;
    v[11] = s->stw_hint;
    for (i = 0; i < 12; i++)
        h = (h ^ v[i]) * 16777619u;
    for (i = 0; i < nseen; i++)
        if (seen[i] == h)
            return;
    if (nseen < 256)
        seen[nseen++] = h;
    mg_line("MGL-CENSUS cc=%d,%d,%d,%d,%d ac=%d,%d,%d,%d,%d blend=%d,%d,%d,%d atest=%d,%d chroma=%d fog=%d "
            "depth=%d,%d,%d tc=%d,%d,%d,%d fmt=%d filt=%d,%d mip=%d cull=%d hint=%x",
            s->cc_func, s->cc_factor, s->cc_local, s->cc_other, s->cc_invert,
            s->ac_func, s->ac_factor, s->ac_local, s->ac_other, s->ac_invert,
            s->blend_src, s->blend_dst, s->blend_asrc, s->blend_adst, s->alpha_test_func, s->alpha_test_ref,
            s->chroma_mode, s->fog_mode, s->depth_mode, s->depth_func, s->depth_mask,
            tmu0.rgb_func, tmu0.rgb_factor, tmu0.alpha_func, tmu0.alpha_factor, tmu0.fmt, tmu0.minf, tmu0.magf,
            tmu0.mipmap, s->cull, s->stw_hint);
}

/* Alpha source from grAlphaCombine. */
static int alpha_source(int *approx, int *quadratic)
{
    const mg_state *s = &mg.st;
    int alocal_iter = s->ac_local == GR_COMBINE_LOCAL_ITERATED, aother_iter = s->ac_other == GR_COMBINE_OTHER_ITERATED;
    int uses_tex = (func_uses_other(s->ac_func) && s->ac_other == GR_COMBINE_OTHER_TEXTURE) ||
                   (func_uses_factor(s->ac_func) &&
                    factor_is_texture(s->ac_factor, s->ac_other == GR_COMBINE_OTHER_TEXTURE));
    if (!uses_tex) {
        int other_iter = func_uses_other(s->ac_func) && aother_iter;
        if (func_uses_factor(s->ac_func) && factor_iterated(s->ac_factor, alocal_iter, alocal_iter, aother_iter) &&
            (other_iter || (func_uses_local(s->ac_func) && alocal_iter && s->ac_func != GR_COMBINE_FUNCTION_LOCAL)))
            *quadratic = 1;
        return AS_SOFT;
    }
    if (s->ac_func == GR_COMBINE_FUNCTION_SCALE_OTHER && s->ac_other == GR_COMBINE_OTHER_TEXTURE && !s->ac_invert) {
        if (s->ac_factor == GR_COMBINE_FACTOR_ONE)
            return AS_TEXTURE;
        if (!factor_is_texture(s->ac_factor, 1))
            return AS_MODULATED_SOFT;
    }
    *approx = 1;
    return AS_TEXTURE;
}

static int mga_src_factor(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return BLEND_ZERO;
    case GR_BLEND_SRC_ALPHA: return BLEND_SRC_ALPHA;
    case GR_BLEND_DST_COLOR: return BLEND_DST_COLOR;
    case GR_BLEND_DST_ALPHA: return BLEND_ONE;              /* no destination alpha: reads as 1 */
    case GR_BLEND_ONE: return BLEND_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return BLEND_ONE_MINUS_SRC_ALPHA;
    case GR_BLEND_ONE_MINUS_DST_COLOR: return BLEND_ONE_MINUS_DST_COLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return BLEND_ZERO;
    case GR_BLEND_ALPHA_SATURATE: return BLEND_SRC_ALPHA_SATURATE;
    default: return BLEND_ONE;
    }
}

static int mga_dst_factor(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return BLEND_ZERO;
    case GR_BLEND_SRC_ALPHA: return BLEND_SRC_ALPHA;
    case GR_BLEND_SRC_COLOR: return BLEND_SRC_COLOR;
    case GR_BLEND_DST_ALPHA: return BLEND_ONE;
    case GR_BLEND_ONE: return BLEND_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return BLEND_ONE_MINUS_SRC_ALPHA;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return BLEND_ONE_MINUS_SRC_COLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return BLEND_ZERO;
    default: return BLEND_ZERO;
    }
}

void mg_alpha_plan(mg_aplan *a)
{
    const mg_state *s = &mg.st;
    GrAlphaBlendFnc_t sf = s->blend_src, df = s->blend_dst;
    a->approx = 0;
    a->quadratic = 0;
    a->alpha_src = alpha_source(&a->approx, &a->quadratic);
    a->stipple = 0;
    a->invert = 0;
    a->fixed_alpha = -1;
    a->native = 0;
    a->factors = 0;
    a->color_off = 0;
    a->skip = 0;
    a->atest = s->alpha_test_func;
    if (a->atest == GR_CMP_NEVER) {
        a->skip = 1;
        return;
    }
    if (sf == GR_BLEND_ONE && df == GR_BLEND_ZERO)
        return;                                            /* opaque */
    if (sf == GR_BLEND_ZERO && df == GR_BLEND_ONE) {
        a->color_off = 1;                                  /* depth-only pass */
        return;
    }
    if (mga.has_alpha_blend) {
        a->native = 1;
        a->factors = (uint32_t)mga_src_factor(sf) | ((uint32_t)mga_dst_factor(df) << 4);
        return;
    }
    /* G100: only the stipple (PRD D8). */
    if (sf == GR_BLEND_SRC_ALPHA && df == GR_BLEND_ONE_MINUS_SRC_ALPHA) {
        a->stipple = 1;
    } else if (sf == GR_BLEND_ONE_MINUS_SRC_ALPHA && df == GR_BLEND_SRC_ALPHA) {
        a->stipple = 1;
        a->invert = 1;
        a->approx = 1;
    } else if (df == GR_BLEND_ONE && (sf == GR_BLEND_ONE || sf == GR_BLEND_SRC_ALPHA)) {
        /* Additive: no destination read on G100. */
        a->approx = 1;
        if (mg_config.g100_additive == 1)
            a->skip = 1;
        else {
            a->stipple = 1;
            if (sf == GR_BLEND_ONE)
                a->fixed_alpha = 128;
        }
    } else if (df == GR_BLEND_SRC_COLOR || sf == GR_BLEND_DST_COLOR || df == GR_BLEND_ONE_MINUS_SRC_COLOR ||
               sf == GR_BLEND_ONE_MINUS_DST_COLOR) {
        a->skip = 1;                                       /* multiplicative: impossible on G100 */
        a->approx = 1;
    } else {
        a->stipple = 1;
        a->fixed_alpha = 128;
        a->approx = 1;
    }
}
