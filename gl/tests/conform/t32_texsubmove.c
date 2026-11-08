/* t32: changes to textures that queued draws still read, beyond t29's one
 * level: whole rows of a mipmapped texture's level 0 and a rectangle of its
 * level 1, and rows and rectangles of a texture 16 texels wide (too narrow
 * for the engine's copies of a rectangle: DOS-GL waits for the engine
 * there). The draws before each change show the old texels, those after it
 * the new ones. */
#include "ct_tex.h"

static unsigned char lvl[64 * 64 * 4], patch[64 * 16 * 4];

/* n texels of one colour, a little lighter along each row of w. */
static void patch_fill(int n, int w, int r, int g, int b)
{
    int i;
    for (i = 0; i < n; i++) {
        patch[i * 4 + 0] = (unsigned char)(r + (i % w) * 2);
        patch[i * 4 + 1] = (unsigned char)(g + (i % w) * 2);
        patch[i * 4 + 2] = (unsigned char)(b + (i % w) * 2);
        patch[i * 4 + 3] = 255;
    }
}

void ct_run(void)
{
    GLuint a, b;
    int k, l, w;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    /* 64x64 with all its levels; each draw reads one level (56 pixels:
     * level 0, 28: level 1). */
    glGenTextures(1, &a);
    glBindTexture(GL_TEXTURE_2D, a);
    for (l = 0, w = 64; w >= 1; l++, w /= 2) {
        ct_checker(lvl, w, w, w >= 4 ? w / 4 : 1, 0);
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, w, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, lvl);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (k = 0; k < 3; k++) {
        float x = 20.0f + 200.0f * (float)k;
        ct_quad2d(x, 380, x + 56, 436, 0, 0, 1, 1);
        ct_quad2d(x + 80, 380, x + 108, 408, 0, 0, 1, 1);
        patch_fill(64 * 8, 64, k == 0 ? 200 : 20, k == 1 ? 200 : 20, k == 2 ? 200 : 20);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 4 + 16 * k, 64, 8, GL_RGBA, GL_UNSIGNED_BYTE, patch);
        patch_fill(12 * 6, 12, 120, 30 + 60 * k, 220 - 60 * k);
        glTexSubImage2D(GL_TEXTURE_2D, 1, 4 + 6 * k, 8, 12, 6, GL_RGBA, GL_UNSIGNED_BYTE, patch);
        ct_quad2d(x, 300, x + 56, 356, 0, 0, 1, 1);
        ct_quad2d(x + 80, 300, x + 108, 328, 0, 0, 1, 1);
    }

    /* 16x16: a rectangle, whole rows, a rectangle. */
    b = ct_texture(16, 16, 4, 0, GL_NEAREST, GL_REPEAT);
    for (k = 0; k < 3; k++) {
        float x = 20.0f + 200.0f * (float)k;
        ct_quad2d(x, 160, x + 64, 224, 0, 0, 1, 1);
        if (k == 1) {
            patch_fill(16 * 3, 16, 230, 230, 40);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 6, 16, 3, GL_RGBA, GL_UNSIGNED_BYTE, patch);
        } else {
            patch_fill(6 * 4, 6, 40, 230, 230 - 100 * k);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 2 + 4 * k, 3 + 4 * k, 6, 4, GL_RGBA, GL_UNSIGNED_BYTE, patch);
        }
        ct_quad2d(x, 60, x + 64, 124, 0, 0, 1, 1);
    }
    glDeleteTextures(1, &a);
    glDeleteTextures(1, &b);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t32_0");
}
