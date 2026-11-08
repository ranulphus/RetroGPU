/* t02: flat-shaded triangles, including slivers and a shared edge. */
#include "ct.h"

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b)
{
    glColor3f(1, 1, 1);                    /* the provoking (last) vertex decides */
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glColor3f(r, g, b);
    glVertex2f(x2, y2);
}

void ct_run(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glShadeModel(GL_FLAT);
    glBegin(GL_TRIANGLES);
    tri(40, 40, 300, 60, 120, 400, 1, 0, 0);
    tri(300, 60, 600, 120, 120, 400, 0, 1, 0);          /* shares an edge with the first */
    tri(320, 300, 620, 302, 320, 304, 0, 0, 1);          /* sliver */
    tri(500.25f, 200.75f, 610.5f, 440.125f, 480.5f, 430.5f, 1, 1, 0);   /* sub-pixel vertices */
    glEnd();
    ct_frame("t02_0");
}
