/* clip.c - polygon clipping in clip coordinates (FR-TS-1).
 *
 * The near and far planes are real clips (GL discards what lies beyond
 * them). In x and y the hardware clips to the scissor itself, so polygons
 * are only cut against a guard band well outside the screen, which keeps
 * vertex coordinates inside the setup's range while leaving nearly every
 * triangle unclipped. Attributes are interpolated linearly in clip space,
 * which is correct before the perspective divide. */
#include "gl_state.h"

enum { P_NEAR, P_FAR, P_LEFT, P_RIGHT, P_BOTTOM, P_TOP, P_COUNT };

static float dist(const dgl_cvtx *v, int plane, float gx, float gy)
{
    switch (plane) {
    case P_NEAR:   return v->z + v->w;
    case P_FAR:    return v->w - v->z;
    case P_LEFT:   return v->x + gx * v->w;
    case P_RIGHT:  return gx * v->w - v->x;
    case P_BOTTOM: return v->y + gy * v->w;
    default:       return gy * v->w - v->y;
    }
}

/* dist() for each plane, written out: the same sums and products. */
unsigned dgl_outcode(const dgl_cvtx *v, float gx, float gy)
{
    unsigned c = 0;
    if (v->z + v->w < 0) c |= 1u << P_NEAR;
    if (v->w - v->z < 0) c |= 1u << P_FAR;
    if (v->x + gx * v->w < 0) c |= 1u << P_LEFT;
    if (gx * v->w - v->x < 0) c |= 1u << P_RIGHT;
    if (v->y + gy * v->w < 0) c |= 1u << P_BOTTOM;
    if (gy * v->w - v->y < 0) c |= 1u << P_TOP;
    return c;
}

static void lerp(dgl_cvtx *o, const dgl_cvtx *a, const dgl_cvtx *b, float t)
{
#define L(f) o->f = a->f + (b->f - a->f) * t
    L(x); L(y); L(z); L(w); L(r); L(g); L(b); L(a); L(s); L(t); L(s1); L(t1); L(eye_d);
#undef L
}

int dgl_clip_polygon(const dgl_cvtx *in, int n, dgl_cvtx *out, float gx, float gy)
{
    dgl_cvtx buf[2][16];
    const dgl_cvtx *src = in;
    int p, i, m, cur = 0;
    unsigned all = 0;
    if (n < 3 || n > 9)
        return 0;
    for (i = 0; i < n; i++)
        all |= dgl_outcode(&in[i], gx, gy);
    if (!all) {
        for (i = 0; i < n; i++)
            out[i] = in[i];
        return n;
    }
    for (p = 0; p < P_COUNT; p++) {
        dgl_cvtx *dst = buf[cur];
        if (!(all & (1u << p)))
            continue;
        m = 0;
        for (i = 0; i < n; i++) {
            const dgl_cvtx *a = &src[i], *b = &src[(i + 1) % n];
            float da = dist(a, p, gx, gy), db = dist(b, p, gx, gy);
            if (da >= 0)
                dst[m++] = *a;
            if ((da >= 0) != (db >= 0) && m < 16)
                lerp(&dst[m++], a, b, da / (da - db));
        }
        n = m;
        if (n < 3)
            return 0;
        src = dst;
        cur ^= 1;
    }
    for (i = 0; i < n; i++)
        out[i] = src[i];
    return n;
}
