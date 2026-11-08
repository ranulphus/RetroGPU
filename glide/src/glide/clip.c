/* clip.c - guDrawTriangleWithClip: clip a triangle to the Glide clip
 * window in software (Sutherland-Hodgman, attributes interpolated
 * linearly in screen space as the hardware would) and draw the fan. */
#include "glide/mg.h"

static void lerp(GrVertex *o, const GrVertex *a, const GrVertex *b, float t)
{
    const float *fa = (const float *)a, *fb = (const float *)b;
    float *fo = (float *)o;
    unsigned i;
    for (i = 0; i < sizeof(GrVertex) / sizeof(float); i++)
        fo[i] = fa[i] + (fb[i] - fa[i]) * t;
}

static int clip_axis(GrVertex *out, const GrVertex *in, int n, int axis, float bound, int keep_greater)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        const GrVertex *a = &in[i], *b = &in[(i + 1) % n];
        float va = axis ? a->y : a->x, vb = axis ? b->y : b->x;
        int ina = keep_greater ? va >= bound : va <= bound;
        int inb = keep_greater ? vb >= bound : vb <= bound;
        if (ina)
            out[m++] = *a;
        if (ina != inb)
            lerp(&out[m++], a, b, (bound - va) / (vb - va));
    }
    return m;
}

GR_ENTRY(void, guDrawTriangleWithClip, (const GrVertex *a, const GrVertex *b, const GrVertex *c))
{
    GrVertex p[16], q[16];
    int n = 3, i, po;
    FPU_ENTER();
    po = PROF_SWITCH(PROF_G_VTX);
    p[0] = *a; p[1] = *b; p[2] = *c;
    n = clip_axis(q, p, n, 0, (float)mg.st.clip_x0, 1);
    n = clip_axis(p, q, n, 0, (float)mg.st.clip_x1, 0);
    n = clip_axis(q, p, n, 1, (float)mg.st.clip_y0, 1);
    n = clip_axis(p, q, n, 1, (float)mg.st.clip_y1, 0);
    for (i = 2; i < n; i++)
        mg_draw_tri(&p[0], &p[i - 1], &p[i]);
    PROF_BACK(po);
    FPU_LEAVE();
}

GR_ENTRY(void, guAADrawTriangleWithClip, (const GrVertex *a, const GrVertex *b, const GrVertex *c))
{
    mg_stub_hit(MGA_API_guAADrawTriangleWithClip);
    guDrawTriangleWithClip(a, b, c);
}

GR_ENTRY(void, guDrawPolygonVertexListWithClip, (int nverts, const GrVertex vlist[]))
{
    static GrVertex p[72], q[72];
    int n, i, po;
    if (nverts < 3)
        return;
    FPU_ENTER();
    po = PROF_SWITCH(PROF_G_VTX);
    if (nverts > 64) {                   /* beyond the work arrays: unclipped fan */
        for (i = 2; i < nverts; i++)
            mg_draw_tri(&vlist[0], &vlist[i - 1], &vlist[i]);
        PROF_BACK(po);
        FPU_LEAVE();
        return;
    }
    for (i = 0; i < nverts; i++)
        p[i] = vlist[i];
    n = clip_axis(q, p, nverts, 0, (float)mg.st.clip_x0, 1);
    n = clip_axis(p, q, n, 0, (float)mg.st.clip_x1, 0);
    n = clip_axis(q, p, n, 1, (float)mg.st.clip_y0, 1);
    n = clip_axis(p, q, n, 1, (float)mg.st.clip_y1, 0);
    for (i = 2; i < n; i++)
        mg_draw_tri(&p[0], &p[i - 1], &p[i]);
    PROF_BACK(po);
    FPU_LEAVE();
}
