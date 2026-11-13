/* tex.c - the Matrox back end's textures (docs/b1.md): each texture
 * object's copy in VRAM, moved from src/gl/texture.c in B1 with nothing
 * changed in what it writes.
 *
 * The hardware copy holds level 0 and, for mipmapped filters, the following
 * levels while they are at least 8x8 (the G200-style window, up to 5
 * levels). Levels smaller than 8 texels are widened to 8 by repetition
 * (wrap) or edge extension (clamp). Uploads go through the LFB without
 * waiting for the engine: a texture drawn since the last engine sync (busy)
 * is re-uploaded into a new block (rename-on-write) while its old block is
 * retired until the next sync, and glTexSubImage2D writes just the rectangle
 * when the new texels fit its format (into a new block, the engine copying
 * the rest, when the texture is busy). When VRAM runs out, the least
 * recently drawn textures are evicted (their shadow copies bring them back).
 * A texture's part here (mga_tex, its `be`) is made at its first upload. */
#include "mga_be.h"
#include "mga/mmio.h"                  /* mga_cmd_open */
#include "mga/sys.h"                   /* sys_time_us */
#include <stdlib.h>
#include <string.h>

#define WINDOW_LEVELS 5

static uint32_t white_off;
static int white_ok;
/* The hardware texture LUT (G200): the palette last loaded into it, from a
 * 512-byte block of RGB565 entries. DGL_TLUT: unset uses it on the G200
 * only (the G400 specification says to expand 8-bit textures; untested on
 * silicon), 1 on any card that has one, 0 never. */
static int tlut_mode;
static int iload_mode;             /* DGL_ILOAD: 0 CPU writes only, 1 ILOAD, -1 (default) ILOAD
                                       unless the WARPs' DMA stream is open (sub_iload) */
static int move_mode;              /* DGL_TEXMOVE: 0 waits for the engine instead (sub_move) */
static uint32_t lut_off;
static int lut_ok;
static const dgl_palette *lut_pal;
static unsigned lut_gen;

uint32_t dgl_sync_epoch;

void dgl_mga_synced(void)
{
    dgl_sync_epoch++;
    dgl_vram_sync_done();
}

static int log2i(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

static void release_vram(dgl_texture *t)
{
    if (!t->be || !MT(t)->resident)
        return;
    if (!dgl_texture_busy(t))
        dgl_vram_free(MT(t)->vram_off);
    else if (dgl_vram_retire(MT(t)->vram_off) != 0) {   /* retire list full */
        dgl_texc.syncs++;
        dgl_sync();
        dgl_vram_free(MT(t)->vram_off);
    }
    MT(t)->resident = 0;
}

/* A block for t: from the free heap, then after a sync (retired blocks come
 * back, busy textures become evictable), then by evicting the least
 * recently drawn textures that are not busy. */
static int alloc_vram(const dgl_texture *self, uint32_t size, uint32_t *off)
{
    int synced = 0;
    for (;;) {
        dgl_texture *lru = NULL;
        GLuint i;
        if (dgl_vram_alloc(size, off) == 0)
            return 0;
        if (!synced && dgl_vram_retired()) {
            synced = 1;
            dgl_texc.syncs++;
            if (dgl_sync() == 0)
                continue;
        }
        for (i = 0; i < (GLuint)dgl_texture_count(); i++) {
            dgl_texture *c = dgl_texture_nth((int)i);
            if (c && c != self && c->be && MT(c)->resident && !dgl_texture_busy(c) && (!lru || MT(c)->drawn < MT(lru)->drawn))
                lru = c;
        }
        if (!lru) {
            if (synced)
                return -1;
            synced = 1;
            dgl_texc.syncs++;
            if (dgl_sync() != 0)
                return -1;
            continue;
        }
        release_vram(lru);
        lru->dirty = 1;
        dgl_texc.evictions++;
    }
}

/* Texel i of a level as RGBA8: its shadow, or its index looked up in pal. */
static const unsigned char *texel(const dgl_level *L, const dgl_palette *pal, size_t i)
{
    return L->idx ? dgl_palette_texel(pal, L->idx[i]) : L->rgba + i * 4;
}

/* The alpha class of n texels of a level from texel i. */
static int texels_class(const dgl_level *L, const dgl_palette *pal, size_t i, long n)
{
    int cls = DGL_ALPHA_OPAQUE;
    long k;
    if (!L->idx && (L->ifc == DGL_IF_RGB || L->ifc == DGL_IF_LUMINANCE))
        return DGL_ALPHA_OPAQUE;                 /* (dgl_apply_ifmt made every alpha 1) */
    if (!L->idx)
        return dgl_alpha_class(L->rgba + i * 4, n);
    for (k = 0; k < n; k++) {
        unsigned a = dgl_palette_texel(pal, L->idx[i + (size_t)k])[3];
        if (a == 255)
            continue;
        if (a != 0)
            return DGL_ALPHA_GRADIENT;
        cls = DGL_ALPHA_BINARY;
    }
    return cls;
}

static uint16_t pack(const dgl_texture *t, int hwfmt, const unsigned char *p)
{
    return MT(t)->grey && hwfmt == DGL_TW16 ? dgl_pack_grey565(p[0]) : dgl_pack_texel(hwfmt, p[0], p[1], p[2], p[3]);
}

/* OR n texels of a level, from texel src on, into a row of dwords as the
 * hardware stores them (first texel in the low bits) from position at. */
static void pack_span(const dgl_texture *t, int hwfmt, const dgl_level *L, const dgl_palette *pal, size_t src,
                      int n, uint32_t *row, int at)
{
    static uint16_t px[2048];
    int i;
    if (hwfmt == DGL_TW8)
        for (i = 0; i < n; i++)
            row[(at + i) >> 2] |= (uint32_t)L->idx[src + i] << (((at + i) & 3) * 8);
    else if (!L->idx && n <= 2048) {
        dgl_pack_row(hwfmt, MT(t)->grey, L->rgba + src * 4, n, px);     /* (a row at once, by table) */
        for (i = 0; i < n; i++)
            row[(at + i) >> 1] |= (uint32_t)px[i] << (((at + i) & 1) * 16);
    } else
        for (i = 0; i < n; i++)
            row[(at + i) >> 1] |= (uint32_t)pack(t, hwfmt, texel(L, pal, src + i)) << (((at + i) & 1) * 16);
}

/* Write a rectangle of a level's hardware copy through the engine (ILOAD):
 * queued behind the draws that read the old texels, so neither a wait nor a
 * rename. Needs the HAL's pitch and alignment (level width a multiple of 32
 * texels, 64 for TW8). Returns 1 if done. Not by default while the WARPs'
 * DMA stream is open (a G400 or G450 drawing through the WARPs): there an
 * ILOAD waits for the WARPs to finish first, as OPMODE may not leave general
 * purpose while they work (the HAL's engine.c), and on bench-g400 CPU
 * writes made GLQuake's demo1 faster (39.3 fps against 35.9; -mtex 33.0
 * against 31.6). */
static int sub_iload(dgl_texture *t, int level, const dgl_palette *pal, int x, int y, int w, int h)
{
    static uint32_t row[1024];
    const dgl_level *L = &t->level[level];
    int tw8 = MT(t)->hwfmt == DGL_TW8, n, j;
    if (!iload_mode || (iload_mode < 0 && mga_cmd_open) || w > 2048)
        return 0;
    DGL_GPU_ENTER();
    n = engine_iload_begin(MT(t)->level_off[level], L->w, tw8 ? 8 : 16, x, y, w, h);
    if (!n)
        return 0;
    for (j = 0; j < h; j++) {
        memset(row, 0, (size_t)n * 4);
        pack_span(t, MT(t)->hwfmt, L, pal, (size_t)(y + j) * L->w + x, w, row, 0);
        engine_iload_row(row);
    }
    engine_iload_end();
    dgl_texture_drawn(t);               /* the queued ILOAD writes it: busy until the next sync */
    dgl_texc.sub_iload++;
    return 1;
}

/* A texture that queued draws still read, moved to a new block rather than
 * waited for: the CPU writes the changed rectangle there (sub_in_place) and
 * the engine copies the rest from the old block, queued behind those draws
 * (engine_vram_copy). The old block is retired until the next sync. GLQuake's
 * multitexture path changes a lightmap page between the surfaces that use
 * it, hundreds of times a second. Returns 1 if moved. */
static int sub_move(dgl_texture *t, int level, int x, int y, int w, int h)
{
    const dgl_level *L = &t->level[level];
    uint32_t bpt = MT(t)->hwfmt == DGL_TW8 ? 1 : 2, rowb = (uint32_t)L->w * bpt, old = MT(t)->vram_off, d, noff, at, end;
    int full = x == 0 && w == L->w, ok, l;
    if (!move_mode || !mga.has_dstorg || (!full && (rowb & 63u) != 0) || dgl_vram_alloc(MT(t)->vram_size, &noff) != 0)
        return 0;
    DGL_GPU_ENTER();
    d = noff - old;                                     /* (modulo 2^32: either way) */
    at = MT(t)->level_off[level] + (uint32_t)y * rowb;      /* the rectangle's first row */
    end = at + (uint32_t)h * rowb;                      /* ... and the row after its last */
    ok = engine_vram_move(noff, old, at - old) &&
         (full || (engine_vram_copy(at + d, at, L->w, (int)bpt * 8, x, h) &&
                   engine_vram_copy(at + d + (uint32_t)(x + w) * bpt, at + (uint32_t)(x + w) * bpt, L->w,
                                    (int)bpt * 8, L->w - x - w, h))) &&
         engine_vram_move(end + d, end, old + MT(t)->vram_size - end);
    engine_vram_copy_end();
    if (!ok) {                                          /* (some copies may be queued into it) */
        if (dgl_vram_retire(noff) != 0) {
            dgl_texc.syncs++;
            dgl_sync();
            dgl_vram_free(noff);
        }
        return 0;
    }
    if (dgl_vram_retire(old) != 0) {                    /* retire list full */
        dgl_texc.syncs++;
        dgl_sync();
        dgl_vram_free(old);
    }
    MT(t)->vram_off = noff;
    for (l = 0; l < MT(t)->hw_levels; l++)
        MT(t)->level_off[l] += d;
    dgl_texture_drawn(t);                               /* the queued copies write it */
    dgl_texc.sub_move++;
    return 1;
}

/* Write a changed rectangle straight into the hardware copy: only when the
 * copy is current, holds this level at its own size (not widened) and its
 * format can represent the new texels' alpha. Through the engine when it
 * can (sub_iload), else by the CPU: into a new block when queued draws
 * still read the texture (sub_move), or once the engine no longer reads it.
 * Returns 1 if done. */
static int sub_in_place(dgl_texture *t, int level, int x, int y, int w, int h)
{
    static const int fits[] = { [DGL_TW16] = DGL_ALPHA_OPAQUE, [DGL_TW15] = DGL_ALPHA_BINARY,
                                [DGL_TW12] = DGL_ALPHA_GRADIENT };
    const dgl_level *L = &t->level[level];
    const dgl_palette *pal = L->idx ? dgl_palette_for(t) : NULL;
    int hw_w, j, i, cls = DGL_ALPHA_OPAQUE;
    if (!t->be || !MT(t)->resident || t->dirty || level >= MT(t)->hw_levels ||
        t->level[0].w < 8 || t->level[0].h < 8 || (pal && (MT(t)->pal_used != pal || MT(t)->pal_gen != pal->gen)))
        return 0;
    for (j = 0; j < h && cls <= fits[MT(t)->hwfmt]; j++) {
        int c = texels_class(L, pal, (size_t)(y + j) * L->w + x, w);
        if (c > cls)
            cls = c;
    }
    if (cls > fits[MT(t)->hwfmt])
        return 0;
    if (sub_iload(t, level, pal, x, y, w, h))
        return 1;
    if (dgl_texture_busy(t) && !sub_move(t, level, x, y, w, h)) {
        /* Queued draws may still read it. A small rectangle is cheaper to
         * write after waiting for the engine than the whole texture is to
         * re-upload elsewhere (GLQuake's multitexture path updates a
         * lightmap page between the surfaces that use it). */
        if ((long)w * h * 4 > (long)L->w * L->h)
            return 0;
        if (dgl_sync() != 0)
            return 0;
        dgl_texc.sub_sync++;
    }
    hw_w = L->w;                                  /* level >= 8x8: stored at its own size */
    if (MT(t)->hwfmt == DGL_TW8) {
        for (j = 0; j < h; j++) {
            volatile uint8_t *d8 = mga_fb + MT(t)->level_off[level] + (size_t)(y + j) * hw_w + x;
            for (i = 0; i < w; i++)
                d8[i] = L->idx[(size_t)(y + j) * L->w + x + i];
        }
        dgl_texc.sub_fast++;
        return 1;
    }
    for (j = 0; j < h; j++) {
        /* A row packed at once, then written two texels a write (the level's
         * rows start on 4 bytes: its offset on 32, at least 8 texels wide). */
        static uint32_t row[1025];
        volatile uint8_t *d = mga_fb + MT(t)->level_off[level] + ((size_t)(y + j) * hw_w + (size_t)(x & ~1)) * 2;
        int n = ((x & 1) + w + 1) >> 1;
        memset(row, 0, (size_t)n * 4);
        pack_span(t, MT(t)->hwfmt, L, pal, (size_t)(y + j) * L->w + x, w, row, x & 1);
        if (x & 1)                                /* (the texel before stays as it is) */
            ((volatile uint16_t *)d)[1] = (uint16_t)(row[0] >> 16);
        for (i = x & 1; i < n - ((x + w) & 1); i++)
            ((volatile uint32_t *)d)[i] = row[i];
        if ((x + w) & 1)                          /* (and the one after) */
            ((volatile uint16_t *)d)[2 * (n - 1)] = (uint16_t)row[n - 1];
    }
    dgl_texc.sub_fast++;
    return 1;
}

/* ---- Residency ------------------------------------------------------------ */
static int mip_filter(GLenum f) { return f >= GL_NEAREST_MIPMAP_NEAREST && f <= GL_LINEAR_MIPMAP_LINEAR; }

/* Write one level into VRAM at off as hw_w x hw_h texels (>= the level;
 * at least 8 wide, so every row is whole dwords), a row at a time in 32-bit
 * writes: half the bus transactions of 16-bit texel writes. */
static void write_level(const dgl_texture *t, int l, int hwfmt, uint32_t off, int hw_w, int hw_h,
                        const dgl_palette *pal)
{
    static uint32_t row[1024];
    const dgl_level *L = &t->level[l];
    volatile uint32_t *dst = (volatile uint32_t *)(mga_fb + off);
    int n = hw_w * (hwfmt == DGL_TW8 ? 1 : 2) / 4, span = hw_w < L->w ? hw_w : L->w, x, y, i;
    for (y = 0; y < hw_h; y++, dst += n) {
        int sy = y;
        if ((y & 15) == 15)
            dgl_yield_maybe(1);                   /* (a big texture's rows take a while: the sound's turn) */
        if (sy >= L->h) sy = t->wrap_t != GL_REPEAT ? L->h - 1 : sy % L->h;
        memset(row, 0, (size_t)n * 4);
        pack_span(t, hwfmt, L, pal, (size_t)sy * L->w, span, row, 0);
        for (x = span; x < hw_w; x++)            /* widened: repeat or extend the edge */
            pack_span(t, hwfmt, L, pal, (size_t)sy * L->w + (t->wrap_s != GL_REPEAT ? L->w - 1 : x % L->w), 1,
                      row, x);
        for (i = 0; i < n; i++)
            dst[i] = row[i];
    }
}

/* Can t (colour indices) be stored as TW8 and read through the LUT? One
 * LUT holds one palette: the shared one, and only while it is opaque. */
static int tlut_capable(const dgl_palette *pal)
{
    return tlut_mode && lut_ok && dgl_gl.shared_palette && pal && pal->width && pal->opaque;
}

void dgl_texture_lut(const dgl_texture *t)
{
    const dgl_palette *pal = MT(t)->pal_used;
    volatile uint16_t *dst = (volatile uint16_t *)(mga_fb + lut_off);
    int i;
    if (MT(t)->hwfmt != DGL_TW8 || !pal || (pal == lut_pal && pal->gen == lut_gen))
        return;
    dgl_sync();                                      /* the last load may still be reading the block */
    for (i = 0; i < 256; i++) {
        const unsigned char *p = dgl_palette_texel(pal, (unsigned)i);
        dst[i] = dgl_pack_texel(DGL_TW16, p[0], p[1], p[2], 255);
    }
    engine_tlut_load(lut_off, 0, 256);
    lut_pal = pal;
    lut_gen = pal->gen;
    dgl_texc.lut_loads++;
}

int dgl_texture_ready(dgl_texture *t)
{
    int levels = 1, l, cls = DGL_ALPHA_OPAQUE, hw_w, hw_h;
    uint32_t size = 0, off;
    const dgl_palette *pal = t->level[0].idx ? dgl_palette_for(t) : NULL;
    int bpt, tw8;
    if (!t->be && !(t->be = calloc(1, sizeof(mga_tex)))) {
        dgl_gl_error(GL_OUT_OF_MEMORY);              /* the draw is skipped (PRD §8.3) */
        return -1;
    }
    if (!t->dirty && MT(t)->resident &&
        (!pal || (MT(t)->pal_used == pal && (MT(t)->hwfmt == DGL_TW8 ? tlut_capable(pal) : MT(t)->pal_gen == pal->gen))))
        return 0;                                    /* a TW8 texture follows its palette through the LUT */
    {
    FT_START();
    /* The levels that go to the hardware: level 0, then while the chain halves
     * correctly and stays >= 8x8, up to the window and GL_TEXTURE_MAX_LEVEL. */
    if (mip_filter(t->min_filter) && mga.max_mip_levels > 1) {
        int limit = mga.max_mip_levels < WINDOW_LEVELS ? mga.max_mip_levels : WINDOW_LEVELS;
        if (limit > t->max_level + 1)
            limit = t->max_level + 1;
        while (levels < limit && DGL_LEVEL_DEFINED(&t->level[levels]) &&
               (t->level[levels].idx != NULL) == (pal != NULL) && t->level[levels].w == t->level[levels - 1].w / 2 &&
               t->level[levels].h == t->level[levels - 1].h / 2 && t->level[levels].w >= 8 &&
               t->level[levels].h >= 8)
            levels++;
    }
    for (l = 0; l < levels; l++) {
        int c = texels_class(&t->level[l], pal, 0, (long)t->level[l].w * t->level[l].h);
        if (c > cls)
            cls = c;
    }
    if (cls == DGL_ALPHA_GRADIENT && t->level[0].ifc == DGL_IF_RGB5_A1)
        cls = DGL_ALPHA_BINARY;                      /* one alpha bit, as asked */
    MT(t)->grey = t->level[0].ifc == DGL_IF_LUMINANCE || t->level[0].ifc == DGL_IF_INTENSITY ||
              t->level[0].ifc == DGL_IF_LUMINANCE_ALPHA;
    hw_w = t->level[0].w < 8 ? 8 : t->level[0].w;
    hw_h = t->level[0].h < 8 ? 8 : t->level[0].h;
    tw8 = pal && tlut_capable(pal);                  /* one byte a texel */
    bpt = tw8 ? 1 : 2;
    for (l = 0; l < levels; l++)
        size += (((uint32_t)(hw_w >> l) * (uint32_t)(hw_h >> l) * (uint32_t)bpt) + 31u) & ~31u;
    if (MT(t)->resident && (MT(t)->vram_size != size || dgl_texture_busy(t))) {
        if (dgl_texture_busy(t))
            dgl_texc.renames++;                      /* queued draws keep reading the old block */
        release_vram(t);
    }
    if (!MT(t)->resident) {
        if (alloc_vram(t, size, &off) != 0) {
            static int logged;
            if (logged < 4 && ++logged)
                DGL_WARN("DGL-TEXOOM tex=%u %dx%d levels=%d bytes=%lu heap_used=%lu largest_free=%lu blocks=%d",
                         t->name, t->level[0].w, t->level[0].h, levels, (unsigned long)size,
                         (unsigned long)dgl_vram_used(), (unsigned long)dgl_vram_largest_free(), dgl_vram_blocks());
            dgl_gl_error(GL_OUT_OF_MEMORY);          /* the draw is skipped (PRD §8.3) */
            FT_END();
            return -1;
        }
        MT(t)->vram_off = off;
        MT(t)->vram_size = size;
        MT(t)->resident = 1;
    }
    dgl_texc.uploads++;
    dgl_texc.upload_bytes += size;
    MT(t)->hwfmt = tw8 ? DGL_TW8 : dgl_hwfmt_for_class(cls);
    MT(t)->hw_levels = levels;
    MT(t)->hw_w_log2 = log2i(hw_w);
    MT(t)->hw_h_log2 = log2i(hw_h);
    off = MT(t)->vram_off;
    for (l = 0; l < levels; l++) {
        MT(t)->level_off[l] = off;
        write_level(t, l, MT(t)->hwfmt, off, hw_w >> l, hw_h >> l, pal);
        off += (((uint32_t)(hw_w >> l) * (uint32_t)(hw_h >> l) * (uint32_t)bpt) + 31u) & ~31u;
    }
    MT(t)->pal_used = pal;
    MT(t)->pal_gen = pal ? pal->gen : 0;
    t->dirty = 0;
    dgl_gl.dirty |= DGL_DIRTY_TEXTURE;               /* new VRAM offsets or format for the emitter */
    FT_END();
    return 0;
    }
}

int dgl_white_texture(uint32_t *off)
{
    *off = white_off;
    return white_ok ? 0 : -1;
}


/* glTexSubImage2D's rectangle into the copy (sub_in_place): 1 when written. */
int dgl_mga_tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    return sub_in_place(t, level, x, y, w, h);
}

void dgl_mga_tex_free(dgl_texture *t)
{
    release_vram(t);
    free(t->be);
    t->be = NULL;
}

/* A new context's heap (every texture freed already), the white texture and
 * the LUT's block in it, and the upload paths' switches. */
void dgl_mga_tex_reset(uint32_t heap_start, uint32_t heap_end)
{
    int i;
    dgl_vram_init(heap_start, heap_end);
    white_ok = dgl_vram_alloc(8 * 8 * 2, &white_off) == 0;
    lut_ok = dgl_vram_alloc(256 * 2, &lut_off) == 0;
    lut_pal = NULL;
    {
        const char *e = getenv("DGL_TLUT");
        tlut_mode = !mga.has_tlut ? 0 : e && *e ? (*e != '0') : !mga.has_dual_tex;
        e = getenv("DGL_ILOAD");
        iload_mode = e && *e ? (*e != '0') : -1;
        e = getenv("DGL_TEXMOVE");
        move_mode = !(e && *e == '0');
    }
    if (white_ok) {
        volatile uint16_t *p = (volatile uint16_t *)(mga_fb + white_off);
        for (i = 0; i < 64; i++)
            p[i] = 0xFFFF;
    }
}

/* GL_TEXTURE_RESIDENT, and the components' bits of the copy (as stored:
 * RGB565 until a first upload says otherwise). */
int dgl_mga_tex_info(const dgl_texture *t, int rgba[4])
{
    int hwfmt = t->be ? MT(t)->hwfmt : 0;
    rgba[0] = rgba[2] = hwfmt == DGL_TW12 ? 4 : 5;
    rgba[1] = hwfmt == DGL_TW12 ? 4 : hwfmt == DGL_TW15 ? 5 : 6;
    rgba[3] = hwfmt == DGL_TW12 ? 4 : hwfmt == DGL_TW15 ? 1 : 0;
    return t->be ? MT(t)->resident : 0;
}
