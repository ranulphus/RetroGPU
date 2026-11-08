/* t08: alpha test on interpolated vertex alpha: each function cuts the
 * alpha ramp at 0.5. */
#include "ct.h"

void ct_run(void)
{
    static const GLenum funcs[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0.3f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_ALPHA_TEST);
    for (i = 0; i < 8; i++) {
        glAlphaFunc(funcs[i], 0.5f);
        glBegin(GL_QUADS);
        glColor4f(1, 1, 1, 0); glVertex2f(20, 440 - i * 55.0f);  glVertex2f(20, 400 - i * 55.0f);
        glColor4f(1, 1, 1, 1); glVertex2f(620, 400 - i * 55.0f); glVertex2f(620, 440 - i * 55.0f);
        glEnd();
    }
    glDisable(GL_ALPHA_TEST);
    ct_frame("t08_0");
}
