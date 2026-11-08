/* cube - M3 rung 3: a spinning, Gouraud-shaded, depth-tested, back-face
 * culled cube through the GL API.
 *   (default)  glDrawArrays (GL_TRIANGLES from a client array)
 *   --imm      the same geometry with glBegin/glEnd (GL_QUADS)
 *   --bench    report triangles per second (in 86Box: the emulator's rate) */
#include "hx.h"
#include <GL/gl.h>
#include <GL/dosgl.h>
#include <string.h>
#include <time.h>

static const float corner[8][3] = {
    { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
    { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 },
};
/* Faces counter-clockwise seen from outside. */
static const int face[6][4] = {
    { 4, 5, 6, 7 }, { 1, 0, 3, 2 }, { 5, 1, 2, 6 }, { 0, 4, 7, 3 }, { 7, 6, 2, 3 }, { 0, 1, 5, 4 },
};
static const unsigned char colour[8][4] = {
    { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 0, 255 },
    { 255, 0, 255, 255 }, { 0, 255, 255, 255 }, { 255, 255, 255, 255 }, { 64, 64, 64, 255 },
};

typedef struct { float x, y, z; unsigned char c[4]; } vtx;   /* stride 16, as ClassiCube's untextured layout */
static vtx tris[36];

static void build(void)
{
    int f, k, n = 0;
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    for (f = 0; f < 6; f++)
        for (k = 0; k < 6; k++) {
            int i = face[f][order[k]];
            tris[n].x = corner[i][0]; tris[n].y = corner[i][1]; tris[n].z = corner[i][2];
            memcpy(tris[n].c, colour[i], 4);
            n++;
        }
}

static void draw(int imm)
{
    int f, k;
    if (!imm) {
        glVertexPointer(3, GL_FLOAT, sizeof(vtx), &tris[0].x);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vtx), tris[0].c);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        return;
    }
    glBegin(GL_QUADS);
    for (f = 0; f < 6; f++)
        for (k = 0; k < 4; k++) {
            int i = face[f][k];
            glColor4ub(colour[i][0], colour[i][1], colour[i][2], colour[i][3]);
            glVertex3f(corner[i][0], corner[i][1], corner[i][2]);
        }
    glEnd();
}

int main(int argc, char **argv)
{
    int imm = 0, bench = 0, frame, i;
    uclock_t t0;
    for (i = 1; i < argc; i++) {            /* take our options out, keep the order of the rest */
        if (!strcmp(argv[i], "--imm")) imm = 1;
        else if (!strcmp(argv[i], "--bench")) bench = 1;
        else continue;
        memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof *argv);
        argc--; i--;
    }
    hx_init(argc, argv, "cube");
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    build();
    if (bench)
        dglSetVSync(0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.375, 0.375, 1.0, 20.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    t0 = uclock();
    for (frame = 0; frame < hx_args.frames; frame++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -5);
        glRotatef(30.0f + frame * 3.0f, 1, 1, 0);
        glRotatef(frame * 2.0f, 0, 0, 1);
        draw(imm);
        if (frame == hx_args.frames - 1)
            glFinish();
        else
            dglSwapBuffers();
    }
    if (bench) {
        double s = (double)(uclock() - t0) / UCLOCKS_PER_SEC;
        hx_stat("bench frames=%d seconds=%.2f tris_per_s=%.0f", frame, s, frame * 12 / (s > 0 ? s : 1));
    }
    hx_test("gl-errors", glGetError() == GL_NO_ERROR, "");
    dglSwapBuffers();
    hx_snap_screen(imm ? "cube_imm" : "cube");
    dglShutdown();
    hx_done(0);
    return 0;
}
