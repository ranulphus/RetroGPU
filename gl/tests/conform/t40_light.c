/* t40: OpenGL 1.1 lighting (docs/dx7.md X2): spheres lit by a directional light, a point light with attenuation, a
 * spotlight; specular with an infinite and a local viewer; colour material (ambient and diffuse from glColor, per
 * vertex); two-sided lighting on an open bowl seen from inside; a sphere scaled by the modelview with GL_NORMALIZE;
 * and the same sphere from normal arrays of bytes, shorts and floats. Frame t40_0; then t40_1, the light's state
 * read back. Everything OpenGL 1.1 (GL_RESCALE_NORMAL_EXT aside, where advertised). */
#include "ct_tex.h"
#include <math.h>
#include <string.h>

#define SL 16                           /* slices and stacks */
#define ST 12
static float pos[(ST + 1) * (SL + 1)][3];
static signed char nb[(ST + 1) * (SL + 1)][3];
static short ns[(ST + 1) * (SL + 1)][3];
static unsigned char cols[(ST + 1) * (SL + 1)][4];
static unsigned short idx[ST * SL * 6];

static void make_sphere(void)
{
    int i, j, k, n = 0;
    for (i = 0; i <= ST; i++)
        for (j = 0; j <= SL; j++) {
            double th = 3.14159265358979 * i / ST, ph = 2 * 3.14159265358979 * j / SL;
            float *p = pos[i * (SL + 1) + j];
            p[0] = (float)(sin(th) * cos(ph)), p[1] = (float)cos(th), p[2] = (float)(sin(th) * sin(ph));
            for (k = 0; k < 3; k++) {
                nb[i * (SL + 1) + j][k] = (signed char)floor(p[k] * 127.0 + 0.5 - 0.5 / 127.0 * 0);
                ns[i * (SL + 1) + j][k] = (short)floor(p[k] * 32767.0 + 0.5);
            }
            cols[i * (SL + 1) + j][0] = (unsigned char)(128 + 127 * p[0]);
            cols[i * (SL + 1) + j][1] = (unsigned char)(128 + 127 * p[1]);
            cols[i * (SL + 1) + j][2] = 200;
            cols[i * (SL + 1) + j][3] = 255;
        }
    for (i = 0; i < ST; i++)
        for (j = 0; j < SL; j++) {
            unsigned short a = (unsigned short)(i * (SL + 1) + j), b = (unsigned short)(a + SL + 1);
            idx[n++] = a, idx[n++] = b, idx[n++] = (unsigned short)(a + 1);
            idx[n++] = (unsigned short)(a + 1), idx[n++] = b, idx[n++] = (unsigned short)(b + 1);
        }
}

/* The sphere in immediate mode (its normals its positions), at (x, y) on a 640x480 screen-sized frustum's plane. */
static void sphere_imm(float x, float y, float r, int with_colour)
{
    int i, k;
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(r, r, r);
    glBegin(GL_TRIANGLES);
    for (i = 0; i < ST * SL * 6; i++) {
        const float *p = pos[idx[i]];
        if (with_colour)
            glColor4ubv(cols[idx[i]]);
        glNormal3fv(p);
        glVertex3fv(p);
    }
    glEnd();
    glPopMatrix();
    (void)k;
}

static void sphere_arrays(float x, float y, float r, GLenum ntype)
{
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(r, r, r);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glNormalPointer(ntype, 0, ntype == GL_BYTE ? (const void *)nb : ntype == GL_SHORT ? (const void *)ns : (const void *)pos);
    glDrawElements(GL_TRIANGLES, ST * SL * 6, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
}

static void defaults(void)
{
    static const GLfloat white[4] = { 1, 1, 1, 1 }, black[4] = { 0, 0, 0, 1 }, amb[4] = { 0.2f, 0.2f, 0.2f, 1 };
    int i;
    for (i = 1; i < 8; i++)
        glDisable(GL_LIGHT0 + i);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glLightfv(GL_LIGHT0, GL_AMBIENT, black);
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 180);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, 0);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 0);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0);
    glDisable(GL_COLOR_MATERIAL);
}

static void material(float r, float g, float b, float spec, float shin)
{
    GLfloat d[4] = { r, g, b, 1 }, a[4] = { r * 0.5f, g * 0.5f, b * 0.5f, 1 }, s[4] = { spec, spec, spec, 1 };
    glMaterialfv(GL_FRONT, GL_DIFFUSE, d);
    glMaterialfv(GL_FRONT, GL_AMBIENT, a);
    glMaterialfv(GL_FRONT, GL_SPECULAR, s);
    glMaterialf(GL_FRONT, GL_SHININESS, shin);
}

void ct_run(void)
{
    static const GLfloat dir[4] = { -1, 1, 1, 0 };
    GLfloat got[4] = { 0 }, l_pos[4];
    int ok;
    make_sphere();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -200, 200);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    defaults();
    glEnable(GL_NORMALIZE);             /* (the spheres are scaled: rows 1 and 2 normalise; row 3 chooses) */

    /* Row 1: directional; point with attenuation; spot; specular (infinite viewer); specular (local viewer). */
    glLightfv(GL_LIGHT0, GL_POSITION, dir);
    material(0.9f, 0.3f, 0.2f, 0, 0);
    sphere_imm(70, 400, 50, 0);

    l_pos[0] = 150, l_pos[1] = 460, l_pos[2] = 120, l_pos[3] = 1;
    glLightfv(GL_LIGHT0, GL_POSITION, l_pos);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.004f);
    material(0.3f, 0.9f, 0.3f, 0, 0);
    sphere_imm(190, 400, 50, 0);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0);

    {
        GLfloat sp[4] = { 310, 400, 150, 1 }, sd[3] = { 0.15f, 0.1f, -1 };
        glLightfv(GL_LIGHT0, GL_POSITION, sp);
        glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, sd);
        glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 25);
        glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 8);
        material(0.4f, 0.5f, 1.0f, 0, 0);
        sphere_imm(310, 400, 50, 0);
        glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 180);
        glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 0);
    }

    glLightfv(GL_LIGHT0, GL_POSITION, dir);
    material(0.6f, 0.6f, 0.2f, 0.9f, 20);
    sphere_imm(430, 400, 50, 0);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, 1);
    sphere_imm(550, 400, 50, 0);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, 0);

    /* Row 2: colour material (ambient and diffuse per vertex); two lights at once; a sphere squashed by the
     * modelview with GL_NORMALIZE; two-sided: a bowl (the sphere's lower half) seen from above, its inside back
     * faces lit by the back material. */
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
    sphere_imm(70, 260, 50, 1);
    glDisable(GL_COLOR_MATERIAL);

    {
        GLfloat red[4] = { 1, 0.2f, 0.2f, 1 }, blue[4] = { 0.2f, 0.3f, 1, 1 }, p1[4] = { 1, -0.5f, 0.6f, 0 };
        glEnable(GL_LIGHT1);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, red);
        glLightfv(GL_LIGHT1, GL_DIFFUSE, blue);
        glLightfv(GL_LIGHT1, GL_POSITION, p1);
        material(0.8f, 0.8f, 0.8f, 0, 0);
        sphere_imm(190, 260, 50, 0);
        glDisable(GL_LIGHT1);
        defaults();
        glLightfv(GL_LIGHT0, GL_POSITION, dir);
    }

    glPushMatrix();
    glTranslatef(310, 260, 0);
    glScalef(1.6f, 0.7f, 1.0f);
    material(0.8f, 0.5f, 0.9f, 0.5f, 10);
    sphere_imm(0, 0, 40, 0);
    glPopMatrix();

    {
        GLfloat back_d[4] = { 0.2f, 0.9f, 0.9f, 1 }, back_a[4] = { 0.1f, 0.4f, 0.4f, 1 };
        int i;
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
        glMaterialfv(GL_BACK, GL_DIFFUSE, back_d);
        glMaterialfv(GL_BACK, GL_AMBIENT, back_a);
        material(0.9f, 0.6f, 0.2f, 0, 0);
        glPushMatrix();
        glTranslatef(470, 260, 0);
        glRotatef(-60, 1, 0, 0);
        glScalef(55, 55, 55);
        glBegin(GL_TRIANGLES);
        for (i = ST / 2 * SL * 6; i < ST * SL * 6; i++) {
            glNormal3fv(pos[idx[i]]);
            glVertex3fv(pos[idx[i]]);
        }
        glEnd();
        glPopMatrix();
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 0);
    }

    /* Row 3: normals from arrays: bytes, shorts (normalised), floats not normalised (the scale darkens them, as GL
     * says), and GL_RESCALE_NORMAL_EXT on the same. */
    material(0.7f, 0.7f, 0.7f, 0.6f, 30);
    sphere_arrays(70, 110, 50, GL_BYTE);
    sphere_arrays(190, 110, 50, GL_SHORT);
    glDisable(GL_NORMALIZE);
    sphere_arrays(310, 110, 50, GL_FLOAT);
    if (ct_has("GL_EXT_rescale_normal")) {
        glEnable(GL_RESCALE_NORMAL_EXT);
        sphere_arrays(430, 110, 50, GL_FLOAT);
        glDisable(GL_RESCALE_NORMAL_EXT);
    } else {
        glEnable(GL_NORMALIZE);
        sphere_arrays(430, 110, 50, GL_FLOAT);
        glDisable(GL_NORMALIZE);
    }
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    ct_frame("t40_0");

    /* The state read back: a position goes in through the modelview. */
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(10, 0, 0);
    l_pos[0] = 1, l_pos[1] = 2, l_pos[2] = 3, l_pos[3] = 1;
    glLightfv(GL_LIGHT2, GL_POSITION, l_pos);
    glPopMatrix();
    glGetLightfv(GL_LIGHT2, GL_POSITION, got);
    ok = got[0] == 11 && got[1] == 2 && got[2] == 3 && got[3] == 1 && glIsEnabled(GL_LIGHT0) && !glIsEnabled(GL_LIGHTING);
    ct_check("light-state", ok, "GL_LIGHT2's position read back (%g, %g, %g, %g)", got[0], got[1], got[2], got[3]);
}
