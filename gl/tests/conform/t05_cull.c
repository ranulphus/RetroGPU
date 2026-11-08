/* t05: face culling: CCW and CW triangles under each cull mode and front face. */
#include "ct.h"

static void pair(float x, float y)
{
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0.5f, 0); glVertex2f(x, y); glVertex2f(x + 60, y); glVertex2f(x + 30, y + 80);   /* CCW */
    glColor3f(0, 0.5f, 1); glVertex2f(x + 70, y); glVertex2f(x + 100, y + 80); glVertex2f(x + 130, y);   /* CW */
    glEnd();
}

void ct_run(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pair(20, 380);                                /* culling off: both */
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);  pair(180, 380);         /* CCW only */
    glCullFace(GL_FRONT); pair(340, 380);         /* CW only */
    glCullFace(GL_FRONT_AND_BACK); pair(500, 380);   /* neither */
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);  pair(180, 240);         /* CW only */
    glCullFace(GL_FRONT); pair(340, 240);         /* CCW only */
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
    ct_frame("t05_0");
}
