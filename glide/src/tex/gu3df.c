/* gu3df.c - .3DF texture files (the public 3dfx texture-file format).
 *
 * A text header of four lines:
 *     3df v<version>
 *     <format name>
 *     lod range: <small> <large>
 *     aspect ratio: <w> <h>
 * then, for palettised formats, a 256-entry big-endian ARGB palette; for
 * YIQ formats an NCC table (16 Y, 12 I and 12 Q values, big-endian 16-bit);
 * then the mip levels, largest first, 16-bit texels big-endian. */
#include "glide/mg.h"
#include "tex/texfmt.h"
#include <string.h>

#define HDR_MAX 256

static const struct { const char *name; GrTextureFormat_t fmt; } fmt_names[] = {
    { "rgb332", GR_TEXFMT_RGB_332 }, { "yiq", GR_TEXFMT_YIQ_422 }, { "a8", GR_TEXFMT_ALPHA_8 },
    { "i8", GR_TEXFMT_INTENSITY_8 }, { "ai44", GR_TEXFMT_ALPHA_INTENSITY_44 }, { "p8", GR_TEXFMT_P_8 },
    { "argb8332", GR_TEXFMT_ARGB_8332 }, { "ayiq8422", GR_TEXFMT_AYIQ_8422 }, { "rgb565", GR_TEXFMT_RGB_565 },
    { "argb1555", GR_TEXFMT_ARGB_1555 }, { "argb4444", GR_TEXFMT_ARGB_4444 },
    { "ai88", GR_TEXFMT_ALPHA_INTENSITY_88 }, { "ap88", GR_TEXFMT_AP_88 },
};

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* Read the next header line (without the newline) into 'line'. */
static const char *next_line(const char *p, const char *end, char *line, int max)
{
    int n = 0;
    while (p < end && *p != '\n') {
        if (n < max - 1 && *p != '\r')
            line[n++] = (char)lower((unsigned char)*p);
        p++;
    }
    line[n] = 0;
    return p < end ? p + 1 : NULL;
}

static int two_ints(const char *s, int *a, int *b)
{
    int v[2], k = 0;
    while (*s && k < 2) {
        if (*s >= '0' && *s <= '9') {
            int x = 0;
            while (*s >= '0' && *s <= '9')
                x = x * 10 + (*s++ - '0');
            v[k++] = x;
        } else
            s++;
    }
    if (k < 2)
        return 0;
    *a = v[0]; *b = v[1];
    return 1;
}

static int log2i(int v)
{
    int l = 0;
    while ((1 << l) < v)
        l++;
    return (1 << l) == v ? l : -1;
}

/* Parses the header; returns the byte offset of the binary data or -1. */
static int parse(const char *buf, int len, Gu3dfInfo *info)
{
    char line[64];
    const char *p = buf, *end = buf + len;
    int i, lo, hi, aw, ah, l_small, l_large, a;
    p = next_line(p, end, line, sizeof line);
    if (!p || strncmp(line, "3df", 3))
        return -1;
    p = next_line(p, end, line, sizeof line);
    if (!p)
        return -1;
    for (i = 0; i < (int)(sizeof fmt_names / sizeof fmt_names[0]); i++)
        if (!strcmp(line, fmt_names[i].name))
            break;
    if (i == (int)(sizeof fmt_names / sizeof fmt_names[0]))
        return -1;
    info->header.format = fmt_names[i].fmt;
    p = next_line(p, end, line, sizeof line);
    if (!p || !two_ints(line, &lo, &hi))
        return -1;
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    p = next_line(p, end, line, sizeof line);
    if (!p || !two_ints(line, &aw, &ah))
        return -1;
    l_small = log2i(lo); l_large = log2i(hi);
    if (l_small < 0 || l_large < 0 || l_large > 8)
        return -1;
    /* GR_LOD_256 = 0 ... GR_LOD_1 = 8; GR_ASPECT_8x1 = 0 ... 1x1 = 3 ... 1x8 = 6. */
    info->header.large_lod = 8 - l_large;
    info->header.small_lod = 8 - l_small;
    if (aw >= ah) {
        a = log2i(aw / (ah ? ah : 1));
        if (a < 0 || a > 3) return -1;
        info->header.aspect_ratio = (GrAspectRatio_t)(3 - a);
        info->header.width = (FxU32)hi;
        info->header.height = (FxU32)(hi >> a);
    } else {
        a = log2i(ah / (aw ? aw : 1));
        if (a < 0 || a > 3) return -1;
        info->header.aspect_ratio = (GrAspectRatio_t)(3 + a);
        info->header.height = (FxU32)hi;
        info->header.width = (FxU32)(hi >> a);
    }
    info->mem_required = grTexCalcMemRequired((GrLOD_t)info->header.small_lod, (GrLOD_t)info->header.large_lod,
                                              info->header.aspect_ratio, info->header.format);
    return (int)(p - buf);
}

static int read_header(const char *filename, Gu3dfInfo *info, int *fh, char *hdr, int *hlen)
{
    int h = mg_file_open(filename), n, off;
    if (h < 0)
        return -1;
    n = mg_file_read(h, hdr, HDR_MAX);
    if (n <= 0 || (off = parse(hdr, n, info)) < 0) {
        mg_file_close(h);
        return -1;
    }
    *fh = h;
    *hlen = n;
    return off;
}

GR_ENTRY(FxBool, gu3dfGetInfo, (const char *filename, Gu3dfInfo *info))
{
    char hdr[HDR_MAX];
    int h, n;
    if (!filename || !info || read_header(filename, info, &h, hdr, &n) < 0)
        return FXFALSE;
    mg_file_close(h);
    return FXTRUE;
}

/* Copy 'n' bytes of file data that starts 'off' bytes into the already
 * read header buffer, then continue from the file. */
static int take(int h, const char *hdr, int hlen, int *off, void *dst, int n)
{
    int from_hdr = hlen - *off;
    if (from_hdr > n) from_hdr = n;
    if (from_hdr > 0) {
        memcpy(dst, hdr + *off, (size_t)from_hdr);
        *off += from_hdr;
    } else
        from_hdr = 0;
    if (n > from_hdr && mg_file_read(h, (uint8_t *)dst + from_hdr, n - from_hdr) != n - from_hdr)
        return -1;
    return 0;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

GR_ENTRY(FxBool, gu3dfLoad, (const char *filename, Gu3dfInfo *info))
{
    char hdr[HDR_MAX];
    uint8_t raw[256 * 4];
    int h, hlen, off, i, bpp, lod, texels = 0;
    FxU32 w, ht;
    if (!filename || !info || !info->data)
        return FXFALSE;
    if ((off = read_header(filename, info, &h, hdr, &hlen)) < 0)
        return FXFALSE;
    if (info->header.format == GR_TEXFMT_P_8 || info->header.format == GR_TEXFMT_AP_88) {
        if (take(h, hdr, hlen, &off, raw, 1024) < 0) goto fail;
        for (i = 0; i < 256; i++)
            info->table.palette.data[i] = ((FxU32)raw[i * 4] << 24) | ((FxU32)raw[i * 4 + 1] << 16) |
                                          ((FxU32)raw[i * 4 + 2] << 8) | raw[i * 4 + 3];
    } else if (info->header.format == GR_TEXFMT_YIQ_422 || info->header.format == GR_TEXFMT_AYIQ_8422) {
        GuNccTable *t = &info->table.nccTable;
        if (take(h, hdr, hlen, &off, raw, 80) < 0) goto fail;
        for (i = 0; i < 16; i++)
            t->yRGB[i] = (FxU8)be16(raw + i * 2);
        for (i = 0; i < 12; i++) {
            t->iRGB[i / 3][i % 3] = (FxI16)be16(raw + 32 + i * 2);
            t->qRGB[i / 3][i % 3] = (FxI16)be16(raw + 56 + i * 2);
        }
        for (i = 0; i < 4; i++)
            t->packed_data[i] = (FxU32)t->yRGB[i * 4] | ((FxU32)t->yRGB[i * 4 + 1] << 8) |
                                ((FxU32)t->yRGB[i * 4 + 2] << 16) | ((FxU32)t->yRGB[i * 4 + 3] << 24);
        for (i = 0; i < 4; i++) {
            t->packed_data[4 + i] = ((FxU32)(t->iRGB[i][0] & 0x1FF) << 18) | ((FxU32)(t->iRGB[i][1] & 0x1FF) << 9) |
                                    (FxU32)(t->iRGB[i][2] & 0x1FF);
            t->packed_data[8 + i] = ((FxU32)(t->qRGB[i][0] & 0x1FF) << 18) | ((FxU32)(t->qRGB[i][1] & 0x1FF) << 9) |
                                    (FxU32)(t->qRGB[i][2] & 0x1FF);
        }
    }
    bpp = tex_bpp(info->header.format);
    w = info->header.width; ht = info->header.height;
    for (lod = info->header.large_lod; lod <= info->header.small_lod; lod++) {
        texels += (int)((w ? w : 1) * (ht ? ht : 1));
        w >>= 1; ht >>= 1;
    }
    if (take(h, hdr, hlen, &off, info->data, texels * bpp) < 0)
        goto fail;
    if (bpp == 2) {
        uint16_t *d = (uint16_t *)info->data;
        for (i = 0; i < texels; i++)
            d[i] = (uint16_t)((d[i] >> 8) | (d[i] << 8));
    }
    mg_file_close(h);
    return FXTRUE;
fail:
    mg_file_close(h);
    return FXFALSE;
}
