/* t25: GL_ARB_texture_env_combine as a GL 1.3-era game uses it, a base
 * texture lit by the iterated colour on unit 0 and a lightmap on unit 1:
 *   1. unit 1 MODULATE(PREVIOUS, TEXTURE) x2, alpha REPLACE(PREVIOUS)
 *      (Xash3D's overbright lightmaps)
 *   2. the same x4
 *   3. the same x1
 *   4. unit 1 ADD(PREVIOUS, TEXTURE)
 *   5. unit 1 REPLACE(PREVIOUS): the lightmap ignored
 *   6. unit 0 alone MODULATE(TEXTURE, PRIMARY_COLOR) x2
 * Where the implementation has no combine (DOS-GL on the G200: one unit)
 * the same pictures are drawn in passes with blending: x2 is dst*src +
 * src*dst, x4 that doubled again by a white quad (dst + dst), ADD one plus
 * one. (SUBTRACT and ADD_SIGNED have no GL 1.1 blending equivalent; the
 * host unit test checks their combiner words.) */
#include "ct_mtex.h"
#include <string.h>

#ifndef GL_COMBINE_ARB
#define GL_COMBINE_ARB        0x8570
#define GL_COMBINE_RGB_ARB    0x8571
#define GL_COMBINE_ALPHA_ARB  0x8572
#define GL_RGB_SCALE_ARB      0x8573
#define GL_PRIMARY_COLOR_ARB  0x8577
#define GL_PREVIOUS_ARB       0x8578
#define GL_SOURCE0_RGB_ARB    0x8580
#define GL_SOURCE1_RGB_ARB    0x8581
#define GL_SOURCE0_ALPHA_ARB  0x8588
#endif

enum { X2, X4, X1, ADD, KEEP, UNIT0 };

/* Unit `unit`'s environment as combine: fn(src0, src1) x scale for colour,
 * REPLACE(PREVIOUS) for alpha. */
static void combine(GLenum unit, GLenum fn, GLenum s0, GLenum s1, float scale)
{
    glActiveTextureARB(unit);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, (GLint)fn);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, (GLint)s0);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, (GLint)s1);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, scale);
}

/* One quad of the grid: column c, row r. */
static void cell(int c, int r, float *x0, float *y0, float *x1, float *y1)
{
    *x0 = 20.0f + c * 205.0f;
    *y0 = 250.0f - r * 220.0f;
    *x1 = *x0 + 190.0f;
    *y1 = *y0 + 200.0f;
}

/* The one-pass picture: combine on the units. */
static void one_pass(int k, GLuint t0, GLuint t1)
{
    float x0, y0, x1, y1;
    cell(k % 3, k / 3, &x0, &y0, &x1, &y1);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t0);
    if (k == UNIT0)
        combine(GL_TEXTURE0_ARB, GL_MODULATE, GL_TEXTURE, GL_PRIMARY_COLOR_ARB, 2.0f);
    else
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    if (k == UNIT0)
        glDisable(GL_TEXTURE_2D);
    else {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t1);
        combine(GL_TEXTURE1_ARB, k == ADD ? GL_ADD : k == KEEP ? GL_REPLACE : GL_MODULATE, GL_PREVIOUS_ARB,
                GL_TEXTURE, k == X2 ? 2.0f : k == X4 ? 4.0f : 1.0f);
    }
    glActiveTextureARB(GL_TEXTURE0_ARB);
    ct_quad2(x0, y0, x1, y1, 2.0f, 0.1f, 1.0f, k != UNIT0);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

/* The same picture with one unit, in passes. */
static void passes(int k, GLuint t0, GLuint t1)
{
    float x0, y0, x1, y1;
    cell(k % 3, k / 3, &x0, &y0, &x1, &y1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    ct_quad2(x0, y0, x1, y1, 2.0f, 0.1f, 1.0f, 0);          /* base x colour */
    glEnable(GL_BLEND);
    if (k != KEEP && k != UNIT0) {
        glBindTexture(GL_TEXTURE_2D, t1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glBlendFunc(k == ADD ? GL_ONE : GL_DST_COLOR, k == ADD ? GL_ONE : k == X1 ? GL_ZERO : GL_SRC_COLOR);
        ct_quad2_pass2(x0, y0, x1, y1, 0.1f, 1.0f);
    }
    if (k == X4 || k == UNIT0) {                             /* doubled again: dst * 1 + dst */
        glDisable(GL_TEXTURE_2D);
        glColor3f(1, 1, 1);
        glBlendFunc(GL_DST_COLOR, GL_ONE);
        ct_quad2(x0, y0, x1, y1, 2.0f, 0.1f, 1.0f, 0);
        glEnable(GL_TEXTURE_2D);
    }
    glDisable(GL_BLEND);
}

void ct_run(void)
{
    static unsigned char lmpx[16 * 16 * 3];
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    int comb = ct_units() >= 2 && ext && strstr(ext, "GL_ARB_texture_env_combine "), i, k;
    GLuint t0, t1;
    glClearColor(0.1f, 0.1f, 0.12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    t0 = ct_texture(64, 64, 8, 0, GL_LINEAR, GL_REPEAT);   /* bilinear, as t20/t21: NEAREST texel
                                                                * choices differ at the edges */
    for (i = 0; i < 16 * 16; i++) {                           /* a lightmap: soft bands, dark to bright */
        int x = i % 16, y = i / 16;
        lmpx[i * 3] = (unsigned char)(40 + x * 12);
        lmpx[i * 3 + 1] = (unsigned char)(60 + y * 10);
        lmpx[i * 3 + 2] = (unsigned char)(((x + y) / 4) & 1 ? 200 : 90);
    }
    glGenTextures(1, &t1);
    glBindTexture(GL_TEXTURE_2D, t1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, lmpx);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glColor3f(0.8f, 0.7f, 0.9f);                             /* the iterated colour (the lighting) */
    for (k = X2; k <= UNIT0; k++) {
        glColor3f(0.8f, 0.7f, 0.9f);
        if (comb)
            one_pass(k, t0, t1);
        else
            passes(k, t0, t1);
    }
    ct_frame("combine");
    ct_log("combine=%s", comb ? "yes" : "passes");
}
