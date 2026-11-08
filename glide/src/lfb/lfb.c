/* lfb.c - linear framebuffer access (PRD §7.8).
 *
 * Read locks and 565 upper-left write locks get a pointer straight into
 * VRAM. Everything else gets a shadow buffer at the Voodoo's stride (wider
 * for games wider than 1024 pixels), pre-filled with a per-lock sentinel so
 * that unlock writes back only the pixels the game touched, converted to
 * 565. In scaled modes (mg.present_scaled) writes to the front buffer are
 * shown once they are done. */
#include "glide/mg.h"
#include "trace/trace.h"
#include "mga/sys.h"
#include <string.h>

/* The Voodoo's 2048 bytes a line (1024 pixels) up to that width, else the
 * next power of two: set at every lock from the game's width. */
static uint32_t shadow_stride16 = 2048u;
#define SHADOW_STRIDE16 shadow_stride16
#define SHADOW_STRIDE32 (shadow_stride16 * 2u)

static struct {
    int       active;
    GrBuffer_t buffer;
    int       shadow;
    GrLfbWriteMode_t mode;
    GrOriginLocation_t origin;
    int       pipe;
    uint32_t  seed;
} lk;


static uint8_t *shadow;
static uint32_t shadow_size;
static GrColorFormat_t lfb_color_format = GR_COLORFORMAT_ARGB;
static uint16_t lfb_const_depth;
static uint8_t  lfb_const_alpha = 0xFF;

/* Sentinel rows: random patterns, a different row offset per lock and per
 * line. Rows are filled with memcpy and untouched rows found with memcmp,
 * which is what makes shadowed locks (and tracing) affordable. For 555 the
 * top bit is set and for 888 the top byte is 0xA5: values the game cannot
 * write in those modes. */
#define PAT_N 3072
static uint16_t pat16[PAT_N], pat16_555[PAT_N];
static uint32_t pat32[PAT_N], pat32_888[PAT_N];
static int pat_ready;

static void pat_init(void)
{
    uint32_t r = 0x2545F491u;
    int i;
    for (i = 0; i < PAT_N; i++) {
        r = r * 1664525u + 1013904223u;
        pat32[i] = r ^ (r >> 13);
        pat16[i] = (uint16_t)(pat32[i] >> 7);
        pat16_555[i] = (uint16_t)(pat16[i] | 0x8000u);
        pat32_888[i] = (pat32[i] & 0xFFFFFFu) | 0xA5000000u;
    }
    pat_ready = 1;
}

static uint32_t row_off(uint32_t y) { return (y * 37u + lk.seed) & 1023u; }

static const uint16_t *srow16(uint32_t y)
{
    return (lk.mode == GR_LFBWRITEMODE_555 ? pat16_555 : pat16) + row_off(y);
}

static const uint32_t *srow32(uint32_t y)
{
    return (lk.mode == GR_LFBWRITEMODE_888 ? pat32_888 : pat32) + row_off(y);
}

static int is32(GrLfbWriteMode_t m)
{
    return m == GR_LFBWRITEMODE_888 || m == GR_LFBWRITEMODE_8888 || m == GR_LFBWRITEMODE_565_DEPTH ||
           m == GR_LFBWRITEMODE_555_DEPTH || m == GR_LFBWRITEMODE_1555_DEPTH;
}

static uint16_t argb_to_565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

static uint32_t order_argb(uint32_t c)
{
    GrColorFormat_t saved = mg.color_format;
    uint32_t r;
    mg.color_format = lfb_color_format;
    r = mg_color_to_argb(c);
    mg.color_format = saved;
    return r;
}

static uint32_t expand565(uint16_t c)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

/* Colour-buffer rows in either depth (MGAGLIDE bpp=32 keeps 8888). */
static uint8_t *crow(uint32_t off, uint32_t y)
{
    return (uint8_t *)mga_fb + off + y * (uint32_t)mg.pitch_px * (uint32_t)(mg.bpp / 8);
}

static uint16_t get565(const uint8_t *row, uint32_t x)
{
    return mg.bpp == 32 ? argb_to_565(((const uint32_t *)row)[x]) : ((const uint16_t *)row)[x];
}

static void put565(uint8_t *row, uint32_t x, uint16_t c)
{
    if (mg.bpp == 32) ((uint32_t *)row)[x] = expand565(c);
    else ((uint16_t *)row)[x] = c;
}

static uint32_t load_argb(const uint8_t *row, uint32_t x)
{
    return mg.bpp == 32 ? ((const uint32_t *)row)[x] & 0xFFFFFFu : expand565(((const uint16_t *)row)[x]);
}

static void store_argb(uint8_t *row, uint32_t x, uint32_t argb)
{
    if (mg.bpp == 32) ((uint32_t *)row)[x] = argb & 0xFFFFFFu;
    else ((uint16_t *)row)[x] = argb_to_565(argb);
}

/* A 32-bit depth-buffer value as the Voodoo would hold it: the Z value's
 * top half, or in W mode the W code of the stored 2^31 * (1/w). */
static uint16_t aux_value32(uint32_t v)
{
    const mg_state *s = &mg.st;
    if (s->depth_mode == GR_DEPTHBUFFER_WBUFFER || s->depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS)
        return (uint16_t)mg_wcode_from_oow((double)v / 2147483648.0);
    return (uint16_t)(v >> 16);
}

/* Resolution override: game pixel (x, y) covers hardware pixels
 * [x0, x1) x [y0, y1); reads sample the pixel at the centre. */
static void hw_rect(uint32_t x, uint32_t y, uint32_t *x0, uint32_t *x1, uint32_t *y0, uint32_t *y1)
{
    if (!mg.scaled) {
        *x0 = x; *x1 = x + 1; *y0 = y; *y1 = y + 1;
        return;
    }
    *x0 = (uint32_t)(x * mg.sx); *x1 = (uint32_t)((x + 1) * mg.sx);
    *y0 = (uint32_t)(y * mg.sy); *y1 = (uint32_t)((y + 1) * mg.sy);
    if (*x1 <= *x0) *x1 = *x0 + 1;
    if (*y1 <= *y0) *y1 = *y0 + 1;
}

static void hw_centre(uint32_t x, uint32_t y, uint32_t *hx, uint32_t *hy)
{
    if (!mg.scaled) {
        *hx = x; *hy = y;
        return;
    }
    *hx = (uint32_t)((x + 0.5) * mg.sx);
    *hy = (uint32_t)((y + 0.5) * mg.sy);
}

static void wr565(uint32_t off, uint32_t x, uint32_t y, uint16_t c)
{
    uint32_t x0, x1, y0, y1, X, Y;
    hw_rect(x, y, &x0, &x1, &y0, &y1);
    for (Y = y0; Y < y1; Y++)
        for (X = x0; X < x1; X++)
            put565(crow(off, Y), X, c);
}

static void wrargb(uint32_t off, uint32_t x, uint32_t y, uint32_t argb)
{
    uint32_t x0, x1, y0, y1, X, Y;
    hw_rect(x, y, &x0, &x1, &y0, &y1);
    for (Y = y0; Y < y1; Y++)
        for (X = x0; X < x1; X++)
            store_argb(crow(off, Y), X, argb);
}

static void wrdepth(uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t x0, x1, y0, y1, X, Y, pitch = (uint32_t)mg.pitch_px;
    hw_rect(x, y, &x0, &x1, &y0, &y1);
    for (Y = y0; Y < y1; Y++)
        for (X = x0; X < x1; X++) {
            if (mg.zbits == 32) ((uint32_t *)(mga_fb + mg.aux_off))[Y * pitch + X] = z;
            else ((uint16_t *)(mga_fb + mg.aux_off))[Y * pitch + X] = (uint16_t)z;
        }
}

static uint16_t rd565(uint32_t off, uint32_t x, uint32_t y)
{
    uint32_t X, Y;
    hw_centre(x, y, &X, &Y);
    return get565(crow(off, Y), X);
}

static uint32_t rdaux(uint32_t x, uint32_t y)
{
    uint32_t X, Y, pitch = (uint32_t)mg.pitch_px;
    hw_centre(x, y, &X, &Y);
    return mg.zbits == 32 ? ((const uint32_t *)(mga_fb + mg.aux_off))[Y * pitch + X]
                          : ((const uint16_t *)(mga_fb + mg.aux_off))[Y * pitch + X];
}

static int need_shadow(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t mode, GrOriginLocation_t origin, FxBool pipe)
{
    if ((type & 1) == GR_LFB_READ_ONLY) {
        if (buffer == GR_BUFFER_AUXBUFFER)
            return mg.zbits != 16 || origin == GR_ORIGIN_LOWER_LEFT || mg.scaled;
        return origin == GR_ORIGIN_LOWER_LEFT || mg.bpp != 16 || mg.scaled;
    }
    if (pipe || mg_trace_on || mg.scaled)
        return 1;                /* tracing: the shadow shows which pixels were written */
    if (buffer == GR_BUFFER_AUXBUFFER)
        return mode != GR_LFBWRITEMODE_ZA16 || mg.zbits != 16 ||
               mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER;
    return !(mode == GR_LFBWRITEMODE_565 || mode == GR_LFBWRITEMODE_ANY) || origin == GR_ORIGIN_LOWER_LEFT ||
           mg.bpp != 16;
}

GR_ENTRY(FxBool, grLfbLock, (GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
                             GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t *info))
{
    uint32_t off;
    if (!mg.open || !info)
        return FXFALSE;
    if (lk.active)
        grLfbUnlock(GR_LFB_WRITE_ONLY, lk.buffer);
    engine_sync(200000);
    if (origin == GR_ORIGIN_ANY)
        origin = mg.st.origin;
    if (writeMode == GR_LFBWRITEMODE_ANY)
        writeMode = buffer == GR_BUFFER_AUXBUFFER ? GR_LFBWRITEMODE_ZA16 : GR_LFBWRITEMODE_565;
    off = mg_buffer_offset(buffer);
    lk.active = 1;
    lk.buffer = buffer;
    lk.mode = writeMode;
    lk.origin = origin;
    lk.shadow = need_shadow(type, buffer, writeMode, origin, pixelPipeline);
    shadow_stride16 = mg.width <= 1024 ? 2048u : (uint32_t)mga_pow2_pitch(mg.width) * 2u;
    lk.pipe = pixelPipeline && (type & 1) == GR_LFB_WRITE_ONLY && buffer != GR_BUFFER_AUXBUFFER;
    info->size = sizeof(GrLfbInfo_t);
    info->writeMode = writeMode;
    info->origin = origin;
    if (!lk.shadow) {
        info->lfbPtr = (void *)(mga_fb + off);
        info->strideInBytes = (FxU32)mg.pitch_px * 2u;
        return FXTRUE;
    }
    {
        uint32_t stride = is32(writeMode) ? SHADOW_STRIDE32 : SHADOW_STRIDE16;
        uint32_t need = stride * (uint32_t)mg.height, x, y;
        if (need > shadow_size) {
            sys_free(shadow);
            shadow = (uint8_t *)sys_alloc(need);
            shadow_size = shadow ? need : 0;
            if (!shadow) {
                lk.active = 0;
                return FXFALSE;
            }
        }
        lk.seed = lk.seed * 1664525u + 1013904223u + mg.frame;
        if ((type & 1) == GR_LFB_READ_ONLY) {
            /* Read locks through a shadow: convert the buffer out. */
            for (y = 0; y < (uint32_t)mg.height; y++) {
                uint32_t sy = origin == GR_ORIGIN_LOWER_LEFT ? (uint32_t)mg.height - 1 - y : y;
                uint16_t *d = (uint16_t *)(shadow + y * SHADOW_STRIDE16);
                for (x = 0; x < (uint32_t)mg.width; x++) {
                    if (buffer == GR_BUFFER_AUXBUFFER)
                        d[x] = mg.zbits == 32 ? aux_value32(rdaux(x, sy)) : (uint16_t)rdaux(x, sy);
                    else
                        d[x] = rd565(off, x, sy);
                }
            }
            info->lfbPtr = shadow;
            info->strideInBytes = SHADOW_STRIDE16;
            return FXTRUE;
        }
        if (!pat_ready)
            pat_init();
        for (y = 0; y < (uint32_t)mg.height; y++) {
            if (stride == SHADOW_STRIDE32)
                memcpy(shadow + y * stride, srow32(y), (uint32_t)mg.width * 4u);
            else
                memcpy(shadow + y * stride, srow16(y), (uint32_t)mg.width * 2u);
        }
        MGA_UNUSED(x);
        info->lfbPtr = shadow;
        info->strideInBytes = stride;
    }
    return FXTRUE;
}

/* ---- Pixel pipeline for LFB writes -------------------------------------
 * The retail runtime sends pipeline writes through chroma key, alpha test,
 * depth test and blending, but not through the colour combine (t18). The
 * Matrox has no such path, so it is done here on the CPU, exactly. */

static int cmp_pass(GrCmpFnc_t f, uint32_t a, uint32_t b)
{
    switch (f) {
    case GR_CMP_NEVER: return 0;
    case GR_CMP_LESS: return a < b;
    case GR_CMP_EQUAL: return a == b;
    case GR_CMP_LEQUAL: return a <= b;
    case GR_CMP_GREATER: return a > b;
    case GR_CMP_NOTEQUAL: return a != b;
    case GR_CMP_GEQUAL: return a >= b;
    default: return 1;
    }
}

static GrCmpFnc_t reversed(GrCmpFnc_t f)
{
    switch (f) {
    case GR_CMP_LESS: return GR_CMP_GREATER;
    case GR_CMP_LEQUAL: return GR_CMP_GEQUAL;
    case GR_CMP_GREATER: return GR_CMP_LESS;
    case GR_CMP_GEQUAL: return GR_CMP_LEQUAL;
    default: return f;
    }
}

/* Blend factor for one channel (0..255). 'other' is the opposite colour
 * (destination for a source factor and vice versa). */
static int blend_factor(GrAlphaBlendFnc_t f, int is_src, int own, int other, int sa, int da)
{
    MGA_UNUSED(own);
    switch (f) {
    case GR_BLEND_ZERO: return 0;
    case GR_BLEND_SRC_ALPHA: return sa;
    case GR_BLEND_SRC_COLOR: return other;          /* DST_COLOR as a source factor, SRC_COLOR as a dest one */
    case GR_BLEND_DST_ALPHA: return da;
    case GR_BLEND_ONE: return 255;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return 255 - sa;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return 255 - other;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return 255 - da;
    case GR_BLEND_ALPHA_SATURATE:
        return is_src ? (sa < 255 - da ? sa : 255 - da) : other;  /* dest: PREFOG_COLOR ~ source colour */
    default: return 255;
    }
}


/* One pipeline pixel: src is ARGB8888; depth is a Glide depth value. */
static void pipe_pixel(uint32_t src, uint16_t depth, uint8_t *row, uint32_t px, uint16_t *z16, uint32_t *z32)
{
    const mg_state *s = &mg.st;
    int sa = (int)(src >> 24), k;
    uint32_t out = 0;
    if (s->chroma_mode == GR_CHROMAKEY_ENABLE &&
        (src & 0xFFFFFF) == (mg_color_to_argb(s->chroma_value) & 0xFFFFFF))
        return;
    if (s->alpha_test_func != GR_CMP_ALWAYS && !cmp_pass(s->alpha_test_func, (uint32_t)sa, s->alpha_test_ref))
        return;
    if (s->depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux) {
        int wmode = s->depth_mode == GR_DEPTHBUFFER_WBUFFER || s->depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
        uint32_t dv = mg_depth_clear_value(depth), cur = mg.zbits == 32 ? *z32 : *z16;
        if (!cmp_pass(wmode ? reversed(s->depth_func) : s->depth_func, dv, cur))
            return;
        if (s->depth_mask) {
            if (mg.zbits == 32) *z32 = dv;
            else *z16 = (uint16_t)dv;
        }
    }
    if (!s->color_mask_rgb)
        return;
    if (s->blend_src == GR_BLEND_ONE && s->blend_dst == GR_BLEND_ZERO) {
        out = src;
    } else {
        uint32_t d = load_argb(row, px);
        for (k = 0; k < 24; k += 8) {
            int sc = (int)((src >> k) & 0xFF), dc = (int)((d >> k) & 0xFF), v;
            int fs = blend_factor(s->blend_src, 1, sc, dc, sa, 255);
            int fd = blend_factor(s->blend_dst, 0, dc, sc, sa, 255);
            v = (sc * fs + dc * fd + 127) / 255;
            out |= (uint32_t)(v > 255 ? 255 : v) << k;
        }
    }
    store_argb(row, px, out);
}

static void wrpipe(uint32_t off, uint32_t x, uint32_t y, uint32_t src, uint16_t depth)
{
    uint32_t x0, x1, y0, y1, X, Y, pitch = (uint32_t)mg.pitch_px;
    hw_rect(x, y, &x0, &x1, &y0, &y1);
    for (Y = y0; Y < y1; Y++)
        for (X = x0; X < x1; X++)
            pipe_pixel(src, depth, crow(off, Y), X, (uint16_t *)(mga_fb + mg.aux_off) + Y * pitch + X,
                       (uint32_t *)(mga_fb + mg.aux_off) + Y * pitch + X);
}

/* Decode one written LFB value to ARGB8888 (+ depth). */
static uint32_t decode_lfb(uint32_t v, uint16_t *depth)
{
    uint32_t r, g, b;
    *depth = lfb_const_depth;
    switch (lk.mode) {
    case GR_LFBWRITEMODE_565:
        return ((uint32_t)lfb_const_alpha << 24) | expand565((uint16_t)v);
    case GR_LFBWRITEMODE_555:
    case GR_LFBWRITEMODE_1555:
    case GR_LFBWRITEMODE_555_DEPTH:
    case GR_LFBWRITEMODE_1555_DEPTH:
        r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
        if (lk.mode == GR_LFBWRITEMODE_555_DEPTH || lk.mode == GR_LFBWRITEMODE_1555_DEPTH)
            *depth = (uint16_t)(v >> 16);
        return ((lk.mode == GR_LFBWRITEMODE_1555 || lk.mode == GR_LFBWRITEMODE_1555_DEPTH)
                    ? ((v & 0x8000) ? 0xFF000000u : 0) : ((uint32_t)lfb_const_alpha << 24)) |
               ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
    case GR_LFBWRITEMODE_565_DEPTH:
        *depth = (uint16_t)(v >> 16);
        return ((uint32_t)lfb_const_alpha << 24) | expand565((uint16_t)v);
    case GR_LFBWRITEMODE_888:
        return ((uint32_t)lfb_const_alpha << 24) | (order_argb(v) & 0xFFFFFF);
    default:                                            /* 8888 */
        return order_argb(v);
    }
}

static void write_back(void)
{
    uint32_t off = mg_buffer_offset(lk.buffer), x, y;
    int depth_buf = lk.buffer == GR_BUFFER_AUXBUFFER;
    for (y = 0; y < (uint32_t)mg.height; y++) {
        uint32_t dy = lk.origin == GR_ORIGIN_LOWER_LEFT ? (uint32_t)mg.height - 1 - y : y;
        if (lk.pipe) {
            const uint32_t *s32 = (const uint32_t *)(shadow + y * SHADOW_STRIDE32);
            const uint16_t *s16 = (const uint16_t *)(shadow + y * SHADOW_STRIDE16);
            const uint32_t *p32 = srow32(y);
            const uint16_t *p16 = srow16(y);
            for (x = 0; x < (uint32_t)mg.width; x++) {
                uint32_t v, sv;
                uint16_t d;
                if (is32(lk.mode)) {
                    v = s32[x]; sv = p32[x];
                } else {
                    v = s16[x]; sv = p16[x];
                }
                if (v == sv)
                    continue;
                {
                    uint32_t argb = decode_lfb(v, &d);   /* before d is read: argument order is unspecified */
                    wrpipe(off, x, dy, argb, d);
                }
            }
            continue;
        }
        if (is32(lk.mode)) {
            const uint32_t *s = (const uint32_t *)(shadow + y * SHADOW_STRIDE32), *pr = srow32(y);
            if (!memcmp(s, pr, (uint32_t)mg.width * 4u))
                continue;                                   /* untouched row */
            for (x = 0; x < (uint32_t)mg.width; x++) {
                uint32_t v = s[x], sv = pr[x];
                uint16_t c;
                if (v == sv)
                    continue;
                switch (lk.mode) {
                case GR_LFBWRITEMODE_888:
                case GR_LFBWRITEMODE_8888:
                    wrargb(off, x, dy, order_argb(v));
                    break;
                default: {                                  /* colour + depth */
                    uint16_t cv = (uint16_t)(v & 0xFFFF), d = (uint16_t)(v >> 16);
                    if (lk.mode == GR_LFBWRITEMODE_565_DEPTH) c = cv;
                    else c = (uint16_t)(((cv & 0x7FE0) << 1) | ((cv >> 4) & 0x20) | (cv & 0x1F));
                    wr565(off, x, dy, c);
                    if (mg.has_aux)
                        wrdepth(x, dy, mg_depth_clear_value(d));
                    break; }
                }
            }
        } else {
            const uint16_t *s = (const uint16_t *)(shadow + y * SHADOW_STRIDE16), *pr = srow16(y);
            if (!memcmp(s, pr, (uint32_t)mg.width * 2u))
                continue;                                   /* untouched row */
            for (x = 0; x < (uint32_t)mg.width; x++) {
                uint16_t v = s[x], sv = pr[x];
                if (v == sv)
                    continue;
                if (depth_buf)
                    wrdepth(x, dy, mg_depth_clear_value(v));
                else if (lk.mode == GR_LFBWRITEMODE_565)
                    wr565(off, x, dy, v);
                else                                       /* 555 / 1555 */
                    wr565(off, x, dy, (uint16_t)(((v & 0x7FE0) << 1) | ((v >> 4) & 0x20) | (v & 0x1F)));
            }
        }
    }
}

/* Tracing: the pixels written through the current lock, as spans
 * {u16 y, u16 x, u16 n, u16 0, n values (padded to 4)} in lock space. */
void tr_lfb_unlock_spans(GrLock_t type, GrBuffer_t buffer)
{
    static uint8_t *sp;
    static uint32_t sp_cap;
    uint32_t args[3], n = 0, x, y;
    int bpp = is32(lk.mode) ? 4 : 2;
    MGA_UNUSED(buffer);
    if (!lk.active || (type & 1) != GR_LFB_WRITE_ONLY || !lk.shadow)
        return;
    for (y = 0; y < (uint32_t)mg.height; y++) {
        const uint32_t *p32 = srow32(y);
        const uint16_t *p16 = srow16(y);
        if (bpp == 4 ? !memcmp(shadow + y * SHADOW_STRIDE32, p32, (uint32_t)mg.width * 4u)
                     : !memcmp(shadow + y * SHADOW_STRIDE16, p16, (uint32_t)mg.width * 2u))
            continue;
        for (x = 0; x < (uint32_t)mg.width;) {
            uint32_t x0 = x, cnt, need;
            while (x < (uint32_t)mg.width) {
                uint32_t v, sv;
                if (bpp == 4) {
                    v = ((const uint32_t *)(shadow + y * SHADOW_STRIDE32))[x];
                    sv = p32[x];
                } else {
                    v = ((const uint16_t *)(shadow + y * SHADOW_STRIDE16))[x];
                    sv = p16[x];
                }
                if (v == sv)
                    break;
                x++;
            }
            cnt = x - x0;
            if (!cnt) {
                x++;
                continue;
            }
            need = n + 8 + ((cnt * (uint32_t)bpp + 3) & ~3u);
            if (need > sp_cap) {
                uint8_t *nb = (uint8_t *)sys_alloc(need * 2);
                if (!nb)
                    return;
                if (sp) {
                    memcpy(nb, sp, n);
                    sys_free(sp);
                }
                sp = nb;
                sp_cap = need * 2;
            }
            ((uint16_t *)(sp + n))[0] = (uint16_t)y;
            ((uint16_t *)(sp + n))[1] = (uint16_t)x0;
            ((uint16_t *)(sp + n))[2] = (uint16_t)cnt;
            ((uint16_t *)(sp + n))[3] = 0;
            memcpy(sp + n + 8, shadow + y * (bpp == 4 ? SHADOW_STRIDE32 : SHADOW_STRIDE16) + x0 * (uint32_t)bpp,
                   cnt * (uint32_t)bpp);
            n = need;
        }
    }
    args[0] = (uint32_t)lk.buffer; args[1] = (uint32_t)lk.mode; args[2] = (uint32_t)bpp;
    tr_begin(TR_OP_LFBSPANS, args, 3, 1);
    tr_blob(sp ? sp : (const void *)args, n);
    tr_end();
}

GR_ENTRY(FxBool, grLfbUnlock, (GrLock_t type, GrBuffer_t buffer))
{
    MGA_UNUSED(buffer);
    if (!lk.active)
        return FXFALSE;
    if (lk.shadow && (type & 1) == GR_LFB_WRITE_ONLY) {
        engine_sync(200000);
        write_back();
    }
    if ((type & 1) == GR_LFB_WRITE_ONLY && lk.buffer == GR_BUFFER_FRONTBUFFER)
        mg_present_front();
    lk.active = 0;
    return FXTRUE;
}

GR_ENTRY(FxBool, grLfbReadRegion, (GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width,
                                   FxU32 src_height, FxU32 dst_stride, void *dst_data))
{
    uint32_t off, y, pitch;
    if (!mg.open || src_x + src_width > (FxU32)mg.width || src_y + src_height > (FxU32)mg.height)
        return FXFALSE;
    engine_sync(200000);
    off = mg_buffer_offset(src_buffer);
    pitch = (uint32_t)mg.pitch_px;
    for (y = 0; y < src_height; y++) {
        uint32_t sy = src_y + y;
        uint16_t *d = (uint16_t *)((uint8_t *)dst_data + y * dst_stride);
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
            sy = (uint32_t)mg.height - 1 - sy;
        if (mg.scaled) {
            uint32_t x;
            for (x = 0; x < src_width; x++)
                d[x] = src_buffer == GR_BUFFER_AUXBUFFER
                           ? (mg.zbits == 32 ? aux_value32(rdaux(src_x + x, sy)) : (uint16_t)rdaux(src_x + x, sy))
                           : rd565(off, src_x + x, sy);
        } else if (src_buffer == GR_BUFFER_AUXBUFFER && mg.zbits == 32) {
            const uint32_t *s = (const uint32_t *)(mga_fb + off) + sy * pitch + src_x;
            uint32_t x;
            for (x = 0; x < src_width; x++)
                d[x] = aux_value32(s[x]);
        } else if (src_buffer == GR_BUFFER_AUXBUFFER || mg.bpp == 16) {
            memcpy(d, (const uint16_t *)(mga_fb + off) + sy * pitch + src_x, src_width * 2);
        } else {
            const uint8_t *cr = crow(off, sy);
            uint32_t x;
            for (x = 0; x < src_width; x++)
                d[x] = get565(cr, src_x + x);
        }
    }
    return FXTRUE;
}

GR_ENTRY(FxBool, grLfbWriteRegion, (GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y, GrLfbSrcFmt_t src_format,
                                    FxU32 src_width, FxU32 src_height, FxI32 src_stride, void *src_data))
{
    uint32_t off, x, y, pitch;
    if (!mg.open || dst_x + src_width > (FxU32)mg.width || dst_y + src_height > (FxU32)mg.height)
        return FXFALSE;
    engine_sync(200000);
    off = mg_buffer_offset(dst_buffer);
    pitch = (uint32_t)mg.pitch_px;
    for (y = 0; y < src_height; y++) {
        uint32_t dy = dst_y + y;
        const uint8_t *s = (const uint8_t *)src_data + (int32_t)y * src_stride;
        uint16_t *zr;
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
            dy = (uint32_t)mg.height - 1 - dy;
        zr = (uint16_t *)(mga_fb + off) + dy * pitch;
        for (x = 0; x < src_width; x++) {
            uint32_t dx = dst_x + x;
            switch (src_format) {
            case GR_LFB_SRC_FMT_565: wr565(off, dx, dy, ((const uint16_t *)s)[x]); break;
            case GR_LFB_SRC_FMT_555:
            case GR_LFB_SRC_FMT_1555: {
                uint16_t v = ((const uint16_t *)s)[x];
                wr565(off, dx, dy, (uint16_t)(((v & 0x7FE0) << 1) | ((v >> 4) & 0x20) | (v & 0x1F)));
                break; }
            case GR_LFB_SRC_FMT_888:
            case GR_LFB_SRC_FMT_8888:
                wrargb(off, dx, dy, order_argb(((const uint32_t *)s)[x]));
                break;
            case GR_LFB_SRC_FMT_ZA16:
                if (mg.scaled) wrdepth(dx, dy, ((const uint16_t *)s)[x]);
                else zr[dx] = ((const uint16_t *)s)[x];
                break;
            default:
                mg_log(MG_LOG_WARN, "lfb: write format %d unsupported", (int)src_format);
                return FXFALSE;
            }
        }
    }
    if (dst_buffer == GR_BUFFER_FRONTBUFFER)
        mg_present_front();
    return FXTRUE;
}

GR_ENTRY(void, grLfbWriteColorFormat, (GrColorFormat_t colorFormat)) { lfb_color_format = colorFormat; }
GR_ENTRY(void, grLfbWriteColorSwizzle, (FxBool swizzleBytes, FxBool swapWords))
{
    if (swizzleBytes || swapWords)
        mg_log(MG_LOG_WARN, "lfb: colour swizzle not supported");
}
GR_ENTRY(void, grLfbConstantAlpha, (GrAlpha_t alpha)) { lfb_const_alpha = alpha; }
GR_ENTRY(void, grLfbConstantDepth, (FxU16 depth)) { lfb_const_depth = depth; }

/* Glide 2.0-era region helpers imported by GTA. */
GR_ENTRY(FxBool, guFbReadRegion, (int src_x, int src_y, int w, int h, void *dst, int strideInBytes))
{
    return grLfbReadRegion(mg.render_buffer, (FxU32)src_x, (FxU32)src_y, (FxU32)w, (FxU32)h,
                           (FxU32)strideInBytes, dst);
}

GR_ENTRY(FxBool, guFbWriteRegion, (int dst_x, int dst_y, int w, int h, const void *src, int strideInBytes))
{
    return grLfbWriteRegion(mg.render_buffer, (FxU32)dst_x, (FxU32)dst_y, GR_LFB_SRC_FMT_565,
                            (FxU32)w, (FxU32)h, strideInBytes, (void *)src);
}
