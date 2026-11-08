/* t06: point-sampled textures: a 64x64 checker at 4x, a 4x4 texture (the
 * hardware minimum is 8), repeat and clamp, and modulation by vertex colour. */
#include "ct_tex.h"

void ct_run(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    ct_texture(64, 64, 8, 0, GL_NEAREST, GL_REPEAT);
    ct_quad2d(16, 208, 272, 464, 0, 0, 1, 1);
    ct_quad2d(288, 208, 416, 336, 0, 0, 2, 2);          /* repeat */
    ct_texture(4, 4, 1, 0, GL_NEAREST, GL_REPEAT);
    ct_quad2d(432, 336, 624, 464, 0, 0, 1, 1);
    ct_texture(16, 16, 4, 0, GL_NEAREST, GL_CLAMP);
    ct_quad2d(288, 16, 480, 192, -0.5f, -0.5f, 1.5f, 1.5f);   /* clamp */
    glColor3f(1.0f, 0.5f, 0.25f);                        /* modulate */
    ct_texture(32, 32, 4, 0, GL_NEAREST, GL_REPEAT);
    ct_quad2d(16, 16, 272, 192, 0, 0, 1, 1);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t06_0");
}
