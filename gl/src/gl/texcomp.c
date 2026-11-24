/* texcomp.c - GL_ARB_texture_compression with GL_EXT_texture_compression_s3tc's four formats (docs/dx7.md, X1).
 *
 * Every card gets them: the blocks are decoded by the CPU into the level's RGBA8 shadow copy, which the back end
 * converts as it converts any texture, so a program that ships DXT textures (or asks for compressed internal
 * formats) runs everywhere. The blocks are kept beside the copy for glGetCompressedTexImageARB (and for a back end
 * that could take them as they are). Internal formats asking for compression with uncompressed texels
 * (GL_COMPRESSED_RGB_ARB, or an S3TC format through glTexImage2D) are kept uncompressed, which the extension
 * allows: GL_TEXTURE_COMPRESSED_ARB is then false. */
#include "gl_tex.h"
#include "../dgl/dgl.h"
#include <stdlib.h>
#include <string.h>

int dgl_s3tc_format(GLenum f)
{
    return f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
           f == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT || f == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
}

int dgl_s3tc_size(GLenum f, int w, int h)
{
    int bw = w > 0 ? (w + 3) / 4 : 0, bh = h > 0 ? (h + 3) / 4 : 0;
    return bw * bh * (f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16);
}

/* A colour block (8 bytes: two RGB565 ends, sixteen 2-bit indices) into 16 RGBA texels. four: the four-colour
 * form always (DXT3 and DXT5), else as the ends' order says (DXT1: c0 <= c1 is three colours and transparent
 * black, opaque black when rgb_only). */
static void colour_block(const unsigned char *b, int four, int rgb_only, unsigned char out[16][4])
{
    unsigned c0 = (unsigned)b[0] | (unsigned)b[1] << 8, c1 = (unsigned)b[2] | (unsigned)b[3] << 8;
    unsigned long bits = (unsigned long)b[4] | (unsigned long)b[5] << 8 | (unsigned long)b[6] << 16 |
                         (unsigned long)b[7] << 24;
    unsigned char pal[4][4];
    int i, k;
    for (i = 0; i < 2; i++) {
        unsigned c = i ? c1 : c0, r = c >> 11 & 31, g = c >> 5 & 63, bl = c & 31;
        pal[i][0] = (unsigned char)(r << 3 | r >> 2);
        pal[i][1] = (unsigned char)(g << 2 | g >> 4);
        pal[i][2] = (unsigned char)(bl << 3 | bl >> 2);
        pal[i][3] = 255;
    }
    if (four || c0 > c1) {
        for (k = 0; k < 3; k++) {
            pal[2][k] = (unsigned char)((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = (unsigned char)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (k = 0; k < 3; k++) {
            pal[2][k] = (unsigned char)((pal[0][k] + pal[1][k] + 1) / 2);
            pal[3][k] = 0;
        }
        pal[2][3] = 255;
        pal[3][3] = (unsigned char)(rgb_only ? 255 : 0);
    }
    for (i = 0; i < 16; i++)
        memcpy(out[i], pal[bits >> (2 * i) & 3], 4);
}

/* One 4x4 block of format f at b into 16 RGBA texels (row by row). */
static void block(GLenum f, const unsigned char *b, unsigned char out[16][4])
{
    int i;
    if (f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
        colour_block(b, 0, f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT, out);
        return;
    }
    colour_block(b + 8, 1, 0, out);
    if (f == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {            /* sixteen explicit 4-bit alphas */
        for (i = 0; i < 16; i++) {
            unsigned a = (unsigned)(b[i / 2] >> (4 * (i & 1)) & 15);
            out[i][3] = (unsigned char)(a << 4 | a);
        }
    } else {                                                /* DXT5: two ends, sixteen 3-bit indices */
        unsigned a0 = b[0], a1 = b[1], pal[8];
        unsigned long long bits = 0;
        for (i = 0; i < 6; i++)
            bits |= (unsigned long long)b[2 + i] << (8 * i);
        pal[0] = a0, pal[1] = a1;
        if (a0 > a1)
            for (i = 1; i < 7; i++)
                pal[1 + i] = ((7 - i) * a0 + i * a1 + 3) / 7;
        else {
            for (i = 1; i < 5; i++)
                pal[1 + i] = ((5 - i) * a0 + i * a1 + 2) / 5;
            pal[6] = 0, pal[7] = 255;
        }
        for (i = 0; i < 16; i++)
            out[i][3] = (unsigned char)pal[bits >> (3 * i) & 7];
    }
}

/* w x h texels of format f from the blocks at src into RGBA rows of w at dst. */
void dgl_s3tc_decode(GLenum f, const unsigned char *src, int w, int h, unsigned char *dst)
{
    int bw = (w + 3) / 4, bh = (h + 3) / 4, bx, by, x, y, n = f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
                                                           f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16;
    unsigned char t[16][4];
    for (by = 0; by < bh; by++)
        for (bx = 0; bx < bw; bx++, src += n) {
            block(f, src, t);
            for (y = 0; y < 4 && by * 4 + y < h; y++)
                for (x = 0; x < 4 && bx * 4 + x < w; x++)
                    memcpy(dst + ((size_t)(by * 4 + y) * w + bx * 4 + x) * 4, t[y * 4 + x], 4);
        }
}

static unsigned char *scratch(size_t n)
{
    static unsigned char *buf;
    static size_t cap;
    if (n > cap) {
        unsigned char *p = (unsigned char *)realloc(buf, n);
        if (!p)
            return NULL;
        buf = p, cap = n;
    }
    return buf;
}

void APIENTRY glCompressedTexImage2DARB(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border,
                                        GLsizei size, const GLvoid *data)
{
    dgl_level *L;
    unsigned char *rgba, *keep;
    if (target != GL_TEXTURE_2D) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (!dgl_s3tc_format(ifmt)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (border != 0 || w < 0 || h < 0 || size != dgl_s3tc_size(ifmt, w, h) || level < 0 || level >= DGL_MAX_LEVELS) {
        dgl_gl_error(GL_INVALID_VALUE);
        return;
    }
    rgba = scratch((size_t)(w ? w : 1) * (h ? h : 1) * 4);
    keep = (unsigned char *)malloc(size ? (size_t)size : 1);
    if (!rgba || !keep) { free(keep); dgl_gl_error(GL_OUT_OF_MEMORY); return; }
    if (data) {
        dgl_s3tc_decode(ifmt, (const unsigned char *)data, w, h, rgba);
        memcpy(keep, data, (size_t)size);
    } else
        memset(rgba, 0, (size_t)(w ? w : 1) * (h ? h : 1) * 4), memset(keep, 0, (size_t)size);
    glTexImage2D(target, level, ifmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ? GL_RGB : GL_RGBA, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data ? rgba : NULL);
    L = dgl_bound_level(level);
    if (!L || !DGL_LEVEL_DEFINED(L) || L->w != w || L->h != h) {   /* (glTexImage2D refused it: its error stands) */
        free(keep);
        return;
    }
    L->ifmt = (GLint)ifmt;
    L->comp = keep;
    L->comp_size = size;
}

void APIENTRY glCompressedTexSubImage2DARB(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                           GLenum format, GLsizei size, const GLvoid *data)
{
    dgl_level *L = target == GL_TEXTURE_2D ? dgl_bound_level(level) : NULL;
    unsigned char *rgba, *keep;
    int bw, bb, rows, n, j;
    if (target != GL_TEXTURE_2D || !dgl_s3tc_format(format)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (!L || !DGL_LEVEL_DEFINED(L) || L->ifmt != (GLint)format) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > L->w || y + h > L->h || (x & 3) || (y & 3) ||
        ((w & 3) && x + w != L->w) || ((h & 3) && y + h != L->h) || size != dgl_s3tc_size(format, w, h)) {
        dgl_gl_error(GL_INVALID_VALUE);
        return;
    }
    if (!w || !h)
        return;
    rgba = scratch((size_t)w * h * 4);
    if (!rgba) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
    dgl_s3tc_decode(format, (const unsigned char *)data, w, h, rgba);
    keep = L->comp;                                     /* (glTexSubImage2D lets the blocks go: put them back) */
    n = L->comp_size;
    L->comp = NULL;
    glTexSubImage2D(target, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (!keep)
        return;
    /* The blocks replaced in the kept copy, row of blocks by row of blocks. */
    bw = (L->w + 3) / 4;
    rows = (h + 3) / 4;
    bb = dgl_s3tc_size(format, 4, 4);                   /* (bytes a block) */
    for (j = 0; j < rows; j++)
        memcpy(keep + ((size_t)(y / 4 + j) * bw + x / 4) * bb, (const unsigned char *)data + (size_t)j * ((w + 3) / 4) * bb,
               (size_t)((w + 3) / 4) * bb);
    L->comp = keep;
    L->comp_size = n;
}

void APIENTRY glGetCompressedTexImageARB(GLenum target, GLint level, GLvoid *img)
{
    dgl_level *L = target == GL_TEXTURE_2D ? dgl_bound_level(level) : NULL;
    if (target != GL_TEXTURE_2D) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (!L || !L->comp) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    memcpy(img, L->comp, (size_t)L->comp_size);
}

/* GL 1.1 has no 1D or 3D textures. */
void APIENTRY glCompressedTexImage1DARB(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLint border, GLsizei size,
                                        const GLvoid *data)
{
    (void)target, (void)level, (void)ifmt, (void)w, (void)border, (void)size, (void)data;
    dgl_gl_error(GL_INVALID_ENUM);
}

void APIENTRY glCompressedTexSubImage1DARB(GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLsizei size,
                                           const GLvoid *data)
{
    (void)target, (void)level, (void)x, (void)w, (void)format, (void)size, (void)data;
    dgl_gl_error(GL_INVALID_ENUM);
}
