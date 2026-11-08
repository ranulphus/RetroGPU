/* ct.h - conformance-test scaffolding shared by the t* tests. */
#ifndef CONFORM_CT_H
#define CONFORM_CT_H
#include "hx.h"
#include "glbind.h"

#define CT_W 640
#define CT_H 480

int  ct_open(int nbuffers, int naux);
void ct_close(void);
/* Read the given buffer, save <test>_<n>.PPM and report its CRC. */
void ct_capture(GrBuffer_t buffer);
/* Read the depth buffer (16-bit view) as a grey image. */
void ct_capture_depth(void);
void ct_vtx(GrVertex *v, float x, float y, float r, float g, float b, float a);
void ct_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb);
extern const char *ct_name;

typedef void (*ct_fn)(void);
typedef struct { const char *name; ct_fn fn; const char *what; } ct_test;
extern const ct_test ct_tests[];
extern const ct_test ct_tex_tests[];
extern const ct_test ct_alpha_tests[];
extern const ct_test ct_fog_tests[];
extern const ct_test ct_lfb_tests[];
extern const ct_test ct_combine_tests[];
extern const ct_test ct_bench_tests[];

#endif
