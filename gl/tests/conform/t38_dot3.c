/* t38: GL_ARB_texture_env_dot3. A normal map of rounded bumps (normals packed as colour, 0.5 for 0) combined by
 * DOT3_RGB with the primary colour as the light's direction, lit from four directions; then DOT3_RGBA, whose
 * result is the alpha too, blended over a stripe so the alpha shows; and a dot3 with a second unit modulating the
 * result by a coloured texture (the common bump-mapping set-up). Skipped where the extension isn't advertised. */
#define GL_GLEXT_PROTOTYPES 1
#include "ct_tex.h"
#include <math.h>

static GLuint normal_map(void)
{
    static unsigned char buf[64 * 64 * 4];
    GLuint t;
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            /* a bump every 16 texels: a hemisphere's normal inside its circle, flat (0, 0, 1) outside */
            double u = (x % 16) - 7.5, v = (y % 16) - 7.5, r2 = (u * u + v * v) / 49.0, nx = 0, ny = 0, nz = 1, l;
            unsigned char *p = buf + (y * 64 + x) * 4;
            if (r2 < 1)
                nx = u / 7.0, ny = v / 7.0, nz = sqrt(1 - r2);
            l = sqrt(nx * nx + ny * ny + nz * nz);
            p[0] = (unsigned char)(127.5 + 127.5 * nx / l);
            p[1] = (unsigned char)(127.5 + 127.5 * ny / l);
            p[2] = (unsigned char)(127.5 + 127.5 * nz / l);
            p[3] = 255;
        }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

/* The light's direction (x, y, z, normalised) as the primary colour, packed as the normals are. */
static void light(double x, double y, double z)
{
    double l = sqrt(x * x + y * y + z * z);
    glColor4f((float)(0.5 + 0.5 * x / l), (float)(0.5 + 0.5 * y / l), (float)(0.5 + 0.5 * z / l), 1.0f);
}

static void dot3(GLenum fn)
{
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, fn);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PRIMARY_COLOR_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);
}

void ct_run(void)
{
    static const double dirs[4][3] = { { 1, 0, 1 }, { -1, 0, 1 }, { 0, 1, 1 }, { 0, -1, 0.3 } };
    GLuint nm, base;
    int i;
    if (!ct_has("GL_ARB_texture_env_dot3")) {
        ct_log("CT-SKIP no GL_ARB_texture_env_dot3");
        return;
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    nm = normal_map();
    glEnable(GL_TEXTURE_2D);

    /* Top row: DOT3_RGB, a light from the right, the left, above, and grazing from below. */
    dot3(GL_DOT3_RGB_ARB);
    for (i = 0; i < 4; i++) {
        light(dirs[i][0], dirs[i][1], dirs[i][2]);
        ct_quad2d(24.0f + i * 152.0f, 312.0f, 152.0f + i * 152.0f, 440.0f, 0, 0, 2, 2);
    }

    /* Middle row: DOT3_RGBA over a stripe, blended by the result's alpha (the dot product), against DOT3_RGB with
     * the primary alpha (opaque). */
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.9f, 0.3f, 0.1f);
    ct_quad2d(24, 200, 616, 232, 0, 0, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    dot3(GL_DOT3_RGBA_ARB);
    light(1, 1, 1);
    ct_quad2d(24, 168, 312, 264, 0, 0, 2, 1);
    dot3(GL_DOT3_RGB_ARB);
    ct_quad2d(328, 168, 616, 264, 0, 0, 2, 1);
    glDisable(GL_BLEND);

    /* Bottom row: unit 0 the dot3, unit 1 a checker modulating it (GL_MODULATE with the previous). */
    base = ct_texture(64, 64, 8, 0, GL_LINEAR, GL_REPEAT);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, base);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(GL_TEXTURE_2D, nm);
    dot3(GL_DOT3_RGB_ARB);
    light(-1, 1, 1.5);
    glBegin(GL_QUADS);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0, 0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0, 0); glVertex2f(24, 24);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 4, 0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 2, 0); glVertex2f(616, 24);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 4, 1); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 2, 0.5f); glVertex2f(616, 152);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0, 1); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0, 0.5f); glVertex2f(24, 152);
    glEnd();
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t38_0");
}
