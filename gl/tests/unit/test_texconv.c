/* test_texconv.c - texel formats, alpha classes and client format expansion. */
#include "unit.h"
#include "../../src/gl/gl_tex.h"
#include <string.h>

void unit_run(void)
{
    unsigned char opaque[8] = { 255, 0, 0, 255, 0, 255, 0, 255 };
    int i;
    unsigned char binary[8] = { 255, 0, 0, 0, 0, 255, 0, 255 };
    unsigned char grad[8] = { 255, 0, 0, 128, 0, 255, 0, 255 };
    unsigned char bgra[4] = { 10, 20, 30, 40 }, out[8];
    uint16_t t[2];
    CHECK(dgl_alpha_class(opaque, 2) == DGL_ALPHA_OPAQUE);
    CHECK(dgl_alpha_class(binary, 2) == DGL_ALPHA_BINARY);
    CHECK(dgl_alpha_class(grad, 2) == DGL_ALPHA_GRADIENT);
    CHECK(dgl_hwfmt_for_class(DGL_ALPHA_OPAQUE) == DGL_TW16);
    CHECK(dgl_hwfmt_for_class(DGL_ALPHA_BINARY) == DGL_TW15);
    CHECK(dgl_hwfmt_for_class(DGL_ALPHA_GRADIENT) == DGL_TW12);
    /* Exact extremes and rounding. */
    CHECK(dgl_pack_texel(DGL_TW16, 255, 255, 255, 255) == 0xFFFF);
    CHECK(dgl_pack_texel(DGL_TW16, 255, 0, 0, 255) == 0xF800);
    CHECK(dgl_pack_texel(DGL_TW16, 0, 255, 0, 255) == 0x07E0);
    CHECK(dgl_pack_texel(DGL_TW16, 132, 0, 0, 255) == (16u << 11));   /* 132*31/255 = 16.05 */
    CHECK(dgl_pack_texel(DGL_TW15, 255, 255, 255, 0) == 0x7FFF);
    CHECK(dgl_pack_texel(DGL_TW15, 0, 0, 0, 255) == 0x8000);
    CHECK(dgl_pack_texel(DGL_TW12, 255, 0, 0, 128) == 0x8F00);
    dgl_convert_rgba(DGL_TW15, binary, 2, t);
    CHECK(t[0] == 0x7C00 && t[1] == 0x83E0);
    /* BGRA and RGB expand to RGBA. */
    CHECK(dgl_to_rgba(GL_BGRA_EXT, bgra, 1, out) == 0);
    CHECK(out[0] == 30 && out[1] == 20 && out[2] == 10 && out[3] == 40);
    CHECK(dgl_to_rgba(GL_RGB, bgra, 1, out) == 0 && out[3] == 255 && out[0] == 10);
    CHECK(dgl_to_rgba(GL_LUMINANCE, bgra, 1, out) == 0 && out[1] == 10);
    CHECK(dgl_to_rgba(0x1234, bgra, 1, out) == -1);
    CHECK(dgl_format_bytes(GL_BGRA_EXT) == 4 && dgl_format_bytes(GL_RGB) == 3);
    /* Internal formats: the class, then what the texel keeps. */
    CHECK(dgl_ifmt_class(3) == DGL_IF_RGB && dgl_ifmt_class(GL_RGB5) == DGL_IF_RGB);
    CHECK(dgl_ifmt_class(4) == DGL_IF_RGBA && dgl_ifmt_class(GL_RGBA4) == DGL_IF_RGBA);
    CHECK(dgl_ifmt_class(GL_RGB5_A1) == DGL_IF_RGB5_A1 && dgl_ifmt_class(1) == DGL_IF_LUMINANCE);
    CHECK(dgl_ifmt_class(GL_INTENSITY8) == DGL_IF_INTENSITY && dgl_ifmt_class(GL_ALPHA8) == DGL_IF_ALPHA);
    CHECK(dgl_ifmt_class(GL_LUMINANCE8_ALPHA8) == DGL_IF_LUMINANCE_ALPHA && dgl_ifmt_class(5) == -1);
    {
        unsigned char px[4];
        memcpy(px, "\x50\x60\x70\x10", 4); dgl_apply_ifmt(DGL_IF_RGB, px, 1);
        CHECK(px[0] == 0x50 && px[2] == 0x70 && px[3] == 255);
        memcpy(px, "\x50\x60\x70\x10", 4); dgl_apply_ifmt(DGL_IF_LUMINANCE, px, 1);
        CHECK(px[1] == 0x50 && px[2] == 0x50 && px[3] == 255);
        memcpy(px, "\x50\x60\x70\x10", 4); dgl_apply_ifmt(DGL_IF_INTENSITY, px, 1);
        CHECK(px[1] == 0x50 && px[3] == 0x50);
        memcpy(px, "\x50\x60\x70\x10", 4); dgl_apply_ifmt(DGL_IF_ALPHA, px, 1);
        CHECK(px[0] == 255 && px[2] == 255 && px[3] == 0x10);
    }
    /* Greys in RGB565 keep green = red widened, for every level. */
    for (i = 0; i < 256; i++) {
        uint16_t g = dgl_pack_grey565((unsigned)i);
        unsigned r5 = g >> 11, g6 = (g >> 5) & 63, b5 = g & 31;
        if (r5 != b5 || g6 != ((r5 << 1) | (r5 >> 4)))
            break;
    }
    CHECK(i == 256);
    /* dgl_pack_row (by table) gives dgl_pack_texel's and dgl_pack_grey565's
     * values: every level in each channel, the others varied. */
    {
        static const int fmts[3] = { DGL_TW16, DGL_TW15, DGL_TW12 };
        unsigned char px[256 * 4];
        uint16_t got[256], grey[256];
        int f, bad = 0;
        for (i = 0; i < 256; i++) {
            px[i * 4 + 0] = (unsigned char)i;
            px[i * 4 + 1] = (unsigned char)(i * 7 + 3);
            px[i * 4 + 2] = (unsigned char)(255 - i);
            px[i * 4 + 3] = (unsigned char)(i * 13 + 5);
        }
        for (f = 0; f < 3; f++) {
            dgl_pack_row(fmts[f], 0, px, 256, got);
            for (i = 0; i < 256; i++)
                bad += got[i] != dgl_pack_texel(fmts[f], px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]);
            dgl_pack_row(fmts[f], 0, px + 4, 255, got);         /* (shifted: green and alpha take the other values) */
            for (i = 0; i < 255; i++)
                bad += got[i] != dgl_pack_texel(fmts[f], px[i * 4 + 4], px[i * 4 + 5], px[i * 4 + 6], px[i * 4 + 7]);
        }
        dgl_pack_row(DGL_TW16, 1, px, 256, grey);
        for (i = 0; i < 256; i++)
            bad += grey[i] != dgl_pack_grey565((unsigned)i);
        CHECK(bad == 0);
    }
    /* Formats expand the same a row at a time. */
    {
        unsigned char src[6] = { 10, 20, 30, 40, 50, 60 }, rgba[12];
        CHECK(dgl_to_rgba(GL_LUMINANCE_ALPHA, src, 3, rgba) == 0);
        CHECK(rgba[0] == 10 && rgba[2] == 10 && rgba[3] == 20 && rgba[8] == 50 && rgba[11] == 60);
        CHECK(dgl_to_rgba(GL_BGR_EXT, src, 2, rgba) == 0);
        CHECK(rgba[0] == 30 && rgba[2] == 10 && rgba[3] == 255 && rgba[4] == 60 && rgba[6] == 40);
        dgl_apply_ifmt(DGL_IF_INTENSITY, rgba, 2);
        CHECK(rgba[1] == 30 && rgba[3] == 30 && rgba[7] == 60);
    }
}
