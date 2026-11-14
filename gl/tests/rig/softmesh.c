#define _GNU_SOURCE
/* softmesh.c - the software renderer's per-triangle cost through GL
 * (gl/docs/b2.md B2.9): a 65,536-triangle Gouraud sphere at 320x200 by
 * glDrawElements, depth-tested, on the rig build's memory device; triangles a
 * second. make soft-meshbench [FRAMES=40] [PROF=1]: PROF=1 also samples where
 * the time goes (SIGPROF every 200 us; perf is off on the build host, -pg
 * builds crash here) into samples.txt, which tools/soft/attr.py maps onto the
 * program's functions. DGL_SOFT_TILE and the other DGL_SOFT_ settings apply. */
#include "ct.h"
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

#define SLICES 256
#define STACKS 128

static float pos[(SLICES + 1) * (STACKS + 1)][3];
static unsigned char col[(SLICES + 1) * (STACKS + 1)][4];
static unsigned short idx[SLICES * STACKS * 6];

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

void ct_run(void)
{
    int i, j, n = 0, f, frames = getenv("FRAMES") ? atoi(getenv("FRAMES")) : 10;
    double t0, t;
    for (j = 0; j <= STACKS; j++)
        for (i = 0; i <= SLICES; i++) {
            double th = 2 * M_PI * i / SLICES, ph = M_PI * j / STACKS;
            int k = j * (SLICES + 1) + i;
            pos[k][0] = (float)(sin(ph) * cos(th)), pos[k][1] = (float)cos(ph), pos[k][2] = (float)(sin(ph) * sin(th));
            col[k][0] = (unsigned char)(128 + 127 * pos[k][0]), col[k][1] = (unsigned char)(128 + 127 * pos[k][1]);
            col[k][2] = (unsigned char)(128 + 127 * pos[k][2]), col[k][3] = 255;
        }
    for (j = 0; j < STACKS; j++)
        for (i = 0; i < SLICES; i++) {
            unsigned short a = (unsigned short)(j * (SLICES + 1) + i), b = (unsigned short)(a + 1);
            unsigned short c = (unsigned short)(a + SLICES + 1), d = (unsigned short)(c + 1);
            idx[n++] = a, idx[n++] = c, idx[n++] = b, idx[n++] = b, idx[n++] = c, idx[n++] = d;
        }
    glViewport(0, 0, 320, 200);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.8, 0.8, -0.5, 0.5, 1, 10);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    if (getenv("PROF"))
        prof_start();
    t0 = now();
    for (f = 0; f < frames; f++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0, 0, -3);
        glRotatef((float)(f * 7), 0.3f, 1, 0);
        glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_SHORT, idx);
        glFinish();
    }
    t = now() - t0;
    if (getenv("PROF"))
        prof_stop();
    printf("softmesh: %d frames of %d triangles in %.3f s: %.0f triangles/s, %.1f fps\n", frames, n / 3, t,
           frames * (n / 3) / t, frames / t);
}
