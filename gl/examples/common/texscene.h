/* texscene.h - texcube's scene (M4 rung 4: textured, mipmapped, fogged
 * cubes spinning, an alpha-tested cutout and a translucent quad), shared by
 * TEXCUBE (DOS-GL directly) and SDLGL (DOS-GL through SDL), whose frames must
 * be the same. Include once, after <GL/gl.h>. */
#ifndef TEXSCENE_H
#define TEXSCENE_H

static void checker(unsigned char *p, int s, int c, int alpha)
{
    int x, y;
    for (y = 0; y < s; y++)
        for (x = 0; x < s; x++) {
            int on = ((x / c) + (y / c)) & 1;
            unsigned char *t = p + (y * s + x) * 4;
            t[0] = (unsigned char)(on ? 255 : 40 + x * 200 / s);
            t[1] = (unsigned char)(on ? 220 : 40 + y * 200 / s);
            t[2] = (unsigned char)(on ? 40 : 160);
            t[3] = (unsigned char)(alpha == 0 ? 255 : alpha == 1 ? (on ? 255 : 0) : x * 255 / (s - 1));
        }
}

static GLuint texture(int size, int cell, int alpha, int mip)
{
    static unsigned char buf[64 * 64 * 4];
    GLuint t;
    int l, s;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    for (l = 0, s = size; s >= 1 && (l == 0 || mip); l++, s /= 2) {
        checker(buf, s, s >= cell * 2 ? cell >> l ? cell >> l : 1 : 1, alpha);
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

static void face(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-1, -1, 1); glTexCoord2f(1, 0); glVertex3f(1, -1, 1);
    glTexCoord2f(1, 1); glVertex3f(1, 1, 1);   glTexCoord2f(0, 1); glVertex3f(-1, 1, 1);
    glEnd();
}

static void cube(void)
{
    int i;
    for (i = 0; i < 4; i++) { face(); glRotatef(90, 0, 1, 0); }
    glRotatef(90, 1, 0, 0); face();
    glRotatef(180, 1, 0, 0); face();
}

static GLuint scene_wall, scene_cut, scene_glass;

static void texscene_setup(void)
{
    static const GLfloat fogc[4] = { 0.3f, 0.3f, 0.35f, 1 };
    GLuint wall, cut, glass;
    wall = texture(64, 8, 0, 1);
    cut = texture(32, 4, 1, 0);
    glass = texture(32, 8, 2, 0);
    glMatrixMode(GL_PROJECTION);
    glFrustum(-0.5, 0.5, -0.375, 0.375, 1, 40);
    glMatrixMode(GL_MODELVIEW);
    glClearColor(0.3f, 0.3f, 0.35f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_FOG);
    glFogfv(GL_FOG_COLOR, fogc);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 4);
    glFogf(GL_FOG_END, 18);
    glAlphaFunc(GL_GREATER, 0.5f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    scene_wall = wall;
    scene_cut = cut;
    scene_glass = glass;
}

/* Frame f of the scene (the caller swaps). */
static void texscene_draw(int f)
{
    GLuint wall = scene_wall, cut = scene_cut, glass = scene_glass;
    int i;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, wall);
    for (i = 0; i < 3; i++) {
        glLoadIdentity();
        glTranslatef(-2.4f + i * 2.4f, 0, -4.5f - i * 4.0f);
        glRotatef(f * 2.0f + i * 30, 1, 1, 0);
        glScalef(0.8f, 0.8f, 0.8f);
        cube();
    }
    glLoadIdentity();
    glTranslatef(0, -0.9f, -3);
    glDisable(GL_CULL_FACE);
    glBindTexture(GL_TEXTURE_2D, cut);
    glEnable(GL_ALPHA_TEST);
    glScalef(0.5f, 0.5f, 0.5f);
    glTranslatef(-1.5f, 0, 0);
    face();
    glDisable(GL_ALPHA_TEST);
    glBindTexture(GL_TEXTURE_2D, glass);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glTranslatef(3, 0, 0);
    face();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

#endif
