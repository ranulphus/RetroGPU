/* texstore.c - how a texture level's shadow copy holds its texels (gl_tex.h DGL_ST_*).
 *
 * GL's copy of every level stays in system memory: the back ends load their own copies from it, and again after
 * video memory ran out and a texture was evicted. Kept as RGBA8 it was four bytes a texel whatever the texture held:
 * GLQuake's demo1 peaked at 18.7 MB of copies (17.4 MB of them plain RGB), on a 32 MB machine beside its own 13 MB.
 * Where the drawing back end allows it (dgl_backend.tex_store) a copy is held as the card will draw it: plain RGB
 * as RGB565 quantised exactly as that back end quantises (so its upload makes the same texels, bit for bit), or in
 * three bytes where it is drawn at eight bits; luminance, alpha and intensity (and RGB levels that are all grey) in
 * one byte; luminance-alpha in two. The rest, and every level under a back end without tex_store, stays RGBA8.
 *
 * Readers: dgl_level_span (n texels from one place, into the caller's buffer), dgl_level_rgba (a whole level, into
 * a scratch slot), dgl_level_rgba_rect (a rectangle at its place in a level-sized slot); dgl_level_rgba_done when
 * the loading is over (the larger slots freed again). For an RGBA8 level each returns the copy itself. */
#include "gl_tex.h"
#include "../backend/backend.h"
#include <stdlib.h>
#include <string.h>

static const unsigned char st_bytes[] = { 4, 3, 2, 2, 1, 2 };
int dgl_st_bytes(int st) { return st >= 0 && st <= DGL_ST_XA8 ? st_bytes[st] : 4; }

/* RGB565: truncating (the NV layer's >> 3, >> 2) and rounding (the Matrox back end's q()), each with an expansion
 * the same quantiser takes back to the same value (tests/unit: every value). */
static unsigned char q5r[256], q6r[256], e5r[32], e6r[64], e5t[32], e6t[64];
static int ready;

static unsigned qr(unsigned v, int bits) { return (v * ((1u << bits) - 1) + 127) / 255; }

static void tables(void)
{
    unsigned v, p;
    for (v = 0; v < 256; v++)
        q5r[v] = (unsigned char)qr(v, 5), q6r[v] = (unsigned char)qr(v, 6);
    for (p = 0; p < 32; p++) {
        e5t[p] = (unsigned char)(p << 3 | p >> 2);
        v = (p * 255 + 15) / 31;
        while (qr(v, 5) > p) v--;
        while (qr(v, 5) < p) v++;
        e5r[p] = (unsigned char)v;
    }
    for (p = 0; p < 64; p++) {
        e6t[p] = (unsigned char)(p << 2 | p >> 4);
        v = (p * 255 + 31) / 63;
        while (qr(v, 6) > p) v--;
        while (qr(v, 6) < p) v++;
        e6r[p] = (unsigned char)v;
    }
    ready = 1;
}

/* n texels of RGBA8 (already reduced to the level's class) into form st. */
static void store_px(int st, int ifc, const unsigned char *s, long n, unsigned char *d)
{
    long i;
    unsigned w;
    if (!ready)
        tables();
    switch (st) {
    case DGL_ST_RGB8:
        for (i = 0; i < n; i++, s += 4, d += 3)
            d[0] = s[0], d[1] = s[1], d[2] = s[2];
        break;
    case DGL_ST_RGB565T:
        for (i = 0; i < n; i++, s += 4, d += 2)
            w = (unsigned)(s[0] >> 3) << 11 | (unsigned)(s[1] >> 2) << 5 | (unsigned)(s[2] >> 3),
            d[0] = (unsigned char)w, d[1] = (unsigned char)(w >> 8);
        break;
    case DGL_ST_RGB565R:
        for (i = 0; i < n; i++, s += 4, d += 2)
            w = (unsigned)q5r[s[0]] << 11 | (unsigned)q6r[s[1]] << 5 | q5r[s[2]],
            d[0] = (unsigned char)w, d[1] = (unsigned char)(w >> 8);
        break;
    case DGL_ST_X8:
        if (ifc == DGL_IF_ALPHA)
            for (i = 0; i < n; i++, s += 4)
                d[i] = s[3];
        else
            for (i = 0; i < n; i++, s += 4)
                d[i] = s[0];
        break;
    case DGL_ST_XA8:
        for (i = 0; i < n; i++, s += 4, d += 2)
            d[0] = s[0], d[1] = s[3];
        break;
    default:
        memcpy(d, s, (size_t)n * 4);
        break;
    }
}

/* ... and back to RGBA8 as the class has it (dgl_apply_ifmt's forms). */
static void expand_px(int st, int ifc, const unsigned char *s, long n, unsigned char *d)
{
    long i;
    unsigned w;
    if (!ready)
        tables();
    switch (st) {
    case DGL_ST_RGB8:
        for (i = 0; i < n; i++, s += 3, d += 4)
            d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = 255;
        break;
    case DGL_ST_RGB565T:
        for (i = 0; i < n; i++, s += 2, d += 4)
            w = (unsigned)s[0] | (unsigned)s[1] << 8,
            d[0] = e5t[w >> 11], d[1] = e6t[(w >> 5) & 63], d[2] = e5t[w & 31], d[3] = 255;
        break;
    case DGL_ST_RGB565R:
        for (i = 0; i < n; i++, s += 2, d += 4)
            w = (unsigned)s[0] | (unsigned)s[1] << 8,
            d[0] = e5r[w >> 11], d[1] = e6r[(w >> 5) & 63], d[2] = e5r[w & 31], d[3] = 255;
        break;
    case DGL_ST_X8:
        if (ifc == DGL_IF_ALPHA)
            for (i = 0; i < n; i++, d += 4)
                d[0] = d[1] = d[2] = 255, d[3] = s[i];
        else if (ifc == DGL_IF_INTENSITY)
            for (i = 0; i < n; i++, d += 4)
                d[0] = d[1] = d[2] = d[3] = s[i];
        else
            for (i = 0; i < n; i++, d += 4)
                d[0] = d[1] = d[2] = s[i], d[3] = 255;
        break;
    case DGL_ST_XA8:
        for (i = 0; i < n; i++, s += 2, d += 4)
            d[0] = d[1] = d[2] = s[0], d[3] = s[1];
        break;
    default:
        memcpy(d, s, (size_t)n * 4);
        break;
    }
}

static int grey(const unsigned char *p, long n)
{
    long i;
    for (i = 0; i < n; i++, p += 4)
        if (p[0] != p[1] || p[0] != p[2])
            return 0;
    return 1;
}

/* The form for a level's RGBA8 texels: the back end's for plain RGB (RGB565 or RGB8), one or two bytes for the
 * classes with one or two components, RGBA8 otherwise or without the back end's say. */
static int form_for(const dgl_level *L, const unsigned char *rgba)
{
    int rgb = dgl_be->tex_store ? dgl_be->tex_store(L->ifc, L->ifmt) : DGL_ST_RGBA8;
    if (rgb == DGL_ST_RGBA8)
        return DGL_ST_RGBA8;
    switch (L->ifc) {
    case DGL_IF_LUMINANCE: case DGL_IF_ALPHA: case DGL_IF_INTENSITY:
        return DGL_ST_X8;
    case DGL_IF_LUMINANCE_ALPHA:
        return DGL_ST_XA8;
    case DGL_IF_RGB:
        return grey(rgba, (long)L->w * L->h) ? DGL_ST_X8 : rgb;
    default:
        return DGL_ST_RGBA8;
    }
}

void dgl_level_compact(dgl_level *L)
{
    int st;
    unsigned char *px;
    if (!L->rgba || L->idx)
        return;
    st = form_for(L, L->rgba);
    if (st == DGL_ST_RGBA8)
        return;
    if (!(px = (unsigned char *)malloc((size_t)L->w * L->h * (size_t)dgl_st_bytes(st))))
        return;                                 /* (kept as RGBA8) */
    store_px(st, L->ifc, L->rgba, (long)L->w * L->h, px);
    free(L->rgba);
    L->rgba = NULL, L->px = px, L->st = st;
}

int dgl_level_uncompact(dgl_level *L)
{
    unsigned char *rgba;
    if (!L->px)
        return 0;
    if (!(rgba = (unsigned char *)malloc((size_t)L->w * L->h * 4)))
        return -1;
    expand_px(L->st, L->ifc, L->px, (long)L->w * L->h, rgba);
    free(L->px);
    L->px = NULL, L->rgba = rgba, L->st = DGL_ST_RGBA8;
    return 0;
}

/* Rows of RGBA8 texels (w by h, reduced to the class) into a compact level at (x, y). With narrow, only the texels
 * whose stored form changes are written and box gets their bounds (x0, y0, x1, y1; x1 < x0 when none changed). A
 * grey RGB level (one byte) given colour goes back to its back end's RGB form first. */
void dgl_level_store_rows(dgl_level *L, const unsigned char *rgba, int x, int y, int w, int h, int narrow, int *box)
{
    unsigned char row[2048 * 4];
    int bpt, r, x0 = w, x1 = -1, y0 = -1, y1 = -1;
    if (L->st == DGL_ST_X8 && L->ifc == DGL_IF_RGB && !grey(rgba, (long)w * h)) {
        int i;
        if (dgl_level_uncompact(L) == 0) {
            for (i = 0; i < h; i++)
                memcpy(L->rgba + ((size_t)(y + i) * L->w + x) * 4, rgba + (size_t)i * w * 4, (size_t)w * 4);
            dgl_level_compact(L);
        }
        box[0] = x, box[1] = y, box[2] = x + w - 1, box[3] = y + h - 1;
        return;
    }
    bpt = dgl_st_bytes(L->st);
    for (r = 0; r < h; r++) {
        unsigned char *d = L->px + ((size_t)(y + r) * L->w + x) * bpt;
        const unsigned char *s = rgba + (size_t)r * w * 4;
        int done = 0;
        while (done < w) {
            int n = w - done > 2048 ? 2048 : w - done, a = 0, b = n - 1;
            store_px(L->st, L->ifc, s + (size_t)done * 4, n, row);
            if (narrow) {
                while (a <= b && !memcmp(d + (size_t)(done + a) * bpt, row + a * bpt, (size_t)bpt))
                    a++;
                while (b >= a && !memcmp(d + (size_t)(done + b) * bpt, row + b * bpt, (size_t)bpt))
                    b--;
            }
            if (a <= b) {
                memcpy(d + (size_t)(done + a) * bpt, row + a * bpt, (size_t)(b - a + 1) * bpt);
                if (done + a < x0) x0 = done + a;
                if (done + b > x1) x1 = done + b;
                if (y0 < 0) y0 = r;
                y1 = r;
            }
            done += n;
        }
    }
    box[0] = x + x0, box[1] = y + y0, box[2] = x + x1, box[3] = y + y1;
}

const unsigned char *dgl_level_span(const dgl_level *L, size_t i, long n, unsigned char *buf)
{
    if (L->rgba)
        return L->rgba + i * 4;
    expand_px(L->st, L->ifc, L->px + i * (size_t)dgl_st_bytes(L->st), n, buf);
    return buf;
}

static unsigned char *slot_buf[DGL_MAX_LEVELS];
static size_t slot_n[DGL_MAX_LEVELS];

static unsigned char *slot(int k, size_t n)
{
    if (k < 0 || k >= DGL_MAX_LEVELS)
        return NULL;
    if (n > slot_n[k]) {
        unsigned char *p = (unsigned char *)realloc(slot_buf[k], n);
        if (!p)
            return NULL;
        slot_buf[k] = p, slot_n[k] = n;
    }
    return slot_buf[k];
}

const unsigned char *dgl_level_rgba(const dgl_level *L, int k)
{
    unsigned char *p;
    if (L->rgba || !L->px)
        return L->rgba;
    if (!(p = slot(k, (size_t)L->w * L->h * 4)))
        return NULL;
    expand_px(L->st, L->ifc, L->px, (long)L->w * L->h, p);
    return p;
}

const unsigned char *dgl_level_rgba_rect(const dgl_level *L, int k, int x, int y, int w, int h)
{
    unsigned char *p;
    int r;
    size_t bpt;
    if (L->rgba || !L->px)
        return L->rgba;
    if (!(p = slot(k, (size_t)L->w * L->h * 4)))
        return NULL;
    bpt = (size_t)dgl_st_bytes(L->st);
    for (r = 0; r < h; r++)
        expand_px(L->st, L->ifc, L->px + ((size_t)(y + r) * L->w + x) * bpt, w,
                  p + ((size_t)(y + r) * L->w + x) * 4);
    return p;
}

void dgl_level_rgba_done(void)
{
    int k;
    for (k = 0; k < DGL_MAX_LEVELS; k++)
        if (slot_n[k] > 65536u) {               /* (a 128x128 lightmap's slot kept: GLQuake's -mtex rewrites them) */
            free(slot_buf[k]);
            slot_buf[k] = NULL, slot_n[k] = 0;
        }
}
