/* mga_be_fmt.h - the Matrox back end's texel formats (texconv.c), apart from
 * mga_be.h so the host's unit test needs no HAL headers. */
#ifndef DGL_MGA_BE_FMT_H
#define DGL_MGA_BE_FMT_H
#include <stdint.h>

/* Hardware texel formats (TEXCTL.texformat codes). */
enum { DGL_TW8 = 1, DGL_TW15 = 2, DGL_TW16 = 3, DGL_TW12 = 4 };   /* TW8: indices into the texture LUT */

int      dgl_hwfmt_for_class(int cls);
uint16_t dgl_pack_texel(int hwfmt, unsigned r, unsigned g, unsigned b, unsigned a);
void     dgl_convert_rgba(int hwfmt, const unsigned char *rgba, long n, uint16_t *out);
uint16_t dgl_pack_grey565(unsigned l);                          /* RGB565 grey, green from red */
/* n RGBA8 texels in hwfmt (TW16, TW15, TW12), as dgl_pack_texel, or from red as
 * dgl_pack_grey565 when grey and TW16. */
void dgl_pack_row(int hwfmt, int grey, const unsigned char *rgba, long n, uint16_t *out);

#endif
