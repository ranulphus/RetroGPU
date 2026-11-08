/* t03: Gouraud shading: a triangle with primary colours and a fan. */
#include "ct.h"
#include <math.h>

void ct_run(void)
{
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex2f(40, 40);
    glColor3f(0, 1, 0); glVertex2f(300, 60);
    glColor3f(0, 0, 1); glVertex2f(150, 420);
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1, 1, 1); glVertex2f(470, 240);
    for (i = 0; i <= 12; i++) {
        float a = (float)(i * 2 * M_PI / 12);
        glColor3f(0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a), 0.5f);
        glVertex2f(470 + 140 * cosf(a), 240 + 140 * sinf(a));
    }
    glEnd();
    ct_frame("t03_0");
}
