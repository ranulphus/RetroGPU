/* combine.h - reduce Glide's colour/alpha/texture combine state to what the
 * Matrox can do (PRD §7.3). */
#ifndef MG_COMBINE_H
#define MG_COMBINE_H

enum {
    CS_ITERATED,        /* per-vertex r,g,b */
    CS_CONSTANT,        /* constant colour */
    CS_ITER_ALPHA,      /* per-vertex a replicated to r,g,b */
    CS_CONST_ALPHA,     /* constant alpha replicated */
    CS_ZERO,
    CS_ONE,
    CS_SOFT,            /* the whole colour combine evaluated per vertex (no texture term) */
    CS_SOFT_FACTOR      /* textured modulate: the combine factor evaluated per vertex */
};

typedef struct {
    int textured;       /* draw with the texture (TEXTURE_TRAP) */
    int modulate;       /* texel * colour source; otherwise decal */
    int color_src;      /* CS_*: untextured colour, or the modulation factor */
    int tex_white;      /* texture combine yields 1 (use the white texture) */
    int approx;         /* the result is an approximation (logged once per state) */
    int quadratic;      /* product of two iterated terms: subdivide to keep it close */
    int spec;           /* G200 specular add: SPEC_LOCAL / SPEC_LOCAL_ALPHA, 0 = none */
} mg_plan;

enum { SPEC_NONE, SPEC_LOCAL, SPEC_LOCAL_ALPHA };

void mg_combine_plan(mg_plan *p);

enum { AS_ONE, AS_ITERATED, AS_CONSTANT, AS_TEXTURE, AS_MODULATED_ITER, AS_MODULATED_CONST,
       AS_SOFT,             /* alpha combine evaluated per vertex (no texture term) */
       AS_MODULATED_SOFT }; /* texture alpha x the alpha factor evaluated per vertex */

typedef struct {
    int alpha_src;      /* AS_*: where the fragment's alpha comes from */
    int stipple;        /* G100: approximate the blend with the stipple */
    int invert;         /* stipple with 255 - alpha */
    int fixed_alpha;    /* >= 0: stipple with this constant coverage instead */
    int native;         /* chip blends: ALPHACTRL factors below */
    uint32_t factors;   /* ALPHACTRL src | dst << 4 */
    int color_off;      /* draw depth only (blend leaves colour unchanged) */
    int skip;           /* nothing to draw */
    int atest;          /* alpha test to apply (GR_CMP_*), GR_CMP_ALWAYS = none */
    int approx;
    int quadratic;      /* as in mg_plan */
} mg_aplan;

void mg_alpha_plan(mg_aplan *a);

/* Per-vertex evaluation of the Voodoo colour / alpha combine units. */
void  mg_combine_vertex(const GrVertex *v, int csrc, float rgb[3]);
float mg_combine_vertex_alpha(const GrVertex *v, int asrc);
void  mg_combine_vertex_spec(const GrVertex *v, int spec, float rgb[3]);

#endif
