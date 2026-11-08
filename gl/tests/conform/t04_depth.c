/* t04: depth test: interpenetrating triangles, and each depth function
 * against a fixed depth field. */
#include "ct.h"

static void quad(float x, float y, float w, float h, float z, float r, float g, float b)
{
    glColor3f(r, g, b);
    glVertex3f(x, y, z); glVertex3f(x + w, y, z); glVertex3f(x + w, y + h, z); glVertex3f(x, y + h, z);
}

void ct_run(void)
{
    static const GLenum funcs[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    int i;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);           /* z maps to depth 1 - (z + 1) / 2 */
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glBegin(GL_TRIANGLES);                       /* two triangles crossing in depth */
    glColor3f(1, 0, 0); glVertex3f(40, 40, -0.8f); glVertex3f(600, 60, 0.8f); glVertex3f(320, 220, 0.0f);
    glColor3f(0, 1, 0); glVertex3f(40, 220, 0.8f); glVertex3f(600, 40, -0.8f); glVertex3f(320, 60, 0.0f);
    glEnd();
    /* Depth functions: a background at z = 0 (depth 0.5), then each function
     * with a quad in front (left half) and behind (right half). */
    glDepthFunc(GL_ALWAYS);
    glBegin(GL_QUADS);
    quad(20, 260, 600, 200, 0.0f, 0.2f, 0.2f, 0.2f);
    glEnd();
    for (i = 0; i < 8; i++) {
        glDepthFunc(funcs[i]);
        glBegin(GL_QUADS);
        quad(30 + i * 74, 280, 32, 160, 0.5f, 1, 1, 0);     /* nearer: depth 0.25 */
        quad(62 + i * 74, 280, 32, 160, -0.5f, 0, 1, 1);    /* farther: depth 0.75 */
        glEnd();
    }
    glDepthFunc(GL_LESS);
    ct_frame("t04_0");
}
