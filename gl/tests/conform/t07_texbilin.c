/* t07: bilinear filtering and perspective-correct texturing on a floor. */
#include "ct_tex.h"

void ct_run(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -0.75, 0.75, 1, 50);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    ct_texture(64, 64, 8, 0, GL_LINEAR, GL_REPEAT);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-3, -1, -1.2f);
    glTexCoord2f(4, 0); glVertex3f(3, -1, -1.2f);
    glTexCoord2f(4, 8); glVertex3f(3, -1, -12);
    glTexCoord2f(0, 8); glVertex3f(-3, -1, -12);
    glEnd();
    /* A magnified 8x8 texture shows the filter itself. (GL_REPEAT: with
     * GL_CLAMP, GL 1.1 blends edge texels with the border colour, which the
     * chip does not; DOS-GL treats GL_CLAMP as clamp-to-edge.) */
    ct_texture(8, 8, 1, 0, GL_LINEAR, GL_REPEAT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    ct_quad2d(400, 280, 624, 464, 0, 0, 1, 1);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t07_0");
}
