/* resgl - every mode dglEnumModes offers (the BIOS's, and the sizes shown
 * scaled or zoomed in a larger BIOS mode): initialise it, draw with GL,
 * read the frame back, swap, and compare the unit tester's picture of the
 * monitor with the frame as DGLDeviceInfo says it is shown. Then draw into
 * GL_FRONT, glFlush, and the change must be on screen too.
 *
 *   RESGL [WxH ...]     only these sizes
 *
 * DGL_ZOOM=1, DGL_PRESENT=force and DGL_SCALE_FILTER=bilinear change how
 * modes are shown; with bilinear only pixels whose four source texels agree
 * are compared. Real cards have no unit tester: there it checks the
 * readback only. */
#include "hx.h"
#include <GL/gl.h>
#include <GL/dosgl.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bilinear;

static void wait_frames(int n)
{
    long spin;
    while (n-- > 0) {
        for (spin = 0; spin < 2000000L && (inportb(0x3DA) & 8); spin++)
            ;
        for (spin = 0; spin < 2000000L && !(inportb(0x3DA) & 8); spin++)
            ;
    }
}

/* Bilinear results may differ by one step per channel (the engine's
 * fixed-point weights at the picture's edges); nearest must be exact. */
static int same(uint16_t a, uint16_t b)
{
    int t = bilinear ? 1 : 0;
    return abs(((a >> 11) & 31) - ((b >> 11) & 31)) <= t && abs(((a >> 5) & 63) - ((b >> 5) & 63)) <= 2 * t &&
           abs((a & 31) - (b & 31)) <= t;
}

static uint16_t to565(const unsigned char *p)
{
    return (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
}

/* The display pixel (X, Y), from the w x h frame (top row first). */
static int model(const DGLDeviceInfo *di, const uint16_t *f, int w, int h, int X, int Y, uint16_t *out)
{
    int i = X - di->picture_x, j = Y - di->picture_y, dw = di->picture_width, dh = di->picture_height;
    if (!strcmp(di->fit, "zoom")) {
        *out = f[(Y / 2) * w + X / 2];
        return 1;
    }
    if (i < 0 || j < 0 || i >= dw || j >= dh) {
        *out = 0;
        return 1;
    }
    if (!strcmp(di->fit, "native")) {
        *out = f[j * w + i];
        return 1;
    }
    if (bilinear) {
        int x = (int)((double)i * (w - 1) / (dw > 1 ? dw - 1 : 1)), y = (int)((double)j * (h - 1) / (dh > 1 ? dh - 1 : 1));
        int x1 = x + 1 < w ? x + 1 : x, y1 = y + 1 < h ? y + 1 : y;
        uint16_t a = f[y * w + x];
        if (f[y * w + x1] != a || f[y1 * w + x] != a || f[y1 * w + x1] != a)
            return 0;
        *out = a;
        return 1;
    }
    *out = f[(int)((((long)j * 2 + 1) * h * 32 + dh) / (64L * dh)) * w + (int)((((long)i * 2 + 1) * w * 32 + dw) / (64L * dw))];
    return 1;
}

static void scene(int w, int h)
{
    static unsigned char tex[32 * 32 * 3];
    GLuint t;
    int x, y;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);                     /* y down, like the screen */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DITHER);
    glClearColor(0.25f, 0.25f, 0.25f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0.25f, 0); glVertex2f(w * 0.1f, h * 0.1f);
    glColor3f(0, 1, 0.5f); glVertex2f(w * 0.9f, h * 0.2f);
    glColor3f(0.2f, 0.3f, 1); glVertex2f(w * 0.3f, h * 0.9f);
    glEnd();
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++) {
            unsigned char *p = tex + (y * 32 + x) * 3;
            p[0] = (unsigned char)(x * 8); p[1] = (unsigned char)(y * 8); p[2] = (unsigned char)(((x ^ y) & 4) ? 255 : 0);
        }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 32, 32, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(w * 0.55f, h * 0.55f);
    glTexCoord2f(1, 0); glVertex2f(w * 0.95f, h * 0.55f);
    glTexCoord2f(1, 1); glVertex2f(w * 0.95f, h * 0.95f);
    glTexCoord2f(0, 1); glVertex2f(w * 0.55f, h * 0.95f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &t);
    glBegin(GL_LINES);                              /* the frame's edges */
    glColor3f(1, 1, 1); glVertex2f(0.5f, 0.5f); glVertex2f(w - 0.5f, 0.5f);
    glColor3f(0, 1, 0); glVertex2f(w - 0.5f, 0.5f); glVertex2f(w - 0.5f, h - 0.5f);
    glEnd();
}

static void one(int w, int h)
{
    DGLConfig c;
    const DGLDeviceInfo *di;
    char name[24];
    unsigned char *rgba = NULL, *bgrx = NULL;
    uint16_t *f = NULL, sw = 0, sh = 0, want;
    int x, y, W, H, shot_bad = 0, front_ok = -1;
    snprintf(name, sizeof name, "%dx%d", w, h);
    memset(&c, 0, sizeof c);
    c.width = w; c.height = h; c.color_bits = 16; c.depth_bits = 16; c.double_buffer = 1; c.vsync = 0;
    if (dglInit(&c) != 0) {
        hx_test(name, 0, "dglInit: %s", dglGetErrorString());
        return;
    }
    di = dglGetDeviceInfo();
    W = di->display_width; H = di->display_height;
    scene(w, h);
    rgba = (unsigned char *)malloc((size_t)w * h * 4);
    f = (uint16_t *)malloc((size_t)w * h * 2);
    bgrx = (unsigned char *)malloc((size_t)W * 4);
    if (!rgba || !f || !bgrx) {
        free(rgba); free(f); free(bgrx);
        dglShutdown();
        hx_test(name, 0, "out of memory");
        return;
    }
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (y = 0; y < h; y++)                         /* GL rows are bottom-up */
        for (x = 0; x < w; x++)
            f[y * w + x] = to565(rgba + ((size_t)(h - 1 - y) * w + x) * 4);
    dglSwapBuffers();
    wait_frames(3);
    if (di->emulated && hx_ut_present() && hx_ut_capture(&sw, &sh) == 0) {
        if (sw != W || sh != H)
            shot_bad = -1;
        else
            for (y = 0; y < H && shot_bad >= 0; y++) {
                if (hx_ut_read(0, y, W, 1, bgrx) != 0) { shot_bad = -2; break; }
                for (x = 0; x < W; x++) {
                    unsigned char px[3];
                    px[0] = bgrx[x * 4 + 2]; px[1] = bgrx[x * 4 + 1]; px[2] = bgrx[x * 4];
                    if (model(di, f, w, h, x, y, &want) && !same(to565(px), want) && shot_bad++ < 4)
                        hx_log("HX-STAT resgl %s screen (%d,%d) got %04x want %04x", name, x, y, to565(px), want);
                }
            }
        /* Drawing into GL_FRONT shows after glFlush. */
        glDrawBuffer(GL_FRONT);
        glColor3f(1, 0, 1);
        glBegin(GL_QUADS);
        glVertex2f(w * 0.45f, h * 0.45f); glVertex2f(w * 0.55f, h * 0.45f);
        glVertex2f(w * 0.55f, h * 0.55f); glVertex2f(w * 0.45f, h * 0.55f);
        glEnd();
        glFlush();
        glFinish();
        wait_frames(3);
        {
            int X = di->picture_x + di->picture_width / 2, Y = di->picture_y + di->picture_height / 2;
            unsigned char px[3];
            if (!strcmp(di->fit, "zoom")) { X = w; Y = h; }
            front_ok = hx_ut_capture(&sw, &sh) == 0 && hx_ut_read(X, Y, 1, 1, bgrx) == 0;
            px[0] = bgrx[2]; px[1] = bgrx[1]; px[2] = bgrx[0];
            front_ok = front_ok && to565(px) == 0xF81F;
        }
        glDrawBuffer(GL_BACK);
    }
    hx_log("HX-STAT resgl %s fit=%s display=%dx%d picture=%d,%d,%dx%d screen_bad=%d front=%d", name, di->fit, W, H,
           di->picture_x, di->picture_y, di->picture_width, di->picture_height, shot_bad, front_ok);
    hx_test(name, shot_bad == 0 && front_ok != 0 && glGetError() == GL_NO_ERROR, "%s in %dx%d: %d screen mismatches%s",
            di->fit, W, H, shot_bad, front_ok == 0 ? ", GL_FRONT not shown" : "");
    free(rgba); free(f); free(bgrx);
    dglShutdown();
}

int main(int argc, char **argv)
{
    DGLMode modes[32];
    int n, i, j, want[16][2], nwant = 0;
    const char *e;
    for (i = 1; i < argc; i++)
        if (argv[i][0] != '-') {
            if (nwant < 16 && sscanf(argv[i], "%dx%d", &want[nwant][0], &want[nwant][1]) == 2)
                nwant++;
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof *argv);
            argc--; i--;
        }
    hx_init(argc, argv, "resgl");
    e = getenv("DGL_SCALE_FILTER");
    bilinear = e && !strcmp(e, "bilinear");
    n = dglEnumModes(modes, 32);
    for (i = 0; i < n; i++) {
        int pick = !nwant;
        for (j = 0; j < nwant; j++)
            pick |= modes[i].width == want[j][0] && modes[i].height == want[j][1];
        hx_log("HX-STAT resgl mode %dx%d display=%dx%d scaled=%d zoomed=%d pitch=%d double=%d depth=%d",
               modes[i].width, modes[i].height, modes[i].display_width, modes[i].display_height, modes[i].scaled,
               modes[i].zoomed, modes[i].pitch_px, modes[i].can_double_buffer, modes[i].max_depth_bits);
        if (pick)
            one(modes[i].width, modes[i].height);
    }
    hx_test("modes", n > 0, "%d modes", n);
    hx_done(0);
    return 0;
}
