/* t41: OpenGL 1.1's user clip planes and texture-coordinate generation (docs/dx7.md X2). Frame t41_0 under an
 * orthographic projection: a checkered sphere with object-linear s and t, the same turned with eye-linear ones (the
 * planes given under the identity, so they stay with the eye), a sphere-mapped sphere; a sphere cut by one plane
 * (given under a rotated modelview), a quad cut by two, lines and points across a plane. Frame t41_1 under a
 * perspective projection: a floor cut by a plane through its middle (the plane into clip coordinates through the
 * projection's inverse) with eye-linear texture coordinates. Then the planes and modes read back. */
#include "ct_tex.h"
#include <math.h>

#define SL 20
#define ST 14
static float pos[(ST + 1) * (SL + 1)][3];
static unsigned short idx[ST * SL * 6];

static void make_sphere(void)
{
    int i, j, n = 0;
    for (i = 0; i <= ST; i++)
        for (j = 0; j <= SL; j++) {
            double th = 3.14159265358979 * i / ST, ph = 2 * 3.14159265358979 * j / SL;
            float *p = pos[i * (SL + 1) + j];
            p[0] = (float)(sin(th) * cos(ph)), p[1] = (float)cos(th), p[2] = (float)(sin(th) * sin(ph));
        }
    for (i = 0; i < ST; i++)
        for (j = 0; j < SL; j++) {
            unsigned short a = (unsigned short)(i * (SL + 1) + j), b = (unsigned short)(a + SL + 1);
            idx[n++] = a, idx[n++] = b, idx[n++] = (unsigned short)(a + 1);
            idx[n++] = (unsigned short)(a + 1), idx[n++] = b, idx[n++] = (unsigned short)(b + 1);
        }
}

static void sphere(float x, float y, float r)
{
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(r, r, r);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glNormalPointer(GL_FLOAT, 0, pos);
    glDrawElements(GL_TRIANGLES, ST * SL * 6, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
}

static void gen(GLenum mode, const GLfloat *sp, const GLfloat *tp)
{
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, (GLint)mode);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, (GLint)mode);
    if (sp) {
        glTexGenfv(GL_S, mode == GL_OBJECT_LINEAR ? GL_OBJECT_PLANE : GL_EYE_PLANE, sp);
        glTexGenfv(GL_T, mode == GL_OBJECT_LINEAR ? GL_OBJECT_PLANE : GL_EYE_PLANE, tp);
    }
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
}

void ct_run(void)
{
    static const GLfloat sp[4] = { 1.5f, 0, 0, 0.5f }, tp[4] = { 0, 1.5f, 0, 0.5f };
    static const GLfloat esp[4] = { 1.0f / 40, 0, 0, 0 }, etp[4] = { 0, 1.0f / 40, 0, 0 };
    GLdouble eq[4], got[4];
    GLint mode = 0;
    int i, ok;
    make_sphere();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -200, 200);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glColor3f(1, 1, 1);
    ct_texture(64, 64, 8, 0, GL_LINEAR, GL_REPEAT);
    glEnable(GL_TEXTURE_2D);

    /* Row 1: object-linear; eye-linear (planes under the identity, the sphere turned); sphere map. */
    gen(GL_OBJECT_LINEAR, sp, tp);
    glPushMatrix();
    glTranslatef(100, 380, 0);
    glRotatef(35, 0, 1, 0);
    sphere(0, 0, 70);
    glPopMatrix();
    glLoadIdentity();
    gen(GL_EYE_LINEAR, esp, etp);
    glPushMatrix();
    glTranslatef(300, 380, 0);
    glRotatef(35, 0, 1, 0);
    sphere(0, 0, 70);
    glPopMatrix();
    gen(GL_SPHERE_MAP, NULL, NULL);
    glEnable(GL_NORMALIZE);
    sphere(500, 380, 70);
    glDisable(GL_NORMALIZE);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_2D);

    /* Row 2: a sphere cut by a plane given under a rotation (x > 100 before it: a tilted cut through the middle). */
    glColor3f(0.9f, 0.5f, 0.2f);
    glPushMatrix();
    glTranslatef(100, 200, 0);
    glRotatef(30, 0, 0, 1);
    eq[0] = 1, eq[1] = 0, eq[2] = 0, eq[3] = 10;
    glClipPlane(GL_CLIP_PLANE0, eq);
    glPopMatrix();
    glEnable(GL_CLIP_PLANE0);
    sphere(100, 200, 70);
    glDisable(GL_CLIP_PLANE0);

    /* A quad cut by two planes (a wedge out of its corner), flat-shaded so the provoking colour shows on the pieces. */
    eq[0] = -1, eq[1] = 0, eq[2] = 0, eq[3] = 340;              /* x < 340 */
    glClipPlane(GL_CLIP_PLANE1, eq);
    eq[0] = 0.5, eq[1] = -1, eq[2] = 0, eq[3] = 120;            /* y < 120 + x / 2 */
    glClipPlane(GL_CLIP_PLANE4, eq);
    glEnable(GL_CLIP_PLANE1);
    glEnable(GL_CLIP_PLANE4);
    glShadeModel(GL_FLAT);
    glBegin(GL_QUADS);
    glColor3f(0.2f, 0.8f, 0.3f); glVertex2f(220, 140);
    glColor3f(0.2f, 0.3f, 0.8f); glVertex2f(380, 140);
    glColor3f(0.8f, 0.8f, 0.2f); glVertex2f(380, 270);
    glColor3f(0.8f, 0.2f, 0.6f); glVertex2f(220, 270);
    glEnd();
    glShadeModel(GL_SMOOTH);
    /* Lines and points across plane 1 (x < 340). */
    glDisable(GL_CLIP_PLANE4);
    glBegin(GL_LINES);
    for (i = 0; i < 8; i++) {
        glColor3f(1, 1, 1); glVertex2f(300, 40.0f + i * 10);
        glColor3f(1, 0.2f, 0.2f); glVertex2f(420.0f - i * 10, 50.0f + i * 10);
    }
    glEnd();
    glPointSize(4);
    glBegin(GL_POINTS);
    for (i = 0; i < 10; i++) {
        glColor3f(0.3f, 1, 1);
        glVertex2f(300.0f + i * 10, 20);
    }
    glEnd();
    glPointSize(1);
    glDisable(GL_CLIP_PLANE1);
    glDisable(GL_DEPTH_TEST);
    ct_frame("t41_0");

    /* t41_1: perspective; a floor with eye-linear coordinates, cut by a plane through its middle at an angle. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -0.75, 0.75, 1, 50);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    {
        static const GLfloat fs[4] = { 0.5f, 0, 0, 0 }, ft[4] = { 0, 0, 0.5f, 0 };
        gen(GL_EYE_LINEAR, fs, ft);
    }
    eq[0] = 1, eq[1] = 0, eq[2] = 0.15, eq[3] = 0;              /* x + 0.15 z > 0 */
    glClipPlane(GL_CLIP_PLANE2, eq);
    glEnable(GL_CLIP_PLANE2);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glVertex3f(-4, -1, -1.2f);
    glVertex3f(4, -1, -1.2f);
    glVertex3f(4, -1, -20);
    glVertex3f(-4, -1, -20);
    glEnd();
    glDisable(GL_CLIP_PLANE2);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t41_1");

    glGetClipPlane(GL_CLIP_PLANE2, got);
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mode);
    ok = got[0] == 1 && got[1] == 0 && fabs(got[2] - 0.15) < 1e-6 && got[3] == 0 && mode == GL_EYE_LINEAR &&
         !glIsEnabled(GL_CLIP_PLANE2) && !glIsEnabled(GL_TEXTURE_GEN_S);
    ct_check("clipgen-state", ok, "plane 2 read back (%g, %g, %g, %g), s's mode %04x", got[0], got[1], got[2], got[3],
             mode);
}
