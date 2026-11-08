/* rigbench - Loop C: how fast a real Matrox chip draws DOS-GL's triangles.
 *
 * DOS-GL's rig build (make rig) on a machine whose CPU is much faster than
 * the chip: batches of triangles are queued far quicker than the chip draws
 * them, so the time until glFinish returns is the chip's (and the bus's).
 * Each case is timed several times and the median kept. The CPU half of a
 * frame's cost on a Pentium II is measured elsewhere (plan Part C2).
 *
 *   RIG_BDF=0000:09:00.0 DGL_RIG_VRAM_BASE=... rigbench [reps]
 *
 * Output, one line per case (stdout):
 *   HX-RIG case=<shade>-<size>px[-z] tris=N us_med=T us_per_tri=X mpix_s=Y
 * and the raw register write rate:
 *   HX-RIG case=mmio-write words=N us_med=T mb_s=Z                        */
#include <GL/gl.h>
#include <GL/dosgl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mga/mmio.h"

#define W 640
#define H 480

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

enum { FLAT, GOURAUD, TEX, TEXBLEND, NSHADE };
static const char *shade_name[NSHADE] = { "flat", "gouraud", "tex565", "texblend" };

static GLuint make_texture(void)
{
    static uint8_t px[64 * 64 * 4];
    GLuint t;
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            uint8_t *p = &px[(y * 64 + x) * 4];
            p[0] = (uint8_t)(x * 4);
            p[1] = (uint8_t)(y * 4);
            p[2] = (uint8_t)(((x ^ y) & 8) ? 255 : 40);
            p[3] = 255;
        }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return t;
}

/* ntris right triangles of about area px pixels each, tiled over the screen,
 * at eye z -0.5: inside glOrtho(..., 0, 1)'s depth range (z 0 to -1). At
 * +0.5 every one was clipped away and the cases timed rejection. */
static void draw_batch(int ntris, int area, int shade)
{
    float leg = 1.0f;
    int i, per_row, x, y;
    while (leg * leg / 2 < area)
        leg += 1.0f;
    per_row = (int)(W / leg);
    if (per_row < 1)
        per_row = 1;
    glBegin(GL_TRIANGLES);
    for (i = 0; i < ntris; i++) {
        int cell = i / 2 % (per_row * (int)(H / leg > 1 ? H / leg : 1));
        x = (int)(cell % per_row * leg);
        y = (int)(cell / per_row * leg);
        if (shade == FLAT)
            glColor3ub(200, 120, 40);
        if (i & 1) {
            if (shade == GOURAUD) glColor3ub(255, 0, 0);
            glTexCoord2f(0, 0); glVertex3f(x, y, -0.5f);
            if (shade == GOURAUD) glColor3ub(0, 255, 0);
            glTexCoord2f(1, 1); glVertex3f(x + leg, y + leg, -0.5f);
            if (shade == GOURAUD) glColor3ub(0, 0, 255);
            glTexCoord2f(1, 0); glVertex3f(x + leg, y, -0.5f);
        } else {
            if (shade == GOURAUD) glColor3ub(255, 255, 0);
            glTexCoord2f(0, 0); glVertex3f(x, y, -0.5f);
            if (shade == GOURAUD) glColor3ub(0, 255, 255);
            glTexCoord2f(0, 1); glVertex3f(x, y + leg, -0.5f);
            if (shade == GOURAUD) glColor3ub(255, 0, 255);
            glTexCoord2f(1, 1); glVertex3f(x + leg, y + leg, -0.5f);
        }
    }
    glEnd();
}

static double time_case(int ntris, int area, int shade, int depth, int reps)
{
    double t[16];
    int r;
    if (reps > 16)
        reps = 16;
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    if (shade >= TEX)
        glEnable(GL_TEXTURE_2D);
    if (shade == TEXBLEND) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4ub(255, 255, 255, 160);
    } else
        glColor4ub(255, 255, 255, 255);
    if (depth)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    for (r = 0; r < reps; r++) {
        double t0;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glFinish();
        t0 = now_us();
        draw_batch(ntris, area, shade);
        glFinish();
        t[r] = now_us() - t0;
    }
    qsort(t, (size_t)reps, sizeof t[0], cmp_d);
    return t[reps / 2];
}

int main(int argc, char **argv)
{
    static const int sizes[] = { 16, 64, 256, 1024 };
    DGLConfig c;
    int reps = argc > 1 ? atoi(argv[1]) : 7, s, sh, z, i, clipped = 0;
    memset(&c, 0, sizeof c);
    c.width = W;
    c.height = H;
    c.double_buffer = 1;
    c.depth_bits = 16;
    printf("HX-START rigbench %s\n", dglVersion());
    if (dglInit(&c) != 0) {
        printf("HX-TEST init FAIL %s\nHX-DONE 2\n", dglGetErrorString());
        return 2;
    }
    printf("HX-STAT device %s reps=%d\n", dglGetDeviceInfo()->chip_name, reps);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, H, 0, 0, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDepthFunc(GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, make_texture());
    for (sh = 0; sh < NSHADE; sh++)
        for (s = 0; s < (int)(sizeof sizes / sizeof sizes[0]); s++)
            for (z = 0; z <= 1; z++) {
                int ntris = sizes[s] >= 1024 ? 1000 : 4000;
                unsigned long before, set_up;
                double us;
                dglSwapBuffers();                  /* DGLStats' triangles: the HAL's count, at a swap */
                before = dglGetStats()->triangles;
                us = time_case(ntris, sizes[s], sh, z, reps);
                dglSwapBuffers();
                set_up = dglGetStats()->triangles - before;
                printf("HX-RIG case=%s-%dpx%s tris=%d us_med=%.0f us_per_tri=%.3f mpix_s=%.2f set_up=%lu\n",
                       shade_name[sh], sizes[s], z ? "-z" : "", ntris, us, us / ntris,
                       (double)ntris * sizes[s] / us, set_up);
                if (set_up < (unsigned long)ntris * (unsigned long)(reps < 16 ? reps : 16))
                    clipped++;                     /* timing rejection, not drawing */
            }
    {
        /* The bus alone: writes to a drawing register the engine only latches
         * (AR0, without the GO bit), as fast as the CPU can issue them. */
        double t[16];
        int words = 200000, r;
        for (r = 0; r < reps && r < 16; r++) {
            double t0 = now_us();
            for (i = 0; i < words; i++)
                MGA_WR32(0x1C60, (uint32_t)i);
            (void)MGA_RD32(0x1E14);            /* STATUS: the writes have left the CPU */
            t[r] = now_us() - t0;
        }
        qsort(t, (size_t)(reps < 16 ? reps : 16), sizeof t[0], cmp_d);
        printf("HX-RIG case=mmio-write words=%d us_med=%.0f mb_s=%.1f\n", words, t[reps / 2],
               words * 4.0 / t[reps / 2]);
    }
    printf("HX-TEST gl-errors %s\n", glGetError() == GL_NO_ERROR ? "PASS" : "FAIL");
    printf("HX-TEST drawn %s %d cases set up fewer triangles than they drew\n", clipped ? "FAIL" : "PASS", clipped);
    dglShutdown();
    printf("HX-DONE 0\n");
    return 0;
}
