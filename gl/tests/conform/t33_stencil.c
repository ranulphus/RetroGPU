/* t33: the stencil buffer (8 bits; the software renderer's, gl/docs/b2.md):
 * a shape written into the stencil only, then colour where it is and is
 * not; overlapping triangles counted with INCR and shown by EQUAL; a depth
 * test's failures marked with INVERT; and the masks: a write mask keeping
 * the upper bits and a compare mask on the lower. Matrox cards report no
 * stencil bits, so this runs on the software renderer only (manifest). */
#include "ct.h"

static void quad(float x0, float y0, float x1, float y1, float z)
{
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z); glVertex3f(x1, y0, z); glVertex3f(x1, y1, z); glVertex3f(x0, y1, z);
    glEnd();
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2)
{
    glBegin(GL_TRIANGLES);
    glVertex3f(x0, y0, 0); glVertex3f(x1, y1, 0); glVertex3f(x2, y2, 0);
    glEnd();
}

void ct_run(void)
{
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 640, 0, 480, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClearStencil(0);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);

    /* 1: a diamond into the stencil only (ref 1, REPLACE), then red where 1, blue where not. */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(110, 400); glVertex2f(190, 320); glVertex2f(110, 240); glVertex2f(30, 320);
    glEnd();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glColor3f(0.9f, 0.2f, 0.2f);
    quad(20, 230, 200, 410, 0);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glColor3f(0.2f, 0.3f, 0.9f);
    quad(20, 230, 200, 410, 0);

    /* 2: three overlapping triangles counted (INCR), each count its own colour. */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    tri(240, 250, 420, 250, 330, 420);
    tri(260, 400, 440, 400, 350, 230);
    tri(230, 330, 450, 300, 300, 440);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    for (i = 1; i <= 3; i++) {
        glStencilFunc(GL_EQUAL, i, 0xFF);
        glColor3f(i == 1 ? 0.9f : 0.2f, i == 2 ? 0.9f : 0.2f, i == 3 ? 0.9f : 0.2f);
        quad(220, 220, 460, 450, 0);
    }

    /* 3: depth failures marked: an occluder in front (glOrtho's z: +0.5 is
       nearer than -0.5), then a quad behind it whose failing pixels INVERT the
       stencil; shown green where it was hidden. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColor3f(0.5f, 0.5f, 0.5f);
    quad(500, 300, 600, 420, 0.5f);
    glStencilOp(GL_KEEP, GL_INVERT, GL_KEEP);
    glColor3f(0.8f, 0.6f, 0.2f);
    quad(470, 250, 630, 460, -0.5f);
    glDisable(GL_DEPTH_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 0xFF, 0xFF);
    glColor3f(0.2f, 0.9f, 0.3f);
    quad(470, 250, 630, 460, 0);

    /* 4: masks: REPLACE 0xA5 through a write mask of 0xF0 (0xA0 stored), then
       EQUAL 0x00 on the low bits passes everywhere it was written. */
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilMask(0xF0);
    glStencilFunc(GL_ALWAYS, 0xA5, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    tri(40, 40, 300, 60, 160, 200);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 0xA0, 0xF0);        /* (the upper bits written) */
    glColor3f(0.9f, 0.9f, 0.2f);
    quad(20, 20, 320, 220, 0);
    glStencilFunc(GL_EQUAL, 0x00, 0x0F);        /* (the lower bits kept at 0 everywhere) */
    glColor3f(0.6f, 0.2f, 0.7f);
    quad(340, 20, 620, 220, 0);
    glDisable(GL_STENCIL_TEST);
    ct_frame("t33_0");
}
