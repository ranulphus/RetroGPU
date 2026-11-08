/* t12: display list replay: a textured, coloured mesh compiled once with
 * glDrawElements from client arrays (as ClassiCube does), the arrays then
 * overwritten, and the list replayed under different matrices. */
#include "ct_tex.h"
#include <string.h>

typedef struct { float x, y, z; unsigned char c[4]; float s, t; } vtx;    /* stride 24 */

void ct_run(void)
{
    static vtx v[16];
    static unsigned short idx[54];
    GLuint list;
    int i, j, n = 0;
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++) {
            vtx *p = &v[j * 4 + i];
            p->x = i * 40.0f; p->y = j * 40.0f; p->z = 0;
            p->c[0] = (unsigned char)(255 - i * 60); p->c[1] = (unsigned char)(120 + j * 40); p->c[2] = 200; p->c[3] = 255;
            p->s = i / 3.0f; p->t = j / 3.0f;
        }
    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++) {
            unsigned short a = (unsigned short)(j * 4 + i);
            idx[n++] = a; idx[n++] = (unsigned short)(a + 1); idx[n++] = (unsigned short)(a + 5);
            idx[n++] = a; idx[n++] = (unsigned short)(a + 5); idx[n++] = (unsigned short)(a + 4);
        }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ct_texture(32, 32, 4, 0, GL_NEAREST, GL_REPEAT);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(vtx), &v[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vtx), v[0].c);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), &v[0].s);
    list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_SHORT, idx);
    glEndList();
    memset(v, 0, sizeof v);                 /* the application reuses its buffer */
    for (i = 0; i < 6; i++) {
        glLoadIdentity();
        glTranslatef(30.0f + (i % 3) * 200.0f, 60.0f + (i / 3) * 220.0f, 0);
        if (i & 1) {
            glTranslatef(60, 60, 0);
            glRotatef(20.0f * i, 0, 0, 1);
            glTranslatef(-60, -60, 0);
        }
        glCallList(list);
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t12_0");
}
