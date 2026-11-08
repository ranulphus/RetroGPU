/* t13: integration: a textured, mipmapped, depth-tested cube (min filter
 * GL_NEAREST_MIPMAP_LINEAR as ClassiCube uses), an alpha-tested cutout, a
 * translucent quad and fog. */
#include "ct_tex.h"

static void mipmapped(void)
{
    static unsigned char buf[64 * 64 * 4];
    GLuint t;
    int l, s;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    for (l = 0, s = 64; s >= 1; l++, s /= 2) {
        ct_checker(buf, s, s, s >= 8 ? s / 8 : 1, 0);
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void face(float s)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-s, -s, s);
    glTexCoord2f(1, 0); glVertex3f(s, -s, s);
    glTexCoord2f(1, 1); glVertex3f(s, s, s);
    glTexCoord2f(0, 1); glVertex3f(-s, s, s);
    glEnd();
}

void ct_run(void)
{
    static const GLfloat fogc[4] = { 0.3f, 0.3f, 0.35f, 1 };
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.375, 0.375, 1, 40);
    glMatrixMode(GL_MODELVIEW);
    glClearColor(0.3f, 0.3f, 0.35f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_FOG);
    glFogfv(GL_FOG_COLOR, fogc);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 4);
    glFogf(GL_FOG_END, 16);
    glColor3f(1, 1, 1);
    mipmapped();
    for (i = 0; i < 3; i++) {
        glLoadIdentity();
        glTranslatef(-2.2f + i * 2.2f, 0, -4.0f - i * 4.0f);
        glRotatef(35, 1, 0, 0);
        glRotatef(40, 0, 1, 0);
        face(0.8f);
        glRotatef(90, 0, 1, 0); face(0.8f);
        glRotatef(90, 1, 0, 0); face(0.8f);
        glRotatef(90, 0, 1, 0); face(0.8f);
    }
    glDisable(GL_CULL_FACE);
    /* Alpha-tested cutout (binary alpha) and a translucent quad (gradient alpha). */
    glLoadIdentity();
    glTranslatef(0, 0, -3);
    ct_texture(32, 32, 4, 1, GL_NEAREST, GL_REPEAT);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-1.4f, -1.0f, 0); glTexCoord2f(2, 0); glVertex3f(-0.4f, -1.0f, 0);
    glTexCoord2f(2, 2); glVertex3f(-0.4f, 0.0f, 0);  glTexCoord2f(0, 2); glVertex3f(-1.4f, 0.0f, 0);
    glEnd();
    glDisable(GL_ALPHA_TEST);
    ct_texture(32, 32, 8, 2, GL_NEAREST, GL_REPEAT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(0.2f, -1.0f, 0.5f); glTexCoord2f(1, 0); glVertex3f(1.4f, -1.0f, 0.5f);
    glTexCoord2f(1, 1); glVertex3f(1.4f, 0.2f, 0.5f);  glTexCoord2f(0, 1); glVertex3f(0.2f, 0.2f, 0.5f);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t13_0");
}
