/* t35: a viewport smaller than the window confines drawing (GL clips
 * primitives to the view volume, which the viewport maps), but not clears
 * (only the scissor does). An orthographic viewport with a quad and triangles
 * reaching far past every edge; a perspective one whose floor runs out of
 * view on all sides; thin lines crossing its edges; and a scissored clear
 * that reaches outside the viewport in force, which must clear all of its
 * scissor. Before the fix DOS-GL drew past a viewport's edges on every back
 * end (it clips by a guard band and cut only at the window and the scissor):
 * GLQuake's status bar, below its 3D view, flickered on the G400.
 *
 * The viewports are power-of-two sizes so that positions are exact. */
#include "ct.h"

static void view(int x, int y, int w, int h)
{
    glViewport(x, y, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void ct_run(void)
{
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.15f, 0.15f, 0.15f, 1);
    glViewport(0, 0, CT_W, CT_H);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* 1: an orthographic viewport at (64, 64), 256 x 128: a quad four times its size, and triangles past each edge. */
    view(64, 64, 256, 128);
    glBegin(GL_QUADS);                          /* (colours planar: a quad's split into triangles is GL's choice) */
    glColor3f(0.9f, 0.3f, 0.2f); glVertex2f(-256, -128);
    glColor3f(0.3f, 0.9f, 0.2f); glVertex2f(512, -128);
    glColor3f(0.3f, 0.9f, 0.8f); glVertex2f(512, 256);
    glColor3f(0.9f, 0.3f, 0.8f); glVertex2f(-256, 256);
    glEnd();
    glBegin(GL_TRIANGLES);
    glColor3f(1, 1, 1);
    glVertex2f(-200, 40); glVertex2f(60, 64); glVertex2f(-200, 88);         /* (out past the left) */
    glVertex2f(456, 40); glVertex2f(196, 64); glVertex2f(456, 88);          /* (the right) */
    glVertex2f(100, 300); glVertex2f(128, 100); glVertex2f(156, 300);       /* (the top) */
    glVertex2f(100, -200); glVertex2f(128, 30); glVertex2f(156, -200);      /* (the bottom) */
    glEnd();
    /* Thin lines crossing its edges, slopes in quarters. */
    glBegin(GL_LINES);
    glColor3f(0, 0, 0);
    glVertex2f(-40.25f, 20.5f); glVertex2f(119.75f, 60.5f);
    glVertex2f(200.25f, 100.5f); glVertex2f(296.25f, 124.5f);
    glVertex2f(30.5f, -30.25f); glVertex2f(40.5f, 9.75f);
    glEnd();

    /* 2: a perspective viewport at (384, 256), 128 x 128: a floor reaching far beyond the view on every side. */
    glViewport(384, 256, 128, 128);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5, 0.5, 1, 50);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, -1, 0);
    glColor3f(0.3f, 0.8f, 0.8f);                /* (one colour: interpolation in depth isn't this test's business) */
    glBegin(GL_QUADS);
    glVertex3f(-20, 0, -1.5f); glVertex3f(20, 0, -1.5f); glVertex3f(20, 0, -40); glVertex3f(-20, 0, -40);
    glEnd();
    glBegin(GL_TRIANGLES);                      /* (a wall standing up through the top edge) */
    glColor3f(1, 0.5f, 0);
    glVertex3f(-0.5f, -1, -3); glVertex3f(0.5f, -1, -3); glVertex3f(0, 6, -3);
    glEnd();

    /* 3: a clear with a scissor reaching outside the viewport in force (the perspective one): all of the scissor. */
    glEnable(GL_SCISSOR_TEST);
    glScissor(352, 320, 192, 96);
    glClearColor(0.2f, 0.2f, 0.7f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    ct_frame("t35_0");
}
