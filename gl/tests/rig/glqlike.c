#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES 1
/* glqlike.c - GLQuake's call pattern for the host rig build (the X1/X2 regression hunt, 2026-11-24): per frame 400
 * surfaces, each a texture bind on unit 0 and a lightmap bind on unit 1, a GL_POLYGON of 6 vertices with both units'
 * coordinates by glMultiTexCoord2fARB, every 20th surface a 16x16 lightmap glTexSubImage2D; tiny polygons (a pixel or
 * two) at 64x64 so drawing stays cheap. PROF=1: SIGPROF samples into samples.txt (tools/soft/attr.py). Prints
 * microseconds a frame. */
#include "ct.h"
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

static unsigned samples[400000], nsamp;
static void on_prof(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *uc = (ucontext_t *)ctx;
    (void)sig, (void)si;
    if (nsamp < 400000)
        samples[nsamp++] = (unsigned)uc->uc_mcontext.gregs[REG_EIP];
}
static void prof_start(void)
{
    struct sigaction sa;
    struct itimerval it = { { 0, 200 }, { 0, 200 } };
    sa.sa_sigaction = on_prof, sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    setitimer(ITIMER_PROF, &it, NULL);
}
static void prof_stop(void)
{
    struct itimerval it = { { 0, 0 }, { 0, 0 } };
    FILE *f = fopen("samples.txt", "w");
    unsigned i;
    setitimer(ITIMER_PROF, &it, NULL);
    for (i = 0; i < nsamp; i++)
        fprintf(f, "%x\n", samples[i]);
    fclose(f);
}
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

void ct_run(void)
{
    static unsigned char tex[64 * 64 * 4], lm[16 * 16 * 4];
    GLuint t[32], l[32];
    int i, f, s, frames = getenv("FRAMES") ? atoi(getenv("FRAMES")) : 200;
    double t0, el;
    for (i = 0; i < 64 * 64 * 4; i++) tex[i] = (unsigned char)(i * 7);
    for (i = 0; i < 16 * 16 * 4; i++) lm[i] = (unsigned char)(i * 3);
    glGenTextures(32, t);
    glGenTextures(32, l);
    for (i = 0; i < 32; i++) {
        glBindTexture(GL_TEXTURE_2D, t[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, l[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, lm);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glViewport(0, 0, 64, 64);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 1, 1000);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (getenv("PROF"))
        prof_start();
    t0 = now();
    for (f = 0; f < frames; f++) {
        glClear(GL_DEPTH_BUFFER_BIT);
        for (s = 0; s < 400; s++) {
            float x = (float)((s * 37) % 61 - 30), y = (float)((s * 53) % 59 - 29), z = -400.0f - (float)(s % 97);
            int k;
            glActiveTextureARB(GL_TEXTURE0_ARB);
            glBindTexture(GL_TEXTURE_2D, t[s & 31]);
            glActiveTextureARB(GL_TEXTURE1_ARB);
            glBindTexture(GL_TEXTURE_2D, l[(s * 5) & 31]);
            if (s % 20 == 0)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, lm);
            glBegin(GL_POLYGON);
            for (k = 0; k < 6; k++) {
                float a = (float)k * 1.0472f;
                glMultiTexCoord2fARB(GL_TEXTURE0_ARB, cosf(a), sinf(a));
                glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f + 0.4f * cosf(a), 0.5f + 0.4f * sinf(a));
                glVertex3f(x + 3 * cosf(a), y + 3 * sinf(a), z);
            }
            glEnd();
        }
        glFinish();
    }
    el = now() - t0;
    if (getenv("PROF"))
        prof_stop();
    printf("glqlike: %d frames of 400 surfaces: %.1f us a frame\n", frames, el * 1e6 / frames);
}
