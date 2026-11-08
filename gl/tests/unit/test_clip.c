/* test_clip.c - polygon clipping against near/far and the guard band. */
#include "unit.h"
#include "../../src/gl/gl_state.h"
#include <string.h>

static dgl_cvtx v(float x, float y, float z, float w, float r)
{
    dgl_cvtx o;
    memset(&o, 0, sizeof o);
    o.x = x; o.y = y; o.z = z; o.w = w; o.r = r;
    return o;
}

void unit_run(void)
{
    dgl_cvtx in[3], out[16];
    int n, i;
    /* Inside: unchanged. */
    in[0] = v(0, 0, 0, 1, 0); in[1] = v(0.5f, 0, 0, 1, 1); in[2] = v(0, 0.5f, 0, 1, 0.5f);
    n = dgl_clip_polygon(in, 3, out, 4, 4);
    CHECK(n == 3);
    CHECK(!memcmp(out, in, sizeof in));
    /* Crossing the near plane (z = -w): a quad remains, all outputs on or in front. */
    in[0] = v(0, 0, -2, 1, 0); in[1] = v(0.5f, 0, 0.5f, 1, 1); in[2] = v(0, 0.5f, 0.5f, 1, 1);
    n = dgl_clip_polygon(in, 3, out, 4, 4);
    CHECK(n == 4);
    for (i = 0; i < n; i++)
        CHECK(out[i].z + out[i].w >= -1e-5f);
    /* The new vertices carry interpolated attributes (r goes 1 -> 0 over d = 1.5 -> -1: 0.4 at the plane). */
    CHECK_NEAR(out[n - 1].r, 0.4, 1e-5);
    /* Entirely behind the viewer: rejected. */
    in[0] = v(0, 0, -3, 1, 0); in[1] = v(1, 0, -3, 1, 0); in[2] = v(0, 1, -3, 1, 0);
    CHECK(dgl_clip_polygon(in, 3, out, 4, 4) == 0);
    /* Beyond the guard band in x: cut to it. */
    in[0] = v(-10, 0, 0, 1, 0); in[1] = v(10, 0, 0, 1, 0); in[2] = v(0, 1, 0, 1, 0);
    n = dgl_clip_polygon(in, 3, out, 4, 4);
    CHECK(n >= 3);
    for (i = 0; i < n; i++)
        CHECK(out[i].x >= -4.0001f && out[i].x <= 4.0001f);
    /* Outcodes. */
    CHECK(dgl_outcode(&in[0], 4, 4) != 0);
    CHECK(dgl_outcode(&in[2], 4, 4) == 0);
}
