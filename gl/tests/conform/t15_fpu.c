/* t15: drawing with a game's FPU state: the x87 at 24-bit precision and
 * rounding toward zero, as Quake 2 leaves it for its software renderer.
 * DOS-GL switches to its own control word inside drawing calls (at 24-bit
 * precision libm's lrint returns 0, which once collapsed every triangle).
 * The Mesa reference draws the same scene with the host's FPU untouched. */
#include "ct_tex.h"

static void game_fpu(void)
{
#if defined(__DJGPP__) && defined(__i386__)
    unsigned short cw = 0x0C7F;            /* 24-bit, round toward zero, exceptions masked */
    __asm__ __volatile__("fldcw %0" : : "m"(cw));
#endif
}

void ct_run(void)
{
    int i;
    game_fpu();
    /* A clear colour that rounds differently when truncated (0.25 * 63 = 15.75). */
    glClearColor(0.2f, 0.25f, 0.3f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* 2D: Gouraud triangles at sub-pixel positions and a textured quad. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex2f(20.5f, 20.25f);
    glColor3f(0, 1, 0); glVertex2f(300.75f, 40.5f);
    glColor3f(0, 0, 1); glVertex2f(100.125f, 220.875f);
    glColor3f(1, 1, 0); glVertex2f(330.5f, 30.5f);
    glColor3f(0, 1, 1); glVertex2f(620.25f, 60.75f);
    glColor3f(1, 0, 1); glVertex2f(470.5f, 200.25f);
    glEnd();
    glColor3f(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    ct_texture(64, 64, 8, 0, GL_NEAREST, GL_REPEAT);
    ct_quad2d(24.5f, 250.5f, 216.5f, 442.5f, 0, 0, 1, 1);

    /* 3D: a depth-tested pair of textured quads in perspective, one rotated
     * through the other. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.375, 0.375, 1, 20);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    for (i = 0; i < 2; i++) {
        glLoadIdentity();
        glTranslatef(0.9f, 0.35f, -3.0f);
        glRotatef(i ? 50.0f : -20.0f, 0, 1, 0);
        glRotatef(15.0f, 1, 0, 0);
        glColor3f(i ? 0.6f : 1.0f, 1.0f, i ? 1.0f : 0.7f);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex3f(-0.7f, -0.5f, 0);
        glTexCoord2f(2, 0); glVertex3f(0.7f, -0.5f, 0);
        glTexCoord2f(2, 2); glVertex3f(0.7f, 0.5f, 0);
        glTexCoord2f(0, 2); glVertex3f(-0.7f, 0.5f, 0);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    /* A line across everything. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(1, 1, 1);
    glBegin(GL_LINES);
    glVertex2f(10.5f, 470.5f);
    glVertex2f(630.5f, 240.5f);
    glEnd();
    ct_frame("t15_0");
}
