/* t37: GL_EXT_texture_filter_anisotropic. A floor of stripes running away from the eye (the texels vary across the
 * floor only), trilinear: the footprint of a far pixel is long along the floor and narrow across it. Filtered
 * isotropically, the level follows the long axis and the stripes fade to grey; anisotropically, the narrow axis
 * keeps them. Frame t37_0 isotropic (compared with Mesa as any frame), t37_1 at the card's largest ratio (up to
 * 16): cards filter anisotropically each in their own way, so its far part is judged by the test itself, by the
 * stripes' contrast in rows near the horizon against t37_0's. Skipped where the extension isn't advertised. */
#include "ct_tex.h"
#include <math.h>
#include <string.h>

#define FAR_Y0 170                      /* GL rows (from the bottom) whose contrast is measured: the floor's far part */
#define FAR_Y1 200

/* Stripes 8 texels wide across s, every level box-filtered from the one above (the last ones grey). */
static void stripes(void)
{
    static unsigned char a[256 * 256 * 4], b[128 * 128 * 4];
    int x, y, l, size;
    for (y = 0; y < 256; y++)
        for (x = 0; x < 256; x++) {
            unsigned char *p = a + (y * 256 + x) * 4;
            int on = (x / 4) & 1;
            p[0] = (unsigned char)(on ? 240 : 30);
            p[1] = (unsigned char)(on ? 230 : 40);
            p[2] = (unsigned char)(on ? 200 : 60);
            p[3] = 255;
        }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, a);
    for (l = 1, size = 128; size >= 1; l++, size /= 2) {
        int c;
        for (y = 0; y < size; y++)
            for (x = 0; x < size; x++)
                for (c = 0; c < 4; c++) {
                    int w2 = size * 2;
                    b[(y * size + x) * 4 + c] = (unsigned char)((a[((2 * y) * w2 + 2 * x) * 4 + c] +
                                                                   a[((2 * y) * w2 + 2 * x + 1) * 4 + c] +
                                                                   a[((2 * y + 1) * w2 + 2 * x) * 4 + c] +
                                                                   a[((2 * y + 1) * w2 + 2 * x + 1) * 4 + c] + 2) / 4);
                }
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, b);
        memcpy(a, b, (size_t)size * size * 4);
    }
}

/* The stripes' contrast in the far rows: the mean, over those rows, of the luminance's standard deviation across
 * the floor's middle columns. */
static double far_contrast(void)
{
    static unsigned char px[CT_W * (FAR_Y1 - FAR_Y0) * 4];
    double sum = 0;
    int y, x;
    glReadPixels(0, FAR_Y0, CT_W, FAR_Y1 - FAR_Y0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (y = 0; y < FAR_Y1 - FAR_Y0; y++) {
        double s = 0, s2 = 0, n = 0;
        for (x = 280; x < 360; x++) {
            const unsigned char *p = px + (y * CT_W + x) * 4;
            double l = 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];
            s += l, s2 += l * l, n++;
        }
        sum += sqrt(s2 / n - (s / n) * (s / n) > 0 ? s2 / n - (s / n) * (s / n) : 0);
    }
    return sum / (FAR_Y1 - FAR_Y0);
}

static void floor_quad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-3, -1, -1.2f);
    glTexCoord2f(1, 0); glVertex3f(3, -1, -1.2f);
    glTexCoord2f(1, 8); glVertex3f(3, -1, -12);
    glTexCoord2f(0, 8); glVertex3f(-3, -1, -12);
    glEnd();
}

void ct_run(void)
{
    GLfloat max = 0, got = 0;
    double iso, aniso;
    GLuint t;
    if (!ct_has("GL_EXT_texture_filter_anisotropic")) {
        ct_log("CT-SKIP no GL_EXT_texture_filter_anisotropic");
        return;
    }
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max);
    if (max > 16)
        max = 16;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -0.75, 0.75, 1, 50);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    stripes();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glClear(GL_COLOR_BUFFER_BIT);
    floor_quad();
    iso = far_contrast();
    ct_frame("t37_0");

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max);
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &got);
    glClear(GL_COLOR_BUFFER_BIT);
    floor_quad();
    aniso = far_contrast();
    ct_frame("t37_1");
    glDisable(GL_TEXTURE_2D);

    ct_check("aniso-query", max >= 2.0f && got == max, "GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT %g, set and read back %g",
             max, got);
    ct_check("aniso-sharper", aniso > iso * 1.5 + 2.0, "far rows' stripe contrast: isotropic %.1f, %g:1 %.1f", iso,
             max, aniso);
}
