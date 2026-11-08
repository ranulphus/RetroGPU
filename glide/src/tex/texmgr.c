/* texmgr.c - see texmgr.h. */
#include "glide/mg.h"
#include "tex/texmgr.h"
#include "tex/texfmt.h"
#include "mga/sys.h"
#include <string.h>

#define MAX_RECS  1024
#define MAX_FREE  2048
#define HW_ALIGN  64u

typedef struct {
    int      used;
    uint32_t start, size;                   /* Voodoo TMU range */
    GrLOD_t  large, small;
    GrAspectRatio_t aspect;
    GrTextureFormat_t fmt;
    FxU32    evenOdd;
    uint8_t *shadow;                        /* game data, levels large..8 */
    uint32_t shadow_off[9];
    uint8_t  present[9];
    uint32_t hw_block, hw_bytes;            /* VRAM allocation (0 bytes = none) */
    tex_level_hw hw[9];
    uint8_t  hw_valid[9];
    uint32_t hw_pal_gen, hw_ncc_gen, hw_var;
    int      hw_clamp;
    uint32_t last_frame;
} tex_rec;

tex_unit tmu0;
static tex_rec recs[MAX_RECS];
static uint32_t frame_clock;
static uint32_t *scratch;                   /* 256*256 ARGB decode buffer */
static uint16_t *staging;                   /* 256*256 converted texels */

/* ---- VRAM heap (first fit, coalescing) --------------------------------- */
static struct { uint32_t off, size; } freelist[MAX_FREE];
static int nfree;
static uint32_t heap_lo, heap_hi, white_off;

void tex_heap_init(uint32_t start, uint32_t end)
{
    int i;
    /* A new window lays VRAM out afresh: blocks allocated in the old heap
     * are gone, and freeing them into this one (tex_reset after a
     * grSstWinOpen at another size) would hand out VRAM twice, or inside
     * the new colour and depth buffers. */
    for (i = 0; i < MAX_RECS; i++) {
        recs[i].hw_bytes = 0;
        memset(recs[i].hw_valid, 0, sizeof recs[i].hw_valid);
    }
    heap_lo = (start + HW_ALIGN - 1) & ~(HW_ALIGN - 1);
    heap_hi = end;
    nfree = 1;
    freelist[0].off = heap_lo;
    freelist[0].size = heap_hi - heap_lo;
}

static uint32_t heap_alloc(uint32_t bytes)
{
    int i;
    bytes = (bytes + HW_ALIGN - 1) & ~(HW_ALIGN - 1);
    for (i = 0; i < nfree; i++) {
        if (freelist[i].size >= bytes) {
            uint32_t off = freelist[i].off;
            freelist[i].off += bytes;
            freelist[i].size -= bytes;
            if (!freelist[i].size) {
                memmove(&freelist[i], &freelist[i + 1], (size_t)(nfree - i - 1) * sizeof freelist[0]);
                nfree--;
            }
            return off;
        }
    }
    return 0;
}

static void heap_free(uint32_t off, uint32_t bytes)
{
    int i;
    bytes = (bytes + HW_ALIGN - 1) & ~(HW_ALIGN - 1);
    for (i = 0; i < nfree && freelist[i].off < off; i++)
        ;
    if (nfree == MAX_FREE)
        return;                                         /* leak rather than corrupt */
    memmove(&freelist[i + 1], &freelist[i], (size_t)(nfree - i) * sizeof freelist[0]);
    freelist[i].off = off;
    freelist[i].size = bytes;
    nfree++;
    if (i + 1 < nfree && freelist[i].off + freelist[i].size == freelist[i + 1].off) {
        freelist[i].size += freelist[i + 1].size;
        memmove(&freelist[i + 1], &freelist[i + 2], (size_t)(nfree - i - 2) * sizeof freelist[0]);
        nfree--;
    }
    if (i > 0 && freelist[i - 1].off + freelist[i - 1].size == freelist[i].off) {
        freelist[i - 1].size += freelist[i].size;
        memmove(&freelist[i], &freelist[i + 1], (size_t)(nfree - i - 1) * sizeof freelist[0]);
        nfree--;
    }
}

/* ---- Records ------------------------------------------------------------- */
static void rec_release_hw(tex_rec *r)
{
    if (r->hw_bytes)
        heap_free(r->hw_block, r->hw_bytes);
    r->hw_bytes = 0;
    memset(r->hw_valid, 0, sizeof r->hw_valid);
}

static void rec_free(int i)
{
    tex_rec *r = &recs[i];
    rec_release_hw(r);
    sys_free(r->shadow);
    memset(r, 0, sizeof *r);
    if (tmu0.rec == i)
        tmu0.rec = -1;
}

void tex_reset(void)
{
    int i;
    for (i = 0; i < MAX_RECS; i++)
        if (recs[i].used)
            rec_free(i);
    memset(&tmu0, 0, sizeof tmu0);
    tmu0.rec = -1;
    tmu0.large = tmu0.small = GR_LOD_1;
    tmu0.aspect = GR_ASPECT_1x1;
    tmu0.fmt = GR_TEXFMT_RGB_565;
    tmu0.evenOdd = GR_MIPMAPLEVELMASK_BOTH;
    tmu0.rgb_func = GR_COMBINE_FUNCTION_LOCAL;
    tmu0.alpha_func = GR_COMBINE_FUNCTION_LOCAL;
    tmu0.minf = tmu0.magf = GR_TEXTUREFILTER_POINT_SAMPLED;
    /* Retail defaults measured on the Voodoo (conformance t25): clamp in s
     * and t, and no coordinate iteration until a texture combine is set. */
    tmu0.clamp_s = tmu0.clamp_t = GR_TEXTURECLAMP_CLAMP;
    tmu0.combine_set = 0;
    tmu0.mipmap = GR_MIPMAP_DISABLE;
    if (!scratch) {
        scratch = (uint32_t *)sys_alloc(256 * 256 * 4);
        staging = (uint16_t *)sys_alloc(256 * 256 * 2);
    }
    /* Resident 8x8 white TW16 texture for untextured fog/alpha paths. */
    white_off = heap_alloc(8 * 8 * 2);
    if (white_off) {
        volatile uint16_t *p = (volatile uint16_t *)(mga_fb + white_off);
        for (i = 0; i < 64; i++)
            p[i] = 0xFFFF;
    }
}

int tex_white(tex_level_hw *hw)
{
    hw->org = white_off;
    hw->w_log2 = hw->h_log2 = 3;
    hw->hwfmt = HW_TW16;
    hw->pitch = 8;
    hw->logical_maxdim = 8;
    return white_off ? 0 : -1;
}

static int overlaps(const tex_rec *r, uint32_t start, uint32_t size)
{
    return r->used && start < r->start + r->size && r->start < start + size;
}

static int rec_alloc(void)
{
    int i, oldest = -1;
    for (i = 0; i < MAX_RECS; i++)
        if (!recs[i].used)
            return i;
    for (i = 0; i < MAX_RECS; i++)
        if (i != tmu0.rec && (oldest < 0 || recs[i].last_frame < recs[oldest].last_frame))
            oldest = i;
    rec_free(oldest);
    return oldest;
}

static int source_lookup(void);

static int rec_find(uint32_t start, GrLOD_t large, GrAspectRatio_t aspect, GrTextureFormat_t fmt, FxU32 evenOdd)
{
    int i;
    for (i = 0; i < MAX_RECS; i++) {
        const tex_rec *r = &recs[i];
        if (r->used && r->start == start && r->large == large && r->aspect == aspect &&
            r->fmt == fmt && (r->evenOdd & evenOdd) == evenOdd)
            return i;
    }
    return -1;
}

/* Create (or reset) the record for a chain at 'start', dropping every
 * other record the new range overlaps: the game reused that memory. */
static int rec_create(uint32_t start, GrLOD_t large, GrLOD_t small, GrAspectRatio_t aspect,
                      GrTextureFormat_t fmt, FxU32 evenOdd)
{
    uint32_t size = tex_mem_required(small, large, aspect, fmt, evenOdd), bytes = 0;
    int i, l;
    tex_rec *r;
    for (i = 0; i < MAX_RECS; i++)
        if (overlaps(&recs[i], start, size ? size : 8))
            rec_free(i);
    i = rec_alloc();
    r = &recs[i];
    memset(r, 0, sizeof *r);
    r->used = 1;
    r->start = start;
    r->size = size;
    r->large = large;
    r->small = small;
    r->aspect = aspect;
    r->fmt = fmt;
    r->evenOdd = evenOdd;
    for (l = large; l <= GR_LOD_1; l++) {
        r->shadow_off[l] = bytes;
        bytes += tex_level_data_bytes(l, aspect, fmt);
    }
    r->shadow = (uint8_t *)sys_alloc(bytes);
    if (!r->shadow) {
        r->used = 0;
        return -1;
    }
    r->last_frame = frame_clock;
    /* A download into the chain being sourced replaces its record: the
     * source stays bound, as on a Voodoo, where the TMU simply reads the
     * new texels (games update textures in place without grTexSource). */
    if (tmu0.has_source && tmu0.rec < 0)
        tmu0.rec = source_lookup();
    return i;
}

static void level_store(tex_rec *r, GrLOD_t lod, const void *data, int row0, int row1)
{
    int w, h, bpp = tex_bpp(r->fmt);
    tex_dims(lod, r->aspect, &w, &h);
    if (row1 < 0 || row1 >= h) row1 = h - 1;
    if (row0 < 0) row0 = 0;
    if (row0 > row1)
        return;
    memcpy(r->shadow + r->shadow_off[lod] + (uint32_t)(row0 * w * bpp), data, (size_t)((row1 - row0 + 1) * w * bpp));
    r->present[lod] = 1;
    r->hw_valid[lod] = 0;
}

/* ---- Glide download API ---------------------------------------------------- */

GR_ENTRY(FxU32, grTexMinAddress, (GrChipID_t tmu)) { MGA_UNUSED(tmu); return 0; }
GR_ENTRY(FxU32, grTexMaxAddress, (GrChipID_t tmu))
{
    MGA_UNUSED(tmu);
    return (FxU32)mg_config.report_tmu_mb * 1024u * 1024u - 8u;
}

GR_ENTRY(FxU32, grTexCalcMemRequired, (GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt))
{
    return tex_mem_required(lodmin, lodmax, aspect, fmt, GR_MIPMAPLEVELMASK_BOTH);
}

GR_ENTRY(FxU32, grTexTextureMemRequired, (FxU32 evenOdd, GrTexInfo *info))
{
    return tex_mem_required(info->smallLod, info->largeLod, info->aspectRatio, info->format, evenOdd);
}

GR_ENTRY(void, grTexDownloadMipMap, (GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info))
{
    int i, l;
    const uint8_t *src;
    if (tmu != GR_TMU0 || !info || !tex_valid_format(info->format))
        return;
    i = rec_create(startAddress, info->largeLod, info->smallLod, info->aspectRatio, info->format, evenOdd);
    if (i < 0)
        return;
    src = (const uint8_t *)info->data;
    for (l = info->largeLod; l <= info->smallLod; l++) {
        uint32_t n = tex_level_data_bytes(l, info->aspectRatio, info->format);
        /* The game's data holds every level; levels outside evenOdd are
         * skipped (they live on the other TMU). */
        if (tex_level_in(l, evenOdd))
            level_store(&recs[i], l, src, 0, -1);
        src += n;
    }
}

static int rec_for_level(FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                         GrTextureFormat_t fmt, FxU32 evenOdd)
{
    int i = rec_find(startAddress, largeLod, aspect, fmt, evenOdd);
    if (i < 0)
        i = rec_create(startAddress, largeLod, thisLod, aspect, fmt, evenOdd);
    else if (thisLod > recs[i].small) {
        recs[i].small = thisLod;
        recs[i].size = tex_mem_required(thisLod, largeLod, aspect, fmt, evenOdd);
    }
    return i;
}

GR_ENTRY(void, grTexDownloadMipMapLevel, (GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                          GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
                                          void *data))
{
    int i;
    if (tmu != GR_TMU0 || !tex_valid_format(format) || !tex_level_in(thisLod, evenOdd))
        return;
    i = rec_for_level(startAddress, thisLod, largeLod, aspectRatio, format, evenOdd);
    if (i >= 0)
        level_store(&recs[i], thisLod, data, 0, -1);
}

GR_ENTRY(void, grTexDownloadMipMapLevelPartial, (GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                                 GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
                                                 void *data, int start, int end))
{
    int i;
    if (tmu != GR_TMU0 || !tex_valid_format(format) || !tex_level_in(thisLod, evenOdd))
        return;
    i = rec_for_level(startAddress, thisLod, largeLod, aspectRatio, format, evenOdd);
    if (i >= 0)
        level_store(&recs[i], thisLod, data, start, end);
}

/* The record the current source refers to: an exact chain, or a view of a
 * longer chain that starts at one of its levels; -1 if none. */
static int source_lookup(void)
{
    int i = rec_find(tmu0.addr, tmu0.large, tmu0.aspect, tmu0.fmt, tmu0.evenOdd), j;
    for (j = 0; j < MAX_RECS && i < 0; j++) {
        const tex_rec *r = &recs[j];
        if (r->used && r->aspect == tmu0.aspect && r->fmt == tmu0.fmt &&
            tmu0.large >= r->large && tmu0.large <= GR_LOD_1 &&
            r->start + tex_level_offset(tmu0.large, r->large, r->aspect, r->fmt, r->evenOdd) == tmu0.addr)
            i = j;
    }
    return i;
}

GR_ENTRY(void, grTexSource, (GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info))
{
    int i;
    if (tmu != GR_TMU0 || !info)
        return;
    tmu0.large = info->largeLod;
    tmu0.small = info->smallLod;
    tmu0.aspect = info->aspectRatio;
    tmu0.fmt = info->format;
    tmu0.evenOdd = evenOdd;
    tmu0.addr = startAddress;
    tmu0.has_source = 1;
    i = source_lookup();
    if (i < 0) {
        static uint32_t warned;
        if (warned++ < 8)
            mg_log(MG_LOG_WARN, "texture source %06x not downloaded (lod %d..%d fmt %d)", startAddress,
                   info->largeLod, info->smallLod, info->format);
    }
    tmu0.rec = i;
}

GR_ENTRY(void, grTexDownloadTable, (GrChipID_t tmu, GrTexTable_t type, void *data))
{
    if (tmu != GR_TMU0 || !data)
        return;
    if (type == GR_TEXTABLE_PALETTE) {
        memcpy(tmu0.tables.palette, data, sizeof tmu0.tables.palette);
        tmu0.pal_gen++;
    } else if (type == GR_TEXTABLE_NCC0 || type == GR_TEXTABLE_NCC1) {
        memcpy(tmu0.tables.ncc[type], ((const GuNccTable *)data)->packed_data, sizeof tmu0.tables.ncc[0]);
        tmu0.ncc_gen++;
    }
}

GR_ENTRY(void, grTexDownloadTablePartial, (GrChipID_t tmu, GrTexTable_t type, void *data, int start, int end))
{
    if (tmu != GR_TMU0 || !data)
        return;
    if (type == GR_TEXTABLE_PALETTE && start >= 0 && end < 256 && start <= end) {
        /* Glide passes the whole table; entries start..end are loaded. */
        memcpy(&tmu0.tables.palette[start], (const uint32_t *)data + start, (size_t)(end - start + 1) * 4);
        tmu0.pal_gen++;
    } else {
        grTexDownloadTable(tmu, type, data);
    }
}

GR_ENTRY(void, grTexNCCTable, (GrChipID_t tmu, GrNCCTable_t table))
{
    if (tmu == GR_TMU0) {
        tmu0.tables.ncc_sel = table == GR_NCCTABLE_NCC1;
        tmu0.ncc_gen++;
    }
}

/* ---- Residency ------------------------------------------------------------ */

void tex_frame(void) { frame_clock++; }

static int log2i(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

static int ensure_block(int ri)
{
    tex_rec *r = &recs[ri];
    uint32_t bytes = 0, off;
    int l, i;
    if (r->hw_bytes)
        return 0;
    for (l = r->large; l <= GR_LOD_1; l++) {
        int w, h;
        tex_dims(l, r->aspect, &w, &h);
        if (w < 8) w = 8;
        if (h < 8) h = 8;
        bytes += ((uint32_t)(w * h * 2) + HW_ALIGN - 1) & ~(HW_ALIGN - 1);
    }
    off = heap_alloc(bytes);
    while (!off) {
        /* Evict the least recently used record that is not in use. */
        int victim = -1;
        for (i = 0; i < MAX_RECS; i++)
            if (recs[i].used && recs[i].hw_bytes && i != ri &&
                (victim < 0 || recs[i].last_frame < recs[victim].last_frame))
                victim = i;
        if (victim < 0)
            return -1;
        if (recs[victim].last_frame == frame_clock)
            engine_sync(200000);                /* it may still be referenced by queued draws */
        rec_release_hw(&recs[victim]);
        off = heap_alloc(bytes);
    }
    r->hw_block = off;
    r->hw_bytes = bytes;
    for (l = r->large; l <= GR_LOD_1; l++) {
        int w, h;
        tex_dims(l, r->aspect, &w, &h);
        if (w < 8) w = 8;
        if (h < 8) h = 8;
        r->hw[l].org = off;
        r->hw[l].w_log2 = log2i(w);
        r->hw[l].h_log2 = log2i(h);
        r->hw[l].pitch = w;
        off += ((uint32_t)(w * h * 2) + HW_ALIGN - 1) & ~(HW_ALIGN - 1);
    }
    return 0;
}

static int last_hwfmt = HW_TW16;
int tex_level_alpha_class(void) { return last_hwfmt; }

uint32_t tex_vram_gen;

int tex_bind_level(GrLOD_t lod, const tex_variant *var, tex_level_hw *hw)
{
    uint32_t vkey = tex_variant_key(var);
    tex_rec *r;
    int clamp_key = (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP) | ((tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP) << 1);
    int w, h, palettised, nccfmt;
    if (tmu0.rec < 0 || !recs[tmu0.rec].used)
        return -1;
    r = &recs[tmu0.rec];
    if (lod < r->large) lod = r->large;
    while (lod > r->large && !r->present[lod])
        lod--;
    if (!r->present[lod])
        return -1;
    r->last_frame = frame_clock;
    if (ensure_block(tmu0.rec) < 0)
        return -1;
    palettised = r->fmt == GR_TEXFMT_P_8 || r->fmt == GR_TEXFMT_AP_88;
    nccfmt = r->fmt == GR_TEXFMT_YIQ_422 || r->fmt == GR_TEXFMT_AYIQ_8422;
    tex_dims(lod, r->aspect, &w, &h);
    if ((palettised && r->hw_pal_gen != tmu0.pal_gen) || (nccfmt && r->hw_ncc_gen != tmu0.ncc_gen) ||
        ((w < 8 || h < 8) && r->hw_clamp != clamp_key) || r->hw_var != vkey) {
        memset(r->hw_valid, 0, sizeof r->hw_valid);
        r->hw_pal_gen = tmu0.pal_gen;
        r->hw_ncc_gen = tmu0.ncc_gen;
        r->hw_clamp = clamp_key;
        r->hw_var = vkey;
    }
    if (!r->hw_valid[lod]) {
        tex_level_hw *lv = &r->hw[lod];
        int hw_w = 1 << lv->w_log2, hw_h = 1 << lv->h_log2, y, po = PROF_SWITCH(PROF_G_TEXDL);
        /* The level may still be read by queued draws. */
        engine_sync(200000);
        lv->hwfmt = tex_convert_level(r->fmt, r->shadow + r->shadow_off[lod], w, h, staging, hw_w, hw_w, hw_h,
                                      clamp_key & 1, (clamp_key >> 1) & 1, &tmu0.tables, -1, scratch, var);
        for (y = 0; y < hw_h; y++)
            memcpy((void *)(mga_fb + lv->org + (uint32_t)(y * hw_w * 2)), staging + y * hw_w, (size_t)hw_w * 2);
        r->hw_valid[lod] = 1;
        tex_vram_gen++;
        PROF_BACK(po);
    }
    *hw = r->hw[lod];
    hw->logical_maxdim = w > h ? w : h;
    last_hwfmt = hw->hwfmt;
    return 0;
}

void tex_save(tex_saved *t)
{
    t->addr = tmu0.addr; t->evenOdd = tmu0.evenOdd; t->has_source = tmu0.has_source;
    t->large = tmu0.large; t->small = tmu0.small; t->aspect = tmu0.aspect; t->fmt = tmu0.fmt;
    t->minf = tmu0.minf; t->magf = tmu0.magf; t->clamp_s = tmu0.clamp_s; t->clamp_t = tmu0.clamp_t;
    t->mipmap = tmu0.mipmap; t->lod_blend = tmu0.lod_blend; t->lod_bias = tmu0.lod_bias;
    t->rgb_func = tmu0.rgb_func; t->alpha_func = tmu0.alpha_func;
    t->rgb_factor = tmu0.rgb_factor; t->alpha_factor = tmu0.alpha_factor;
    t->rgb_invert = tmu0.rgb_invert; t->alpha_invert = tmu0.alpha_invert;
    t->combine_set = tmu0.combine_set;
}

void tex_restore(const tex_saved *t)
{
    tmu0.minf = (GrTextureFilterMode_t)t->minf; tmu0.magf = (GrTextureFilterMode_t)t->magf;
    tmu0.clamp_s = (GrTextureClampMode_t)t->clamp_s; tmu0.clamp_t = (GrTextureClampMode_t)t->clamp_t;
    tmu0.mipmap = (GrMipMapMode_t)t->mipmap; tmu0.lod_blend = t->lod_blend; tmu0.lod_bias = t->lod_bias;
    tmu0.rgb_func = (GrCombineFunction_t)t->rgb_func; tmu0.alpha_func = (GrCombineFunction_t)t->alpha_func;
    tmu0.rgb_factor = (GrCombineFactor_t)t->rgb_factor; tmu0.alpha_factor = (GrCombineFactor_t)t->alpha_factor;
    tmu0.rgb_invert = t->rgb_invert; tmu0.alpha_invert = t->alpha_invert;
    tmu0.combine_set = t->combine_set;
    if (t->has_source) {
        GrTexInfo info;
        info.largeLod = (GrLOD_t)t->large; info.smallLod = (GrLOD_t)t->small;
        info.aspectRatio = (GrAspectRatio_t)t->aspect; info.format = (GrTextureFormat_t)t->fmt;
        info.data = NULL;
        grTexSource(GR_TMU0, t->addr, t->evenOdd, &info);
    } else {
        tmu0.has_source = 0;
        tmu0.rec = -1;
    }
}
