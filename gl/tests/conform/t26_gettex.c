/* t26: glGetTexImage. Each texture is read back and uploaded again as a new
 * texture; the original and its copy are drawn side by side and must look
 * the same: an opaque checkerboard (RGBA), one with alpha (drawn blended),
 * a luminance ramp read as RGB, as luminance and as alpha, and mipmap
 * levels read one by one (each level a colour of its own, drawn as a flat
 * quad in the colour read from its first texel). Rows are read with a pack
 * alignment of 4 and 1 into narrow levels, where the padding shows. */
#include "ct_tex.h"
#include <string.h>

static GLuint upload(int w, int h, GLenum ifmt, GLenum fmt, const unsigned char *p)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ifmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, p);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

void ct_run(void)
{
    static unsigned char buf[64 * 64 * 4], back[64 * 64 * 4], lum[32 * 32], lev[16 * 16 * 4];
    static const unsigned char level_rgb[5][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {0, 255, 255}};
    GLuint t;
    int i, l;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0.1f, 0.1f, 0.15f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    /* Opaque checkerboard: original, then its read-back copy. */
    ct_checker(buf, 64, 64, 8, 0);
    upload(64, 64, GL_RGBA, GL_RGBA, buf);
    ct_quad2d(10, 330, 140, 460, 0, 0, 1, 1);
    memset(back, 0, sizeof back);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
    upload(64, 64, GL_RGBA, GL_RGBA, back);
    ct_quad2d(150, 330, 280, 460, 0, 0, 1, 1);

    /* Checkerboard with alpha, blended over a white bar: the alpha must survive. */
    glDisable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glVertex2f(300, 380); glVertex2f(630, 380); glVertex2f(630, 410); glVertex2f(300, 410);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ct_checker(buf, 64, 64, 8, 2);
    upload(64, 64, GL_RGBA, GL_RGBA, buf);
    ct_quad2d(300, 330, 430, 460, 0, 0, 1, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
    upload(64, 64, GL_RGBA, GL_RGBA, back);
    ct_quad2d(440, 330, 570, 460, 0, 0, 1, 1);
    glDisable(GL_BLEND);

    /* Luminance ramp: read as RGB, as luminance and as alpha (= 255). */
    for (i = 0; i < 32 * 32; i++)
        lum[i] = (unsigned char)((i % 32) * 8 + (i / 32) / 4);
    upload(32, 32, GL_LUMINANCE, GL_LUMINANCE, lum);
    ct_quad2d(10, 180, 130, 300, 0, 0, 1, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, back);
    upload(32, 32, GL_RGB, GL_RGB, back);
    ct_quad2d(140, 180, 260, 300, 0, 0, 1, 1);
    upload(32, 32, GL_LUMINANCE, GL_LUMINANCE, lum);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, back);
    upload(32, 32, GL_LUMINANCE, GL_LUMINANCE, back);
    ct_quad2d(270, 180, 390, 300, 0, 0, 1, 1);
    upload(32, 32, GL_LUMINANCE, GL_LUMINANCE, lum);
    memset(back, 0, sizeof back);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_UNSIGNED_BYTE, back);
    upload(32, 32, GL_LUMINANCE, GL_LUMINANCE, back);   /* all white */
    ct_quad2d(400, 180, 520, 300, 0, 0, 1, 1);

    /* Mipmap levels 0-4 of a 16x16 texture, each one colour; each level
     * read back (RGB, pack alignment 1 for the odd widths of 2 and 1) and
     * shown as a flat quad of its first texel's colour, next to a strip
     * of the level itself. */
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    for (l = 0; l < 5; l++) {
        int s = 16 >> l;
        for (i = 0; i < s * s; i++)
            memcpy(lev + i * 4, level_rgb[l], 3), lev[i * 4 + 3] = 255;
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, lev);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (l = 0; l < 5; l++) {
        int s = 16 >> l;
        memset(back, 0, sizeof back);
        glBindTexture(GL_TEXTURE_2D, t);
        glGetTexImage(GL_TEXTURE_2D, l, GL_RGB, GL_UNSIGNED_BYTE, back);
        glDisable(GL_TEXTURE_2D);
        /* the last texel of the level (row padding would misplace it) */
        glColor3ub(back[(s * s - 1) * 3], back[(s * s - 1) * 3 + 1], back[(s * s - 1) * 3 + 2]);
        glBegin(GL_QUADS);
        glVertex2f(10.0f + (float)l * 120, 30);  glVertex2f(110.0f + (float)l * 120, 30);
        glVertex2f(110.0f + (float)l * 120, 130); glVertex2f(10.0f + (float)l * 120, 130);
        glEnd();
        glColor3f(1, 1, 1);
        glEnable(GL_TEXTURE_2D);
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    ct_frame("t26_0");
}
