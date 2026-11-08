/* texfmt.c - see texfmt.h. */
#include "tex/texfmt.h"

int tex_maxdim(GrLOD_t lod) { return 256 >> lod; }

void tex_dims(GrLOD_t lod, GrAspectRatio_t aspect, int *w, int *h)
{
    int size = 256 >> lod;
    if (aspect < GR_ASPECT_1x1) {
        *w = size;
        *h = size >> (GR_ASPECT_1x1 - aspect);
    } else {
        *h = size;
        *w = size >> (aspect - GR_ASPECT_1x1);
    }
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

void tex_store_dims(GrLOD_t lod, GrAspectRatio_t aspect, int *w, int *h)
{
    tex_dims(lod, aspect, w, h);
    if (aspect < GR_ASPECT_1x1 && *h < 2) *h = 2;
    if (aspect > GR_ASPECT_1x1 && *w < 2) *w = 2;
}

int tex_valid_format(GrTextureFormat_t f)
{
    return f >= 0 && f <= GR_TEXFMT_AP_88 && f != GR_TEXFMT_RSVD0 && f != GR_TEXFMT_RSVD1;
}

int tex_bpp(GrTextureFormat_t f) { return f >= GR_TEXFMT_16BIT ? 2 : 1; }

uint32_t tex_level_data_bytes(GrLOD_t lod, GrAspectRatio_t aspect, GrTextureFormat_t fmt)
{
    int w, h;
    tex_dims(lod, aspect, &w, &h);
    return (uint32_t)(w * h * tex_bpp(fmt));
}

int tex_level_in(GrLOD_t lod, FxU32 evenOdd)
{
    if ((evenOdd & GR_MIPMAPLEVELMASK_BOTH) == GR_MIPMAPLEVELMASK_BOTH)
        return 1;
    if (evenOdd & GR_MIPMAPLEVELMASK_EVEN)
        return (lod & 1) == 0;
    if (evenOdd & GR_MIPMAPLEVELMASK_ODD)
        return (lod & 1) == 1;
    return 0;
}

static uint32_t store_bytes(GrLOD_t lod, GrAspectRatio_t aspect, GrTextureFormat_t fmt)
{
    int w, h;
    tex_store_dims(lod, aspect, &w, &h);
    return (uint32_t)(w * h * tex_bpp(fmt));
}

uint32_t tex_mem_required(GrLOD_t small, GrLOD_t large, GrAspectRatio_t aspect,
                          GrTextureFormat_t fmt, FxU32 evenOdd)
{
    uint32_t total = 0;
    GrLOD_t l;
    for (l = large; l <= small; l++)
        if (tex_level_in(l, evenOdd))
            total += store_bytes(l, aspect, fmt);
    return (total + 7) & ~7u;
}

uint32_t tex_level_offset(GrLOD_t lod, GrLOD_t large, GrAspectRatio_t aspect,
                          GrTextureFormat_t fmt, FxU32 evenOdd)
{
    uint32_t off = 0;
    GrLOD_t l;
    for (l = large; l < lod; l++)
        if (tex_level_in(l, evenOdd))
            off += store_bytes(l, aspect, fmt);
    return off;
}
