/* eval.h - Glide's combine units at a vertex (eval.c), with no texture term: what a triangle's vertices carry
 * when the card iterates the result (exact while the combine is affine in the iterated values). 0..255. */
#ifndef MG_COMBINE_EVAL_H
#define MG_COMBINE_EVAL_H
#include "glide/mg.h"

void  mg_cc_vertex(const GrVertex *v, int factor_only, float rgb[3]);   /* the colour combine, or its factor alone */
float mg_ac_vertex(const GrVertex *v, int factor_only);                 /* the alpha combine, or its factor alone */
void  mg_cc_local_vertex(const GrVertex *v, int local_alpha, float rgb[3]);  /* the colour combine's local (or local
                                                                           alpha, as grey) */
#endif
