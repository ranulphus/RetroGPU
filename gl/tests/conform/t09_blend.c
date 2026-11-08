/* t09: blending: common factor pairs over a colour ramp background. */
#include "ct.h"

static void bg(void)
{
    glBegin(GL_QUADS);
    glColor3f(0, 0, 0); glVertex2f(0, 0);
    glColor3f(1, 0, 0); glVertex2f(CT_W, 0);
    glColor3f(0, 0, 1); glVertex2f(CT_W, CT_H);
    glColor3f(0, 1, 0); glVertex2f(0, CT_H);
    glEnd();
}

void ct_run(void)
{
    static const GLenum pairs[6][2] = {
        { GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA }, { GL_ONE, GL_ONE }, { GL_DST_COLOR, GL_ZERO },
        { GL_ONE_MINUS_DST_COLOR, GL_ZERO }, { GL_SRC_ALPHA, GL_ONE }, { GL_ZERO, GL_SRC_COLOR },
    };
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    bg();
    glEnable(GL_BLEND);
    for (i = 0; i < 6; i++) {
        float x = 20 + (i % 3) * 205.0f, y = 260 - (i / 3) * 220.0f;
        glBlendFunc(pairs[i][0], pairs[i][1]);
        glBegin(GL_QUADS);
        glColor4f(1, 1, 0.2f, 0.1f); glVertex2f(x, y);
        glColor4f(1, 1, 0.2f, 0.9f); glVertex2f(x + 190, y);
        glColor4f(0.2f, 1, 1, 0.9f); glVertex2f(x + 190, y + 200);
        glColor4f(0.2f, 1, 1, 0.1f); glVertex2f(x, y + 200);
        glEnd();
    }
    glDisable(GL_BLEND);
    ct_frame("t09_0");
}
