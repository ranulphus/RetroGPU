/* t14: texture coordinate range (the per-triangle prescale and the wrap
 * shift): the same repeating texture drawn with coordinates offset by 0,
 * 7, 100, 1000 and 30000 periods, and at 1x to 64x repetition, must look
 * the same as at small coordinates. */
#include "ct_tex.h"

void ct_run(void)
{
    static const float offs[5] = { 0, 7, 100, 1000, 30000 };
    static const float reps[4] = { 1, 4, 16, 64 };
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    ct_texture(16, 16, 4, 0, GL_NEAREST, GL_REPEAT);
    for (i = 0; i < 5; i++)
        ct_quad2d(10 + i * 126.0f, 250, 126 + i * 126.0f, 460, offs[i], offs[i], offs[i] + 1, offs[i] + 1);
    for (i = 0; i < 4; i++)
        ct_quad2d(10 + i * 157.0f, 20, 157 + i * 157.0f, 230, -reps[i] / 2, 0, reps[i] / 2, reps[i]);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t14_0");
}
