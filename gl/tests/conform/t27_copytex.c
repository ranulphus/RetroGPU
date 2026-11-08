/* t27: glCopyTexImage2D and glCopyTexSubImage2D. A scene is drawn, then
 * copied into textures three ways: the whole 640x480 frame into part of a
 * 1024x512 texture (as PrBoom-plus's GL screen wipe does), a 128x64
 * rectangle into a texture of its own, and a 16x16 corner into the middle
 * of an existing checkerboard. The frame is cleared and the copies drawn
 * back 1:1 or magnified (nearest filtering, so every pixel is one texel):
 * the whole frame shifted by (24, 16), the rectangle and the patched
 * checkerboard over it. */
#include "ct_tex.h"
#include <stddef.h>

static void scene(void)
{
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);                       /* a Gouraud background */
    glColor3f(0.1f, 0.2f, 0.6f); glVertex2f(0, 0);
    glColor3f(0.6f, 0.2f, 0.1f); glVertex2f(CT_W, 0);
    glColor3f(0.9f, 0.8f, 0.2f); glVertex2f(CT_W, CT_H);
    glColor3f(0.2f, 0.7f, 0.3f); glVertex2f(0, CT_H);
    glEnd();
    glBegin(GL_TRIANGLES);
    glColor3f(1, 1, 1); glVertex2f(60, 60); glVertex2f(260, 90); glVertex2f(150, 260);
    glColor3f(1, 0, 1); glVertex2f(400, 300); glVertex2f(600, 420); glVertex2f(380, 460);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    ct_texture(64, 64, 8, 0, GL_NEAREST, GL_REPEAT);
    ct_quad2d(300, 100, 428, 228, 0, 0, 2, 2);
}

static GLuint nearest_texture(void)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    return t;
}

void ct_run(void)
{
    GLuint frame, rect, patched;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    scene();

    /* The frame into the corner of a 1024x512 texture defined without data. */
    frame = nearest_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1024, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, CT_W, CT_H);
    /* A rectangle across the white triangle's edge, as a texture of its own. */
    rect = nearest_texture();
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 100, 120, 128, 64, 0);
    /* The bottom-left 16x16 into the middle of a checkerboard. */
    patched = ct_texture(64, 64, 8, 0, GL_NEAREST, GL_CLAMP);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 24, 24, 0, 0, 16, 16);

    glClearColor(0.05f, 0.05f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, frame);
    ct_quad2d(24, 16, 24 + CT_W, 16 + CT_H, 0, 0, (float)CT_W / 1024, (float)CT_H / 512);
    glBindTexture(GL_TEXTURE_2D, rect);
    ct_quad2d(480, 380, 608, 444, 0, 0, 1, 1);
    glBindTexture(GL_TEXTURE_2D, patched);
    ct_quad2d(440, 40, 632, 232, 0, 0, 1, 1);     /* x3 */
    ct_frame("t27_0");
}
