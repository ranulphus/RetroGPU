/* texfmt.h - Voodoo texture geometry and memory arithmetic (exact: matches
 * the retail runtime on every format, aspect and LOD range; see
 * tests/unit/data/texmem.txt). */
#ifndef MG_TEXFMT_H
#define MG_TEXFMT_H
#include "glide/glide2.h"
#include "mga/types.h"

/* Logical dimensions of a level (what the game's data holds). */
void     tex_dims(GrLOD_t lod, GrAspectRatio_t aspect, int *w, int *h);
/* Storage dimensions in Voodoo memory (short side at least 2 texels). */
void     tex_store_dims(GrLOD_t lod, GrAspectRatio_t aspect, int *w, int *h);
int      tex_bpp(GrTextureFormat_t fmt);                 /* bytes per texel */
int      tex_valid_format(GrTextureFormat_t fmt);
/* Bytes of the game's source data for one level. */
uint32_t tex_level_data_bytes(GrLOD_t lod, GrAspectRatio_t aspect, GrTextureFormat_t fmt);
/* Voodoo memory for levels large..small restricted by evenOdd (1 even
 * LOD indices, 2 odd, 3 both), rounded to 8 bytes. */
uint32_t tex_mem_required(GrLOD_t small, GrLOD_t large, GrAspectRatio_t aspect,
                          GrTextureFormat_t fmt, FxU32 evenOdd);
/* Offset of level 'lod' from the chain start (levels stored large first). */
uint32_t tex_level_offset(GrLOD_t lod, GrLOD_t large, GrAspectRatio_t aspect,
                          GrTextureFormat_t fmt, FxU32 evenOdd);
int      tex_level_in(GrLOD_t lod, FxU32 evenOdd);
int      tex_maxdim(GrLOD_t lod);                         /* 256 >> lod */

#define TEX_TMU_BYTES (2u << 20)

#endif
