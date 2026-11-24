/* t36: GL_EXT_texture_lod_bias. A 256x256 texture whose nine levels are each one colour, drawn on screen-aligned
 * squares at a constant level of detail (lambda 1 or 2: 2 or 4 texels a pixel), with biases chosen so the biased
 * lambda never sits on a level boundary: which level (GL_NEAREST_MIPMAP_NEAREST) or which mix of two
 * (GL_LINEAR_MIPMAP_LINEAR) each square shows says what bias was applied. Biases stay within -1..3 (the Radeon's
 * range is -1..4). Skipped where the extension isn't advertised. */
#include "ct_tex.h"

static const unsigned char level_rgb[9][3] = {
    { 220, 40, 40 }, { 40, 200, 40 }, { 40, 40, 220 }, { 220, 220, 40 }, { 220, 40, 220 },
    { 40, 220, 220 }, { 240, 240, 240 }, { 120, 120, 120 }, { 240, 140, 20 },
};

static void levels(void)
{
    static unsigned char buf[256 * 256 * 4];
    int l, size, i;
    for (l = 0, size = 256; size >= 1; l++, size /= 2) {
        for (i = 0; i < size * size; i++) {
            buf[i * 4 + 0] = level_rgb[l][0];
            buf[i * 4 + 1] = level_rgb[l][1];
            buf[i * 4 + 2] = level_rgb[l][2];
            buf[i * 4 + 3] = 255;
        }
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
}

/* A 64x64 square at (x, y) showing lambda = log2(texels a pixel) = lam (1 or 2), drawn with this bias. */
static void square(float x, float y, int lam, float bias)
{
    float span = 64.0f * (float)(1 << lam) / 256.0f;
    glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, bias);
    ct_quad2d(x, y, x + 64, y + 64, 0.0625f, 0.0625f, 0.0625f + span, 0.0625f + span);
}

void ct_run(void)
{
    static const float near_biases[6] = { -1.0f, 0.0f, 1.0f, 2.0f, 3.0f, -0.75f };   /* lambda 1 (the last: 2) */
    static const float mix_biases[6] = { 0.0f, 0.5f, 0.25f, -0.25f, -0.5f, 0.75f };  /* lambda 1 */
    GLfloat got = -9, max = 0;
    GLuint t;
    int i;
    if (!ct_has("GL_EXT_texture_lod_bias")) {
        ct_log("CT-SKIP no GL_EXT_texture_lod_bias");
        return;
    }
    glGetFloatv(GL_MAX_TEXTURE_LOD_BIAS_EXT, &max);
    glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, 0.5f);
    glGetTexEnvfv(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, &got);
    ct_check("bias-query", got == 0.5f && max >= 3.0f, "bias read back %g, GL_MAX_TEXTURE_LOD_BIAS_EXT %g", got, max);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    levels();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    /* Top row: one level each (nearest), a whole level apart; the last at lambda 2 biased down by 0.75 (level 1). */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (i = 0; i < 6; i++)
        square(40.0f + i * 96.0f, 320.0f, i == 5 ? 2 : 1, near_biases[i]);
    /* Middle row: two levels mixed (trilinear) in quarters. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (i = 0; i < 6; i++)
        square(40.0f + i * 96.0f, 192.0f, 1, mix_biases[i]);
    /* Bottom row: a bias, then back at 0 with nothing else changed (the words built again: level 2, then 1). */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    square(40.0f, 64.0f, 1, 1.0f);
    square(136.0f, 64.0f, 1, 0.0f);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t36_0");
}
