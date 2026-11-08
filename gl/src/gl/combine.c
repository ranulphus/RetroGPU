/* combine.c - GL texture environments as the G400's combiner words
 * (TDUALSTAGE0/1). The values are Mesa's G400 driver's, which ran on real
 * G400s, collected with their sources in MGA-Glide's
 * docs/g400-dual-texture.md; unit 1's GL_BLEND words are derived there
 * (Mesa used the diffuse colour where GL means the previous unit). Stage 0
 * works on texture 0 and the iterated colour, stage 1 on texture 1 and
 * stage 0's result.
 *
 * GL_COMBINE_ARB (GL_ARB_texture_env_combine) is translated from the
 * register layout in the same document (§2): the colour unit's ARG1 is
 * always the stage's own texture and ARG2 a choice of the iterated colour,
 * FCOL or the previous stage, each optionally as its alpha and inverted;
 * the multiplier and the adder combine them. So a function works on the
 * texture and one other source: REPLACE, MODULATE (x2, x4: modbright), ADD
 * and ADD_SIGNED (x2: add2x), SUBTRACT (either order, by inverting both
 * inputs as Mesa did). INTERPOLATE, two non-texture sources, x4 on an add,
 * and GL_CONSTANT_ARB (FCOL, not programmed yet) are not done: the stage
 * word is refused and emit.c draws GL_MODULATE, logged once. */
#include "gl_tex.h"

enum { BASE_RGB, BASE_RGBA, BASE_A, BASE_I };

/* The texture's base format as the environments see it. */
static int base_of(const dgl_texture *t)
{
    switch (t->level[0].ifc) {
    case DGL_IF_RGB: case DGL_IF_LUMINANCE: return BASE_RGB;
    case DGL_IF_ALPHA: return BASE_A;
    case DGL_IF_INTENSITY: return BASE_I;
    default: return BASE_RGBA;              /* RGBA, LA, RGB5_A1, colour index */
    }
}

/* TDUALSTAGE fields (docs/g400-dual-texture.md §2) */
#define C_ARG2SEL(x)   ((uint32_t)(x) << 0)     /* 0 diffuse, 1 specular, 2 FCOL, 3 previous stage */
#define C_ARG1ALPHA    (1u << 5)
#define C_ARG1INV      (1u << 6)
#define C_ARG2ALPHA    (1u << 7)
#define C_ARG2INV      (1u << 8)
#define C_MODBRIGHT(x) ((uint32_t)(x) << 15)    /* 1: x2, 2: x4 */
#define C_ADD          (1u << 17)               /* else subtract */
#define C_ADD2X        (1u << 18)
#define C_ADDBIAS      (1u << 19)
#define C_SEL(x)       ((uint32_t)(x) << 21)    /* 0 ARG1, 1 ARG2, 2 ADD, 3 MUL */
#define A_ARG1INV      (1u << 23)
#define A_ARG2SEL(x)   ((uint32_t)(x) << 24)    /* 0 diffuse, 1 FCOL, 2 previous texture, 3 previous stage */
#define A_ARG2INV      (1u << 26)
#define A_ADD          (1u << 27)
#define A_ADDBIAS      (1u << 28)               /* with sel MUL: modbright x2 */
#define A_ADD2X        (1u << 29)               /* with sel MUL: modbright x4 */
#define A_SEL(x)       ((uint32_t)(x) << 30)
enum { SEL_ARG1, SEL_ARG2, SEL_ADD, SEL_MUL };

/* One source of a combine function, as a combiner input: the stage's
 * texture (ARG1) or the ARG2 mux, taken as its colour or alpha, inverted
 * or not. 0 when the combiner has no such input. */
typedef struct { int tex; unsigned mux; int alpha, inv; } comb_arg;

static int comb_source(int stage, GLenum src, GLenum op, int for_alpha, comb_arg *a)
{
    a->alpha = op == GL_SRC_ALPHA || op == GL_ONE_MINUS_SRC_ALPHA;
    a->inv = op == GL_ONE_MINUS_SRC_COLOR || op == GL_ONE_MINUS_SRC_ALPHA;
    a->tex = 0;
    a->mux = 0;
    switch (src) {
    case GL_TEXTURE: a->tex = 1; return 1;
    case GL_PRIMARY_COLOR_ARB: return 1;                    /* diffuse (0 on both muxes) */
    case GL_PREVIOUS_ARB: a->mux = stage ? 3 : 0; return 1; /* stage 0: the iterated colour */
    default: (void)for_alpha; return 0;                     /* GL_CONSTANT_ARB: FCOL is not set */
    }
}

/* A two-input function needs the texture on one side and the mux on the
 * other; *swapped says the texture was the second GL source. */
static int two_inputs(const comb_arg *a0, const comb_arg *a1, const comb_arg **t, const comb_arg **m, int *swapped)
{
    if (a0->tex && !a1->tex) { *t = a0; *m = a1; *swapped = 0; return 1; }
    if (a1->tex && !a0->tex) { *t = a1; *m = a0; *swapped = 1; return 1; }
    return 0;
}

static int comb_rgb(int stage, const dgl_texenv *e, uint32_t *w)
{
    comb_arg a0, a1;
    const comb_arg *t, *m;
    int swapped, scale = e->rgb_scale == 4.0f ? 2 : e->rgb_scale == 2.0f ? 1 : 0;
    uint32_t x = 0;
    if (!comb_source(stage, e->src_rgb[0], e->op_rgb[0], 0, &a0))
        return 0;
    if (e->combine_rgb == GL_REPLACE) {
        if (scale)
            return 0;
        if (a0.tex)
            x = C_SEL(SEL_ARG1) | (a0.alpha ? C_ARG1ALPHA : 0) | (a0.inv ? C_ARG1INV : 0);
        else
            x = C_SEL(SEL_ARG2) | C_ARG2SEL(a0.mux) | (a0.alpha ? C_ARG2ALPHA : 0) | (a0.inv ? C_ARG2INV : 0);
        *w |= x;
        return 1;
    }
    if (!comb_source(stage, e->src_rgb[1], e->op_rgb[1], 0, &a1) || !two_inputs(&a0, &a1, &t, &m, &swapped))
        return 0;
    x = C_ARG2SEL(m->mux) | (t->alpha ? C_ARG1ALPHA : 0) | (m->alpha ? C_ARG2ALPHA : 0);
    switch (e->combine_rgb) {
    case GL_MODULATE:                        /* ARG1 x ARG2, x2 / x4 */
        x |= C_SEL(SEL_MUL) | C_MODBRIGHT(scale) | (t->inv ? C_ARG1INV : 0) | (m->inv ? C_ARG2INV : 0);
        break;
    case GL_ADD:
    case GL_ADD_SIGNED_ARB:                  /* ARG1 + ARG2 (- 0.5), x2 */
        if (scale == 2)
            return 0;
        x |= C_SEL(SEL_ADD) | C_ADD | (scale ? C_ADD2X : 0) | (e->combine_rgb == GL_ADD_SIGNED_ARB ? C_ADDBIAS : 0) |
             (t->inv ? C_ARG1INV : 0) | (m->inv ? C_ARG2INV : 0);
        break;
    case GL_SUBTRACT_ARB:                    /* source 0 - source 1: ARG1 - ARG2, or both inverted */
        if (scale == 2)
            return 0;
        x |= C_SEL(SEL_ADD) | (scale ? C_ADD2X : 0) |
             ((t->inv != swapped) ? C_ARG1INV : 0) | ((m->inv != swapped) ? C_ARG2INV : 0);
        break;
    default:                                 /* GL_INTERPOLATE_ARB */
        return 0;
    }
    *w |= x;
    return 1;
}

static int comb_alpha(int stage, const dgl_texenv *e, uint32_t *w)
{
    comb_arg a0, a1;
    const comb_arg *t, *m;
    int swapped, scale = e->alpha_scale == 4.0f ? 2 : e->alpha_scale == 2.0f ? 1 : 0;
    uint32_t x = 0;
    if (!comb_source(stage, e->src_alpha[0], e->op_alpha[0], 1, &a0))
        return 0;
    if (e->combine_alpha == GL_REPLACE) {
        if (scale)
            return 0;
        if (a0.tex)
            x = A_SEL(SEL_ARG1) | (a0.inv ? A_ARG1INV : 0);
        else
            x = A_SEL(SEL_ARG2) | A_ARG2SEL(a0.mux) | (a0.inv ? A_ARG2INV : 0);
        *w |= x;
        return 1;
    }
    if (!comb_source(stage, e->src_alpha[1], e->op_alpha[1], 1, &a1) || !two_inputs(&a0, &a1, &t, &m, &swapped))
        return 0;
    x = A_ARG2SEL(m->mux);
    switch (e->combine_alpha) {
    case GL_MODULATE:                        /* the add bits become modbright under MUL */
        x |= A_SEL(SEL_MUL) | (scale == 1 ? A_ADDBIAS : scale == 2 ? A_ADD2X : 0) |
             (t->inv ? A_ARG1INV : 0) | (m->inv ? A_ARG2INV : 0);
        break;
    case GL_ADD:
    case GL_ADD_SIGNED_ARB:
        if (scale == 2)
            return 0;
        x |= A_SEL(SEL_ADD) | A_ADD | (scale ? A_ADD2X : 0) | (e->combine_alpha == GL_ADD_SIGNED_ARB ? A_ADDBIAS : 0) |
             (t->inv ? A_ARG1INV : 0) | (m->inv ? A_ARG2INV : 0);
        break;
    case GL_SUBTRACT_ARB:
        if (scale == 2)
            return 0;
        x |= A_SEL(SEL_ADD) | (scale ? A_ADD2X : 0) |
             ((t->inv != swapped) ? A_ARG1INV : 0) | ((m->inv != swapped) ? A_ARG2INV : 0);
        break;
    default:
        return 0;
    }
    *w |= x;
    return 1;
}

/* Stage `stage`'s word for the unit environment e on texture t; 0 (with *w
 * unset) when the combiner cannot do it in one stage: GL_BLEND with an
 * environment colour other than black, and the combine functions above. */
int dgl_stage_word(int stage, const dgl_texenv *e, const dgl_texture *t, uint32_t *w)
{
    static const uint32_t replace[2][4]  = { { 0x40000000u, 0x00000000u, 0x00200000u, 0x00000000u },
                                             { 0x43000000u, 0x00000000u, 0x00200003u, 0x00000000u } };
    static const uint32_t modulate[2][4] = { { 0x40600000u, 0xC0600000u, 0xC0200000u, 0xC0600000u },
                                             { 0x43600003u, 0xC3600003u, 0xC3200003u, 0xC3600003u } };
    static const uint32_t decal[2][4]    = { { 0x40000000u, 0x40526A08u, 0x40200000u, 0x40200000u },
                                             { 0x43000000u, 0x43526A0Bu, 0x43200003u, 0x43200003u } };
    static const uint32_t blend[2][4]    = { { 0x40600040u, 0xC0600040u, 0xC0200000u, 0xC0E00040u },
                                             { 0x43600043u, 0xC3600043u, 0xC3200003u, 0xC3E00043u } };
    static const uint32_t add[2][4]      = { { 0x40420000u, 0xC0420000u, 0xC0200000u, 0x88420000u },
                                             { 0x43420003u, 0xC3420003u, 0xC3200003u, 0x8B420003u } };
    const GLfloat *c = e->color;
    int b = base_of(t), s = stage ? 1 : 0;
    uint32_t x = 0;
    switch (e->mode) {
    case GL_REPLACE: *w = replace[s][b]; return 1;
    case GL_MODULATE: *w = modulate[s][b]; return 1;
    case GL_DECAL: *w = decal[s][b]; return 1;
    case GL_ADD: *w = add[s][b]; return 1;
    case GL_BLEND:
        /* Cf * (1 - Ct): exact only for a black environment colour (and, for
         * intensity, a zero alpha). */
        if (c[0] != 0 || c[1] != 0 || c[2] != 0 || (b == BASE_I && c[3] != 0))
            return 0;
        *w = blend[s][b];
        return 1;
    case GL_COMBINE_ARB:
        if (!comb_rgb(s, e, &x) || !comb_alpha(s, e, &x))
            return 0;
        *w = x;
        return 1;
    default:
        return 0;
    }
}

/* Does this environment need more than TEXCTL's modulate/replace with one
 * texture? GL_BLEND, GL_ADD and GL_COMBINE_ARB always; GL_DECAL on a
 * texture with alpha (a blend by texel alpha: the G400's combiner in its
 * blend mode, which needs dual texturing on, docs/g400-dual-texture.md §2;
 * the G200's decalblend). */
int dgl_env_needs_combiner(GLenum env, const dgl_texture *t)
{
    return env == GL_BLEND || env == GL_ADD || env == GL_COMBINE_ARB || (env == GL_DECAL && base_of(t) != BASE_RGB);
}
