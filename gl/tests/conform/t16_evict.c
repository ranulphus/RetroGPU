/* t16: texture memory runs out. The manifest caps DOS-GL's texture heap
 * at 600 KB (DGL_TEXHEAP_KB); 30 textures of 128x128 (32 KB each in VRAM,
 * 960 KB in all) are drawn in one frame, so textures drawn earlier in the
 * same frame have to be evicted (after a sync), then all 30 are drawn again
 * and come back from DOS-GL's copies. Each texture differs (cell size and
 * ramp direction), so one uploaded into the wrong place shows. */
#include "ct_tex.h"

#define N 30

static GLuint make(int i)
{
    static unsigned char buf[128 * 128 * 4];
    GLuint t;
    int k;
    ct_checker(buf, 128, 128, 2 + (i % 7) * 3, 0);
    if (i & 1)                                   /* mirror half of them */
        for (k = 0; k < 128 * 128; k++) {
            unsigned char r = buf[k * 4];
            buf[k * 4] = buf[k * 4 + 2];
            buf[k * 4 + 2] = r;
        }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

static void grid(const GLuint *tex)
{
    int i;
    for (i = 0; i < N; i++) {
        float x = 8.0f + (float)(i % 6) * 104.0f, y = 8.0f + (float)(i / 6) * 94.0f;
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glColor3f(1.0f, 0.6f + 0.4f * (float)(i % 3) / 2.0f, 1.0f);
        ct_quad2d(x, y, x + 96.0f, y + 86.0f, 0, 0, 1, 1);
    }
}

void ct_run(void)
{
    GLuint tex[N];
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glEnable(GL_TEXTURE_2D);
    for (i = 0; i < N; i++)
        tex[i] = make(i);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    grid(tex);                                   /* uploads, evicting within the frame */
    glClear(GL_COLOR_BUFFER_BIT);
    grid(tex);                                   /* everything again, back from the copies */
    ct_frame("t16_0");
}
