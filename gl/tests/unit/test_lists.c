/* test_lists.c - display lists replay a snapshot, isolated from later
 * changes to the application's arrays. */
#include "unit.h"
#include "../../src/gl/gl_state.h"
#include "../../src/gl/gl_draw.h"
#include <string.h>

static int ntri;
static float xs[16];
static void on_tri(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *p)
{
    (void)p;
    if (ntri < 5) { xs[ntri * 3] = a->x; xs[ntri * 3 + 1] = b->x; xs[ntri * 3 + 2] = c->x; }
    ntri++;
}

void unit_run(void)
{
    float pos[6][3];
    unsigned short idx[6] = { 3, 4, 5, 5, 4, 3 };
    GLuint l;
    int i;
    for (i = 0; i < 6; i++) { pos[i][0] = (float)(10 + i); pos[i][1] = pos[i][2] = 0; }
    dgl_gl_reset();
    memset(&dgl_sink, 0, sizeof dgl_sink);
    dgl_sink.triangle = on_tri;
    l = glGenLists(2);
    CHECK(l == 1);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glEnableClientState(GL_VERTEX_ARRAY);
    glNewList(l, GL_COMPILE);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glEndList();
    CHECK(ntri == 0);                                  /* compiled, not drawn */
    for (i = 0; i < 6; i++) pos[i][0] = -1;            /* the application reuses its arrays */
    glCallList(l);
    CHECK(ntri == 2);
    CHECK(xs[0] == 13 && xs[1] == 14 && xs[2] == 15 && xs[3] == 15 && xs[5] == 13);
    /* Immediate mode inside a list. */
    ntri = 0;
    glNewList(l + 1, GL_COMPILE);
    glBegin(GL_TRIANGLES); glVertex3f(1, 0, 0); glVertex3f(2, 0, 0); glVertex3f(3, 0, 0); glEnd();
    glEndList();
    glCallList(l + 1);
    glCallList(l + 1);
    CHECK(ntri == 2 && xs[3] == 1);
    /* Errors and deletion. */
    glEndList();
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glDeleteLists(l, 2);
    ntri = 0;
    glCallList(l);
    CHECK(ntri == 0);
    CHECK(glGenLists(1) == 1);                         /* names are reused */
}
