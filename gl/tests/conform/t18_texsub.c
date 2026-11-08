/* t18: glTexSubImage2D between draws in one frame. A quad drawn before an
 * update must keep the old texels and one drawn after must show the new
 * ones: DOS-GL writes in place when the texture is idle and re-uploads into
 * a new block when queued draws may still read it. Also a luminance
 * lightmap updated in odd-sized rectangles with an unpack alignment of 1. */
#include "ct_tex.h"

static void fill(unsigned char *p, int n, unsigned char r, unsigned char g, unsigned char b)
{
    int i;
    for (i = 0; i < n; i++, p += 4) {
        p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
    }
}

void ct_run(void)
{
    static unsigned char red[16 * 16 * 4], green[24 * 8 * 4], lm[16 * 16], patch[3 * 5];
    GLuint t, l;
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0.05f, 0.05f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    t = ct_texture(64, 64, 8, 0, GL_NEAREST, GL_REPEAT);
    ct_quad2d(10, 10, 20, 20, 0, 0, 1, 1);        /* makes it resident */
    glFinish();                                    /* ...and idle */
    fill(red, 16 * 16, 255, 0, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 8, 8, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, red);        /* in place */
    ct_quad2d(20, 200, 220, 400, 0, 0, 1, 1);     /* red square only */
    fill(green, 24 * 8, 0, 255, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 30, 40, 24, 8, GL_RGBA, GL_UNSIGNED_BYTE, green);     /* busy */
    ct_quad2d(230, 200, 430, 400, 0, 0, 1, 1);    /* red and green */
    fill(red, 16 * 16, 0, 0, 255);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 40, 4, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, red);       /* busy again */
    ct_quad2d(440, 200, 640 - 10, 400, 0, 0, 1, 1);
    /* Lightmap: 16x16 luminance, patched in 3x5 pieces. */
    for (i = 0; i < 16 * 16; i++)
        lm[i] = (unsigned char)(i & 255);
    glGenTextures(1, &l);
    glBindTexture(GL_TEXTURE_2D, l);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 16, 16, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lm);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ct_quad2d(20, 20, 180, 180, 0, 0, 1, 1);
    for (i = 0; i < 3 * 5; i++)
        patch[i] = (unsigned char)(250 - i * 10);
    for (i = 0; i < 4; i++) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 1 + i * 4, 2 + i * 2, 3, 5, GL_LUMINANCE, GL_UNSIGNED_BYTE, patch);
        ct_quad2d(200.0f + (float)i * 110.0f, 20, 300.0f + (float)i * 110.0f, 180, 0, 0, 1, 1);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    (void)t;
    ct_frame("t18_0");
}
