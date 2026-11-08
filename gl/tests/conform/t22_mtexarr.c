/* t22: two textures from client arrays: a grid mesh drawn with
 * glDrawElements from vertex, colour and one texture-coordinate array per
 * unit (glClientActiveTextureARB), unit 1 transformed by its own texture
 * matrix; then the same mesh from a display list, moved. With one unit
 * (the G200) the test draws each mesh twice. */
#include "ct_mtex.h"

#define N 9                                  /* vertices per side */

static float pos[N * N][2], tc0[N * N][2], tc1[N * N][2];
static unsigned char col[N * N][4];
static GLushort idx[(N - 1) * (N - 1) * 6];

static void mesh(float x0, float y0, float size)
{
    int i, j, k = 0;
    for (j = 0; j < N; j++)
        for (i = 0; i < N; i++) {
            int v = j * N + i;
            pos[v][0] = x0 + size * i / (N - 1) + (j & 1 ? 4.0f : 0.0f);   /* a little skew */
            pos[v][1] = y0 + size * j / (N - 1);
            tc0[v][0] = 2.0f * i / (N - 1);
            tc0[v][1] = 2.0f * j / (N - 1);
            tc1[v][0] = (float)i / (N - 1);
            tc1[v][1] = (float)j / (N - 1);
            col[v][0] = (unsigned char)(150 + i * 12);
            col[v][1] = (unsigned char)(255 - j * 10);
            col[v][2] = 200;
            col[v][3] = 255;
        }
    for (j = 0; j < N - 1; j++)
        for (i = 0; i < N - 1; i++) {
            GLushort a = (GLushort)(j * N + i), b = (GLushort)(a + 1), c = (GLushort)(a + N), d = (GLushort)(c + 1);
            idx[k++] = a; idx[k++] = b; idx[k++] = d;
            idx[k++] = a; idx[k++] = d; idx[k++] = c;
        }
}

/* Unit 1's texture matrix: the lightmap turned and shrunk about its centre. */
static void unit1_matrix(void)
{
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef(0.5f, 0.5f, 0);
    glRotatef(30, 0, 0, 1);
    glScalef(1.5f, 1.5f, 1);
    glTranslatef(-0.5f, -0.5f, 0);
    glMatrixMode(GL_MODELVIEW);
}

static void draw_mesh(int two, int pass)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, pos);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    if (two) {
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, tc0);
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, tc1);
    } else {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, pass == 2 ? tc1 : tc0);
    }
    glDrawElements(GL_TRIANGLES, (N - 1) * (N - 1) * 6, GL_UNSIGNED_SHORT, idx);
    if (two) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void ct_run(void)
{
    static unsigned char lmpx[16 * 16 * 3];
    int two = ct_units() >= 2, i, rep;
    GLuint t0, t1, list;
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
    /* Lists hold geometry only (DOS-GL's lists capture draws; state calls
     * made while compiling take effect at once): list 0 the one-pass mesh or
     * the first pass, list 1 the second pass. */
    list = glGenLists(2);
    glNewList(list, GL_COMPILE);
    draw_mesh(two, 1);
    glEndList();
    if (!two) {
        glNewList(list + 1, GL_COMPILE);
        glColor3f(1, 1, 1);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, pos);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, tc1);
        glDrawElements(GL_TRIANGLES, (N - 1) * (N - 1) * 6, GL_UNSIGNED_SHORT, idx);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glEndList();
    }
    for (rep = 0; rep < 3; rep++) {                     /* drawn now, then the lists twice, moved */
        if (rep == 1)
            glTranslatef(320, 0, 0);
        if (rep == 2)
            glTranslatef(0, 200, 0);
        if (two) {
            glActiveTextureARB(GL_TEXTURE0_ARB);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, t0);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glActiveTextureARB(GL_TEXTURE1_ARB);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, t1);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            unit1_matrix();
            if (rep)
                glCallList(list);
            else
                draw_mesh(1, 0);
            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glDisable(GL_TEXTURE_2D);
            glActiveTextureARB(GL_TEXTURE0_ARB);
        } else {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, t0);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            if (rep)
                glCallList(list);
            else
                draw_mesh(0, 1);
            glBindTexture(GL_TEXTURE_2D, t1);
            ct_second_pass(GL_MODULATE);
            unit1_matrix();
            glCallList(list + 1);
            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glDisable(GL_BLEND);
        }
    }
    glLoadIdentity();
    ct_frame("t22_0");
}
