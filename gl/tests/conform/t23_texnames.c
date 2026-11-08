/* t23: texture names a program picks itself. GL 1.1 creates a texture
 * object at the first glBindTexture of any unused name, generated or not,
 * and some programs choose large ones (Xash3D binds its sky sides to
 * 5800-5805; DOS-GL once refused every name from 4096 up). Textures under
 * generated names and under fixed ones on both sides of 4096, up to 2^30,
 * are drawn; one fixed name is deleted and reused for a different texture.
 * Each texture differs (cell size, colour order), so a mix-up shows. */
#include "ct_tex.h"

static void upload(GLuint name, int cell, int swap)
{
    static unsigned char buf[64 * 64 * 4];
    int k;
    ct_checker(buf, 64, 64, cell, 0);
    if (swap)
        for (k = 0; k < 64 * 64; k++) {
            unsigned char r = buf[k * 4];
            buf[k * 4] = buf[k * 4 + 2];
            buf[k * 4 + 2] = r;
        }
    glBindTexture(GL_TEXTURE_2D, name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void ct_run(void)
{
    static const GLuint fixed[] = { 5, 4095, 4096, 5800, 5801, 5802, 5803, 5804, 5805, 70000, 1u << 30 };
    GLuint names[4 + 11];
    int i, n = 0;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glEnable(GL_TEXTURE_2D);

    glGenTextures(4, names);                     /* 1-4 in a fresh context */
    for (i = 0; i < 4; i++)
        upload(names[n++], 2 + i, i & 1);
    for (i = 0; i < 11; i++) {
        upload(fixed[i], 3 + i % 5, (i + 1) & 1);
        names[n++] = fixed[i];
    }
    glDeleteTextures(1, &fixed[3]);              /* 5800, then a different texture under it */
    upload(fixed[3], 9, 1);
    ct_log("t23 is-texture 70000=%d 5801=%d 123456=%d", glIsTexture(70000), glIsTexture(5801), glIsTexture(123456));

    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < n; i++) {
        float x = 8.0f + (float)(i % 6) * 104.0f, y = 8.0f + (float)(i / 6) * 150.0f;
        glBindTexture(GL_TEXTURE_2D, names[i]);
        ct_quad2d(x, y, x + 96.0f, y + 140.0f, 0, 0, 1, 1);
    }
    ct_frame("t23_0");
}
