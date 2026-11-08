/* texconv.c - see texconv.h. Decoding follows the Voodoo's documented
 * expansion rules (bit replication), which 86Box's Voodoo model also uses. */
#include "tex/texconv.h"
#include "tex/texfmt.h"

static uint32_t argb(int a, int r, int g, int b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t rgb332(uint32_t c)
{
    int r = (int)(c & 0xE0), g = (int)((c << 3) & 0xE0), b = (int)((c << 6) & 0xC0);
    r |= (r >> 3) | (r >> 6);
    g |= (g >> 3) | (g >> 6);
    b |= b >> 2;
    b |= b >> 4;
    return argb(0xFF, r, g, b);
}

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static int sx9(uint32_t v) { v &= 0x1FF; return (v & 0x100) ? (int)v - 0x200 : (int)v; }

static uint32_t ncc(uint32_t c, const tex_tables *t)
{
    const uint32_t *tb = t->ncc[t->ncc_sel & 1];
    int yi = (int)(c >> 4), ii = (int)((c >> 2) & 3), qi = (int)(c & 3);
    int y = (int)((tb[yi >> 2] >> ((yi & 3) * 8)) & 0xFF);
    uint32_t I = tb[4 + ii], Q = tb[8 + qi];
    return argb(0xFF, clamp255(y + sx9(I >> 18) + sx9(Q >> 18)),
                clamp255(y + sx9(I >> 9) + sx9(Q >> 9)), clamp255(y + sx9(I) + sx9(Q)));
}

uint32_t tex_decode(GrTextureFormat_t fmt, uint32_t c, const tex_tables *t)
{
    switch (fmt) {
    case GR_TEXFMT_RGB_332: return rgb332(c & 0xFF);
    case GR_TEXFMT_YIQ_422: return ncc(c & 0xFF, t);
    case GR_TEXFMT_ALPHA_8: c &= 0xFF; return argb((int)c, (int)c, (int)c, (int)c);
    case GR_TEXFMT_INTENSITY_8: c &= 0xFF; return argb(0xFF, (int)c, (int)c, (int)c);
    case GR_TEXFMT_ALPHA_INTENSITY_44: {
        int i = (int)(c & 0x0F), a = (int)(c & 0xF0);
        i |= i << 4;
        a |= a >> 4;
        return argb(a, i, i, i); }
    case GR_TEXFMT_P_8: return 0xFF000000u | (t->palette[c & 0xFF] & 0xFFFFFF);
    case GR_TEXFMT_ARGB_8332: return (rgb332(c & 0xFF) & 0xFFFFFF) | ((c >> 8) << 24);
    case GR_TEXFMT_AYIQ_8422: return (ncc(c & 0xFF, t) & 0xFFFFFF) | (((c >> 8) & 0xFF) << 24);
    case GR_TEXFMT_RGB_565: {
        int r = (int)((c >> 8) & 0xF8), g = (int)((c >> 3) & 0xFC), b = (int)((c << 3) & 0xF8);
        return argb(0xFF, r | (r >> 5), g | (g >> 6), b | (b >> 5)); }
    case GR_TEXFMT_ARGB_1555: {
        int r = (int)((c >> 7) & 0xF8), g = (int)((c >> 2) & 0xF8), b = (int)((c << 3) & 0xF8);
        return argb((c & 0x8000) ? 0xFF : 0, r | (r >> 5), g | (g >> 5), b | (b >> 5)); }
    case GR_TEXFMT_ARGB_4444: {
        int a = (int)((c >> 8) & 0xF0), r = (int)((c >> 4) & 0xF0), g = (int)(c & 0xF0), b = (int)((c << 4) & 0xF0);
        return argb(a | (a >> 4), r | (r >> 4), g | (g >> 4), b | (b >> 4)); }
    case GR_TEXFMT_ALPHA_INTENSITY_88: {
        int i = (int)(c & 0xFF);
        return argb((int)((c >> 8) & 0xFF), i, i, i); }
    case GR_TEXFMT_AP_88: return (t->palette[c & 0xFF] & 0xFFFFFF) | (((c >> 8) & 0xFF) << 24);
    default: return 0xFFFFFFFFu;
    }
}

int tex_classify(const uint32_t *px, int n)
{
    int i, binary = 1;
    for (i = 0; i < n; i++) {
        uint32_t a = px[i] >> 24;
        if (a != 0xFF) {
            if (a != 0)
                return HW_TW12;
            binary = 2;
        }
    }
    return binary == 2 ? HW_TW15 : HW_TW16;
}

static uint16_t enc(int hwfmt, uint32_t c)
{
    switch (hwfmt) {
    case HW_TW15:
        return (uint16_t)(((c >> 31) << 15) | ((c >> 9) & 0x7C00) | ((c >> 6) & 0x03E0) | ((c >> 3) & 0x1F));
    case HW_TW12:
        return (uint16_t)(((c >> 16) & 0xF000) | ((c >> 12) & 0x0F00) | ((c >> 8) & 0x00F0) | ((c >> 4) & 0x0F));
    default:
        return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F));
    }
}

static int src_index(int x, int len, int clamp)
{
    if (x < len)
        return x;
    return clamp ? len - 1 : x % len;
}

uint32_t tex_variant_key(const tex_variant *v)
{
    if (!v || !v->kind)
        return 0;
    return ((uint32_t)v->kind << 30) ^ ((v->kind & TV_CHROMA) ? (v->chroma & 0xFFFFFF) : 0) ^
           ((v->kind & TV_ATEST) ? (((uint32_t)v->afunc << 24) | ((uint32_t)v->aref << 16)) * 31u : 0);
}

static int atest_pass(int func, int a, int ref)
{
    switch (func) {
    case GR_CMP_NEVER: return 0;
    case GR_CMP_LESS: return a < ref;
    case GR_CMP_EQUAL: return a == ref;
    case GR_CMP_LEQUAL: return a <= ref;
    case GR_CMP_GREATER: return a > ref;
    case GR_CMP_NOTEQUAL: return a != ref;
    case GR_CMP_GEQUAL: return a >= ref;
    default: return 1;
    }
}

/* Give each transparent texel the mean colour of its opaque 4-neighbours.
 * The key is carried by the alpha bit alone, so this changes nothing that
 * is drawn directly; it stops bilinear filtering from pulling the edges of
 * keyed sprites towards the colour hidden behind the key (usually black).
 * Only opaque texels are read and only transparent ones written, so one
 * in-place pass is order-independent. */
static void bleed_keyed(uint32_t *px, int w, int h, int clamp_s, int clamp_t)
{
    static const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
    int x, y, k;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned r = 0, g = 0, b = 0, n = 0;
            if (px[y * w + x] >> 24)
                continue;
            for (k = 0; k < 4; k++) {
                int nx = x + dx[k], ny = y + dy[k];
                uint32_t c;
                if (nx < 0 || nx >= w) {
                    if (clamp_s) continue;
                    nx = (nx + w) % w;
                }
                if (ny < 0 || ny >= h) {
                    if (clamp_t) continue;
                    ny = (ny + h) % h;
                }
                c = px[ny * w + nx];
                if (!(c >> 24))
                    continue;
                r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF; n++;
            }
            if (n)
                px[y * w + x] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
}

int tex_convert_level(GrTextureFormat_t fmt, const void *src, int w, int h,
                      uint16_t *dst, int dst_pitch, int hw_w, int hw_h, int clamp_s, int clamp_t,
                      const tex_tables *t, int force_hwfmt, uint32_t *scratch, const tex_variant *var)
{
    int x, y, hwfmt, native = -1;
    const uint8_t *s8 = (const uint8_t *)src;
    const uint16_t *s16 = (const uint16_t *)src;
    /* Formats the Matrox holds natively are copied bit-exactly. */
    if (fmt == GR_TEXFMT_RGB_565) native = HW_TW16;
    else if (fmt == GR_TEXFMT_ARGB_1555) native = HW_TW15;
    else if (fmt == GR_TEXFMT_ARGB_4444) native = HW_TW12;
    if (native >= 0 && (force_hwfmt < 0 || force_hwfmt == native) && !(var && var->kind)) {
        for (y = 0; y < hw_h; y++) {
            const uint16_t *row = s16 + src_index(y, h, clamp_t) * w;
            for (x = 0; x < hw_w; x++)
                dst[y * dst_pitch + x] = row[src_index(x, w, clamp_s)];
        }
        return native;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            scratch[y * w + x] = tex_decode(fmt, tex_bpp(fmt) == 2 ? s16[y * w + x] : s8[y * w + x], t);
    if (var && var->kind) {
        /* Keyed variant: 1-bit alpha, transparent where the key matches or
         * the alpha test fails; drawn with the hardware alpha key. */
        for (x = 0; x < w * h; x++) {
            uint32_t c = scratch[x];
            int keep = 1;
            if ((var->kind & TV_CHROMA) && (c & 0xFFFFFF) == (var->chroma & 0xFFFFFF))
                keep = 0;
            if ((var->kind & TV_ATEST) && !atest_pass(var->afunc, (int)(c >> 24), var->aref))
                keep = 0;
            scratch[x] = (c & 0xFFFFFF) | (keep ? 0xFF000000u : 0);
        }
        bleed_keyed(scratch, w, h, clamp_s, clamp_t);
        hwfmt = HW_TW15;
    } else
        hwfmt = force_hwfmt >= 0 ? force_hwfmt : tex_classify(scratch, w * h);
    for (y = 0; y < hw_h; y++) {
        const uint32_t *row = scratch + src_index(y, h, clamp_t) * w;
        for (x = 0; x < hw_w; x++)
            dst[y * dst_pitch + x] = enc(hwfmt, row[src_index(x, w, clamp_s)]);
    }
    return hwfmt;
}
