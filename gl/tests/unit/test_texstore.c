/* test_texstore.c - texture levels' copies held smaller (src/gl/texstore.c): every form gives back the texels the
 * back end would have made from the RGBA8 copy. RGB565 against both quantisers for all 2^24 colours (the Matrox
 * back end's rounding dgl_pack_texel, the NV layer's truncation); one and two bytes for luminance, alpha,
 * intensity, grey RGB and luminance-alpha exactly; sub-images narrowed to what changed; a grey RGB level given
 * colour held again in the back end's RGB form. */
#include "unit.h"
#include "../../src/gl/gl_tex.h"
#include "../../src/backend/backend.h"
#include "../../src/backend/mga/mga_be_fmt.h"
#include <stdlib.h>
#include <string.h>

static int rgb_form = DGL_ST_RGB565R;
static int store(int ifc, GLint ifmt) { (void)ifc, (void)ifmt; return rgb_form; }
static dgl_backend be = { .name = "test", .tex_store = store };
const dgl_backend *dgl_be = &be;

static unsigned rnd(void) { static unsigned s = 12345; s = s * 1103515245u + 12345u; return s >> 8; }

static void level(dgl_level *L, int ifc, int w, int h)
{
    memset(L, 0, sizeof *L);
    L->w = w, L->h = h, L->ifc = ifc, L->ifmt = GL_RGB;
    L->rgba = (unsigned char *)malloc((size_t)w * h * 4);
}

static void drop(dgl_level *L) { free(L->rgba); free(L->px); L->rgba = L->px = NULL; }

/* Every colour (r fixed a level, g and b 256 x 256) through form st: the back end's 16-bit texel from the expanded
 * copy equals the one from the original. */
static void all_colours(int st)
{
    dgl_level L;
    int r, i, bad = 0;
    unsigned char *orig = (unsigned char *)malloc(65536 * 4);
    rgb_form = st;
    for (r = 0; r < 256 && !bad; r++) {
        const unsigned char *e;
        level(&L, DGL_IF_RGB, 256, 256);
        for (i = 0; i < 65536; i++)
            L.rgba[i * 4] = (unsigned char)r, L.rgba[i * 4 + 1] = (unsigned char)(i >> 8),
            L.rgba[i * 4 + 2] = (unsigned char)i, L.rgba[i * 4 + 3] = 255;
        memcpy(orig, L.rgba, 65536 * 4);
        dgl_level_compact(&L);
        CHECK(L.px && !L.rgba && L.st == st);
        e = dgl_level_rgba(&L, 0);
        for (i = 0; i < 65536 && !bad; i++) {
            const unsigned char *o = orig + i * 4, *x = e + i * 4;
            if (st == DGL_ST_RGB565R)
                bad = dgl_pack_texel(DGL_TW16, o[0], o[1], o[2], 255) != dgl_pack_texel(DGL_TW16, x[0], x[1], x[2], 255);
            else
                bad = (o[0] >> 3) != (x[0] >> 3) || (o[1] >> 2) != (x[1] >> 2) || (o[2] >> 3) != (x[2] >> 3);
            bad |= x[3] != 255;
        }
        drop(&L);
    }
    CHECK(!bad);
    dgl_level_rgba_done();
    free(orig);
}

/* A level of class ifc with random texels (reduced to the class) held in form want and given back exactly. */
static void exact(int ifc, int rgb, int want, int make_grey)
{
    dgl_level L;
    unsigned char *orig;
    int i, n = 64 * 32;
    rgb_form = rgb;
    level(&L, ifc, 64, 32);
    for (i = 0; i < n * 4; i++)
        L.rgba[i] = (unsigned char)rnd();
    if (make_grey)
        for (i = 0; i < n; i++)
            L.rgba[i * 4 + 1] = L.rgba[i * 4 + 2] = L.rgba[i * 4];
    dgl_apply_ifmt(ifc, L.rgba, n);
    orig = (unsigned char *)malloc((size_t)n * 4);
    memcpy(orig, L.rgba, (size_t)n * 4);
    dgl_level_compact(&L);
    CHECK((L.st) == (want));
    CHECK(want == DGL_ST_RGBA8 ? L.rgba != NULL : L.px != NULL && L.rgba == NULL);
    CHECK(!memcmp(dgl_level_rgba(&L, 1), orig, (size_t)n * 4));
    {
        unsigned char buf[40 * 4];
        CHECK(!memcmp(dgl_level_span(&L, 1000, 40, buf), orig + 1000 * 4, 40 * 4));
    }
    drop(&L);
    free(orig);
}

void unit_run(void)
{
    all_colours(DGL_ST_RGB565R);
    all_colours(DGL_ST_RGB565T);
    exact(DGL_IF_LUMINANCE, DGL_ST_RGB565T, DGL_ST_X8, 0);
    exact(DGL_IF_ALPHA, DGL_ST_RGB565T, DGL_ST_X8, 0);
    exact(DGL_IF_INTENSITY, DGL_ST_RGB565R, DGL_ST_X8, 0);
    exact(DGL_IF_LUMINANCE_ALPHA, DGL_ST_RGB565R, DGL_ST_XA8, 0);
    exact(DGL_IF_RGB, DGL_ST_RGB565T, DGL_ST_X8, 1);        /* (a grey RGB level: one byte, exact) */
    exact(DGL_IF_RGB, DGL_ST_RGB8, DGL_ST_RGB8, 0);
    exact(DGL_IF_RGBA, DGL_ST_RGB565R, DGL_ST_RGBA8, 0);    /* (RGBA kept as it is) */
    exact(DGL_IF_RGB, DGL_ST_RGBA8, DGL_ST_RGBA8, 0);       /* (no say from the back end: RGBA8) */
    {
        /* Sub-images: a 16x16 luminance level, a 4x3 rectangle at (5, 6) of which only (6, 7) changes. */
        dgl_level L;
        unsigned char sub[4 * 3 * 4];
        int box[4], i;
        rgb_form = DGL_ST_RGB565T;
        level(&L, DGL_IF_LUMINANCE, 16, 16);
        for (i = 0; i < 256; i++)
            L.rgba[i * 4] = L.rgba[i * 4 + 1] = L.rgba[i * 4 + 2] = (unsigned char)i, L.rgba[i * 4 + 3] = 255;
        dgl_level_compact(&L);
        CHECK((L.st) == (DGL_ST_X8));
        for (i = 0; i < 12; i++) {
            int v = (6 + i / 4) * 16 + 5 + i % 4;
            sub[i * 4] = sub[i * 4 + 1] = sub[i * 4 + 2] = (unsigned char)v, sub[i * 4 + 3] = 255;
        }
        sub[(1 * 4 + 1) * 4] = sub[(1 * 4 + 1) * 4 + 1] = sub[(1 * 4 + 1) * 4 + 2] = 7;
        dgl_level_store_rows(&L, sub, 5, 6, 4, 3, 1, box);
        CHECK(box[0] == 6 && box[1] == 7 && box[2] == 6 && box[3] == 7);
        CHECK((L.px[7 * 16 + 6]) == (7));
        CHECK((L.px[7 * 16 + 7]) == (7 * 16 + 7));
        dgl_level_store_rows(&L, sub, 5, 6, 4, 3, 1, box);
        CHECK(box[2] < box[0]);                     /* (nothing changed the second time) */
        drop(&L);
        /* A grey RGB level (one byte) given colour: held as RGB565 again, every texel right. */
        level(&L, DGL_IF_RGB, 16, 16);
        for (i = 0; i < 256; i++)
            L.rgba[i * 4] = L.rgba[i * 4 + 1] = L.rgba[i * 4 + 2] = (unsigned char)i, L.rgba[i * 4 + 3] = 255;
        dgl_level_compact(&L);
        CHECK((L.st) == (DGL_ST_X8));
        sub[0] = 255, sub[1] = 0, sub[2] = 0, sub[3] = 255;
        dgl_level_store_rows(&L, sub, 0, 0, 1, 1, 1, box);
        CHECK((L.st) == (DGL_ST_RGB565T));
        {
            const unsigned char *e = dgl_level_rgba(&L, 0);
            CHECK(e[0] >> 3 == 31 && e[1] >> 2 == 0 && e[2] >> 3 == 0);
            CHECK(e[100 * 4] >> 3 == 100 >> 3 && e[100 * 4 + 1] >> 2 == 100 >> 2);
        }
        drop(&L);
    }
    dgl_level_rgba_done();
}
