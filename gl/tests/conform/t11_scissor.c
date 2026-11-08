/* t11: scissor rectangle clipping triangles and a clear. */
#include "ct.h"

void ct_run(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(100, 80, 300, 200);
    glClearColor(0.2f, 0.2f, 0.5f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex2f(20, 20);
    glColor3f(0, 1, 0); glVertex2f(620, 60);
    glColor3f(0, 0, 1); glVertex2f(250, 460);
    glEnd();
    glScissor(420, 300, 150, 120);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 1, 0); glVertex2f(300, 250); glVertex2f(640, 250); glVertex2f(500, 480);
    glEnd();
    glDisable(GL_SCISSOR_TEST);
    ct_frame("t11_0");
}
