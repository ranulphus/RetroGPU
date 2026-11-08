/* t24: buffer objects (GL_ARB_vertex_buffer_object) as a GL 1.5 game
 * draws its world: one interleaved vertex buffer (position, colour, a
 * texture coordinate per unit) and one element buffer, the pointers given
 * as offsets, drawn with glDrawRangeElementsEXT; then the buffer changed
 * with glBufferSubDataARB (the colours) and through glMapBufferARB (unit
 * 1's coordinates) and drawn again, moved. Two textures in one pass where
 * there are two units, two passes with one (the G200), as t22. */
#include "ct_mtex.h"
#include <stddef.h>

#define N 9                                  /* vertices per side */

typedef struct {
    float pos[2];
    unsigned char col[4];
    float tc0[2], tc1[2];
} vtx;

static vtx v[N * N];
static GLushort idx[(N - 1) * (N - 1) * 6];

static void mesh(float x0, float y0, float size)
{
    int i, j, k = 0;
    for (j = 0; j < N; j++)
        for (i = 0; i < N; i++) {
            vtx *p = &v[j * N + i];
            p->pos[0] = x0 + size * i / (N - 1) + (j & 1 ? 4.0f : 0.0f);
            p->pos[1] = y0 + size * j / (N - 1);
            p->tc0[0] = 2.0f * i / (N - 1);
            p->tc0[1] = 2.0f * j / (N - 1);
            p->tc1[0] = (float)i / (N - 1);
            p->tc1[1] = (float)j / (N - 1);
            p->col[0] = (unsigned char)(150 + i * 12);
            p->col[1] = (unsigned char)(255 - j * 10);
            p->col[2] = 200;
            p->col[3] = 255;
        }
    for (j = 0; j < N - 1; j++)
        for (i = 0; i < N - 1; i++) {
            GLushort a = (GLushort)(j * N + i), b = (GLushort)(a + 1), c = (GLushort)(a + N), d = (GLushort)(c + 1);
            idx[k++] = a; idx[k++] = b; idx[k++] = d;
            idx[k++] = a; idx[k++] = d; idx[k++] = c;
        }
}

#define OFF(f) ((const GLvoid *)offsetof(vtx, f))

/* The mesh from the bound buffers: pass 0 both units at once, 1 and 2 the
 * two passes of the one-unit picture. */
static void draw(int pass)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(vtx), OFF(pos));
    if (pass != 2) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vtx), OFF(col));
    } else
        glColor3f(1, 1, 1);
    if (pass == 0) {
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), OFF(tc1));
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
    }
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), pass == 2 ? OFF(tc1) : OFF(tc0));
    glDrawRangeElementsEXT(GL_TRIANGLES, 0, N * N - 1, (N - 1) * (N - 1) * 6, GL_UNSIGNED_SHORT, (const GLvoid *)0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (pass == 0) {
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void textures(int two, GLuint t0, GLuint t1)
{
    if (two) {
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        draw(0);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        return;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_BLEND);
    draw(1);
    glBindTexture(GL_TEXTURE_2D, t1);
    ct_second_pass(GL_MODULATE);
    draw(2);
    glDisable(GL_BLEND);
}

void ct_run(void)
{
    static unsigned char lmpx[16 * 16 * 3];
    int two = ct_units() >= 2, i;
    GLuint t0, t1, buf[2];
    vtx *m;
    glClearColor(0.1f, 0.1f, 0.12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    t0 = ct_texture(64, 64, 8, 0, GL_NEAREST, GL_REPEAT);
    for (i = 0; i < 16 * 16; i++) {                       /* diagonal stripes */
        int x = i % 16, y = i / 16;
        lmpx[i * 3] = (unsigned char)(((x + y) / 4) & 1 ? 255 : 90);
        lmpx[i * 3 + 1] = (unsigned char)(160 + x * 5);
        lmpx[i * 3 + 2] = (unsigned char)(255 - y * 8);
    }
    glGenTextures(1, &t1);
    glBindTexture(GL_TEXTURE_2D, t1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, lmpx);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    mesh(20, 20, 280);

    glGenBuffersARB(2, buf);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, buf[0]);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof v, v, GL_STATIC_DRAW_ARB);
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, buf[1]);
    glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof idx, idx, GL_STATIC_DRAW_ARB);
    textures(two, t0, t1);

    /* New colours, in place: the next draw sees them. */
    for (i = 0; i < N * N; i++) {
        v[i].col[0] = (unsigned char)(255 - v[i].col[0] / 2);
        v[i].col[2] = 120;
    }
    glBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, sizeof v, v);
    glTranslatef(320, 0, 0);
    textures(two, t0, t1);

    /* Unit 1's coordinates halved, written through the mapped buffer. */
    m = (vtx *)glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
    if (m) {
        for (i = 0; i < N * N; i++) {
            m[i] = v[i];
            m[i].tc1[0] = v[i].tc1[0] * 0.5f;
            m[i].tc1[1] = v[i].tc1[1] * 0.5f;
        }
        glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
    }
    glTranslatef(0, 200, 0);
    textures(two, t0, t1);

    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
    glDeleteBuffersARB(2, buf);
    ct_frame("vbo");
    ct_log("units=%d map=%s", two ? 2 : 1, m ? "yes" : "no");
}
