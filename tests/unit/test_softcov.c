/* test_softcov.c - the software core's coverage (hal/src/soft/raster.c;
 * gl/docs/b2.md B2.2): each triangle's pixels against OpenGL's rule worked
 * out here in integers (a pixel drawn when its centre is inside; on an edge,
 * by the top-left rule), on the 1/16-pixel grid the core snaps to, with ties
 * made on purpose (vertices on pixel centres and edges through them); and a
 * mesh of shared edges drawn once each: no pixel missed or drawn twice. */
#include "unit.h"
#include "soft/soft.h"
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 64

static uint32_t buf[W * H];
static unsigned seed = 11;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

static void setup(void)
{
    soft_target t;
    soft_state s;
    memset(&t, 0, sizeof t);
    t.color = (uint8_t *)buf, t.color_pitch = W * 4, t.format = SOFT_XRGB8888, t.width = W, t.height = H;
    soft_target_set(&t);
    memset(&s, 0, sizeof s);
    s.scissor[2] = W, s.scissor[3] = H, s.color_mask = 15, s.smooth = 0;
    soft_state_set(&s);
}

/* Inside by OpenGL's rule: edge functions at the centre, in 1/16 units, for
 * a triangle wound either way; an edge's own centres count when it is a top
 * edge (horizontal, the triangle below it) or a left one. */
static int inside(const int64_t *X, const int64_t *Y, int px, int py)
{
    int64_t cx = 16 * px + 8, cy = 16 * py + 8, area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    int i, s = area > 0 ? 1 : -1;
    for (i = 0; i < 3; i++) {
        int a = i, b = (i + 1) % 3;
        int64_t e = s * ((X[b] - X[a]) * (cy - Y[a]) - (Y[b] - Y[a]) * (cx - X[a]));
        int64_t dx = s * (X[b] - X[a]), dy = s * (Y[b] - Y[a]);
        /* (with the winding made "area > 0" in y-down screen space, clockwise on the screen, inside is e > 0; on
            the edge (e == 0) the centre counts for a top edge (horizontal, going right: dx > 0) or a left one
            (going up: dy < 0)) */
        if (e < 0 || (e == 0 && !((dy == 0 && dx > 0) || dy < 0)))
            return 0;
    }
    return 1;
}

static int check_one(const int64_t *X, const int64_t *Y)
{
    soft_vertex v[3];
    int i, x, y, bad = 0;
    memset(buf, 0, sizeof buf);
    memset(v, 0, sizeof v);
    for (i = 0; i < 3; i++) {
        v[i].x = (float)X[i] / 16.0f, v[i].y = (float)Y[i] / 16.0f;
        v[i].r = v[i].g = v[i].b = v[i].a = 1.0f;
    }
    soft_triangle(&v[0], &v[1], &v[2]);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if ((buf[y * W + x] != 0) != inside(X, Y, x, y))
                bad++;
    return bad;
}

int unit_main(void)
{
    int n, i, bad = 0, worst = 0;
    setup();

    /* Random triangles, a quarter of their vertices on pixel centres or corners (ties). */
    for (n = 0; n < 4000; n++) {
        int64_t X[3], Y[3];
        for (i = 0; i < 3; i++) {
            if (rnd(4) == 0) {
                X[i] = 16 * (int64_t)rnd(W) + (rnd(2) ? 8 : 0);
                Y[i] = 16 * (int64_t)rnd(H) + (rnd(2) ? 8 : 0);
            } else {
                X[i] = (int64_t)rnd(W * 16), Y[i] = (int64_t)rnd(H * 16);
            }
        }
        if (n % 5 == 0)
            Y[1] = Y[0];                        /* (a horizontal edge) */
        if (n % 7 == 0)
            X[2] = X[1];                        /* (a vertical edge) */
        i = check_one(X, Y);
        bad += i != 0;
        if (i > worst)
            worst = i;
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(worst, 0);

    /* A jittered grid of quads, two triangles each, both windings: every
     * pixel of the grid's inside drawn exactly once. */
    {
        static int64_t gx[9][9], gy[9][9];
        static unsigned count[W * H];
        int j, k, holes = 0, twice = 0;
        memset(count, 0, sizeof count);
        for (j = 0; j < 9; j++)
            for (k = 0; k < 9; k++) {
                gx[j][k] = 16 * (4 + 7 * k) + (k && k < 8 ? (int64_t)rnd(48) - 24 : 0);
                gy[j][k] = 16 * (4 + 7 * j) + (j && j < 8 ? (int64_t)rnd(48) - 24 : 0);
            }
        for (j = 0; j < 8; j++)
            for (k = 0; k < 8; k++) {
                int64_t t1x[3] = { gx[j][k], gx[j][k + 1], gx[j + 1][k] }, t1y[3] = { gy[j][k], gy[j][k + 1], gy[j + 1][k] };
                int64_t t2x[3] = { gx[j][k + 1], gx[j + 1][k], gx[j + 1][k + 1] },
                        t2y[3] = { gy[j][k + 1], gy[j + 1][k], gy[j + 1][k + 1] };
                int x, y;
                check_one(t1x, t1y);
                for (i = 0; i < W * H; i++) count[i] += buf[i] != 0;
                check_one(t2x, t2y);
                for (i = 0; i < W * H; i++) count[i] += buf[i] != 0;
                (void)x, (void)y;
            }
        for (j = 4 + 2; j < 4 + 7 * 8 - 2; j++)        /* (inside the grid's border) */
            for (k = 4 + 2; k < 4 + 7 * 8 - 2; k++) {
                holes += count[j * W + k] == 0;
                twice += count[j * W + k] > 1;
            }
        CHECK_EQ(holes, 0);
        CHECK_EQ(twice, 0);
    }
    return 0;
}
