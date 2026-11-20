/* share.c - drawing helpers for back ends on the CPU's set-up (share.h): copied from the TNT2 back end
 * (glide/src/backend/tnt2/tnt2.c's snap16, st_of, uses_texture, lod_setup, level_at, mid and split), which keeps
 * its own until the TNT2 is back on a bench to check the switch. */
#include "backend/share.h"
#include "tex/texmgr.h"
#include "mga/fp.h"

int32_t mg_snap16(float v)
{
    int ok;
    int32_t r = mga_trunc_x16(v, &ok);
    double x;
    if (ok)
        return r;
    x = (double)(v * 16.0f);
    if (x > -2147483647.0 && x < 2147483647.0)
        return mga_itrunc(x);
    return (int32_t)(v * 16.0f);
}

void mg_st_of(const GrVertex *v, double *s, double *t)
{
    double q = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v->tmuvtx[0].oow : v->oow;
    if (q == 0)
        q = 1e-9;
    *s = v->tmuvtx[0].sow / q;
    *t = v->tmuvtx[0].tow / q;
}

int mg_uses_texture(void)
{
    const mg_state *s = &mg.st;
    int cf = s->cc_factor & 7, af = s->ac_factor & 7;
    return s->cc_other == GR_COMBINE_OTHER_TEXTURE || s->ac_other == GR_COMBINE_OTHER_TEXTURE ||
           cf == GR_COMBINE_FACTOR_TEXTURE_ALPHA || cf == GR_COMBINE_FACTOR_TEXTURE_RGB ||
           af == GR_COMBINE_FACTOR_TEXTURE_ALPHA || af == GR_COMBINE_FACTOR_TEXTURE_RGB;
}

void mg_lod_setup(mg_lod_planes *L, const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    const GrVertex *v[3];
    double x[3], y[3], S[3], T[3], Q[3], area, k = (double)(256 >> tmu0.large) / 256.0;
    int i;
    v[0] = a; v[1] = b; v[2] = c;
    for (i = 0; i < 3; i++) {
        x[i] = v[i]->x; y[i] = v[i]->y;
        Q[i] = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v[i]->tmuvtx[0].oow : v[i]->oow;
        S[i] = v[i]->tmuvtx[0].sow * k;
        T[i] = v[i]->tmuvtx[0].tow * k;
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    L->valid = area != 0;
    if (!L->valid)
        return;
#define PL(A, o) do { \
        L->o##x = ((A[1] - A[0]) * (y[2] - y[0]) - (A[2] - A[0]) * (y[1] - y[0])) / area; \
        L->o##y = ((x[1] - x[0]) * (A[2] - A[0]) - (x[2] - x[0]) * (A[1] - A[0])) / area; \
        L->o##0 = A[0]; } while (0)
    PL(S, s); PL(T, t); PL(Q, q);
#undef PL
    L->x0 = x[0]; L->y0 = y[0];
}

GrLOD_t mg_level_at(const mg_lod_planes *L, double px, double py)
{
    double dx = px - L->x0, dy = py - L->y0, S, T, Q, u, v, gx, gy, g;
    int q, l;
    if (tmu0.mipmap == GR_MIPMAP_DISABLE || !L->valid)
        return tmu0.large;
    S = L->s0 + L->sx * dx + L->sy * dy, T = L->t0 + L->tx * dx + L->ty * dy, Q = L->q0 + L->qx * dx + L->qy * dy;
    if (Q <= 1e-12)
        return tmu0.large;
    u = S / Q, v = T / Q;
    gx = (L->sx - u * L->qx) * (L->sx - u * L->qx) + (L->tx - v * L->qx) * (L->tx - v * L->qx);
    gy = (L->sy - u * L->qy) * (L->sy - u * L->qy) + (L->ty - v * L->qy) * (L->ty - v * L->qy);
    g = (gx > gy ? gx : gy) / (Q * Q);
    if (!(g > 0))
        return tmu0.large;
    /* quarter levels: floor(4 * log2(sqrt g)) = floor(2 * log2 g), and the bias's */
    q = mga_ifloor(2.0 * mg_ln(g) / 0.69314718055994531) +
        (int)(tmu0.lod_bias * 4.0f + (tmu0.lod_bias >= 0 ? 0.5f : -0.5f));
    l = (q >= 0 ? q / 4 : -((-q + 3) / 4)) + tmu0.large;
    return (GrLOD_t)(l < tmu0.large ? tmu0.large : l > tmu0.small ? tmu0.small : l);
}

static void mid(GrVertex *o, const GrVertex *a, const GrVertex *b)
{
    const float *p = (const float *)a, *q = (const float *)b;
    float *r = (float *)o;
    unsigned i;
    for (i = 0; i < sizeof *o / sizeof(float); i++)
        r[i] = (p[i] + q[i]) * 0.5f;
}

static void split(const GrVertex *a, const GrVertex *b, const GrVertex *c, int depth, int fogged, int mip,
                  mg_tri_level_fn draw)
{
    GrVertex ab, bc, ca;
    mg_lod_planes L;
    GrLOD_t la = tmu0.large;
    int cut = 0;
    if (mip) {
        mg_lod_setup(&L, a, b, c);
        la = mg_level_at(&L, a->x, a->y);
        cut = mg_level_at(&L, b->x, b->y) != la || mg_level_at(&L, c->x, c->y) != la;
    }
    if (fogged && !cut) {
        double fa = mg_fog_vertex(a), fb = mg_fog_vertex(b), fc = mg_fog_vertex(c), lo, hi;
        lo = fa < fb ? fa : fb, lo = fc < lo ? fc : lo;
        hi = fa > fb ? fa : fb, hi = fc > hi ? fc : hi;
        cut = hi - lo > 16.0;
    }
    if (!cut || depth >= 5) {
        if (mip && cut)
            la = mg_level_at(&L, (a->x + b->x + c->x) / 3.0, (a->y + b->y + c->y) / 3.0);
        draw(a, b, c, la);
        return;
    }
    mid(&ab, a, b), mid(&bc, b, c), mid(&ca, c, a);
    split(a, &ab, &ca, depth + 1, fogged, mip, draw);
    split(&ab, b, &bc, depth + 1, fogged, mip, draw);
    split(&ca, &bc, c, depth + 1, fogged, mip, draw);
    split(&ab, &bc, &ca, depth + 1, fogged, mip, draw);
}

void mg_split(const GrVertex *a, const GrVertex *b, const GrVertex *c, int fogged, int mip, mg_tri_level_fn draw)
{
    split(a, b, c, 0, fogged, mip, draw);
}
