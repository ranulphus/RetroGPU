/* t30: two textures on fans, polygons and strips, as GLQuake (-mtex) and
 * Quake 2 draw their walls: a surface texture on unit 0 and a lightmap on
 * unit 1 multiplied in, each unit's coordinates over several repeats (and
 * below zero), the lightmap clamped on one of them. Flat and smooth, with
 * depth. The G400 sends such a primitive whole through the WARPs' ACCEPT
 * sequencer (10-dword vertices); with one unit (the G200) it is drawn in
 * two passes. */
#include "ct_mtex.h"
#include <math.h>

static int two;

static void tc(float s0, float t0, float s1, float t1)
{
    if (two) {
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, s0, t0);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, s1, t1);
    } else
        glTexCoord2f(s0, t0);
}

/* pass 2 (one unit): unit 1's coordinates on the only unit */
static void tc_pass(int pass, float s0, float t0, float s1, float t1)
{
    if (pass == 2)
        glTexCoord2f(s1, t1);
    else
        tc(s0, t0, s1, t1);
}

static void ring(int pass, float cx, float cy, float r, int n, float o0, float k0, float o1, float k1, int smooth)
{
    int i;
    glBegin(GL_POLYGON);
    for (i = 0; i < n; i++) {
        float a = (float)i * 6.2831853f / (float)n;
        float x = cx + r * cosf(a), y = cy + r * sinf(a);
        if (smooth && pass != 2)
            glColor3f(0.5f + 0.5f * cosf(a), 0.6f + 0.4f * sinf(a), 1.0f - 0.5f * (float)i / (float)n);
        tc_pass(pass, o0 + (x - cx) * k0, o0 * 0.5f + (y - cy) * k0, o1 + (x - cx) * k1, o1 - (y - cy) * k1);
        glVertex3f(x, y, 0.25f * (float)i / (float)n);
    }
    glEnd();
}

static void strip(int pass)
{
    int i;
    glBegin(GL_TRIANGLE_STRIP);
    for (i = 0; i < 10; i++) {
        float x = 20.0f + (float)(i / 2) * 60.0f, y = (i & 1) ? 40.0f : 170.0f;
        if (pass != 2)
            glColor3f((float)i / 10.0f, 1.0f - (float)i / 10.0f, 0.7f);
        tc_pass(pass, -2.0f + x * 0.02f, 1.5f + y * 0.02f, -0.7f + x * 0.006f, -1.2f + y * 0.009f);
        glVertex3f(x, y, 0);
    }
    glEnd();
}

static void fan(int pass)
{
    int i;
    if (pass != 2)
        glColor3f(1, 0.8f, 0.4f);
    tc_pass(pass, 0.5f, 0.5f, 0.5f, 0.5f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(480, 100, 0);
    for (i = 0; i <= 6; i++) {
        float a = (float)i * 0.5f;
        if (pass != 2)
            glColor3f(0.3f + 0.1f * (float)i, 0.6f, 1.0f - 0.1f * (float)i);
        tc_pass(pass, 0.5f + 1.5f * cosf(a), 0.5f + 1.5f * sinf(a), 0.5f + 0.7f * cosf(a), 0.5f + 0.7f * sinf(a));
        glVertex3f(480 + 140 * cosf(a), 100 + 80 * sinf(a), 0);
    }
    glEnd();
}

static void units(GLuint t0, GLuint t1, GLenum env0, void (*draw)(int pass))
{
    if (two) {
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)env0);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        draw(0);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);
    } else {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)env0);
        draw(1);
        glBindTexture(GL_TEXTURE_2D, t1);
        ct_second_pass(GL_MODULATE);
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);
        draw(2);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_BLEND);
    }
}

static void top_left(int pass) { ring(pass, 160, 360, 100, 7, -1.3f, 0.03f, 0.2f, 0.008f, 0); }
static void top_right(int pass) { ring(pass, 480, 360, 100, 8, 2.6f, 0.02f, -1.6f, 0.011f, 1); }

void ct_run(void)
{
    GLuint base, light, lclamp;
    two = ct_units() >= 2;
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
    lclamp = ct_texture(16, 16, 2, 0, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColor3f(1, 1, 1);

    /* Top left: a flat heptagon, unit 0 replacing (GLQuake's walls). */
    glShadeModel(GL_FLAT);
    units(base, light, GL_REPLACE, top_left);
    /* Top right: a smooth octagon, both units modulating. */
    glShadeModel(GL_SMOOTH);
    units(base, light, GL_MODULATE, top_right);
    /* Bottom: a smooth strip, and a flat fan with the lightmap clamped. */
    units(base, light, GL_MODULATE, strip);
    glShadeModel(GL_FLAT);
    units(base, lclamp, GL_MODULATE, fan);
    glShadeModel(GL_SMOOTH);
    glDisable(GL_DEPTH_TEST);
    ct_frame("t30_0");
}
