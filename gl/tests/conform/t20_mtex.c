/* t20: two textures, Quake 2 style: an RGB lightmap on unit 1 multiplied
 * into a surface texture on unit 0 (GL_MODULATE over GL_MODULATE and over
 * GL_REPLACE), with different coordinate ranges per unit, and the same on
 * a perspective quad with depth. DOS-GL on the G400/G450 draws it in one
 * pass with both texture maps; on the G200 the test draws two passes. */
#include "ct_mtex.h"

static GLuint lightmap(void)
{
    static unsigned char px[16 * 16 * 3];
    GLuint t;
    int x, y;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            int d = (x - 7) * (x - 7) + (y - 9) * (y - 9);
            unsigned char *p = px + (y * 16 + x) * 3;
            p[0] = (unsigned char)(d > 60 ? 70 : 255 - d * 3);
            p[1] = (unsigned char)(d > 60 ? 60 : 230 - d * 2);
            p[2] = (unsigned char)(120 + x * 6);
        }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

/* Unit 0 with env0, unit 1 GL_MODULATE, around draw(1); or two passes. */
static void two_units(GLuint t0, GLuint t1, GLenum env0, int two, void (*draw)(int pass))
{
    if (two) {
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)env0);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        draw(0);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);
    } else {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)env0);
        draw(1);
        glBindTexture(GL_TEXTURE_2D, t1);
        ct_second_pass(GL_MODULATE);
        glDepthFunc(GL_EQUAL);
        glColor3f(1, 1, 1);
        draw(2);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_BLEND);
    }
}

static int two;

static void quad_a(int pass)
{
    glColor3f(0.9f, 0.75f, 1.0f);
    if (pass == 2)
        ct_quad2_pass2(20, 250, 300, 460, 0.1f, 0.8f);
    else
        ct_quad2(20, 250, 300, 460, 2.0f, 0.1f, 0.8f, pass == 0);
}

static void quad_b(int pass)
{
    if (pass == 2)
        ct_quad2_pass2(330, 250, 620, 460, -0.2f, 1.4f);
    else
        ct_quad2(330, 250, 620, 460, 3.0f, -0.2f, 1.4f, pass == 0);
}

/* A perspective quad receding into the screen (on screen: corners far
 * outside it cost the engine precision, a separate matter). */
static void quad_c(int pass)
{
    static const float v[4][3] = { { -0.9f, -0.6f, -2.0f }, { 0.9f, -0.6f, -2.0f }, { 1.2f, -0.6f, -7.0f }, { -1.2f, -0.6f, -7.0f } };
    static const float c0[4][2] = { { 0, 0 }, { 4, 0 }, { 4, 6 }, { 0, 6 } };
    static const float c1[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int i;
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    for (i = 0; i < 4; i++) {
        if (pass == 0) {
            glMultiTexCoord2fARB(GL_TEXTURE0_ARB, c0[i][0], c0[i][1]);
            glMultiTexCoord2fARB(GL_TEXTURE1_ARB, c1[i][0], c1[i][1]);
        } else
            glTexCoord2f(pass == 2 ? c1[i][0] : c0[i][0], pass == 2 ? c1[i][1] : c0[i][1]);
        glVertex3fv(v[i]);
    }
    glEnd();
}

void ct_run(void)
{
    GLuint t0, t1;
    two = ct_units() >= 2;
    glClearColor(0.1f, 0.1f, 0.12f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    t0 = ct_texture(64, 64, 8, 0, GL_LINEAR, GL_REPEAT);
    t1 = lightmap();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    two_units(t0, t1, GL_MODULATE, two, quad_a);
    two_units(t0, t1, GL_REPLACE, two, quad_b);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.375, 0.375, 1, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    two_units(t0, t1, GL_REPLACE, two, quad_c);
    glDisable(GL_DEPTH_TEST);
    ct_frame("t20_0");
}
