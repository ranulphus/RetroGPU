/* backend.c - DOS-GL's drawing back end on the NV1x's own 3D class, Celsius (0x96): the GeForce2 MX and the Quadro2
 * MXR (RetroGPU's docs/nv10.md; the Quadro2 plan's Q4), on the TNT2 device (hal/src/rg/tnt2.c), whose layer drives
 * the NV1x since Q1.
 *
 * GL transforms and clips on the CPU, as for the other cards; the chip takes the clip-space vertices (x, y, z, w) from
 * a vertex area in video memory with a projection that is the viewport alone (Mesa's NV1x driver's software-transform
 * path), divides, and interpolates colours, texture coordinates and fog in perspective. So:
 *  - fog is GL's per fragment: each vertex's eye distance goes in the fog slot, and the chip applies the mode to each
 *    pixel's interpolated value (measured by DCFOG on bench E: linear f = v - 1, exp 2^(16v - 24), exp2
 *    2^(-(16v - 24)^2 / 8) with v = k0 + k1 c); no triangle splitting;
 *  - the texture environments (GL 1.3's table, ARB_texture_env_combine with subtract and dot3) on the register
 *    combiners, after Mesa's nv10_state_frag.c (MIT: Francisco Jerez);
 *  - two texture units, blending, the alpha, depth and stencil tests (stencil with Z24S8 at 32 bits), colour masks;
 *  - culling here (the sign of the clip-space triangle's determinant, no division); polygon offset by the chip.
 * Textures are the TNT2's: swizzled by the layer's loader into the device's heap (the Matrox back end's allocator,
 * vram.c), Celsius's format words. Clears by the 2D engine. Lines and points are drawn as quads. */
#include "../backend.h"
#include "../../dgl/dgl.h"
#include "../../gl/gl_draw.h"
#include "../mga/vram.h"
#include "rg/tnt2_dev.h"
#include "tnt2/tnt2.h"
#include "mga/prof.h"
#include "mga/fp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The clipper's x and y planes: 8,000 pixels beyond the viewport's centre, in w (DCGUARD on bench E: Celsius draws
 * vertices 10,000 pixels off the screen right, at w 1 and 4); near and far exactly. */
#define GUARD_PX 8000.0f
static float guard_x = 1.0f, guard_y = 1.0f;

#define S7 7                                    /* Celsius's subchannel (tnt2_celsius_init) */

/* Celsius's methods (Mesa's nv10_3d.xml.h, envytools' rnndb). */
#define M_RT_HORIZ        0x200
#define M_RT_VERT         0x204
#define M_TEX_OFFSET(i)   (0x218u + 4u * (uint32_t)(i))
#define M_TEX_FORMAT(i)   (0x220u + 4u * (uint32_t)(i))
#define M_TEX_ENABLE(i)   (0x228u + 4u * (uint32_t)(i))
#define M_TEX_FILTER(i)   (0x248u + 4u * (uint32_t)(i))
#define M_RC_IN_ALPHA(i)  (0x260u + 4u * (uint32_t)(i))
#define M_RC_IN_RGB(i)    (0x268u + 4u * (uint32_t)(i))
#define M_RC_COLOR(i)     (0x270u + 4u * (uint32_t)(i))
#define M_RC_OUT_ALPHA(i) (0x278u + 4u * (uint32_t)(i))
#define M_RC_OUT_RGB(i)   (0x280u + 4u * (uint32_t)(i))
#define M_RC_FINAL0       0x288
#define M_RC_FINAL1       0x28C
#define M_FOG_MODE        0x29C
#define M_FOG_COORD       0x2A0
#define M_FOG_ENABLE      0x2A4
#define M_FOG_COLOR       0x2A8
#define M_CLIP_HORIZ      0x2C0
#define M_CLIP_VERT       0x2E0
#define M_ALPHA_ENABLE    0x300
#define M_BLEND_ENABLE    0x304
#define M_CULL_ENABLE     0x308
#define M_DEPTH_ENABLE    0x30C
#define M_DITHER_ENABLE   0x310
#define M_LIGHTING_ENABLE 0x314
#define M_STENCIL_ENABLE  0x32C
#define M_OFFSET_POINT    0x330
#define M_OFFSET_LINE     0x334
#define M_OFFSET_FILL     0x338
#define M_ALPHA_FUNC      0x33C
#define M_ALPHA_REF       0x340
#define M_BLEND_SRC       0x344
#define M_BLEND_DST       0x348
#define M_BLEND_COLOR     0x34C
#define M_BLEND_EQUATION  0x350
#define M_DEPTH_FUNC      0x354
#define M_COLOR_MASK      0x358
#define M_DEPTH_WRITE     0x35C
#define M_STENCIL_MASK    0x360
#define M_STENCIL_FUNC    0x364
#define M_STENCIL_REF     0x368
#define M_STENCIL_FMASK   0x36C
#define M_STENCIL_FAIL    0x370
#define M_STENCIL_ZFAIL   0x374
#define M_STENCIL_ZPASS   0x378
#define M_SHADE_MODEL     0x37C
#define M_OFFSET_FACTOR   0x384
#define M_OFFSET_UNITS    0x388
#define M_DEPTH_NEAR      0x394
#define M_DEPTH_FAR       0x398
#define M_PROJECTION      0x500
#define M_FOG_COEFF       0x680
#define M_VP_TRANSLATE    0x6E8
#define M_VTXBUF_VALIDATE 0xCF0
#define M_VTXBUF_OFFSET(i) (0xD00u + 8u * (uint32_t)(i))
#define M_VTXBUF_FMT(i)   (0xD04u + 8u * (uint32_t)(i))
#define M_BEGIN_END       0x13FC
#define M_BATCH           0x1400
#define M_ELEMENT16       0xE00                 /* an array of 128: two 16-bit indices a word */
#define M_ELEMENT32       0x1100                /* an array of 64: one index a word */
#define M_MODELVIEW       0x400
#define M_CULL_FACE       0x39C
#define M_FRONT_FACE      0x3A0

/* Texture words. */
#define TF_DMA0      0x00000001u
#define TF_BASE      0x00001050u                /* (Mesa's 5 << 4 | 1 << 12) */
#define TF_L8        0x00000000u
#define TF_A1R5G5B5  0x00000100u
#define TF_A4R4G4B4  0x00000200u
#define TF_R5G6B5    0x00000280u
#define TF_A8R8G8B8  0x00000300u
#define TF_MIPMAP    0x00008000u
#define TE_ENABLE    0x40000000u

/* The register combiners' inputs (a byte: source, alpha usage 0x10, mapping << 5). */
enum { RS_ZERO = 0, RS_CONST0 = 1, RS_CONST1 = 2, RS_FOG = 3, RS_PRIMARY = 4, RS_SECONDARY = 5, RS_TEX0 = 8,
       RS_TEX1 = 9, RS_SPARE0 = 0xC };
#define RU_ALPHA        0x10u
#define RM_IDENTITY     0x00u
#define RM_INVERT       0x20u
#define RM_EXPAND       0x40u
#define RM_EXPAND_NEG   0x60u
#define R_ONE           (RS_ZERO | RM_INVERT)   /* (zero inverted: one) */
#define R_MINUS_ONE     (RS_ZERO | RM_EXPAND)   /* (zero expanded: 2 * 0 - 1) */
/* Outputs. */
#define RO_AB_SPARE0    0x000000C0u
#define RO_SUM_SPARE0   0x00000C00u
#define RO_AB_DOT       0x00002000u
#define RO_BIAS         0x00008000u
#define RO_SCALE2       0x00010000u
#define RO_SCALE4       0x00020000u

/* A vertex as the chip reads it from the vertex area: 40 bytes. */
typedef struct {
    float    x, y, z, w;                        /* clip space: VTXBUF_FMT's homogeneous bit */
    uint32_t col;                               /* B8G8R8A8 */
    float    s, t, s1, t1;
    float    fog;                               /* the eye distance (the fog slot, which is the secondary's alpha) */
} cvert;
#define VSIZE 40u
#define VA_SIZE (256u * 1024u)                  /* the vertex area */
#define VA_PARTS 4                              /* fenced a quarter at a time */
#define VA_PART (VA_SIZE / VA_PARTS)
#define NSIZE 12u                               /* a lit draw's normal (x y z floats), in an array of its own */
#define NA_SIZE ((VA_SIZE / VSIZE + 1u) * NSIZE)

/* A texture's copy in VRAM. */
typedef struct {
    uint32_t block, off;
    uint32_t format;                            /* TEX_FORMAT without wraps and the mipmap bit */
    int      levels;
    int      family;                            /* FAM_: the texture environments' table */
    unsigned stamp;
    unsigned palgen;
    const dgl_palette *pal;
    int      code, lw, lh;
    uint32_t mark;
} ctex;
enum { FAM_RGBA, FAM_RGB, FAM_ALPHA, FAM_INTENSITY };

static ctex *cur_t0, *cur_t1;
static unsigned stamp;
static uint32_t tris, lines_, points_;
static int warned[12];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        DGL_WARN("DGL-WARN nv10: %s", what);
    }
}

/* ---- Methods, with the values sent kept so only changes go out ------------- */

#define SH_N (0xC00 / 4)                        /* state methods below 0xC00 (the lights' end at 0xBFF) */
static uint32_t sh[SH_N];
static unsigned char sh_ok[SH_N];

static void m1(uint32_t mthd, uint32_t v) { tnt2_methods(S7, mthd, &v, 1); }

static void mc(uint32_t mthd, uint32_t v)
{
    uint32_t i = mthd >> 2;
    if (i < SH_N) {
        if (sh_ok[i] && sh[i] == v)
            return;
        sh[i] = v, sh_ok[i] = 1;
    }
    m1(mthd, v);
}

static uint32_t fbits(float f) { union { float f; uint32_t u; } c; c.f = f; return c.u; }

/* A vector method (the fog's coefficients, the projection, the viewport's translation) sent whole when any of it
 * changed: the chip takes such a vector when its last word is written (t10 on bench E: exp's coefficients after
 * linear's, k2 = 0 both times and so skipped, were never taken). */
static void mcv(uint32_t mthd, const uint32_t *v, int n);
static void mcf_one(uint32_t mthd, float f) { mcv(mthd, (const uint32_t[]){ fbits(f) }, 1); }
static void mcv(uint32_t mthd, const uint32_t *v, int n)
{
    uint32_t i0 = mthd >> 2;
    int i, same = 1;
    if (i0 + (uint32_t)n > SH_N) {              /* (beyond the cache: sent as it is) */
        tnt2_methods(S7, mthd, v, n);
        return;
    }
    for (i = 0; i < n; i++)
        if (!sh_ok[i0 + (uint32_t)i] || sh[i0 + (uint32_t)i] != v[i])
            same = 0;
    if (same)
        return;
    for (i = 0; i < n; i++)
        sh[i0 + (uint32_t)i] = v[i], sh_ok[i0 + (uint32_t)i] = 1;
    tnt2_methods(S7, mthd, v, n);
}

/* ---- Textures ------------------------------------------------------------- */

static int log2_of(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

static uint32_t chip_format(const dgl_level *l, int *cpp, int *family)
{
    *cpp = 2;
    switch (l->ifc) {
    case DGL_IF_RGB5_A1:
        *family = FAM_RGBA;
        return TF_A1R5G5B5;
    case DGL_IF_RGB:
    case DGL_IF_LUMINANCE:
        *family = FAM_RGB;
        if (l->ifmt == GL_RGB8 || l->ifmt == GL_RGB10 || l->ifmt == GL_RGB12 || l->ifmt == GL_RGB16) {
            *cpp = 4;
            return TF_A8R8G8B8;
        }
        return TF_R5G6B5;
    case DGL_IF_RGBA:
        *family = FAM_RGBA;
        if (l->ifmt == GL_RGBA4 || l->ifmt == GL_RGBA2)
            return TF_A4R4G4B4;
        *cpp = 4;
        return TF_A8R8G8B8;
    case DGL_IF_ALPHA:
        *family = FAM_ALPHA;
        break;
    case DGL_IF_INTENSITY:
        *family = FAM_INTENSITY;
        break;
    default:                                    /* (luminance-alpha: as RGBA) */
        *family = FAM_RGBA;
        break;
    }
    *cpp = 4;
    return TF_A8R8G8B8;
}

static void flush(void);

static void tex_drop(dgl_texture *t)
{
    ctex *x = (ctex *)t->be;
    flush();                                    /* (batched triangles may read it) */
    if (x == cur_t0) cur_t0 = NULL;
    if (x == cur_t1) cur_t1 = NULL;
    if (x) {
        if (dgl_vram_retire(x->block) != 0) {
            tnt2_sync(1000000);
            dgl_vram_sync_done();
            dgl_vram_free(x->block);
        }
        free(x);
        t->be = NULL;
    }
}

static int room(uint32_t size, uint32_t *block, const dgl_texture *keep)
{
    flush();                                    /* (batched triangles may read what is freed below) */
    while (dgl_vram_alloc(size, block) != 0) {
        dgl_texture *old = NULL, *u;
        int i, n = dgl_texture_count();
        if (dgl_vram_retired()) {
            tnt2_sync(1000000);
            dgl_vram_sync_done();
            continue;
        }
        for (i = 0; i < n; i++)
            if ((u = dgl_texture_nth(i)) && u->be && u != keep &&
                (!old || ((ctex *)u->be)->stamp < ((ctex *)old->be)->stamp))
                old = u;
        if (!old)
            return -1;
        tex_drop(old);
        dgl_texc.evictions++;
    }
    return 0;
}

/* The texture's copy, made or made again when its levels changed (the TNT2 back end's, Celsius's words). */
static ctex *prepare(dgl_texture *t)
{
    PROF_SCOPE(PROF_D_TEX);
    ctex *x = (ctex *)t->be;
    const dgl_level *l0 = &t->level[0];
    uint32_t fmt, size = 0;
    int cpp, family, n, i, lw, lh;
    const dgl_palette *pal = l0->idx ? dgl_palette_for(t) : NULL;
    unsigned char *expanded[DGL_MAX_LEVELS];
    if (x && !t->dirty && (!pal || (x->pal == pal && x->palgen == pal->gen))) {
        x->stamp = ++stamp;
        return x;
    }
    if (!l0->rgba && !l0->idx)
        return NULL;
    lw = log2_of(l0->w), lh = log2_of(l0->h);
    if ((1 << lw) != l0->w || (1 << lh) != l0->h || lw > 11 || lh > 11)
        return NULL;
    if (pal) {
        cpp = 4, family = pal->opaque ? FAM_RGB : FAM_RGBA, fmt = TF_A8R8G8B8;
    } else
        fmt = chip_format(l0, &cpp, &family);
    for (n = 1; n < DGL_MAX_LEVELS && n <= t->max_level; n++) {
        const dgl_level *l = &t->level[n];
        int w = l0->w >> n ? l0->w >> n : 1, h = l0->h >> n ? l0->h >> n : 1;
        if (!(pal ? l->idx != NULL : l->rgba != NULL) || l->w != w || l->h != h)
            break;
        if (w == 1 && h == 1) {
            n++;
            break;
        }
    }
    for (i = 0; i < n; i++)
        size += (uint32_t)(t->level[i].w * t->level[i].h * cpp);
    {
        ctex *old = (ctex *)t->be;
        uint32_t oldblock = old ? old->block : 0;
        t->be = NULL;
        if (old == cur_t0) cur_t0 = NULL;
        if (old == cur_t1) cur_t1 = NULL;
        free(old);
        x = (ctex *)calloc(1, sizeof *x);
        if (!x || room(size + 256u, &x->block, t) != 0) {
            free(x);
            if (old) {
                tnt2_sync(1000000);
                dgl_vram_free(oldblock);
            }
            warn_once(1, "out of texture memory: a texture drawn untextured");
            return NULL;
        }
        if (old && dgl_vram_retire(oldblock) != 0) {
            tnt2_sync(1000000);
            dgl_vram_sync_done();
            dgl_vram_free(oldblock);
        }
    }
    x->off = (x->block + 255u) & ~255u;
    {
        const uint8_t *src[DGL_MAX_LEVELS];
        int code = fmt == TF_A8R8G8B8 ? TNT2_TEX_A8R8G8B8 : fmt == TF_R5G6B5 ? TNT2_TEX_R5G6B5
                 : fmt == TF_A1R5G5B5 ? TNT2_TEX_A1R5G5B5 : TNT2_TEX_A4R4G4B4;
        for (i = 0; i < n; i++) {
            const dgl_level *l = &t->level[i];
            expanded[i] = NULL;
            src[i] = l->rgba;
            if (pal) {
                int k;
                if (!(expanded[i] = (unsigned char *)malloc((size_t)l->w * l->h * 4)))
                    src[i] = NULL;
                else {
                    for (k = 0; k < l->w * l->h; k++)
                        memcpy(expanded[i] + 4 * k, dgl_palette_texel(pal, l->idx[k]), 4);
                    src[i] = expanded[i];
                }
            }
        }
        for (i = 0; i < n && src[i]; i++)
            ;
        if (i == n)
            tnt2_tex_load_rgba8(x->off, code, lw, lh, n, src);
        x->code = code, x->lw = lw, x->lh = lh;
        for (i = 0; i < n; i++)
            free(expanded[i]);
    }
    x->format = TF_DMA0 | TF_BASE | fmt | (uint32_t)lh << 20 | (uint32_t)lw << 16;
    x->levels = n;
    x->family = family;
    x->stamp = ++stamp;
    x->mark = tnt2_mark();
    x->pal = pal;
    x->palgen = pal ? pal->gen : 0;
    t->be = x;
    t->dirty = 0;
    dgl_texc.uploads++;
    dgl_texc.upload_bytes += size;
    return x;
}

/* ---- The vertex area ------------------------------------------------------- */

static uint32_t va_block, va_off;               /* its heap block and its 256-aligned start (0: none yet) */
static uint32_t va_n;                           /* vertices written so far (the next one's index) */
static uint32_t va_first;                       /* the first vertex not yet drawn */
static uint32_t va_mark[VA_PARTS];              /* the stream after the last draw from each quarter */
static int va_formats;                          /* VTXBUF_FMT sent */
static uint32_t offs_base;                      /* where VTXBUF_OFFSET points: the vertex area, or a kept draw's */
static uint32_t na_off;                         /* the vertex area's normals (vertex i's at na_off + 12 i) */
static uint32_t offs_nbase;                     /* where slot 5 (normals) points, 0: off */

static int va_get(void)
{
    if (va_off)
        return 0;
    if (room(VA_SIZE + NA_SIZE + 512u, &va_block, NULL) != 0) {
        warn_once(2, "no room for the vertex area: nothing drawn");
        return -1;
    }
    va_off = (va_block + 255u) & ~255u;
    na_off = va_off + ((VA_SIZE + 255u) & ~255u);
    va_n = va_first = 0;
    memset(va_mark, 0, sizeof va_mark);
    va_formats = 0;
    return 0;
}

static void va_formats_send(void)
{
    static const uint32_t fmt[8] = {
        0x01000000u | VSIZE << 8 | 4u << 4 | 2u,        /* position: x y z w, floats, homogeneous */
        VSIZE << 8 | 4u << 4 | 0u,                      /* colour: B8G8R8A8 */
        0x0002u,                                        /* (the secondary colour: off) */
        VSIZE << 8 | 2u << 4 | 2u,                      /* texture 0: s t */
        VSIZE << 8 | 2u << 4 | 2u,                      /* texture 1: s t */
        0x0002u, 0x0002u,                               /* (normal, weight: off) */
        VSIZE << 8 | 1u << 4 | 2u };                    /* fog */
    int i;
    for (i = 0; i < 8; i++)
        m1(M_VTXBUF_FMT(i), fmt[i]);
    va_formats = 1;
    offs_base = 0, offs_nbase = 0;
}

/* VTXBUF_OFFSET at a block of the 40-byte vertices (the vertex area's, or a kept draw's), where it isn't already;
 * nbase: a lit draw's normals (slot 5, 12 bytes a vertex), 0: none. */
static void set_base2(uint32_t base, uint32_t nbase)
{
    static const uint32_t at[8] = { 0, 16, 0, 20, 28, 0, 0, 36 };
    static const unsigned char used[8] = { 1, 1, 0, 1, 1, 0, 0, 1 };
    int i;
    if (!va_formats)
        va_formats_send();
    if (offs_base != base) {
        for (i = 0; i < 8; i++)
            if (used[i])
                m1(M_VTXBUF_OFFSET(i), base + at[i]);
        offs_base = base;
    }
    if (offs_nbase != nbase) {
        m1(M_VTXBUF_FMT(5), nbase ? NSIZE << 8 | 3u << 4 | 2u : 0x0002u);
        if (nbase)
            m1(M_VTXBUF_OFFSET(5), nbase);
        offs_nbase = nbase;
    }
}

static void set_base(uint32_t base) { set_base2(base, 0); }

/* The vertices written since the last flush drawn: one triangle list. */
static void flush(void)
{
    uint32_t first = va_first, n = va_n - va_first;
    if (!n)
        return;
    set_base(va_off);
    /* (before every draw: the chip's vertex fetch keeps video memory's lines, and a draw's first vertices often share
     * a line with the last draw's end, fetched before the CPU wrote them: conformance t16/t18 drew triangles from
     * stale vertices without it. DCPERF: a thousand of them cost 45 us) */
    m1(M_VTXBUF_VALIDATE, 0);
    m1(M_BEGIN_END, 5);                         /* GL_TRIANGLES + 1 */
    while (n) {
        uint32_t k = n > 255u ? 255u : n;       /* (whole triangles: 255 = 85 of them) */
        m1(M_BATCH, (k - 1u) << 24 | first);
        first += k, n -= k;
    }
    m1(M_BEGIN_END, 0);
    va_first = va_n;
    va_mark[(va_n ? va_n - 1u : 0u) * VSIZE / VA_PART] = tnt2_mark();
    {
        /* (the textures these triangles read: a rectangle written into one waits for the chip to pass here) */
        uint32_t m = tnt2_mark();
        if (cur_t0) cur_t0->mark = m;
        if (cur_t1) cur_t1->mark = m;
    }
    tnt2_kick_soon();                           /* (every wait kicks first; the CPU's vertex writes reach video memory
                                                   before the chip reads them: a kick reads video memory first) */
}

/* Room for k more vertices: past the area's end, back to its start; into a quarter, once the chip is done with it. */
static int va_room(uint32_t k)
{
    uint32_t end = (va_n + k) * VSIZE, part0 = va_n ? (va_n * VSIZE - 1u) / VA_PART : (uint32_t)-1, p;
    if (end > VA_SIZE) {
        flush();
        va_n = va_first = 0;
        part0 = (uint32_t)-1;
        end = k * VSIZE;
    }
    for (p = part0 + 1u; p * VA_PART < end; p++)
        if (p != part0 && tnt2_mark_wait(va_mark[p], 1000000) != 0) {
            warn_once(3, "the chip didn't finish with the vertex area: drawing dropped");
            return -1;
        }
    return 0;
}

/* ---- State ------------------------------------------------------------------ */

static int fogging;
static int valid_ok;
static int target_ok;                           /* the render target sent (a change costs six no-ops and the chip's
                                                   pipeline: only when GL's target changes, not when state does) */
static const dgl_texture *valid_tex, *valid_tex1;
static int shift_s[2], shift_t[2];
static int nothing;                             /* the scissor leaves no pixels */
static int hw_unit_of[2];                       /* GL's unit to the chip's (unit 1 alone goes to the chip's 0) */
static int swap_st;                             /* ... and then its coordinates go in the vertex's first pair */

/* 0..1 to a byte (the TNT2 back end's, no FLDCW). */
static uint32_t ub(float v)
{
    union { float f; int32_t i; } u;
    int32_t i;
    u.f = v * 255.0f + 12582912.0f;
    i = u.i - 0x4B400000;
    return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static uint32_t argb(const GLfloat *c) { return ub(c[3]) << 24 | ub(c[0]) << 16 | ub(c[1]) << 8 | ub(c[2]); }

static uint32_t col_key[4], col_val, col_set;

static uint32_t colour_of(const dgl_cvtx *c)
{
    uint32_t k[4];
    memcpy(k, &c->r, sizeof k);
    if (col_set && k[0] == col_key[0] && k[1] == col_key[1] && k[2] == col_key[2] && k[3] == col_key[3])
        return col_val;
    memcpy(col_key, k, sizeof k);
    col_set = 1;
    return col_val = ub(c->a) << 24 | ub(c->r) << 16 | ub(c->g) << 8 | ub(c->b);
}

static void cut(int *c, int x0, int y0, int x1, int y1)
{
    if (x0 > c[0]) c[0] = x0;
    if (y0 > c[1]) c[1] = y0;
    if (x1 < c[2]) c[2] = x1;
    if (y1 < c[3]) c[3] = y1;
}

/* The scissor (draw: and the viewport) in window rows (down). */
static void scissor(int draw, int *c)
{
    int h = dgl_ctx.height;
    c[0] = 0, c[1] = 0, c[2] = rg_tnt2.width, c[3] = rg_tnt2.height;
    if (dgl_gl.scissor_test)
        cut(c, dgl_gl.scissor[0], h - (dgl_gl.scissor[1] + dgl_gl.scissor[3]), dgl_gl.scissor[0] + dgl_gl.scissor[2],
            h - dgl_gl.scissor[1]);
    if (draw)
        cut(c, dgl_gl.viewport[0], h - (dgl_gl.viewport[1] + dgl_gl.viewport[3]),
            dgl_gl.viewport[0] + dgl_gl.viewport[2], h - dgl_gl.viewport[1]);
    if (c[2] < c[0]) c[2] = c[0];
    if (c[3] < c[1]) c[3] = c[1];
}

/* A unit's texture words (TEX_OFFSET, FORMAT, FILTER, ENABLE) for the chip's unit hw. */
static ctex *tex_unit(dgl_texture *t, int gl_unit, int hw)
{
    ctex *x = t ? prepare(t) : NULL;
    uint32_t mag, min, en;
    int mip;
    shift_s[gl_unit] = shift_t[gl_unit] = 0;
    if (!x) {
        mc(M_TEX_ENABLE(hw), 0);
        return NULL;
    }
    mip = t->min_filter != GL_NEAREST && t->min_filter != GL_LINEAR && x->levels > 1;
    mag = t->mag_filter == GL_LINEAR ? 2u : 1u;
    switch (t->min_filter) {
    case GL_LINEAR: min = 2; break;
    case GL_NEAREST_MIPMAP_NEAREST: min = mip ? 3u : 1u; break;
    case GL_LINEAR_MIPMAP_NEAREST: min = mip ? 4u : 2u; break;
    case GL_NEAREST_MIPMAP_LINEAR: min = mip ? 5u : 1u; break;
    case GL_LINEAR_MIPMAP_LINEAR: min = mip ? 6u : 2u; break;
    default: min = 1; break;
    }
    shift_s[gl_unit] = t->wrap_s == GL_REPEAT, shift_t[gl_unit] = t->wrap_t == GL_REPEAT;
    en = TE_ENABLE;
    if (mip)
        en |= (uint32_t)(x->levels - 1) << 14;  /* (MIPMAP_MAX_LOD; the least 0) */
    mc(M_TEX_OFFSET(hw), x->off);
    mc(M_TEX_FORMAT(hw), x->format | (mip ? TF_MIPMAP : 0u) |
                         (t->wrap_s == GL_REPEAT ? 1u : 3u) << 24 | (t->wrap_t == GL_REPEAT ? 1u : 3u) << 28);
    mc(M_TEX_FILTER(hw), mag << 28 | min << 24);
    mc(M_TEX_ENABLE(hw), en);
    return x;
}

/* ---- The texture environments on the register combiners -------------------- */

/* One argument: a source and an operand (GL's), as the combiners take it. */
typedef struct { GLenum src, op; } carg;
typedef struct { GLenum fn; carg a[3]; float scale; } comb;

/* GL's environment for a texture of a family as two combines (OpenGL 1.3's table 3.24; the Radeon back end's). */
static void env_combines(const dgl_texenv *e, int family, comb *rgb, comb *al)
{
    static const carg P = { GL_PREVIOUS_ARB, GL_SRC_COLOR }, T = { GL_TEXTURE, GL_SRC_COLOR },
                      TA = { GL_TEXTURE, GL_SRC_ALPHA }, K = { GL_CONSTANT_ARB, GL_SRC_COLOR },
                      Z = { GL_ZERO, GL_SRC_COLOR };
    int i;
    memset(rgb, 0, sizeof *rgb), memset(al, 0, sizeof *al);
    rgb->scale = al->scale = 1.0f;
#define SET(c, f, x, y, z) ((c)->fn = (f), (c)->a[0] = (x), (c)->a[1] = (y), (c)->a[2] = (z))
    if (e->mode == GL_COMBINE_ARB) {
        rgb->fn = e->combine_rgb, al->fn = e->combine_alpha;
        rgb->scale = e->rgb_scale, al->scale = e->alpha_scale;
        for (i = 0; i < 3; i++) {
            rgb->a[i].src = e->src_rgb[i], rgb->a[i].op = e->op_rgb[i];
            al->a[i].src = e->src_alpha[i], al->a[i].op = e->op_alpha[i];
        }
        return;
    }
    if (family == FAM_RGB)
        SET(al, GL_REPLACE, P, Z, Z);
    else if (e->mode == GL_REPLACE)
        SET(al, GL_REPLACE, T, Z, Z);
    else if (e->mode == GL_DECAL)
        SET(al, GL_REPLACE, P, Z, Z);
    else if (family == FAM_INTENSITY && e->mode == GL_ADD)
        SET(al, GL_ADD, P, T, Z);
    else if (family == FAM_INTENSITY && e->mode == GL_BLEND)
        SET(al, GL_INTERPOLATE_ARB, K, P, T);
    else
        SET(al, GL_MODULATE, P, T, Z);
    if (family == FAM_ALPHA)
        SET(rgb, GL_REPLACE, P, Z, Z);
    else
        switch (e->mode) {
        case GL_REPLACE: SET(rgb, GL_REPLACE, T, Z, Z); break;
        case GL_DECAL:
            if (family == FAM_RGBA)
                SET(rgb, GL_INTERPOLATE_ARB, T, P, TA);
            else
                SET(rgb, GL_REPLACE, T, Z, Z);
            break;
        case GL_ADD: SET(rgb, GL_ADD, P, T, Z); break;
        case GL_BLEND: SET(rgb, GL_INTERPOLATE_ARB, K, P, T); break;
        default: SET(rgb, GL_MODULATE, P, T, Z); break;
        }
#undef SET
}

/* An argument's input byte (Mesa's get_input_arg/get_input_mapping): flags 1 invert, 2 expand (dot3's [-1, 1]). The
 * alpha combiner takes every operand as alpha. hw: this combiner (= the chip's unit); u0: the chip's unit for GL's
 * unit 0's texture. */
static uint32_t in_byte(const carg *a, int alpha, int hw, int flags)
{
    uint32_t s, m;
    int colour_op = !alpha && (a->op == GL_SRC_COLOR || a->op == GL_ONE_MINUS_SRC_COLOR);
    int neg = a->op == GL_ONE_MINUS_SRC_COLOR || a->op == GL_ONE_MINUS_SRC_ALPHA;
    switch (a->src) {
    case GL_ZERO: s = RS_ZERO; break;
    case GL_TEXTURE: s = RS_TEX0 + (uint32_t)hw; break;
    case GL_TEXTURE0_ARB: s = RS_TEX0 + (uint32_t)hw_unit_of[0]; break;
    case GL_TEXTURE1_ARB: s = RS_TEX0 + (uint32_t)hw_unit_of[1]; break;
    case GL_CONSTANT_ARB: s = RS_CONST0 + (uint32_t)hw; break;
    case GL_PRIMARY_COLOR_ARB: s = RS_PRIMARY; break;
    default: s = hw ? RS_SPARE0 : RS_PRIMARY; break;   /* GL_PREVIOUS_ARB */
    }
    if (neg == !(flags & 1))
        m = flags & 2 ? RM_EXPAND_NEG : RM_INVERT;
    else
        m = flags & 2 ? RM_EXPAND : RM_IDENTITY;
    return s | (colour_op ? 0u : RU_ALPHA) | m;
}

#define IN(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))

/* One combine on one combiner (Mesa's setup_combiner): its input word and its output word. */
static void combiner(const comb *c, int alpha, int hw, uint32_t *in, uint32_t *out)
{
    uint32_t A, B, C = RS_ZERO, D = RS_ZERO, o;
    switch (c->fn) {
    case GL_REPLACE:
        A = in_byte(&c->a[0], alpha, hw, 0), B = R_ONE;
        o = RO_AB_SPARE0;
        break;
    case GL_ADD:
    case GL_ADD_SIGNED_ARB:
        A = in_byte(&c->a[0], alpha, hw, 0), B = R_ONE;
        C = in_byte(&c->a[1], alpha, hw, 0), D = R_ONE;
        o = RO_SUM_SPARE0 | (c->fn == GL_ADD_SIGNED_ARB ? RO_BIAS : 0u);
        break;
    case GL_INTERPOLATE_ARB:
        A = in_byte(&c->a[0], alpha, hw, 0), B = in_byte(&c->a[2], alpha, hw, 0);
        C = in_byte(&c->a[1], alpha, hw, 0), D = in_byte(&c->a[2], alpha, hw, 1);
        o = RO_SUM_SPARE0;
        break;
    case GL_SUBTRACT_ARB:                       /* a0 * 1 + a1 * -1 */
        A = in_byte(&c->a[0], alpha, hw, 0), B = R_ONE;
        C = in_byte(&c->a[1], alpha, hw, 0), D = R_MINUS_ONE;
        o = RO_SUM_SPARE0;
        break;
    case GL_DOT3_RGB_ARB:
    case GL_DOT3_RGBA_ARB:
        A = in_byte(&c->a[0], alpha, hw, 2), B = in_byte(&c->a[1], alpha, hw, 2);
        o = RO_AB_SPARE0 | RO_AB_DOT;
        break;
    default:                                    /* GL_MODULATE */
        A = in_byte(&c->a[0], alpha, hw, 0), B = in_byte(&c->a[1], alpha, hw, 0);
        o = RO_AB_SPARE0;
        break;
    }
    if (alpha)
        o &= ~RO_AB_DOT;                        /* (the alpha combiner has no dot product: DOT3_RGBA below) */
    o |= c->scale >= 3.0f ? RO_SCALE4 : c->scale >= 1.5f ? RO_SCALE2 : 0u;
    *in = IN(A, B, C, D);
    *out = o;
}


/* The combiners for the chip's units in use, and the final combiner (with fog). */
static void combiners(const ctex *x[2], const dgl_texenv *env[2])
{
    uint32_t ain[2] = { 0, 0 }, aout[2] = { 0, 0 }, cin[2] = { 0, 0 }, cout[2] = { 0, 0 }, k[2] = { 0, 0 };
    uint32_t f0, f1;
    int hw, textured = 0, dot_alpha = 0;
    for (hw = 0; hw < 2; hw++) {
        comb rgb, al;
        if (!x[hw])
            continue;
        textured = 1;
        env_combines(env[hw], x[hw]->family, &rgb, &al);
        if (rgb.fn == GL_DOT3_RGBA_ARB) {
            /* (an alpha portion has no dot product, but the colour one's is in all three channels: with the second
             * combiner free, it copies spare0's blue into the alpha; on the second unit, the alpha's own combine) */
            if (hw == 0 && !x[1])
                dot_alpha = 1;
            else
                warn_once(7, "DOT3_RGBA on the second unit: the alpha drawn by the alpha's combine");
        }
        combiner(&rgb, 0, hw, &cin[hw], &cout[hw]);
        combiner(&al, 1, hw, &ain[hw], &aout[hw]);
        k[hw] = argb(env[hw]->color);
    }
    if (dot_alpha) {
        cin[1] = IN(RS_SPARE0, R_ONE, RS_ZERO, RS_ZERO), cout[1] = RO_AB_SPARE0;     /* (the colour as it is) */
        ain[1] = IN(RS_SPARE0, R_ONE, RS_ZERO, RS_ZERO), aout[1] = RO_AB_SPARE0;     /* (usage RGB here: blue) */
    }
    /* (Mesa: the second combiner's operation bits say how many combiners run: 3 one, 5 two) */
    cout[1] |= (x[1] || dot_alpha ? 5u : 3u) << 27;
    for (hw = 0; hw < 2; hw++) {
        mc(M_RC_IN_ALPHA(hw), ain[hw]);
        mc(M_RC_IN_RGB(hw), cin[hw]);
        mc(M_RC_COLOR(hw), k[hw]);
        mc(M_RC_OUT_ALPHA(hw), aout[hw]);
        mc(M_RC_OUT_RGB(hw), cout[hw]);
    }
    /* Final: x = A B + (1 - A) C + D, alpha G; A the fog factor (or one), C the fog colour, B the combiners' result
     * (or the vertex colour); D = E F nothing (no secondary colour on this path). */
    {
        uint32_t b = textured ? RS_SPARE0 : RS_PRIMARY;
        if (fogging)
            f0 = IN(RS_FOG | RU_ALPHA, b, RS_FOG, RS_ZERO), f1 = IN(RS_FOG | RU_ALPHA, RS_ZERO, b | RU_ALPHA, 0);
        else
            f0 = IN(R_ONE, b, R_ONE, RS_ZERO), f1 = IN(R_ONE, RS_ZERO, b | RU_ALPHA, 0);
    }
    mc(M_RC_FINAL0, f0);
    mc(M_RC_FINAL1, f1);
}

/* Fog: the mode, the colour, and the coefficients that make GL's factor from the eye distance (DCFOG's model). */
static void fog_state(void)
{
    float k0, k1;
    uint32_t mode;
    mc(M_FOG_ENABLE, (uint32_t)fogging);
    if (!fogging)
        return;
    if (dgl_gl.fog_mode == GL_LINEAR) {
        float d = dgl_gl.fog_end - dgl_gl.fog_start;
        mode = 0x2601;
        if (d != 0.0f)
            k0 = 2.0f + dgl_gl.fog_start / d, k1 = -1.0f / d;
        else
            k0 = 2.0f, k1 = 0.0f;               /* (f = 1, as the CPU's path) */
    } else if (dgl_gl.fog_mode == GL_EXP) {
        mode = 0x802;                           /* (EXP_ABS: exp(-density |d|)) */
        k0 = 1.5f, k1 = -0.0901684f * dgl_gl.fog_density;
    } else {
        mode = 0x803;
        k0 = 1.5f, k1 = -0.2123305f * dgl_gl.fog_density;
    }
    mc(M_FOG_MODE, mode);
    mc(M_FOG_COORD, 0);                         /* (the vertex's own value) */
    mc(M_FOG_COLOR, 0xFF000000u | ub(dgl_gl.fog_color[2]) << 16 | ub(dgl_gl.fog_color[1]) << 8 | ub(dgl_gl.fog_color[0]));
    {
        uint32_t k[3];
        k[0] = fbits(k0), k[1] = fbits(k1), k[2] = fbits(0.0f);
        mcv(M_FOG_COEFF, k, 3);
    }
}

static void viewport_state(void)
{
    float sx = dgl_gl.viewport[2] * 0.5f, sy = dgl_gl.viewport[3] * 0.5f;
    float zmax = rg_tnt2.depth_bits == 24 ? 16777215.0f : 65535.0f;
    float n = (float)dgl_gl.depth_near, f = (float)dgl_gl.depth_far;
    uint32_t pm[16], tr[4];
    int i;
    for (i = 0; i < 16; i++)
        pm[i] = 0;
    pm[0] = fbits(sx), pm[5] = fbits(-sy), pm[10] = fbits(zmax * (f - n) * 0.5f), pm[15] = fbits(1.0f);
    tr[0] = fbits(dgl_gl.viewport[0] + sx - 2048.0f);
    tr[1] = fbits((float)dgl_ctx.height - (dgl_gl.viewport[1] + sy) - 2048.0f);
    tr[2] = fbits(zmax * (f + n) * 0.5f);
    tr[3] = 0;
    guard_x = sx > 0.0f ? GUARD_PX / sx : 1.0f;
    guard_y = sy > 0.0f ? GUARD_PX / sy : 1.0f;
    guard_x = guard_x < 1.0f ? 1.0f : guard_x > 64.0f ? 64.0f : guard_x;
    guard_y = guard_y < 1.0f ? 1.0f : guard_y > 64.0f ? 64.0f : guard_y;
    mcv(M_PROJECTION, pm, 16);
    mcv(M_VP_TRANSLATE, tr, 4);
    {
        uint32_t dr[2];
        dr[0] = fbits(0.0f), dr[1] = fbits(zmax);
        mcv(M_DEPTH_NEAR, dr, 2);
    }
}

static void target_state(void)
{
    int i;
    tnt2_celsius_target(rg_tnt2_color_off(dgl_ctx.draw_front), rg_tnt2.z_off, rg_tnt2.pitch_bytes, rg_tnt2.width,
                        rg_tnt2.height, rg_tnt2.bpp);
    /* (its RT_HORIZ/VERT and the rest written there: no longer what the cache holds) */
    for (i = 0x200 / 4; i <= 0x214 / 4; i++)
        sh_ok[i] = 0;
    mc(M_CLIP_HORIZ, (uint32_t)(0x800 + rg_tnt2.width - 1) << 16 | 0x800u);
    mc(M_CLIP_VERT, (uint32_t)(0x800 + rg_tnt2.height - 1) << 16 | 0x800u);
}

static void validate(void)
{
    dgl_texture *t = dgl_gl.texture_2d ? dgl_unit_texture(0) : NULL;
    dgl_texture *t1 = dgl_gl.texture_2d1 ? dgl_unit_texture(1) : NULL;
    const ctex *x[2] = { NULL, NULL };
    const dgl_texenv *env[2] = { NULL, NULL };
    int c[4], stencil = dgl_gl.stencil_test && rg_tnt2.depth_bits == 24;
    if (valid_ok && t == valid_tex && t1 == valid_tex1 && !(t && (t->dirty || t->level[0].idx)) &&
        !(t1 && (t1->dirty || t1->level[0].idx)) &&
        !(dgl_gl.dirty & (DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP)))
        return;
    flush();
    if (!target_ok || (dgl_gl.dirty & DGL_DIRTY_TARGET)) {
        target_state();
        target_ok = 1;
    }
    scissor(1, c);
    nothing = c[2] <= c[0] || c[3] <= c[1];
    if (!nothing) {
        mc(M_RT_HORIZ, (uint32_t)(c[2] - c[0]) << 16 | (uint32_t)c[0]);
        mc(M_RT_VERT, (uint32_t)(c[3] - c[1]) << 16 | (uint32_t)c[1]);
    }
    viewport_state();
    /* Textures: GL's unit 1 alone goes to the chip's unit 0 (its PREVIOUS is the vertex colour then). */
    hw_unit_of[0] = 0, hw_unit_of[1] = 1;
    cur_t0 = cur_t1 = NULL;
    if (t1 && !t) {
        int k;
        hw_unit_of[1] = 0;
        cur_t0 = (ctex *)(x[0] = tex_unit(t1, 1, 0));
        env[0] = dgl_tex_env(1);
        mc(M_TEX_ENABLE(1), 0);
        swap_st = 1;                            /* (its coordinates, and their whole-period shifts, in pair 0) */
        shift_s[0] = shift_t[0] = 0;            /* (GL's unit 0 untextured) */
        k = shift_s[0], shift_s[0] = shift_s[1], shift_s[1] = k;
        k = shift_t[0], shift_t[0] = shift_t[1], shift_t[1] = k;
    } else {
        swap_st = 0;
        cur_t0 = (ctex *)(x[0] = tex_unit(t, 0, 0));
        cur_t1 = (ctex *)(x[1] = tex_unit(t1, 1, 1));
        env[0] = dgl_tex_env(0), env[1] = dgl_tex_env(1);
    }
    fogging = dgl_gl.fog;
    combiners(x, env);
    fog_state();
    /* Blending: GL's factors are Celsius's words (GL_ZERO 0, GL_ONE 1, GL_SRC_COLOR 0x300 ...). */
    mc(M_BLEND_ENABLE, (uint32_t)!!dgl_gl.blend);
    if (dgl_gl.blend) {
        mc(M_BLEND_SRC, (uint32_t)dgl_gl.blend_src);
        mc(M_BLEND_DST, (uint32_t)dgl_gl.blend_dst);
        mc(M_BLEND_EQUATION, 0x8006);           /* GL_FUNC_ADD */
    }
    mc(M_ALPHA_ENABLE, (uint32_t)!!dgl_gl.alpha_test);
    if (dgl_gl.alpha_test) {
        mc(M_ALPHA_FUNC, (uint32_t)dgl_gl.alpha_func);
        mc(M_ALPHA_REF, ub(dgl_gl.alpha_ref));
    }
    if (dgl_gl.depth_test && rg_tnt2.z_off) {
        mc(M_DEPTH_ENABLE, 1);
        mc(M_DEPTH_FUNC, (uint32_t)dgl_gl.depth_func);
        mc(M_DEPTH_WRITE, (uint32_t)!!dgl_gl.depth_mask);
    } else {
        mc(M_DEPTH_ENABLE, 0);
        mc(M_DEPTH_WRITE, 0);
    }
    mc(M_DITHER_ENABLE, (uint32_t)!!dgl_gl.dither);
    mc(M_CULL_ENABLE, 0);                       /* (culling here) */
    mc(M_LIGHTING_ENABLE, 0);
    mc(M_SHADE_MODEL, 0x1D01);                  /* (smooth: flat sends the provoking vertex's colour at each) */
    mc(M_COLOR_MASK, (dgl_gl.color_mask[3] && rg_tnt2.bpp == 32 ? 0x01000000u : 0u) |
                     (dgl_gl.color_mask[0] ? 0x00010000u : 0u) | (dgl_gl.color_mask[1] ? 0x00000100u : 0u) |
                     (dgl_gl.color_mask[2] ? 0x00000001u : 0u));
    mc(M_STENCIL_ENABLE, (uint32_t)stencil);
    if (stencil) {
        mc(M_STENCIL_MASK, dgl_gl.stencil_writemask & 255u);
        mc(M_STENCIL_FUNC, (uint32_t)dgl_gl.stencil_func);
        mc(M_STENCIL_REF, (uint32_t)dgl_gl.stencil_ref & 255u);
        mc(M_STENCIL_FMASK, dgl_gl.stencil_mask & 255u);
        mc(M_STENCIL_FAIL, (uint32_t)dgl_gl.stencil_fail);  /* (GL's enums: GL_KEEP 0x1E00 ...) */
        mc(M_STENCIL_ZFAIL, (uint32_t)dgl_gl.stencil_zfail);
        mc(M_STENCIL_ZPASS, (uint32_t)dgl_gl.stencil_zpass);
    } else if (dgl_gl.stencil_test)
        warn_once(5, "the stencil test needs a 32-bit context (Z24S8): not drawn at 16 bits");
    {
        int off = dgl_gl.offset_fill && (dgl_gl.offset_factor != 0.0f || dgl_gl.offset_units != 0.0f);
        mc(M_OFFSET_FILL, (uint32_t)off);
        mc(M_OFFSET_LINE, 0), mc(M_OFFSET_POINT, 0);
        if (off) {
            uint32_t fu[2];
            fu[0] = fbits(dgl_gl.offset_factor), fu[1] = fbits(dgl_gl.offset_units);
            mcv(M_OFFSET_FACTOR, fu, 2);
        }
    }
    dgl_gl.dirty &= ~(DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG | DGL_DIRTY_CLIP);
    valid_ok = 1;
    valid_tex = t;
    valid_tex1 = t1;
}

/* ---- Drawing ---------------------------------------------------------------- */

static int tb_begin(void)
{
    if (!dgl_ctx.active || !rg_tnt2.active || va_get() != 0)
        return 0;
    validate();
    return !nothing;
}

/* A draw's end: its triangles to the chip. (Kept until the next state change instead, GLQuake was no faster on
 * bench E: its draws change a texture almost every time. DCPERF: a thousand draws cost what one does.) */
static void tb_end(void)
{
    flush();
}

/* A vertex's two outcodes (bit set: outside that plane): the view volume's (returned: wholly outside one means
 * nothing to draw) and the clipper's (*guard: near, far and the guard band's planes; none set, nothing to clip). From
 * the signs of the distances read as integers (the TNT2 back end's: no x87 compares). */
static unsigned outcodes(const dgl_cvtx *v, unsigned *guard)
{
    union { float f; uint32_t u; } d[10];
    float gx = guard_x * v->w, gy = guard_y * v->w;
    d[0].f = v->z + v->w, d[1].f = v->w - v->z;
    d[2].f = v->x + v->w, d[3].f = v->w - v->x, d[4].f = v->y + v->w, d[5].f = v->w - v->y;
    d[6].f = v->x + gx, d[7].f = gx - v->x, d[8].f = v->y + gy, d[9].f = gy - v->y;
    *guard = (d[0].u >> 31) | (d[1].u >> 31) << 1 | (d[6].u >> 31) << 2 | (d[7].u >> 31) << 3 |
             (d[8].u >> 31) << 4 | (d[9].u >> 31) << 5;
    return (d[0].u >> 31) | (d[1].u >> 31) << 1 | (d[2].u >> 31) << 2 | (d[3].u >> 31) << 3 |
           (d[4].u >> 31) << 4 | (d[5].u >> 31) << 5;
}

/* Twice the triangle's area in normalised device coordinates, times w0 w1 w2 (positive after clipping): its sign is
 * the winding's, y up (GL's window orientation). */
static double det3(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c)
{
    return (double)a->x * ((double)b->y * c->w - (double)c->y * b->w) -
           (double)a->y * ((double)b->x * c->w - (double)c->x * b->w) +
           (double)a->w * ((double)b->x * c->y - (double)c->x * b->y);
}

/* Is a polygon (clip space, w > 0) culled, or of no area? */
static int culled(const dgl_cvtx *v, int n)
{
    double area = 0;
    int i, front;
    for (i = 1; i + 1 < n; i++)
        area += det3(&v[0], &v[i], &v[i + 1]);
    if (area == 0) {
        dgl_prims.zero_area++;
        return 1;
    }
    if (!dgl_gl.cull_face)
        return 0;
    front = dgl_gl.front_face == GL_CCW ? area > 0 : area < 0;
    if (dgl_gl.cull_mode == GL_FRONT_AND_BACK || (dgl_gl.cull_mode == GL_BACK) != front) {
        dgl_prims.culled++;
        return 1;
    }
    return 0;
}

static void vert(cvert *o, const dgl_cvtx *c, const dgl_cvtx *colour)
{
    o->x = c->x, o->y = c->y, o->z = c->z, o->w = c->w;
    o->col = colour_of(colour);
    if (swap_st)
        o->s = c->s1, o->t = c->t1, o->s1 = c->s, o->t1 = c->t;
    else
        o->s = c->s, o->t = c->t, o->s1 = c->s1, o->t1 = c->t1;
    o->fog = c->eye_d;
}

/* By whole periods so that the smallest coordinate is in [0, 1) (the TNT2 back end's: a float's interpolation loses
 * huge coordinates' fractions, conformance t14). */
static void wrap_shift(cvert *v, int n)
{
    int u;
    for (u = 0; u < 2; u++) {
        float ms, mt;
        int i;
        if (!shift_s[u] && !shift_t[u])
            continue;
        ms = u ? v[0].s1 : v[0].s, mt = u ? v[0].t1 : v[0].t;
        for (i = 1; i < n; i++) {
            float s = u ? v[i].s1 : v[i].s, t = u ? v[i].t1 : v[i].t;
            if (s < ms) ms = s;
            if (t < mt) mt = t;
        }
        ms = shift_s[u] && ms > -1e9f && ms < 1e9f ? (float)mga_ifloor(ms) : 0;
        mt = shift_t[u] && mt > -1e9f && mt < 1e9f ? (float)mga_ifloor(mt) : 0;
        if (ms != 0 || mt != 0)
            for (i = 0; i < n; i++) {
                if (u) v[i].s1 -= ms, v[i].t1 -= mt;
                else v[i].s -= ms, v[i].t -= mt;
            }
    }
}

/* A convex polygon's fan (clip space, inside the view volume) into the vertex area as triangles. */
static int eng;                                 /* the chip's transform drew last: the CPU path's state owed */
static void cpu_state(void);

static void emit_fan(const dgl_cvtx *v, int n, const dgl_cvtx *prov)
{
    cvert w[9 * 3], p[9];
    int i, k = 0, flat = dgl_gl.shade_model == GL_FLAT;
    if (eng)
        cpu_state();
    if (n > 9)
        n = 9;
    for (i = 0; i < n; i++)
        vert(&p[i], &v[i], flat && prov ? prov : &v[i]);
    if (shift_s[0] || shift_t[0] || shift_s[1] || shift_t[1])
        wrap_shift(p, n);
    for (i = 1; i + 1 < n; i++) {
        w[k++] = p[0], w[k++] = p[i], w[k++] = p[i + 1];
    }
    if (va_room((uint32_t)k) != 0)
        return;
    memcpy((void *)(tnt2_fb + va_off + va_n * VSIZE), w, (size_t)k * VSIZE);
    va_n += (uint32_t)k;
    tris += (uint32_t)(n - 2);
}

static void tb_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    PROF_SCOPE(PROF_D_PROJ);
    dgl_cvtx in[3], out[9];
    unsigned ga, gb, gc;
    int n, i;
    dgl_prims.tris_in++;
    if (outcodes(a, &ga) & outcodes(b, &gb) & outcodes(c, &gc)) {
        dgl_prims.clipped++;
        return;
    }
    in[0] = *a, in[1] = *b, in[2] = *c;
    if (!(ga | gb | gc)) {
        if (!culled(in, 3))
            emit_fan(in, 3, prov);
        return;
    }
    if (dgl_gl.shade_model == GL_FLAT)
        for (i = 0; i < 3; i++) {
            in[i].r = prov->r, in[i].g = prov->g, in[i].b = prov->b, in[i].a = prov->a;
        }
    {
        int o = PROF_SWITCH(PROF_D_CLIP);
        n = dgl_clip_polygon(in, 3, out, guard_x, guard_y);
        PROF_BACK(o);
    }
    if (n < 3) {
        dgl_prims.clipped++;
        return;
    }
    if (!culled(out, n))
        emit_fan(out, n, dgl_gl.shade_model == GL_FLAT ? &out[0] : NULL);
}

/* A polygon whole: inside the clipper's planes, its fan as it is; else triangle by triangle (each clipped). */
static void tb_polygon(const dgl_cvtx *v, int n, const dgl_cvtx *prov)
{
    unsigned any = 0, all = ~0u;
    int i;
    if (n < 3)
        return;
    for (i = 0; i < n; i++) {
        unsigned g;
        all &= outcodes(&v[i], &g);
        any |= g;
    }
    if (all) {
        dgl_prims.clipped++;
        return;
    }
    if (any || n > 9) {
        for (i = 1; i + 1 < n; i++)
            tb_triangle(&v[0], &v[i], &v[i + 1], prov);
        return;
    }
    dgl_prims.tris_in += (unsigned)(n - 2);
    if (!culled(v, n)) {
        emit_fan(v, n, prov);
        dgl_prims.seqs++;
    }
}

/* A line: a quad half the width either side across its minor axis, offset in window units (scaled back into clip
 * space by each end's w, so the chip's divide puts it there). */
static void tb_line(const dgl_cvtx *a, const dgl_cvtx *b)
{
    dgl_cvtx in[3], out[9], q[4];
    float hw = (dgl_gl.line_width > 1.0f ? dgl_gl.line_width : 1.0f) * 0.5f;
    float sx = dgl_gl.viewport[2] * 0.5f, sy = dgl_gl.viewport[3] * 0.5f, dx, dy;
    int i;
    in[0] = *a; in[1] = *b; in[2] = *b;
    if (dgl_gl.shade_model == GL_FLAT) {
        in[0].r = b->r; in[0].g = b->g; in[0].b = b->b; in[0].a = b->a;
    }
    if (dgl_clip_polygon(in, 3, out, guard_x, guard_y) < 2)
        return;
    dx = (out[1].x / out[1].w - out[0].x / out[0].w) * sx;
    dy = (out[1].y / out[1].w - out[0].y / out[0].w) * sy;
    for (i = 0; i < 4; i++)
        q[i] = out[i < 2 ? 0 : 1];
    if (fabsf(dx) >= fabsf(dy)) {               /* x-major: widen in y */
        float o0 = hw / sy * q[0].w, o1 = hw / sy * q[2].w;
        q[0].y -= o0, q[1].y += o0, q[2].y += o1, q[3].y -= o1;
    } else {
        float o0 = hw / sx * q[0].w, o1 = hw / sx * q[2].w;
        q[0].x -= o0, q[1].x += o0, q[2].x += o1, q[3].x -= o1;
    }
    emit_fan(q, 4, NULL);
    lines_++;
}

/* A point: a square of the point size about its centre; none when the centre is outside the view volume. */
static void tb_point(const dgl_cvtx *a)
{
    dgl_cvtx q[4];
    float h = (dgl_gl.point_size > 1.0f ? dgl_gl.point_size : 1.0f) * 0.5f;
    float ox = h / (dgl_gl.viewport[2] * 0.5f) * a->w, oy = h / (dgl_gl.viewport[3] * 0.5f) * a->w;
    int i;
    if (dgl_outcode(a, 1.0f, 1.0f))
        return;
    for (i = 0; i < 4; i++)
        q[i] = *a;
    q[0].x -= ox, q[0].y -= oy;
    q[1].x += ox, q[1].y -= oy;
    q[2].x += ox, q[2].y += oy;
    q[3].x -= ox, q[3].y += oy;
    emit_fan(q, 4, NULL);
    points_++;
}

/* ---- The chip's own transform (Q5): whole draws, object space -------------------------------------------------
 * A draw from the arrays (or glBegin's, DGL_TNL_IMM) whose state the chip does: its vertices as GL gives them (object
 * space, w as given: VTXBUF_FMT's homogeneous bit) into the vertex area, GL's own primitive with 16-bit indices, the
 * projection viewport x P x MV (PROJECTION takes the object position: DCTNL), MODELVIEW for the eye distance fog
 * needs (FOG_COORD 3: the eye plane's distance, DCTNL's fog identical to the CPU path's). The chip clips (DCTNL's near
 * case) and culls by GL's winding. Stage 1: unlit, no texgen; texture matrices applied while the vertices are
 * copied (affine ones; projective ones the CPU's path). DGL_TNL=0: every draw the CPU's path. */
static int tnl_on = -1;
static unsigned long tnl_draws_n, tnl_small_n, tnl_tris_n, tnl_fogged_n;

static const float *tex_matrix(int unit)
{
    return unit ? (dgl_gl.tex1_identity ? NULL : dgl_gl.tex1.stack[dgl_gl.tex1.depth - 1].m)
                : (dgl_gl.tex_identity ? NULL : dgl_gl.tex.stack[dgl_gl.tex.depth - 1].m);
}

static int tex_matrix_ok(int unit)
{
    const float *m = tex_matrix(unit);
    return !m || (m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] != 0.0f);
}

static void tex_affine(const float *m, float st[2], float r)
{
    float s0 = st[0], t0 = st[1], iq = 1.0f / m[15];
    st[0] = (m[0] * s0 + m[4] * t0 + m[8] * r + m[12]) * iq;
    st[1] = (m[1] * s0 + m[5] * t0 + m[9] * r + m[13]) * iq;
}

/* ---- Lighting on the chip (Mesa's nv10_state_tnl.c, MIT: Francisco Jerez) ------------------------------------
 * The lights' colours as products with the material (the colour material's part from the vertex instead), the
 * scene's ambient and emission, positions (eye space, as GL keeps them) or directions with their half vectors,
 * attenuation, the spot's and the shininess's coefficients (Mesa's fits to the chip's curves: compared with the
 * CPU's lighting before trusting them), MODELVIEW and its inverse's first three columns for the normals.
 * DGL_TNL_LIGHT=0: lit draws the CPU's path. */
#define M_LIGHT_MODEL     0x294
#define M_COLOR_MATERIAL  0x298
#define M_NORMALIZE       0x3A4
#define M_MATERIAL_FACTOR 0x3A8
#define M_MATERIAL_ALPHA  0x3B4
#define M_SEP_SPECULAR    0x3B8
#define M_ENABLED_LIGHTS  0x3BC
#define M_INVERSE_MV      0x480
#define M_SHININESS       0x6A0
#define M_SCENE_AMBIENT   0x6C4
#define M_LIGHT(i, o)     (0x800u + 0x80u * (uint32_t)(i) + (o))
#define L_AMBIENT 0x00
#define L_DIFFUSE 0x0C
#define L_SPECULAR 0x18
#define L_HALF 0x28
#define L_DIRECTION 0x34
#define L_SPOT 0x40
#define L_POSITION 0x5C
#define L_ATTEN 0x68

static int light_ok(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("DGL_TNL_LIGHT");
        on = !(e && *e == '0');
    }
    return on;
}

static float get_shine(const float p[], float x)
{
    const int n = 15;
    const float *y = &p[1];
    float f = (n - 1) * (1 - 1 / (1 + p[0] * x)) / (1 - 1 / (1 + p[0] * 1024));
    int i = (int)f;
    if (x == 0)
        return y[0];
    else if (i > n - 2)
        return y[n - 1];
    return y[i] + (y[i + 1] - y[i]) * (f - (float)i);
}

static const float spot_params[2][16] = {
    { 0.02f, -3.80e-05f, -1.77f, -2.41f, -2.71f, -2.88f, -2.98f, -3.06f, -3.11f, -3.17f, -3.23f, -3.28f, -3.37f,
      -3.47f, -3.83f, -5.11f },
    { 0.02f, -0.01f, 1.77f, 2.39f, 2.70f, 2.87f, 2.98f, 3.06f, 3.10f, 3.16f, 3.23f, 3.27f, 3.37f, 3.47f, 3.83f, 5.11f },
};

static const float shininess_params[6][16] = {
    { 0.70f, 0.00f, 0.06f, 0.06f, 0.05f, 0.04f, 0.02f, 0.00f, -0.06f, -0.13f, -0.24f, -0.36f, -0.51f, -0.66f, -0.82f,
      -1.00f },
    { 0.01f, 1.00f, -2.29f, -2.77f, -2.96f, -3.06f, -3.12f, -3.18f, -3.24f, -3.29f, -3.36f, -3.43f, -3.51f, -3.75f,
      -4.33f, -5.11f },
    { 0.02f, 0.00f, 2.28f, 2.75f, 2.94f, 3.04f, 3.1f, 3.15f, 3.18f, 3.22f, 3.27f, 3.32f, 3.39f, 3.48f, 3.84f, 5.11f },
    { 0.70f, 0.00f, 0.05f, 0.06f, 0.06f, 0.06f, 0.05f, 0.04f, 0.02f, 0.01f, -0.03f, -0.12f, -0.25f, -0.43f, -0.68f,
      -0.99f },
    { 0.01f, 1.00f, -1.61f, -2.35f, -2.67f, -2.84f, -2.96f, -3.05f, -3.08f, -3.14f, -3.2f, -3.26f, -3.32f, -3.42f,
      -3.54f, -4.21f },
    { 0.01f, 0.00f, 2.25f, 2.73f, 2.92f, 3.03f, 3.09f, 3.15f, 3.16f, 3.21f, 3.25f, 3.29f, 3.35f, 3.43f, 3.56f, 4.22f },
};

static void norm3(const float *a, float *r)
{
    float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    l = l > 0 ? 1.0f / l : 0.0f;
    r[0] = a[0] * l, r[1] = a[1] * l, r[2] = a[2] * l;
}

static void spot_coeff(const dgl_light *l, float k[7])
{
    float e = l->spot_exp, a0, b0, a1, a2, b2, a3, d[3];
    float cosc = cosf(l->spot_cutoff * 3.14159265358979f / 180.0f);
    norm3(l->spot_dir, d);
    a0 = e > 0 ? -1 - 5.36e-3f / sqrtf(e) : -1;
    b0 = 1 / (1 + 0.273f * e);
    a1 = get_shine(spot_params[0], e);
    a2 = get_shine(spot_params[1], e);
    b2 = 1 / (1 + 0.273f * e);
    a3 = 0.9f + 0.278f * e;
    {
        float cutoff = 1 / (1 - cosc);
        if (cutoff < a3)
            cutoff = a3;
        k[0] = a0 + b0 * cutoff > 0 ? a0 + b0 * cutoff : 0;
        k[1] = a1;
        k[2] = a2 + b2 * cutoff;
        k[3] = -cutoff * d[0], k[4] = -cutoff * d[1], k[5] = -cutoff * d[2];
        k[6] = 1 - cutoff;
    }
}

static void fv(uint32_t mthd, const float *f, int n)
{
    uint32_t w[16];
    int i;
    for (i = 0; i < n; i++)
        w[i] = fbits(f[i]);
    mcv(mthd, w, n);
}

/* LIGHT_HALF_VECTOR and LIGHT_ATTENUATION write the same word of the chip's light (envytools' hwtest: both its
 * vector 3), a directional light's and a positional light's: sent each time, never from the cache (t40: a light made
 * positional and then directional again kept the attenuation as its half vector, and lost its highlight). */
static void fv_alias(uint32_t mthd, const float *f)
{
    uint32_t w[3], i0;
    int i, l;
    for (i = 0; i < 3; i++)
        w[i] = fbits(f[i]);
    tnt2_methods(S7, mthd, w, 3);
    l = (int)((mthd - 0x800u) / 0x80u);
    for (i0 = M_LIGHT(l, L_HALF) >> 2, i = 0; i < 3; i++)
        sh_ok[i0 + (uint32_t)i] = 0;
    for (i0 = M_LIGHT(l, L_ATTEN) >> 2, i = 0; i < 3; i++)
        sh_ok[i0 + (uint32_t)i] = 0;
}

static void inverse_mv_state(void);

static void light_state(void)
{
    const dgl_material *m = &dgl_gl.material[0];
    int cm = dgl_gl.color_material && dgl_gl.color_material_face != GL_BACK;
    GLenum cmode = dgl_gl.color_material_mode;
    int cm_em = cm && cmode == GL_EMISSION, cm_am = cm && (cmode == GL_AMBIENT || cmode == GL_AMBIENT_AND_DIFFUSE);
    int cm_di = cm && (cmode == GL_DIFFUSE || cmode == GL_AMBIENT_AND_DIFFUSE), cm_sp = cm && cmode == GL_SPECULAR;
    uint32_t en = 0;
    float scene[3], factor[3] = { 0, 0, 0 }, k[7];
    int i, c;
    mc(M_LIGHTING_ENABLE, 1);
    mc(M_NORMALIZE, (uint32_t)(dgl_gl.normalize || dgl_gl.rescale_normal));
    mc(M_LIGHT_MODEL, dgl_gl.light_local_viewer ? 0x10000u : 0u);
    mc(M_SEP_SPECULAR, 0);
    mc(M_COLOR_MATERIAL, (uint32_t)(cm_em ? 1 : 0) | (cm_am ? 2u : 0u) | (cm_di ? 4u : 0u) | (cm_sp ? 8u : 0u));
    /* The scene's colour: emission + material ambient x the model's ambient (the colour material's part per vertex,
     * MATERIAL_FACTOR the rest); Mesa's nv10_emit_material_ambient. */
    for (c = 0; c < 3; c++) {
        if (cm_am)
            scene[c] = dgl_gl.light_model_ambient[c], factor[c] = m->emission[c];
        else if (cm_em)
            scene[c] = m->ambient[c] * dgl_gl.light_model_ambient[c];
        else
            scene[c] = m->emission[c] + m->ambient[c] * dgl_gl.light_model_ambient[c];
    }
    fv(M_SCENE_AMBIENT, scene, 3);
    if (cm)
        fv(M_MATERIAL_FACTOR, factor, 3);
    mcf_one(M_MATERIAL_ALPHA, m->diffuse[3]);
    {
        float sh[6], x = m->shininess < 0 ? 0 : m->shininess > 1024 ? 1024 : m->shininess;
        for (i = 0; i < 6; i++)
            sh[i] = get_shine(shininess_params[i], x);
        fv(M_SHININESS, sh, 6);
    }
    for (i = 0; i < 8; i++) {
        const dgl_light *l = &dgl_gl.light[i];
        float a[3], d[3], sp[3];
        if (!l->enabled)
            continue;
        for (c = 0; c < 3; c++) {
            a[c] = cm_am ? l->ambient[c] : l->ambient[c] * m->ambient[c];
            d[c] = cm_di ? l->diffuse[c] : l->diffuse[c] * m->diffuse[c];
            sp[c] = cm_sp ? l->specular[c] : l->specular[c] * m->specular[c];
        }
        fv(M_LIGHT(i, L_AMBIENT), a, 3);
        fv(M_LIGHT(i, L_DIFFUSE), d, 3);
        fv(M_LIGHT(i, L_SPECULAR), sp, 3);
        if (l->position[3] == 0) {              /* (a direction towards the light, and the far viewer's half vector) */
            float v[3], h[3];
            norm3(l->position, v);
            h[0] = v[0], h[1] = v[1], h[2] = v[2] + 1.0f;
            norm3(h, h);
            fv(M_LIGHT(i, L_DIRECTION), v, 3);
            fv_alias(M_LIGHT(i, L_HALF), h);
            en |= 1u << 2 * i;                  /* (NONPOSITIONAL) */
        } else {
            float iw = 1.0f / l->position[3], pos[3];
            pos[0] = l->position[0] * iw, pos[1] = l->position[1] * iw, pos[2] = l->position[2] * iw;
            fv(M_LIGHT(i, L_POSITION), pos, 3);
            fv_alias(M_LIGHT(i, L_ATTEN), l->att);
            if (l->spot_cutoff != 180.0f) {
                spot_coeff(l, k);
                fv(M_LIGHT(i, L_SPOT), k, 7);
                en |= 3u << 2 * i;              /* (Mesa's "DIRECTIONAL": a spot) */
            } else
                en |= 2u << 2 * i;              /* (POSITIONAL) */
        }
    }
    mc(M_ENABLED_LIGHTS, en);
    inverse_mv_state();
}

/* ---- Texture coordinates generated by the chip (Mesa's nv10_emit_tex_gen, nv10_emit_tex_mat) -------------------
 * A unit's s and t both generated, object or eye linear or a sphere map (r and q not): TEX_GEN_MODE and the planes
 * (GL keeps the eye plane in eye space already), and the unit's texture matrix on the chip (TEX_MATRIX), as its
 * coordinates don't exist until the chip makes them. Otherwise generation stays the CPU's (DGL_TNL_TEXGEN=0: always). */
#define M_TEX_GEN_MODE(u, c)  (0x3C0u + 0x10u * (uint32_t)(u) + 4u * (uint32_t)(c))
#define M_TEX_GEN_COEFF(u, c) (0x600u + 0x40u * (uint32_t)(u) + 0x10u * (uint32_t)(c))
#define M_TEX_MATRIX_ON(u)    (0x3E0u + 4u * (uint32_t)(u))
#define M_TEX_MATRIX(u)       (0x540u + 0x40u * (uint32_t)(u))

static void matrix_words(uint32_t *w, const float *m);

static uint32_t gen_code(GLenum m)
{
    return m == GL_EYE_LINEAR ? 0x2400u : m == GL_OBJECT_LINEAR ? 0x2401u : m == GL_SPHERE_MAP ? 0x2402u : 0u;
}

static int texgen_ok(int u)
{
    static int on = -1;
    GLenum m;
    if (!dgl_gl.texgen_on[u])
        return 1;
    if (on < 0) {
        const char *e = getenv("DGL_TNL_TEXGEN");
        on = !(e && *e == '0');
    }
    m = dgl_gl.texgen[u][0].mode;
    return on && dgl_gl.texgen_on[u] == 3u && dgl_gl.texgen[u][1].mode == m && gen_code(m) != 0;
}

/* GL's unit g generates on the chip (a textured unit with generation on). */
static int generated(int g)
{
    return dgl_gl.texgen_on[g] && (g ? valid_tex1 != NULL : valid_tex != NULL);
}

static int sphere_used(void)
{
    return (generated(0) && dgl_gl.texgen[0][0].mode == GL_SPHERE_MAP) ||
           (generated(1) && dgl_gl.texgen[1][0].mode == GL_SPHERE_MAP);
}

static void inverse_mv_state(void)
{
    const dgl_mat4 *mv = &dgl_gl.mv.stack[dgl_gl.mv.depth - 1];
    dgl_mat4 inv;
    uint32_t w[12];
    int r;
    if (dgl_mat_inverse(&inv, mv) == 0)         /* (singular: the identity, as the Radeon back end) */
        for (r = 0; r < 16; r++)
            inv.m[r] = (r % 5) == 0 ? 1.0f : 0.0f;
    for (r = 0; r < 12; r++)                    /* (its first three columns, as Mesa sends m->inv) */
        w[r] = fbits(inv.m[r]);
    mcv(0x480, w, 12);
}

/* The chip's units' generation and texture matrices for an engine draw (gen: 1); or all off (the CPU's path). */
static void texgen_state(int engine)
{
    int h;
    for (h = 0; h < 2; h++) {
        int g = swap_st ? 1 - h : h, c;
        if (engine && generated(g) && !(swap_st && h == 1)) {
            const float *tm = tex_matrix(g);
            static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            uint32_t w[16];
            for (c = 0; c < 2; c++) {
                const dgl_texgen *t = &dgl_gl.texgen[g][c];
                mc(M_TEX_GEN_MODE(h, c), gen_code(t->mode));
                if (t->mode != GL_SPHERE_MAP) {
                    const float *pl = t->mode == GL_OBJECT_LINEAR ? t->object : t->eye;
                    uint32_t pw[4];
                    int k;
                    for (k = 0; k < 4; k++)
                        pw[k] = fbits(pl[k]);
                    mcv(M_TEX_GEN_COEFF(h, c), pw, 4);
                }
            }
            mc(M_TEX_GEN_MODE(h, 2), 0), mc(M_TEX_GEN_MODE(h, 3), 0);
            matrix_words(w, tm ? tm : ident);
            mcv(M_TEX_MATRIX(h), w, 16);
            mc(M_TEX_MATRIX_ON(h), 1);
        } else {
            for (c = 0; c < 4; c++)
                mc(M_TEX_GEN_MODE(h, c), 0);
            mc(M_TEX_MATRIX_ON(h), 0);
        }
    }
}

static int tnl_state_ok(GLenum mode)
{
    if (mode == GL_POINTS || mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
        return 0;                               /* (lines and points: the CPU's path draws their widths) */
    if ((dgl_gl.lighting && (dgl_gl.light_two_side || !light_ok())) || !texgen_ok(0) || !texgen_ok(1) ||
        dgl_gl.clip_enabled)
        return 0;                               /* (two-sided lighting and user clip planes: the chip has neither) */
    if ((dgl_gl.texture_cube[0] | dgl_gl.texture_cube[1]) || dgl_gl.polygon_mode != GL_FILL)
        return 0;
    return tex_matrix_ok(0) && tex_matrix_ok(1);
}

/* GL's column-major matrix to the chip's sixteen words (row-major). */
static void matrix_words(uint32_t *w, const float *m)
{
    int r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            w[r * 4 + c] = fbits(m[c * 4 + r]);
}

/* The CPU path's own state again after the chip's transform drew: the viewport-only projection, the vertex's fog
 * value, no culling by the chip, smooth shading. */
static void cpu_state(void)
{
    eng = 0;
    viewport_state();
    if (fogging)
        mc(M_FOG_COORD, 0);
    mc(M_CULL_ENABLE, 0);
    mc(M_SHADE_MODEL, 0x1D01);
    mc(M_LIGHTING_ENABLE, 0);
    texgen_state(0);
    set_base2(offs_base, 0);                    /* (the normals' slot off) */
}

/* A draw's vertices vmin.. (nv of them) converted into 40-byte vertices at video memory offset at: 0 if one can't be
 * (coordinates past 1,024 lose their fraction in the chip's floats, the CPU's path taking whole periods off per
 * triangle, conformance t14; a position not finite, the CPU's path clipping it). */
static int pack(uint32_t at, uint32_t nat, GLint vmin, int nv, const float *tm0, const float *tm1)
{
    cvert c[64];
    float nr[64][3];
    dgl_vin v;
    int i, j = 0;
    for (i = 0; i < nv; i++) {
        cvert *o = &c[j];
        dgl_vertex_of(vmin + i, &v);
        if (tm0)
            tex_affine(tm0, v.tex, dgl_gl.texr_used ? v.texr[0] : 0.0f);
        if (tm1)
            tex_affine(tm1, v.tex1, dgl_gl.texr_used ? v.texr[1] : 0.0f);
        if (v.tex[0] > 1024.0f || v.tex[0] < -1024.0f || v.tex[1] > 1024.0f || v.tex[1] < -1024.0f ||
            v.tex1[0] > 1024.0f || v.tex1[0] < -1024.0f || v.tex1[1] > 1024.0f || v.tex1[1] < -1024.0f ||
            !(v.pos[0] - v.pos[0] == 0.0f) || !(v.pos[1] - v.pos[1] == 0.0f) ||
            !(v.pos[2] - v.pos[2] == 0.0f) || !(v.pos[3] - v.pos[3] == 0.0f))
            return 0;
        o->x = v.pos[0], o->y = v.pos[1], o->z = v.pos[2], o->w = v.pos[3];
        o->col = ub(v.col[3]) << 24 | ub(v.col[0]) << 16 | ub(v.col[1]) << 8 | ub(v.col[2]);
        if (swap_st)
            o->s = v.tex1[0], o->t = v.tex1[1], o->s1 = v.tex[0], o->t1 = v.tex[1];
        else
            o->s = v.tex[0], o->t = v.tex[1], o->s1 = v.tex1[0], o->t1 = v.tex1[1];
        o->fog = 0;
        if (nat)
            nr[j][0] = v.nrm[0], nr[j][1] = v.nrm[1], nr[j][2] = v.nrm[2];
        if (++j == 64 || i + 1 == nv) {
            memcpy((void *)(tnt2_fb + at), c, (size_t)j * VSIZE);
            at += (uint32_t)j * VSIZE;
            if (nat) {
                memcpy((void *)(tnt2_fb + nat), nr, (size_t)j * NSIZE);
                nat += (uint32_t)j * NSIZE;
            }
            j = 0;
        }
    }
    return 1;
}

/* ---- Buffer objects' vertices kept in video memory (the Radeon back end's X3b cache, this card's vertices) -------
 * A draw from the arrays whose every array it reads is in a GL_STATIC_DRAW buffer: its vertices 0..its highest
 * index converted once into a cache area and drawn from there, indices only, while nothing they came from changed
 * (the buffers' generations, the arrays' layout, the current values standing for arrays that are off, the texture
 * matrices applied while converting). DGL_TNL_VBO=0: every draw converted. */
#define VC_SLOTS 1024
#define VC_HASH 2048
typedef struct {
    struct { const void *b, *ptr; unsigned serial, gen; GLsizei stride; GLenum type; GLint size, en; } a[5];
    float cur[13], m0[16], m1[16];
    int units, has_m0, has_m1, texr_used, swap, lit;
} vkey;
static struct { vkey k; uint32_t off, noff, nv; int live, next; } vc[VC_SLOTS];
static int vc_head[VC_HASH];
static uint32_t vc_block, vc_used, vc_bytes, heap_bytes;
static int vc_mode = -1, vc_count;
static unsigned long vc_hits, vc_made;

static unsigned vc_hash(const vkey *k)
{
    const unsigned char *q = (const unsigned char *)k->a;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < sizeof k->a; i++)
        h = (h ^ q[i]) * 16777619u;
    return (h ^ (uint32_t)k->units * 31u) & (VC_HASH - 1);
}

static int vc_arr(vkey *k, int n, const dgl_array *a, int needed)
{
    if (!a->enabled || !needed)
        return 1;
    if (!a->buf || a->buf->usage != GL_STATIC_DRAW_ARB || a->buf->mapped)
        return 0;
    k->a[n].b = a->buf, k->a[n].ptr = a->ptr, k->a[n].serial = a->buf->serial, k->a[n].gen = a->buf->gen;
    k->a[n].stride = a->stride, k->a[n].type = a->type, k->a[n].size = a->size, k->a[n].en = 1;
    return 1;
}

/* The draw's key (units: bit 0 GL's unit 0 textured, bit 1 unit 1), or 0: not one to keep. Only what pack reads
 * goes in it (the Radeon's lesson: Echoes' sky and HUD's current values remade every untextured chunk). */
static int vc_key(vkey *k, int units, int lit, const float *tm0, const float *tm1)
{
    if (vc_mode < 0) {
        const char *e = getenv("DGL_TNL_VBO");
        vc_mode = !(e && *e == '0');
    }
    if (!vc_mode || !dgl_draw_from_arrays() || !dgl_gl.va.enabled || !dgl_gl.va.buf)
        return 0;
    memset(k, 0, sizeof *k);
    if (!vc_arr(k, 0, &dgl_gl.va, 1) || !vc_arr(k, 1, &dgl_gl.ca, 1) || !vc_arr(k, 2, &dgl_gl.ta, units & 1) ||
        !vc_arr(k, 3, &dgl_gl.ta1, units & 2) || !vc_arr(k, 4, &dgl_gl.na, lit))
        return 0;
    if (lit && !dgl_gl.na.enabled)
        memcpy(k->cur + 10, dgl_gl.cur_normal, 3 * sizeof(float));
    k->lit = lit;
    if (!dgl_gl.ca.enabled)
        memcpy(k->cur, dgl_gl.cur_color, 4 * sizeof(float));
    if (units & 1) {
        if (!dgl_gl.ta.enabled)
            memcpy(k->cur + 4, dgl_gl.cur_tex, 2 * sizeof(float));
        if (tm0)
            memcpy(k->m0, tm0, sizeof k->m0), k->has_m0 = 1;
        if (dgl_gl.texr_used)
            k->cur[8] = dgl_gl.cur_texr[0], k->texr_used = 1;
    }
    if (units & 2) {
        if (!dgl_gl.ta1.enabled)
            memcpy(k->cur + 6, dgl_gl.cur_tex1, 2 * sizeof(float));
        if (tm1)
            memcpy(k->m1, tm1, sizeof k->m1), k->has_m1 = 1;
        if (dgl_gl.texr_used)
            k->cur[9] = dgl_gl.cur_texr[1], k->texr_used = 1;
    }
    k->units = units, k->swap = swap_st;
    return 1;
}

/* The video memory offset of the draw's vertices 0..vmax, converted now if they aren't kept yet; 0: they can't be. */
static uint32_t vc_get(const vkey *k, GLint vmax, const float *tm0, const float *tm1, uint32_t *noff)
{
    uint32_t nv = (uint32_t)vmax + 1u, vbytes = (nv * VSIZE + 63u) & ~63u,
             bytes = vbytes + (k->lit ? (nv * NSIZE + 63u) & ~63u : 0u), off;
    unsigned h = vc_hash(k);
    int i, slot;
    for (i = vc_head[h] - 1; i >= 0; i = vc[i].next - 1)
        if (vc[i].live && !memcmp(&vc[i].k, k, sizeof *k)) {
            if (vc[i].nv >= nv) {
                vc_hits++;
                *noff = vc[i].noff;
                return vc[i].off;
            }
            vc[i].live = 0;                     /* (kept for fewer vertices: made again, bigger) */
            break;
        }
    if (!vc_block) {
        uint32_t want = heap_bytes >= (48u << 20) ? 16u << 20 : heap_bytes >= (24u << 20) ? 8u << 20 : 4u << 20;
        for (vc_bytes = want; vc_bytes > (4u << 20); vc_bytes >>= 1)
            if (dgl_vram_alloc(vc_bytes + 256u, &vc_block) == 0)
                break;
        if (vc_bytes == (4u << 20) && room(vc_bytes + 256u, &vc_block, NULL) != 0) {
            vc_block = 0, vc_mode = 0;
            return 0;
        }
    }
    if (bytes > vc_bytes / 4u)
        return 0;
    if (vc_used + bytes > vc_bytes || vc_count == VC_SLOTS) {
        flush();                                /* (full: everything kept goes, once the chip has drawn from it) */
        if (tnt2_sync(1000000) != 0)
            return 0;
        memset(vc_head, 0, sizeof vc_head);
        vc_used = 0, vc_count = 0;
    }
    slot = vc_count;
    off = ((vc_block + 255u) & ~255u) + vc_used;
    *noff = k->lit ? off + vbytes : 0u;
    if (!pack(off, *noff, 0, (int)nv, tm0, tm1))
        return 0;
    vc_count++;
    vc_used += bytes;
    vc[slot].k = *k, vc[slot].off = off, vc[slot].noff = *noff, vc[slot].nv = nv, vc[slot].live = 1;
    vc[slot].next = vc_head[h], vc_head[h] = slot + 1;
    vc_made++;
    return off;
}

static int tb_tnl(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    PROF_SCOPE(PROF_D_SETUP);
    const float *tm0, *tm1;
    GLint vmin, vmax, i;
    uint32_t base, blk = 0, nblk = 0, w[128];
    int nv, k, n, lit;
    vkey key;
    if (tnl_on < 0) {
        const char *e = getenv("DGL_TNL");
        tnl_on = !(e && *e == '0');
    }
    if (!tnl_on || count < 3 || nothing || !tnl_state_ok(mode))
        return 0;
    if (idx) {
        vmin = vmax = idx[0];
        for (i = 1; i < count; i++) {
            if (idx[i] < vmin) vmin = idx[i];
            if (idx[i] > vmax) vmax = idx[i];
        }
    } else
        vmin = first, vmax = first + count - 1;
    nv = vmax - vmin + 1;
    {   /* (a draw of fewer than DGL_TNL_MINV vertices, 256 by default, through the CPU's path: on the Quadro2 the
         * engine's cost a draw beats its transform below that; Echoes' scenes, docs/nv10.md Q5) */
        static int minv = -1;
        if (minv < 0) {
            const char *e = getenv("DGL_TNL_MINV");
            minv = e && *e ? atoi(e) : 256;
        }
        if (nv < minv) {
            tnl_small_n++;
            return 0;
        }
    }
    tm0 = generated(0) ? NULL : tex_matrix(0), tm1 = generated(1) ? NULL : tex_matrix(1);
    lit = dgl_gl.lighting != 0 || sphere_used();    /* (normals: lighting's, or a sphere map's) */
    flush();                                    /* (the CPU path's triangles first) */
    if (vmax < 65536 && vc_key(&key, (valid_tex && !generated(0) ? 1 : 0) | (valid_tex1 && !generated(1) ? 2 : 0),
                               lit, tm0, tm1))
        blk = vc_get(&key, vmax, tm0, tm1, &nblk);
    if (blk) {
        base = 0;                               /* (kept: the indices as they are, from vertex 0) */
        vmin = 0;
    } else {
        if ((uint32_t)nv * VSIZE > VA_PART || (idx && nv > 4 * count))
            return 0;                           /* (a huge or sparse range: the CPU's path) */
        if (va_room((uint32_t)nv) != 0)
            return 0;
        base = va_n;
        if (!pack(va_off + base * VSIZE, lit ? na_off + base * NSIZE : 0u, vmin, nv, tm0, tm1))
            return 0;
        va_n = va_first = base + (uint32_t)nv;
    }
    /* The chip's state for its transform. */
    {
        float vps[16] = { 0 }, vpm[16];
        uint32_t pw[16];
        const float *mvp = dgl_mvp()->m;
        int r, c2, q;
        vps[0] = dgl_gl.viewport[2] * 0.5f, vps[5] = -dgl_gl.viewport[3] * 0.5f;
        vps[10] = (rg_tnt2.depth_bits == 24 ? 16777215.0f : 65535.0f) *
                  (float)(dgl_gl.depth_far - dgl_gl.depth_near) * 0.5f;
        vps[15] = 1.0f;
        for (c2 = 0; c2 < 4; c2++)              /* (vpm = vps x mvp, column-major) */
            for (r = 0; r < 4; r++) {
                float sum = 0;
                for (q = 0; q < 4; q++)
                    sum += vps[q * 4 + r] * mvp[c2 * 4 + q];
                vpm[c2 * 4 + r] = sum;
            }
        matrix_words(pw, vpm);
        mcv(M_PROJECTION, pw, 16);
        if (fogging || lit || generated(0) || generated(1)) {
            matrix_words(pw, dgl_gl.mv.stack[dgl_gl.mv.depth - 1].m);
            mcv(M_MODELVIEW, pw, 16);
        }
        texgen_state(1);
        if (sphere_used() && !dgl_gl.lighting) {
            inverse_mv_state();                 /* (a sphere map's normals: their matrix and GL_NORMALIZE, t41) */
            mc(M_NORMALIZE, (uint32_t)(dgl_gl.normalize || dgl_gl.rescale_normal));
        }
        if (fogging)
            mc(M_FOG_COORD, 3);
        mc(M_CULL_ENABLE, (uint32_t)!!dgl_gl.cull_face);
        if (dgl_gl.cull_face) {
            mc(M_CULL_FACE, (uint32_t)dgl_gl.cull_mode);
            mc(M_FRONT_FACE, (uint32_t)dgl_gl.front_face);
        }
        mc(M_SHADE_MODEL, dgl_gl.shade_model == GL_FLAT ? 0x1D00u : 0x1D01u);
        if (dgl_gl.lighting)
            light_state();                      /* (specular into the primary colour: LIGHT_MODEL bit 1 clear) */
        else
            mc(M_LIGHTING_ENABLE, 0);
        eng = 1;
    }
    set_base2(blk ? blk : va_off, lit ? (blk ? nblk : na_off) : 0u);
    m1(M_VTXBUF_VALIDATE, 0);
    m1(M_BEGIN_END, (uint32_t)mode + 1u);       /* (Celsius's primitives are GL's, plus one) */
    for (k = 0; k + 1 < count; k += 2 * n) {
        n = (count - k) / 2;
        if (n > 128)
            n = 128;
        for (i = 0; i < n; i++) {
            uint32_t a = base + (uint32_t)((idx ? idx[k + 2 * i] : first + k + 2 * i) - vmin);
            uint32_t b = base + (uint32_t)((idx ? idx[k + 2 * i + 1] : first + k + 2 * i + 1) - vmin);
            w[i] = b << 16 | a;
        }
        tnt2_methods(S7, M_ELEMENT16, w, n);
    }
    if (count & 1)
        m1(M_ELEMENT32, base + (uint32_t)((idx ? idx[count - 1] : first + count - 1) - vmin));
    m1(M_BEGIN_END, 0);
    {
        uint32_t m = tnt2_mark();
        if (!blk)
            va_mark[(va_n - 1u) * VSIZE / VA_PART] = m;
        if (cur_t0) cur_t0->mark = m;
        if (cur_t1) cur_t1->mark = m;
    }
    tnt2_kick_soon();
    if (!tnl_draws_n)
        DGL_WARN("DGL-NV10 the transform engine draws (DGL_TNL=0: the CPU transforms instead)");   /* (said once) */
    tnl_draws_n++, tnl_fogged_n += fogging != 0;
    tnl_tris_n += (unsigned long)count / 3u;
    dgl_prims.tris_in += (unsigned long)count / 3u;
    return 1;
}

static int tb_fast(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    GLint ids[DGL_POLY_MAX];
    GLsizei i;
    if (mode != GL_POLYGON || count < 4 || count > DGL_POLY_MAX)
        return 0;
    for (i = 0; i < count; i++)
        ids[i] = idx ? idx[i] : first + i;
    return dgl_poly_general(ids, (int)count, ids[0]);
}

static void install(void)
{
    valid_ok = target_ok = 0;
    memset(sh_ok, 0, sizeof sh_ok);
    va_formats = 0;
    col_set = 0;
    if (tnt2_celsius_init() != 0)
        warn_once(4, "Celsius's set-up didn't finish");
    dgl_sink.begin = tb_begin;
    dgl_sink.triangle = tb_triangle;
    dgl_sink.fast = tb_fast;
    dgl_sink.polygon = tb_polygon;
    dgl_sink.line = tb_line;
    dgl_sink.point = tb_point;
    dgl_sink.end = tb_end;
    dgl_sink.tnl = tb_tnl;
    eng = 0;
}

/* Clears by the 2D engine, in the scissor: colour, and the depth buffer (with the stencil's byte in Z24S8). */
static void clear(GLbitfield mask)
{
    int c[4];
    if (!dgl_ctx.active || !rg_tnt2.active)
        return;
    flush();
    scissor(0, c);
    if (mask & GL_COLOR_BUFFER_BIT) {
        const GLfloat *k = dgl_gl.clear_color;
        uint32_t v = rg_tnt2.bpp == 32
                   ? ub(k[3]) << 24 | ub(k[0]) << 16 | ub(k[1]) << 8 | ub(k[2])
                   : (ub(k[0]) >> 3) << 11 | (ub(k[1]) >> 2) << 5 | (ub(k[2]) >> 3);
        if (!dgl_gl.color_mask[0] || !dgl_gl.color_mask[1] || !dgl_gl.color_mask[2])
            warn_once(6, "a colour clear with a colour mask clears every channel");
        tnt2_set_target(rg_tnt2_color_off(dgl_ctx.draw_front), rg_tnt2.pitch_bytes, rg_tnt2.bpp);
        tnt2_set_clip(c[0], c[1], c[2], c[3]);
        tnt2_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
    }
    if (rg_tnt2.z_off && (((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask) || (mask & GL_STENCIL_BUFFER_BIT))) {
        double d = dgl_gl.clear_depth < 0 ? 0 : dgl_gl.clear_depth > 1 ? 1 : dgl_gl.clear_depth;
        uint32_t v = rg_tnt2.depth_bits == 24
                   ? (uint32_t)(d * 16777215.0 + 0.5) << 8 | ((unsigned)dgl_gl.clear_stencil & 255u)
                   : (uint32_t)(d * 65535.0 + 0.5);
        if (!((mask & GL_DEPTH_BUFFER_BIT) && dgl_gl.depth_mask))
            warn_once(8, "a stencil clear clears the depth buffer too");
        tnt2_set_target(rg_tnt2.z_off, rg_tnt2.pitch_bytes, rg_tnt2.bpp);
        tnt2_set_clip(c[0], c[1], c[2], c[3]);
        tnt2_fill(c[0], c[1], c[2] - c[0], c[3] - c[1], v);
    }
    tnt2_kick();
}

static void caps(dgl_caps *c)
{
    static char name[48];
    memset(c, 0, sizeof *c);
    strcpy(name, tnt2.name ? tnt2.name : "NV1x");
    c->renderer = name;
    c->texture_units = 2;
    c->sub_narrow = 1;
    c->max_texture_size = 2048;
    c->dot3 = 1;
    if (tnl_on < 0) {
        const char *e = getenv("DGL_TNL");
        tnl_on = !(e && *e == '0');
    }
    c->hw_tnl = tnl_on;                         /* (GL_DGL_hw_tnl: games' presets read it) */
}

static void synced(void) { dgl_vram_sync_done(); }

static int tex_sub(dgl_texture *t, int level, int x, int y, int w, int h)
{
    ctex *tx = (ctex *)t->be;
    const dgl_level *l = &t->level[level];
    if (!tx || level != 0 || tx->levels != 1 || tx->pal || !l->rgba || t->dirty || l->w != (1 << tx->lw) ||
        l->h != (1 << tx->lh) || x < 0 || y < 0 || x + w > l->w || y + h > l->h)
        return 0;
    flush();
    {
        int o = PROF_SWITCH(PROF_D_DRAIN), r = tnt2_mark_wait(tx->mark, 1000000);
        PROF_BACK(o);
        if (r != 0)
            return 0;
    }
    tnt2_tex_sub_rgba8(tx->off, tx->code, tx->lw, tx->lh, x, y, w, h, l->rgba, l->w);
    {
        /* (the texture words sent again before the next draw, as the TNT2's layer does after texels written in
         * place: the chip's texture cache may hold the old ones) */
        int u;
        for (u = 0; u < 2; u++)
            sh_ok[M_TEX_OFFSET(u) >> 2] = sh_ok[M_TEX_FORMAT(u) >> 2] = sh_ok[M_TEX_FILTER(u) >> 2] =
                sh_ok[M_TEX_ENABLE(u) >> 2] = 0;
        valid_ok = 0;
    }
    dgl_texc.sub_fast++;
    return 1;
}

static void tex_free(dgl_texture *t) { tex_drop(t); }

static int tex_info(const dgl_texture *t, int rgba[4])
{
    const ctex *x = (const ctex *)t->be;
    uint32_t f = x ? x->format & 0xF80u : 0;
    if (!x)
        return 0;
    if (f == TF_R5G6B5) rgba[0] = 5, rgba[1] = 6, rgba[2] = 5, rgba[3] = 0;
    else if (f == TF_A1R5G5B5) rgba[0] = rgba[1] = rgba[2] = 5, rgba[3] = 1;
    else if (f == TF_A4R4G4B4) rgba[0] = rgba[1] = rgba[2] = rgba[3] = 4;
    else rgba[0] = rgba[1] = rgba[2] = rgba[3] = 8;
    return 1;
}

static void tex_reset(uint32_t start, uint32_t end)
{
    int i, n = dgl_texture_count();
    dgl_texture *t;
    valid_ok = target_ok = 0;
    for (i = 0; i < n; i++)
        if ((t = dgl_texture_nth(i)) && t->be) {
            free(t->be);
            t->be = NULL;
        }
    cur_t0 = cur_t1 = NULL;
    dgl_vram_init(start, end);
    heap_bytes = end - start;
    vc_block = 0, vc_used = 0, vc_count = 0;    /* (the kept vertices went with the heap) */
    memset(vc_head, 0, sizeof vc_head);
    va_off = 0;                                 /* (the vertex area: from the new heap at the next draw) */
}

static void stats(dgl_be_stats *s)
{
    memset(s, 0, sizeof *s);
    s->tris_offloaded = tris + (uint32_t)tnl_tris_n;
    s->tris_tnl = (uint32_t)tnl_tris_n;
    s->tnl_draws = (uint32_t)tnl_draws_n;
    s->tnl_fogged = (uint32_t)tnl_fogged_n;
    s->tnl_small_cpu = (uint32_t)tnl_small_n;
    s->tnl_vbo_hits = (uint32_t)vc_hits;
    s->tnl_vbo_made = (uint32_t)vc_made;
    s->mem_used = dgl_vram_used();
}

/* The NV1x's back end where the TNT2 device found one (DGL_NV_CLASS=95: the TNT2's, on the 0x95 class: Q2's). */
int dgl_nv10_wanted(void)
{
    const char *e = getenv("DGL_NV_CLASS");
    return tnt2.arch == 10 && !(e && atoi(e) == 95);
}

const dgl_backend dgl_nv10_backend = {
    .name = "nv10",
    .dev = &rg_tnt2_device,
    .caps = caps,
    .install = install,
    .clear = clear,
    .synced = synced,
    .tex_sub = tex_sub,
    .tex_free = tex_free,
    .tex_info = tex_info,
    .tex_reset = tex_reset,
    .tex_change = NULL,
    .stats = stats,
};
