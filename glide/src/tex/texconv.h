/* texconv.h - Glide texel formats -> Matrox formats (PRD §7.6). */
#ifndef MG_TEXCONV_H
#define MG_TEXCONV_H
#include "glide/glide2.h"
#include "mga/types.h"

enum { HW_TW16 = 3, HW_TW15 = 2, HW_TW12 = 4 };    /* TEXCTL texformat codes */

typedef struct {
    uint32_t palette[256];     /* 0x00RRGGBB, alpha ignored (Voodoo Graphics) */
    uint32_t ncc[2][12];       /* packed NCC tables (Y[4], I[4], Q[4]) */
    int      ncc_sel;
} tex_tables;

/* Texture variants: alpha rewritten for keying on chips without the
 * corresponding per-pixel test (G100). */
enum { TV_PLAIN = 0, TV_CHROMA = 1, TV_ATEST = 2 };
typedef struct {
    int      kind;          /* TV_* bit set */
    uint32_t chroma;        /* 0x00RRGGBB key: exact matches become transparent */
    int      afunc, aref;   /* alpha test: texels failing become transparent */
} tex_variant;
uint32_t tex_variant_key(const tex_variant *v);

/* Decode one Glide texel to 0xAARRGGBB exactly as the Voodoo does. */
uint32_t tex_decode(GrTextureFormat_t fmt, uint32_t texel, const tex_tables *t);
/* Choose the hardware format for a decoded level. */
int tex_classify(const uint32_t *argb, int n);
/* Convert one level: src (game data, w*h texels) -> dst (hw texels,
 * pitch dst_pitch texels, hw_w x hw_h, padded by tiling or edge
 * extension). Returns the hardware format used. */
int tex_convert_level(GrTextureFormat_t fmt, const void *src, int w, int h,
                      uint16_t *dst, int dst_pitch, int hw_w, int hw_h, int clamp_s, int clamp_t,
                      const tex_tables *t, int force_hwfmt, uint32_t *scratch, const tex_variant *var);

#endif
