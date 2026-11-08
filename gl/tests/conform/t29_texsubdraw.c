/* t29: a texture changed between draws within a frame, as GLQuake's
 * lightmap pages are (glTexSubImage2D into a 128x128 page between the
 * surfaces that use it): the draws before the change show the old texels,
 * those after it the new ones. Polygons and quads, flat and smooth. */
#include "ct_tex.h"

#ifndef T29_FRAME
#define T29_FRAME "t29_0"           /* (t31 draws the same through ILOAD) */
#endif

static unsigned char patch[32 * 16 * 4];

static void poly(float x, float y, float s0, float t0, int n, int flat)
{
    static const float u[6] = { 0, 1, 1.2f, 0.9f, 0.3f, -0.1f }, v[6] = { 0, 0, 0.5f, 1, 1.1f, 0.5f };
    int i;
    glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
    glBegin(GL_POLYGON);
    for (i = 0; i < n; i++) {
        glColor3f(1.0f - 0.1f * (float)i, 0.8f, 0.6f + 0.05f * (float)i);
        glTexCoord2f(s0 + u[i] * 0.4f, t0 + v[i] * 0.4f);
        glVertex3f(x + u[i] * 120.0f, y + v[i] * 90.0f, 0);
    }
    glEnd();
}

void ct_run(void)
{
    int i, k;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ct_texture(128, 128, 8, 0, GL_NEAREST, GL_REPEAT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    for (k = 0; k < 3; k++) {
        /* Draws that read the page as it is, then a change to it, three
         * times over: the patch differs each time. */
        poly(20.0f + 210.0f * (float)k, 360, 0.0f, 0.0f, 6, k & 1);
        poly(20.0f + 210.0f * (float)k, 250, 0.2f, 0.1f, 5, !(k & 1));
        for (i = 0; i < 32 * 16; i++) {
            patch[i * 4 + 0] = (unsigned char)(k == 0 ? 255 : 30);
            patch[i * 4 + 1] = (unsigned char)(k == 1 ? 255 : (i * 7) & 255);
            patch[i * 4 + 2] = (unsigned char)(k == 2 ? 255 : 60);
            patch[i * 4 + 3] = 255;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 32, 16 * (k + 1), 32, 16, GL_RGBA, GL_UNSIGNED_BYTE, patch);
        poly(20.0f + 210.0f * (float)k, 140, 0.0f, 0.0f, 6, k & 1);
        glBegin(GL_QUADS);
        glColor3f(1, 1, 1);
        glTexCoord2f(0, 0); glVertex3f(20.0f + 210.0f * (float)k, 20, 0);
        glTexCoord2f(0.6f, 0); glVertex3f(150.0f + 210.0f * (float)k, 20, 0);
        glTexCoord2f(0.6f, 0.6f); glVertex3f(150.0f + 210.0f * (float)k, 120, 0);
        glTexCoord2f(0, 0.6f); glVertex3f(20.0f + 210.0f * (float)k, 120, 0);
        glEnd();
    }
    glShadeModel(GL_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    ct_frame(T29_FRAME);
}
