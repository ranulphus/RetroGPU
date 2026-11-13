/* texconv.c - the Matrox back end's texel formats (PRD §8.3; docs/b1.md):
 * each texture is stored in the hardware format its alpha needs: all opaque
 * -> RGB565 (TW16), only 0/255 -> ARGB1555 (TW15), anything else -> ARGB4444
 * (TW12). Moved from src/gl/texconv.c in B1. Pure C, unit-tested on the host. */
#include "../../gl/gl_tex.h"
#include "mga_be_fmt.h"

int dgl_hwfmt_for_class(int cls)
{
    return cls == DGL_ALPHA_OPAQUE ? DGL_TW16 : cls == DGL_ALPHA_BINARY ? DGL_TW15 : DGL_TW12;
}

/* Rounded to the nearest representable value, as a GL implementation should. */
static unsigned q(unsigned v, int bits) { return (v * ((1u << bits) - 1) + 127) / 255; }

uint16_t dgl_pack_texel(int hwfmt, unsigned r, unsigned g, unsigned b, unsigned a)
{
    switch (hwfmt) {
    case DGL_TW16: return (uint16_t)((q(r, 5) << 11) | (q(g, 6) << 5) | q(b, 5));
    case DGL_TW15: return (uint16_t)(((a >= 128) << 15) | (q(r, 5) << 10) | (q(g, 5) << 5) | q(b, 5));
    default:       return (uint16_t)((q(a, 4) << 12) | (q(r, 4) << 8) | (q(g, 4) << 4) | q(b, 4));
    }
}

/* dgl_pack_texel's (and dgl_pack_grey565's) quantisations, by table: a row
 * of texels in one pass (sub-images and uploads: GLQuake's -mtex changes
 * lightmap rows hundreds of times a second). */
static unsigned char q4t[256], q5t[256], q6t[256];
static uint16_t greyt[256];
static int tables_ready;

static void tables(void)
{
    unsigned v;
    for (v = 0; v < 256; v++) {
        q4t[v] = (unsigned char)q(v, 4);
        q5t[v] = (unsigned char)q(v, 5);
        q6t[v] = (unsigned char)q(v, 6);
        greyt[v] = dgl_pack_grey565(v);
    }
    tables_ready = 1;
}

void dgl_pack_row(int hwfmt, int grey, const unsigned char *rgba, long n, uint16_t *out)
{
    long i;
    if (!tables_ready)
        tables();
    if (grey && hwfmt == DGL_TW16) {
        for (i = 0; i < n; i++, rgba += 4)
            out[i] = greyt[rgba[0]];
        return;
    }
    switch (hwfmt) {
    case DGL_TW16:
        for (i = 0; i < n; i++, rgba += 4)
            out[i] = (uint16_t)((q5t[rgba[0]] << 11) | (q6t[rgba[1]] << 5) | q5t[rgba[2]]);
        break;
    case DGL_TW15:
        for (i = 0; i < n; i++, rgba += 4)
            out[i] = (uint16_t)(((rgba[3] >= 128) << 15) | (q5t[rgba[0]] << 10) | (q5t[rgba[1]] << 5) | q5t[rgba[2]]);
        break;
    default:
        for (i = 0; i < n; i++, rgba += 4)
            out[i] = (uint16_t)((q4t[rgba[3]] << 12) | (q4t[rgba[0]] << 8) | (q4t[rgba[1]] << 4) | q4t[rgba[2]]);
        break;
    }
}

void dgl_convert_rgba(int hwfmt, const unsigned char *rgba, long n, uint16_t *out)
{
    long i;
    for (i = 0; i < n; i++, rgba += 4)
        out[i] = dgl_pack_texel(hwfmt, rgba[0], rgba[1], rgba[2], rgba[3]);
}

/* Grey in RGB565 with green derived from the quantised red, so a ramp of
 * greys (a luminance lightmap) never alternates between green and magenta
 * tints. */
uint16_t dgl_pack_grey565(unsigned l)
{
    unsigned r = q(l, 5);
    return (uint16_t)((r << 11) | (((r << 1) | (r >> 4)) << 5) | r);
}

