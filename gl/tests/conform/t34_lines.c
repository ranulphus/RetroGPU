/* t34: lines and points (gl/docs/b2.md B2.5), and triangles whose edges run
 * through pixel centres (the fill rule's ties).
 *
 * Frame 0, lines: fans from every quarter-pixel start, x- and y-major, each
 * way, smooth colour (the diamond-exit ends); wide lines 2 to 5 pixels; a
 * flat-shaded strip and loop (the second vertex's colour, the loop's closing
 * segment the first's); a line through a quad's depth; lines leaving the
 * window. Frame 1, points: sizes 1 to 6 and halves (rounded) at every quarter
 * offset; points just inside and just outside the window's edges (a centre
 * outside: no point); textured points and lines. Frame 2: rectangles and
 * triangles with edges on pixel centres, and two quads sharing such an edge.
 *
 * Exact on purpose: each part draws in a viewport of power-of-two size
 * whose object coordinates are window coordinates, so the window transform
 * loses nothing (640 and 480 would scale by fifths and break every tie by
 * rounding); positions on a quarter-pixel grid and slopes in quarters put
 * every corner a renderer works out on a sixteenth (the software core's
 * grid, and llvmpipe's finer one alike). Parts that leave the window do so
 * where their viewport's edge is the window's (DOS-GL clips by a guard band
 * and the window, not the viewport). Texture coordinates keep texel
 * boundaries off pixel centres. The software renderer only (manifest): the
 * Matrox back end draws lines as plain quads, without the diamond-exit ends. */
#include "ct.h"
#include "ct_tex.h"

/* A viewport of power-of-two size at (x, y), its object coordinates the window's. */
static void view(int x, int y, int w, int h)
{
    glViewport(x, y, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(x, x + w, y, y + h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
}

static void quad(float x0, float y0, float x1, float y1, float z)
{
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z); glVertex3f(x1, y0, z); glVertex3f(x1, y1, z); glVertex3f(x0, y1, z);
    glEnd();
}

static void lines(void)
{
    int i, j, k;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glShadeModel(GL_SMOOTH);

    /* Fans (bottom left): centre offsets (f, g) in quarters; directions (4, k) and (k, 4) each way. */
    view(0, 0, 256, 256);
    glBegin(GL_LINES);
    for (i = 0; i < 16; i++) {
        float cx = 32 + 64 * (i % 4) + 0.25f * (i % 4), cy = 32 + 64 * (i / 4) + 0.25f * (i / 4);
        for (k = -4; k <= 4; k++) {
            float d[4][2] = { { 4, (float)k }, { -4, (float)-k }, { (float)k, 4 }, { (float)-k, -4 } };
            for (j = 0; j < 4; j++) {
                float ux = d[j][0] / 4.0f, uy = d[j][1] / 4.0f;
                if (j >= 2 && (k == 4 || k == -4))
                    continue;                   /* (the diagonals: drawn as x-major already) */
                glColor3f(1, 0.2f, 0); glVertex2f(cx + ux, cy + uy);
                glColor3f(0, 0.4f, 1); glVertex2f(cx + 24 * ux, cy + 24 * uy);
            }
        }
    }
    glEnd();

    /* Wide lines (bottom right): a row each for widths 2 to 5; x-major (dx 28, dy 7k) and y-major (dy 48, dx 12k):
       slopes k/4; alternating directions. Then lines leaving by the window's right and bottom edges. */
    view(384, 0, 256, 256);
    for (i = 0; i < 4; i++) {
        static const int kx[4] = { -3, -1, 0, 2 }, ky[4] = { -2, 0, 1, 3 };
        glLineWidth((float)(2 + i));
        glBegin(GL_LINES);
        for (j = 0; j < 8; j++) {
            float x0 = 392 + 30 * j + 0.25f * (j & 3), y0 = 8 + 62 * i + 0.25f * ((j + i) & 3), dx, dy;
            if (j < 4)
                dx = 24, dy = 6.0f * kx[j], y0 += 24;
            else
                dy = 48, dx = 12.0f * ky[j - 4];
            if (j & 1)
                x0 += dx, y0 += dy, dx = -dx, dy = -dy;
            glColor3f(0.2f, 1, 0.3f); glVertex2f(x0, y0);
            glColor3f(1, 0.2f, 0.9f); glVertex2f(x0 + dx, y0 + dy);
        }
        glEnd();
    }
    glLineWidth(1);
    glBegin(GL_LINES);
    glColor3f(1, 1, 1);
    glVertex2f(630.25f, 240.5f); glVertex2f(670.25f, 250.5f);
    glVertex2f(620.5f, 9.75f); glVertex2f(613.0f, -20.25f);
    glEnd();

    /* Flat shading (top left): a strip (slopes +-3/4) and a loop (a diamond), a colour a vertex; then lines
       leaving by the window's left and top edges. */
    view(0, 352, 128, 128);
    glShadeModel(GL_FLAT);
    glBegin(GL_LINE_STRIP);
    for (j = 0; j < 8; j++) {
        glColor3f((float)(j & 1), (float)((j >> 1) & 1), (j >> 2) & 1 ? 1.0f : 0.3f);
        glVertex2f(10.25f + 12 * j, 362.5f + 9 * (j & 1));
    }
    glEnd();
    glBegin(GL_LINE_LOOP);
    glColor3f(1, 0, 0); glVertex2f(20.75f, 420.25f);
    glColor3f(0, 1, 0); glVertex2f(60.75f, 450.25f);
    glColor3f(0, 0, 1); glVertex2f(100.75f, 420.25f);
    glColor3f(1, 1, 0); glVertex2f(60.75f, 390.25f);
    glEnd();
    glShadeModel(GL_SMOOTH);
    glBegin(GL_LINES);
    glColor3f(1, 1, 1);
    glVertex2f(10.25f, 400.5f); glVertex2f(-29.75f, 390.5f);
    glVertex2f(110.5f, 470.25f); glVertex2f(118.0f, 500.25f);
    glEnd();

    /* Depth (top right): a quad at z 0, lines crossing it (in front at one end, behind at the other), and one
       in front. */
    view(384, 352, 256, 128);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.3f, 0.3f, 0.8f);
    quad(392, 360, 632, 472, 0);
    glBegin(GL_LINES);
    glColor3f(1, 1, 1); glVertex3f(396.25f, 365.5f, 0.5f); glColor3f(1, 0.5f, 0); glVertex3f(616.25f, 420.5f, -0.3f);
    glColor3f(0, 1, 1); glVertex3f(628.5f, 468.25f, -0.4f); glColor3f(1, 1, 1); glVertex3f(408.5f, 413.25f, 0.6f);
    glColor3f(1, 1, 0); glVertex3f(500.25f, 466.75f, 0.2f); glColor3f(1, 0, 1); glVertex3f(525.25f, 366.75f, 0.2f);
    glEnd();
    glDisable(GL_DEPTH_TEST);
    ct_frame("t34_0");
}

static void points(void)
{
    static const float extra[5] = { 1.5f, 2.5f, 3.5f, 4.4f, 4.6f };
    int i, j;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* Sizes 1 to 6 and halves, at every quarter offset (bottom left); then points by the left and bottom
       edges: centres just inside (drawn) and just outside (none). */
    view(0, 0, 256, 256);
    for (i = 0; i < 11; i++) {
        glPointSize(i < 6 ? (float)(i + 1) : extra[i - 6]);
        glBegin(GL_POINTS);
        for (j = 0; j < 16; j++) {
            glColor3f(j / 15.0f, i / 10.0f, 1 - j / 15.0f);
            glVertex2f(16 + 14 * j + 0.25f * (j % 4), 16 + 16 * i + 0.25f * (j / 4));
        }
        glEnd();
    }
    glPointSize(5);
    glBegin(GL_POINTS);
    glColor3f(1, 1, 0);
    glVertex2f(0.25f, 200.5f); glVertex2f(-0.25f, 220.5f);
    glVertex2f(200.5f, 0.25f); glVertex2f(220.5f, -0.25f);
    glEnd();
    /* The right and top edges (top right). */
    view(384, 352, 256, 128);
    glBegin(GL_POINTS);
    glVertex2f(639.75f, 400.5f); glVertex2f(640.25f, 420.5f);
    glVertex2f(500.5f, 479.75f); glVertex2f(520.5f, 480.25f);
    glEnd();

    /* Textured (bottom right): points at texel centres, then lines whose texel boundaries fall between pixel
       centres. */
    view(384, 0, 256, 256);
    ct_texture(16, 16, 2, 0, GL_NEAREST, GL_REPEAT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(1, 1, 1);
    glPointSize(4);
    glBegin(GL_POINTS);
    for (j = 0; j < 16; j++) {
        glTexCoord2f((j + 0.5f) / 16, ((j * 5) % 16 + 0.5f) / 16);
        glVertex2f(394.5f + 12 * j, 230.25f);
    }
    glEnd();
    glLineWidth(3);
    glBegin(GL_LINES);
    glTexCoord2f(1 / 32.0f, 3.5f / 16); glVertex2f(394.5f, 150.5f);
    glTexCoord2f(1 / 32.0f + 2, 3.5f / 16); glVertex2f(594.5f, 200.5f);
    glTexCoord2f(9.5f / 16, 1 / 32.0f); glVertex2f(410.5f, 10.5f);
    glTexCoord2f(9.5f / 16, 1 / 32.0f + 2); glVertex2f(445.5f, 150.5f);
    glEnd();
    glLineWidth(1);
    glDisable(GL_TEXTURE_2D);
    ct_frame("t34_1");
}

static void ties(void)
{
    int i;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* Rectangles with every edge on pixel centres, some a pixel or two tall (bottom left). */
    view(0, 0, 256, 256);
    for (i = 0; i < 6; i++) {
        glColor3f(1, 0.2f * i, 0.2f);
        quad(10.5f + 40 * i, 10.5f, 40.5f + 40 * i, 11.5f + (i & 1 ? 1 : 15) * i, 0);
    }
    /* Two quads sharing an edge on centres: horizontal, then vertical. */
    glColor3f(1, 0, 0); quad(20.5f, 100.5f, 120.5f, 150.5f, 0);
    glColor3f(0, 1, 0); quad(20.5f, 150.5f, 120.5f, 200.5f, 0);
    glColor3f(0, 0, 1); quad(140.5f, 100.5f, 190.5f, 200.5f, 0);
    glColor3f(1, 1, 0); quad(190.5f, 100.5f, 240.5f, 200.5f, 0);
    /* Triangles with a horizontal edge on centres, below and above, and a vertex on a centre (bottom right). */
    view(384, 0, 256, 256);
    glBegin(GL_TRIANGLES);
    glColor3f(0, 1, 1);
    glVertex2f(394.5f, 20.5f); glVertex2f(494.5f, 20.5f); glVertex2f(444.5f, 100.5f);
    glColor3f(1, 0, 1);
    glVertex2f(514.5f, 100.5f); glVertex2f(564.5f, 20.5f); glVertex2f(614.5f, 100.5f);
    glColor3f(1, 1, 1);
    glVertex2f(394.5f, 200.5f); glVertex2f(494.5f, 150.5f); glVertex2f(494.5f, 250.5f);
    glEnd();
    ct_frame("t34_2");
}

void ct_run(void)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClearDepth(1);
    lines();
    points();
    ties();
}
