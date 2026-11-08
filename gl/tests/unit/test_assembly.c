/* test_assembly.c - primitives cut into triangles with GL's winding and
 * provoking vertices, from arrays, indices and immediate mode. */
#include "unit.h"
#include "../../src/gl/gl_state.h"
#include "../../src/gl/gl_draw.h"
#include <string.h>

static int ntri, nline, npoint;
static float tris[64][4];                     /* x of a, b, c and of the provoking vertex */

static void on_tri(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *p)
{
    if (ntri < 64) { tris[ntri][0] = a->x; tris[ntri][1] = b->x; tris[ntri][2] = c->x; tris[ntri][3] = p->x; }
    ntri++;
}
static void on_line(const dgl_cvtx *a, const dgl_cvtx *b) { (void)a; (void)b; nline++; }
static void on_point(const dgl_cvtx *a) { (void)a; npoint++; }

static void reset(void) { ntri = nline = npoint = 0; }

void unit_run(void)
{
    /* Vertex i sits at x = i, so triangles are identified by their x values. */
    float pos[8][3];
    unsigned short idx[6] = { 0, 1, 2, 2, 1, 3 };
    int i;
    for (i = 0; i < 8; i++) { pos[i][0] = (float)i; pos[i][1] = 0; pos[i][2] = 0; }
    dgl_gl_reset();
    memset(&dgl_sink, 0, sizeof dgl_sink);
    dgl_sink.triangle = on_tri; dgl_sink.line = on_line; dgl_sink.point = on_point;
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glEnableClientState(GL_VERTEX_ARRAY);

    reset(); glDrawArrays(GL_TRIANGLES, 0, 7);
    CHECK(ntri == 2);
    CHECK(tris[1][0] == 3 && tris[1][3] == 5);
    reset(); glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
    CHECK(ntri == 3);
    CHECK(tris[1][0] == 2 && tris[1][1] == 1 && tris[1][2] == 3);   /* odd: (i+1, i, i+2) */
    CHECK(tris[2][3] == 4);
    reset(); glDrawArrays(GL_TRIANGLE_FAN, 0, 5);
    CHECK(ntri == 3 && tris[2][0] == 0 && tris[2][2] == 4);
    reset(); glDrawArrays(GL_QUADS, 0, 8);
    CHECK(ntri == 4 && tris[1][3] == 3 && tris[3][3] == 7);
    reset(); glDrawArrays(GL_POLYGON, 0, 5);
    CHECK(ntri == 3 && tris[2][3] == 0);                           /* provoking = first */
    reset(); glDrawArrays(GL_LINE_LOOP, 0, 4);
    CHECK(nline == 4);
    reset(); glDrawArrays(GL_POINTS, 2, 3);
    CHECK(npoint == 3);
    reset(); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    CHECK(ntri == 2 && tris[1][0] == 2 && tris[1][2] == 3);
    /* Immediate mode gives the same triangles. */
    reset();
    glBegin(GL_QUADS);
    for (i = 0; i < 4; i++) glVertex3f((float)i, 0, 0);
    glEnd();
    CHECK(ntri == 2 && tris[0][0] == 0 && tris[1][2] == 3);
    /* Vertices sharing a cache slot (index mod 64): the sink gets copies, so
     * each is still the right vertex; elsewhere the slots themselves. */
    {
        static float far[200][3];
        static const unsigned short clash[6] = { 0, 64, 128, 1, 65, 2 };
        for (i = 0; i < 200; i++) { far[i][0] = (float)i; far[i][1] = 0; far[i][2] = 0; }
        glVertexPointer(3, GL_FLOAT, 0, far);
        reset(); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, clash);
        CHECK(ntri == 2 && tris[0][0] == 0 && tris[0][1] == 64 && tris[0][2] == 128 && tris[0][3] == 128);
        CHECK(tris[1][0] == 1 && tris[1][1] == 65 && tris[1][2] == 2 && tris[1][3] == 2);
        reset(); glDrawArrays(GL_TRIANGLE_FAN, 0, 70);                /* (0, 64, 65) clashes */
        CHECK(ntri == 68 && tris[62][0] == 0 && tris[62][1] == 63 && tris[62][2] == 64);
        CHECK(tris[63][0] == 0 && tris[63][1] == 64 && tris[63][2] == 65 && tris[63][3] == 65);
        glVertexPointer(3, GL_FLOAT, 0, pos);
    }
    /* dgl_ub_f: each byte's quotient by 255, as the divide gives it. */
    for (i = 0; i < 256; i++) {
        volatile float num = (float)i, q = num / 255.0f;
        CHECK(memcmp((const void *)&q, &dgl_ub_f[i], sizeof q) == 0);
    }
    glColor4ub(0, 51, 128, 255);
    CHECK(dgl_gl.cur_color[0] == 0.0f && dgl_gl.cur_color[3] == 1.0f && dgl_gl.cur_color[1] == 0.2f);
    /* Errors. */
    glBegin(GL_TRIANGLES); glBegin(GL_TRIANGLES);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glEnd();
    glDrawArrays(0x77, 0, 3);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glVertexPointer(5, GL_FLOAT, 0, pos);
    CHECK(glGetError() == GL_INVALID_VALUE);
}
