/* t39: GL_ARB_texture_compression with GL_EXT_texture_compression_s3tc. Procedural 64x64 images in each of the
 * four formats, made block by block so every form shows: DXT1 with four colours and with three and transparent
 * black (drawn through the alpha test), DXT3's explicit alphas and DXT5's interpolated ones in both of its modes
 * (blended over a stripe); then glCompressedTexSubImage2DARB over part of one, and the blocks read back with
 * glGetCompressedTexImageARB. Mipmapped from the same blocks' smaller images. Skipped where not advertised. */
#define GL_GLEXT_PROTOTYPES 1
#include "ct_tex.h"
#include <string.h>

static unsigned short rgb565(int r, int g, int b) { return (unsigned short)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3); }

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

/* A colour block: ends by the block's place, indices a diagonal ramp (or a ring for the three-colour form). */
static void colour_block(unsigned char *b, int bx, int by, int three)
{
    unsigned c0 = rgb565(40 + bx * 12, 220 - by * 12, 60 + (bx ^ by) * 10), c1 = rgb565(230 - by * 10, 50 + bx * 14, 200);
    unsigned long bits = 0;
    int i;
    if (three ? c0 > c1 : c0 <= c1) {              /* (the ends' order picks the form) */
        unsigned t = c0;
        c0 = c1, c1 = t;
        if (c0 == c1)
            c1 ^= 1;
    }
    put16(b, c0), put16(b + 2, c1);
    for (i = 0; i < 16; i++) {
        int x = i & 3, y = i >> 2, v = three ? ((x == 0 || x == 3 || y == 0 || y == 3) ? (x + y) % 3 : 3)
                                             : (x + y) / 2;
        bits |= (unsigned long)(v & 3) << (2 * i);
    }
    b[4] = (unsigned char)bits, b[5] = (unsigned char)(bits >> 8), b[6] = (unsigned char)(bits >> 16);
    b[7] = (unsigned char)(bits >> 24);
}

/* An image's blocks in format f, w x h texels (w, h at least 4 here). */
static int make(GLenum f, int w, int h, int seed, unsigned char *out)
{
    int bx, by, i, n = 0;
    for (by = 0; by < h / 4; by++)
        for (bx = 0; bx < w / 4; bx++) {
            unsigned char *b = out + n;
            int x = bx + seed, y = by;
            if (f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                colour_block(b, x, y, f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT && ((x + y) & 1));
                n += 8;
                continue;
            }
            if (f == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT)
                for (i = 0; i < 8; i++)            /* alpha nibbles: a ramp across the block, offset by its place */
                    b[i] = (unsigned char)(((2 * i + x) & 15) | ((2 * i + 1 + y) & 15) << 4);
            else {
                unsigned long long bits = 0;
                b[0] = (unsigned char)(((x + y) & 1) ? 40 : 240);     /* both of DXT5's modes */
                b[1] = (unsigned char)(((x + y) & 1) ? 230 : 20);
                for (i = 0; i < 16; i++)
                    bits |= (unsigned long long)((i + x + y) & 7) << (3 * i);
                for (i = 0; i < 6; i++)
                    b[2 + i] = (unsigned char)(bits >> (8 * i));
            }
            colour_block(b + 8, x, y, 0);
            n += 16;
        }
    return n;
}

static GLuint texture(GLenum f, int seed)
{
    static unsigned char blocks[64 * 64];
    GLuint t;
    int l, size, n;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    for (l = 0, size = 64; size >= 4; l++, size /= 2) {
        n = make(f, size, size, seed + l, blocks);
        glCompressedTexImage2DARB(GL_TEXTURE_2D, l, f, size, size, 0, n, blocks);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, l - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

void ct_run(void)
{
    static const GLenum fmt[4] = { GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                                   GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT };
    static unsigned char sub[16 * 16], back[64 * 64], want[64 * 64];
    GLint nf = 0, comp = 0, size = 0, ifmt = 0;
    GLuint t[4];
    int i, n, same;
    if (!ct_has("GL_EXT_texture_compression_s3tc") || !ct_has("GL_ARB_texture_compression")) {
        ct_log("CT-SKIP no GL_EXT_texture_compression_s3tc");
        return;
    }
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS_ARB, &nf);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.9f, 0.9f, 0.9f);
    ct_quad2d(0, 200, 640, 232, 0, 0, 1, 1);        /* (a stripe behind the blended ones) */
    glColor3f(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    for (i = 0; i < 4; i++)
        t[i] = texture(fmt[i], i * 3);

    /* Top row: each format magnified (4 screen pixels a texel); DXT1 RGBA through the alpha test. */
    for (i = 0; i < 4; i++) {
        glBindTexture(GL_TEXTURE_2D, t[i]);
        if (i == 1) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5f);
        } else if (i >= 2) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        ct_quad2d(16.0f + i * 156.0f, 248.0f, 16.0f + i * 156.0f + 144.0f, 392.0f, 0, 0, 1, 1);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_BLEND);
    }
    /* Bottom row: minified onto levels 1 and 2 (32x32 and 16x16 on screen), and a sub-image put into DXT5's level 0. */
    for (i = 0; i < 4; i++) {
        glBindTexture(GL_TEXTURE_2D, t[i]);
        ct_quad2d(16.0f + i * 156.0f, 120.0f, 48.0f + i * 156.0f, 152.0f, 0, 0, 1, 1);
        ct_quad2d(64.0f + i * 156.0f, 120.0f, 80.0f + i * 156.0f, 136.0f, 0, 0, 1, 1);
    }
    glBindTexture(GL_TEXTURE_2D, t[3]);
    n = make(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 16, 16, 7, sub);
    glCompressedTexSubImage2DARB(GL_TEXTURE_2D, 0, 16, 32, 16, 16, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, n, sub);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ct_quad2d(400, 8, 528, 136, 0, 0, 1, 1);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t39_0");

    /* The blocks read back: DXT5's level 0 is its first blocks with the sub-image's in place. */
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_ARB, &comp);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB, &size);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
    memset(back, 0, sizeof back);
    if (comp && size == 64 * 64)
        glGetCompressedTexImageARB(GL_TEXTURE_2D, 0, back);
    make(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 64, 64, 9, want);
    for (i = 0; i < 4; i++)                         /* (the sub-image's four rows of blocks: 4 blocks of 16 bytes) */
        memcpy(want + ((8 + i) * 16 + 4) * 16, sub + i * 4 * 16, 4 * 16);
    same = !memcmp(back, want, sizeof want);
    ct_check("s3tc-query", nf >= 4 && comp && size == 64 * 64 && ifmt == (GLint)GL_COMPRESSED_RGBA_S3TC_DXT5_EXT && same,
             "formats %d, compressed %d, size %d, internal format %04x, blocks read back %s", nf, comp, size, ifmt,
             same ? "as made" : "DIFFERENT");
}
