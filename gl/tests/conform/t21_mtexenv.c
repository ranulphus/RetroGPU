/* t21: the texture environments beyond modulate and replace.
 *  A: GLQuake's multitexture lightmaps: a surface texture replacing on
 *     unit 0, an inverted luminance lightmap on unit 1 in GL_BLEND with the
 *     (default) black environment colour: C = C0 * (1 - L).
 *  B: GL_DECAL on a texture with an alpha ramp over Gouraud colours:
 *     C = Cf * (1 - At) + Ct * At (the G400's combiner blend mode; the
 *     G200's decalblend).
 *  C: GL_BLEND on one unit: C = Cf * (1 - Ct). With one texture unit (the
 *     G200, which has no combiner) the test draws A and C in two passes. */
#include "ct_mtex.h"

static GLuint lum_lightmap(void)
{
    static unsigned char px[32 * 32];
    GLuint t;
    int x, y;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++) {
            int d = (x - 12) * (x - 12) + (y - 18) * (y - 18);
            px[y * 32 + x] = (unsigned char)(d > 200 ? 200 : d);     /* dark where lit: GLQuake stores 1 - light */
        }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 32, 32, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void gouraud_quad(float x0, float y0, float x1, float y1, float r)
{
    glBegin(GL_QUADS);
    glColor3f(1.0f, 0.2f, 0.2f); glTexCoord2f(0, 0); glVertex2f(x0, y0);
    glColor3f(0.2f, 1.0f, 0.2f); glTexCoord2f(r, 0); glVertex2f(x1, y0);
    glColor3f(0.2f, 0.3f, 1.0f); glTexCoord2f(r, r); glVertex2f(x1, y1);
    glColor3f(1.0f, 1.0f, 0.3f); glTexCoord2f(0, r); glVertex2f(x0, y1);
    glEnd();
}

void ct_run(void)
{
    int two = ct_units() >= 2;
    GLuint surf, lm, ramp, rgb;
    glClearColor(0.1f, 0.1f, 0.12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    surf = ct_texture(64, 64, 8, 0, GL_LINEAR, GL_REPEAT);
    ramp = ct_texture(64, 64, 16, 2, GL_NEAREST, GL_REPEAT);       /* alpha rises left to right */
    rgb = ct_texture(64, 64, 4, 0, GL_NEAREST, GL_REPEAT);
    lm = lum_lightmap();

    /* A */
    glColor3f(1, 1, 1);
    if (two) {
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, surf);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, lm);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
        ct_quad2(20, 250, 300, 460, 3.0f, 0.0f, 1.0f, 1);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);
    } else {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, surf);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        ct_quad2(20, 250, 300, 460, 3.0f, 0.0f, 1.0f, 0);
        glBindTexture(GL_TEXTURE_2D, lm);
        ct_second_pass(GL_BLEND);
        ct_quad2_pass2(20, 250, 300, 460, 0.0f, 1.0f);
        glDisable(GL_BLEND);
    }

    /* B */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ramp);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    gouraud_quad(330, 250, 620, 460, 1.0f);

    /* C */
    glBindTexture(GL_TEXTURE_2D, rgb);
    if (two) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
        gouraud_quad(170, 20, 470, 220, 2.0f);
    } else {
        glDisable(GL_TEXTURE_2D);
        gouraud_quad(170, 20, 470, 220, 2.0f);
        glEnable(GL_TEXTURE_2D);
        ct_second_pass(GL_BLEND);
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(170, 20);
        glTexCoord2f(2, 0); glVertex2f(470, 20);
        glTexCoord2f(2, 2); glVertex2f(470, 220);
        glTexCoord2f(0, 2); glVertex2f(170, 220);
        glEnd();
        glDisable(GL_BLEND);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    ct_frame("t21_0");
}
