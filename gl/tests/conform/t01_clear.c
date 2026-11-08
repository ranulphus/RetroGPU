/* t01: clear to a solid colour; a scissored clear; colour mask on a clear. */
#include "ct.h"

void ct_run(void)
{
    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ct_frame("t01_0");
    glEnable(GL_SCISSOR_TEST);
    glScissor(100, 50, 200, 150);
    glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    ct_frame("t01_1");
}
