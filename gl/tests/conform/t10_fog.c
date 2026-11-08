/* t10: fog: LINEAR, EXP and EXP2 on floors receding into the distance. */
#include "ct.h"

static void floor_strip(float x0, float x1)
{
    glBegin(GL_QUADS);
    glColor3f(0.9f, 0.6f, 0.2f);
    glVertex3f(x0, -1, -1.5f); glVertex3f(x1, -1, -1.5f);
    glVertex3f(x1, -1, -20);   glVertex3f(x0, -1, -20);
    glEnd();
}

void ct_run(void)
{
    static const GLfloat fogc[4] = { 0.5f, 0.6f, 0.7f, 1 };
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -0.75, 0.75, 1, 30);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.5f, 0.6f, 0.7f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_FOG);
    glFogfv(GL_FOG_COLOR, fogc);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 2);
    glFogf(GL_FOG_END, 15);
    floor_strip(-3.0f, -1.1f);
    glFogi(GL_FOG_MODE, GL_EXP);
    glFogf(GL_FOG_DENSITY, 0.15f);
    floor_strip(-0.9f, 0.9f);
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogf(GL_FOG_DENSITY, 0.12f);
    floor_strip(1.1f, 3.0f);
    glDisable(GL_FOG);
    ct_frame("t10_0");
}
