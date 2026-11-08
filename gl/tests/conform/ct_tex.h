/* ct_tex.h - procedural textures shared by the textured conformance tests. */
#ifndef CT_TEX_H
#define CT_TEX_H
#include "ct.h"

/* A w x h RGBA checkerboard of cells c texels wide, with a colour ramp so
 * every texel differs (mistaken texel addressing shows). */
static __attribute__((unused)) void ct_checker(unsigned char *p, int w, int h, int c, unsigned char alpha_mode)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int on = ((x / c) + (y / c)) & 1;
            unsigned char *t = p + (y * w + x) * 4;
            t[0] = (unsigned char)(on ? 255 : 40 + (x * 200) / w);
            t[1] = (unsigned char)(on ? 220 : 40 + (y * 200) / h);
            t[2] = (unsigned char)(on ? 40 : 160);
            t[3] = alpha_mode == 0 ? 255 : alpha_mode == 1 ? (on ? 255 : 0) : (unsigned char)((x * 255) / (w - 1));
        }
}

static __attribute__((unused)) GLuint ct_texture(int w, int h, int c, int alpha_mode, GLenum filter, GLenum wrap)
{
    static unsigned char buf[256 * 256 * 4];
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    ct_checker(buf, w, h, c, (unsigned char)alpha_mode);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    return t;
}

static __attribute__((unused)) void ct_quad2d(float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(s0, t0); glVertex2f(x0, y0);
    glTexCoord2f(s1, t0); glVertex2f(x1, y0);
    glTexCoord2f(s1, t1); glVertex2f(x1, y1);
    glTexCoord2f(s0, t1); glVertex2f(x0, y1);
    glEnd();
}
#endif
