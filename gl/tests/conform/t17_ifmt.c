/* t17: internal formats. One RGBA source (red and green ramps, a checker in
 * blue, alpha rising left to right) uploaded as each GL 1.1 base format and
 * drawn blended over a coloured background, so the components each format
 * drops (alpha for RGB, colour for ALPHA) or derives (luminance and
 * intensity from red) show. RGB5_A1 gets a binary-alpha source, since the
 * reference may keep more than one alpha bit. */
#include "ct_tex.h"
#include <string.h>

static unsigned char src[64 * 64 * 4], cut[64 * 64 * 4];

static GLuint tex(GLint ifmt, GLenum fmt, const unsigned char *px)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, 64, 64, 0, fmt, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

static void tile(int i, GLuint t, float r, float g, float b)
{
    float x = 16.0f + (float)(i % 4) * 156.0f, y = 250.0f - (float)(i / 4) * 230.0f;
    glBindTexture(GL_TEXTURE_2D, t);
    glColor3f(r, g, b);
    ct_quad2d(x, y, x + 140.0f, y + 200.0f, 0, 0, 1, 1);
}

void ct_run(void)
{
    static unsigned char lum[64 * 64];
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            unsigned char *p = src + (y * 64 + x) * 4, *c = cut + (y * 64 + x) * 4;
            p[0] = (unsigned char)(x * 4);
            p[1] = (unsigned char)(y * 4);
            p[2] = ((x / 8 + y / 8) & 1) ? 255 : 40;
            p[3] = (unsigned char)(x * 4 + 3);
            memcpy(c, p, 4);
            c[3] = ((x / 4 + y / 4) & 1) ? 255 : 0;
            lum[y * 64 + x] = (unsigned char)(255 - (x + y) * 2);
        }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    /* Background: a Gouraud-shaded quad. */
    glBegin(GL_QUADS);
    glColor3f(0.1f, 0.2f, 0.6f); glVertex2f(0, 0);
    glColor3f(0.7f, 0.2f, 0.1f); glVertex2f(CT_W, 0);
    glColor3f(0.2f, 0.7f, 0.2f); glVertex2f(CT_W, CT_H);
    glColor3f(0.6f, 0.6f, 0.1f); glVertex2f(0, CT_H);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    tile(0, tex(GL_RGBA, GL_RGBA, src), 1, 1, 1);
    tile(1, tex(GL_RGB, GL_RGBA, src), 1, 1, 1);                   /* alpha dropped */
    tile(2, tex(GL_LUMINANCE, GL_RGBA, src), 1, 0.8f, 0.6f);       /* L = red, opaque */
    tile(3, tex(GL_LUMINANCE_ALPHA, GL_RGBA, src), 1, 1, 1);
    tile(4, tex(GL_INTENSITY, GL_RGBA, src), 1, 1, 1);             /* I = red, alpha too */
    tile(5, tex(GL_ALPHA, GL_RGBA, src), 1.0f, 0.5f, 0.2f);        /* colour from the vertex */
    tile(6, tex(GL_RGB5_A1, GL_RGBA, cut), 1, 1, 1);
    /* A luminance lightmap from luminance data, multiplied into the frame
     * (GLQuake's GL_ZERO, GL_ONE_MINUS_SRC_COLOR, here its inverse). */
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    tile(7, tex(GL_LUMINANCE, GL_LUMINANCE, lum), 1, 1, 1);
    ct_frame("t17_0");
}
