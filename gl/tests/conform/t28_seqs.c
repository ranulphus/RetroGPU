/* t28: fans, polygons and strips as the G400 sends them whole (the WARPs'
 * ACCEPT sequencer): flat and smooth, textured with coordinates over several
 * repeats, and a second pass over the first as GLQuake lights its walls
 * (the same polygons, depth LEQUAL, no depth writes, the lightmap
 * multiplied in). */
#include "ct_tex.h"
#include <math.h>

static void ring(float cx, float cy, float r, int n, float s0, float sscale, int flat_colours)
{
    int i;
    glBegin(GL_POLYGON);
    for (i = 0; i < n; i++) {
        float a = (float)i * 6.2831853f / (float)n;
        float x = cx + r * cosf(a), y = cy + r * sinf(a);
        if (!flat_colours)
            glColor3f(0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a), 1.0f - (float)i / (float)n);
        glTexCoord2f(s0 + (x - cx) * sscale, s0 * 0.5f + (y - cy) * sscale);
        glVertex3f(x, y, 0.25f * (float)i / (float)n);
    }
    glEnd();
}

void ct_run(void)
{
    GLuint base, light;
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    base = ct_texture(32, 32, 4, 0, GL_NEAREST, GL_REPEAT);
    light = ct_texture(16, 16, 8, 0, GL_LINEAR, GL_REPEAT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    /* Top left: a flat heptagon over several repeats (GLQuake's walls are
     * flat), then its lightmap pass. */
    glShadeModel(GL_FLAT);
    glColor3f(1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, base);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    ring(160, 360, 100, 7, -1.3f, 0.03f, 1);
    glBindTexture(GL_TEXTURE_2D, light);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    ring(160, 360, 100, 7, 0.1f, 0.004f, 1);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    /* Top right: a smooth octagon, modulated. */
    glShadeModel(GL_SMOOTH);
    glBindTexture(GL_TEXTURE_2D, base);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    ring(480, 360, 100, 8, 2.6f, 0.02f, 0);

    /* Bottom: a smooth strip over repeats, and a flat fan beside it. */
    glBegin(GL_TRIANGLE_STRIP);
    for (i = 0; i < 10; i++) {
        float x = 20.0f + (float)(i / 2) * 60.0f, y = (i & 1) ? 40.0f : 170.0f;
        glColor3f((float)i / 10.0f, 1.0f - (float)i / 10.0f, 0.6f);
        glTexCoord2f(-2.0f + x * 0.02f, 1.5f + y * 0.02f);
        glVertex3f(x, y, 0);
    }
    glEnd();
    glShadeModel(GL_FLAT);
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1, 0.8f, 0.2f);
    glTexCoord2f(0.5f, 0.5f); glVertex3f(480, 100, 0);
    for (i = 0; i <= 6; i++) {
        float a = (float)i * 0.5f;
        glColor3f(0.2f + 0.1f * (float)i, 0.5f, 1.0f - 0.1f * (float)i);
        glTexCoord2f(0.5f + 1.5f * cosf(a), 0.5f + 1.5f * sinf(a));
        glVertex3f(480 + 140 * cosf(a), 100 + 80 * sinf(a), 0);
    }
    glEnd();
    glShadeModel(GL_SMOOTH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t28_0");
}
