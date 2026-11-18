/* trap.c - triangle setup for the MGA trapezoid engine.
 *
 * Coverage: a pixel (x, y) is drawn when its centre (x+0.5, y+0.5) lies
 * inside the triangle, with the top and left edges inclusive (the top-left
 * rule). Rows and edge positions are computed exactly in integers from
 * 1/16-pixel vertices; the engine's edge DDA
 *     while (AR1 < 0) { AR1 += AR0; x += dir; }  AR1 += AR2;
 * then reproduces the same columns row after row. */
#include "mga/setup.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/fp.h"
#include "mga/setupconv.h"
#include "mga/prof.h"

/* The planes and texture values are set and used under the same flags,
 * which GCC's -O2 does not always see once they are inlined. */
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

mga_setup_stats setup_stats;

/* What setup_triangle last wrote to registers that keep their value across
 * draws, so an equal value is not written again (the start values, which
 * the engine advances, are always written). On the G400 the texture
 * registers exist per map: a write while tmap0dis is clear reaches both
 * (bank 0 and 1), the map-1 step's only map 1. setup.h says who clears
 * them. tests/unit/test_setupgold.c checks the register state at every
 * draw against the setup before the shadows. */
enum {
    SH_DWG = 1u << 0, SH_Z = 1u << 1, SH_COL = 1u << 2, SH_ALPHA = 1u << 3, SH_FOG = 1u << 4,
    SH_SPEC = 1u << 5, SH_WH0 = 1u << 6, SH_WH1 = 1u << 7, SH_TM0 = 1u << 8, SH_TM1 = 1u << 9
};
#define DWG_WRITE_TIME (DWG_SOLID | DWG_ARZERO | DWG_SGNZERO | DWG_SHFTZERO)  /* act when DWGCTL is written */
static struct {
    uint32_t dwgctl;
    int      zn;                       /* 2: DR2, DR3; 4: the Z32 LSB/MSB pairs (the same registers) */
    uint32_t z[4], col[6], alpha[2], fog[2], spec[6];
    uint32_t wh[2][2], tm[2][6];       /* [map] TEXWIDTH, TEXHEIGHT; TMR0-5 */
} sh;

typedef char sh_dwg_is_bit_0[SH_DWG == SETUP_VALID_DWG ? 1 : -1];   /* (setup_forget_dwgctl, in engine.c) */

/* DWGCTL for a draw that doesn't come through setup_triangle (the WARP's),
 * under the same shadow and the same rule for bits that act when written. */
void setup_dwgctl(uint32_t dwgctl)
{
    mga_warp_dwgctl = dwgctl;          /* (the barrier's, engine.c) */
    if ((setup_valid & SH_DWG) && sh.dwgctl == dwgctl && !(dwgctl & DWG_WRITE_TIME))
        return;
    fifo_reserve(1);
    MGA_WR32(MGAREG_DWGCTL, dwgctl);
    sh.dwgctl = dwgctl;
    setup_valid |= SH_DWG;
}

static int same(const uint32_t *a, const uint32_t *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void keep(uint32_t *dst, const uint32_t *src, int n)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

/* Does a broadcast write of these texture values change anything? (Both
 * maps on the G400, the one map elsewhere.) */
static int tex_changes(unsigned bit0, unsigned bit1, const uint32_t *v0, const uint32_t *v1, const uint32_t *val, int n)
{
    if (!(setup_valid & bit0) || !same(v0, val, n))
        return 1;
    return mga.has_dual_tex && (!(setup_valid & bit1) || !same(v1, val, n));
}

static int64_t floordiv64(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* First covered row for an edge starting at Y (1/16 px): ceil((Y-8)/16),
 * i.e. floor((Y + 7) / 16), an arithmetic shift while Y + 7 fits. */
static int32_t first_row(int32_t Y)
{
    if (Y <= 0x7FFFFFF8)
        return (Y + 7) >> 4;
    return (int32_t)floordiv64((int64_t)Y - 8 + 15, 16);
}

/* floor(a / b) for b > 0, and *r = a - q * b in [0, b): for |a| < 2^52 and
 * a quotient inside int32 (the edges' sizes) a double quotient, at most one
 * off, corrected exactly in integers; otherwise the 64-bit division (a
 * library call on the x87 compilers). */
static int64_t floordiv_pos(int64_t a, int32_t b, int64_t *r)
{
    double qd = (double)a / (double)b;
    if (a > -4503599627370496LL && a < 4503599627370496LL && qd > -2147483645.0 && qd < 2147483645.0) {
        int32_t q = mga_ifloor(qd);
        int64_t rem = a - (int64_t)q * b;
        if (rem < 0) { q--; rem += b; }
        else if (rem >= b) { q++; rem -= b; }
        if (rem >= 0 && rem < b) {
            *r = rem;
            return q;
        }
    }
    {
        int64_t q = floordiv64(a, b);
        *r = a - q * b;
        return q;
    }
}

/* An edge whose first covered column on row k (k = 0 at the trapezoid's
 * first row) is x_k = floor((N0 + S*k) / D), D > 0. The engine walks it
 * with: while (AR1 < 0) { AR1 += AR0; x += dir; } AR1 += AR2. */
static void edge_set(int64_t x0, int64_t r0, int64_t S, int64_t D, mga_edge *e)
{
    e->x = (int32_t)x0;
    e->neg = S < 0;
    e->ar_step = (int32_t)D;
    e->ar_dec = (int32_t)(S < 0 ? S : -S);
    e->ar_err = (int32_t)((S < 0 ? r0 : (D - 1 - r0)) + e->ar_dec);
}

/* The chip keeps an edge's D, -|S| and its error term, which stays in
 * [-|S|, D), in AR fields of mga.ar_bits bits, signed (18 on the G100 and
 * G200, 22 on the G400; 0: no limit): they fit when D < 2^(b-1) and
 * |S| <= 2^(b-1). */
static int32_t ar_lim(void)
{
    return mga.ar_bits ? (int32_t)1 << (mga.ar_bits - 1) : 0x7FFFFFFF;
}

static int edge_fits(const mga_edge *e, int32_t lim)
{
    return e->ar_step < lim && e->ar_dec >= -lim;
}

/* An edge that does not fit loses factors of 2 common to S and D, with
 * r0 = N0 - x0*D (recovered from the error term) floored: every x_k stays
 * the same (floor(floor(a/2) / b) = floor(a / 2b)). Exact edges' S and D
 * are multiples of 16 (1/16 pixel twice), so they fit up to 8191 pixels. */
static void edge_reduce(mga_edge *e, int32_t lim)
{
    int32_t d = e->ar_step, s = e->neg ? e->ar_dec : -e->ar_dec;
    int32_t r = e->neg ? e->ar_err - e->ar_dec : d - 1 - (e->ar_err - e->ar_dec);
    while ((d >= lim || s > lim || s < -lim) && !((d | s) & 1)) {
        d >>= 1;
        s /= 2;
        r >>= 1;
    }
    e->ar_step = d;
    e->ar_dec = s < 0 ? s : -s;
    e->ar_err = (s < 0 ? r : d - 1 - r) + e->ar_dec;
}

static void edge_from_line(int64_t N0, int64_t S, int64_t D, mga_edge *e)
{
    int64_t x0, r0;
    if (D > 0 && D <= 0x7FFFFFFF)
        x0 = floordiv_pos(N0, (int32_t)D, &r0);
    else {
        x0 = floordiv64(N0, D);
        r0 = N0 - x0 * D;                          /* 0 <= r0 < D */
    }
    edge_set(x0, r0, S, D, e);
}

/* An edge's covered column on row k, from its AR terms (as edge_reduce
 * recovers them): x_k = x + floor((r0 + S*k) / D). */
static void edge_terms(const mga_edge *e, int64_t *s, int64_t *d, int64_t *r)
{
    *d = e->ar_step;
    *s = e->neg ? e->ar_dec : -e->ar_dec;
    *r = e->neg ? e->ar_err - e->ar_dec : *d - 1 - (e->ar_err - e->ar_dec);
}

static int64_t edge_x_at(const mga_edge *e, int32_t k)
{
    int64_t s, d, r;
    edge_terms(e, &s, &d, &r);
    return e->x + floordiv64(r + s * k, d);
}

/* Whether a trapezoid of n rows between edges l and r (columns [l, r) on
 * each row, as the engine walks them) draws a pixel in columns [cx0, cx1):
 * its first or last row does (the usual answer, at most two divisions);
 * neither edge reaches inside (the right edge's furthest column left of
 * cx0, or the left edge's right of cx1: both edges are monotonic) does
 * not; otherwise row by row, as the engine steps. */
static int trap_covers(const mga_edge *l, const mga_edge *r, int32_t n, int cx0, int cx1)
{
    int64_t xl = l->x, xr = r->x, ln, rn, sl, dl, rl, sr, dr, rr, ql, qr;
    int32_t k;
    if ((xl > cx0 ? xl : cx0) < (xr < cx1 ? xr : cx1))
        return 1;
    if (n <= 1)
        return 0;
    ln = edge_x_at(l, n - 1);
    rn = edge_x_at(r, n - 1);
    if ((ln > cx0 ? ln : cx0) < (rn < cx1 ? rn : cx1))
        return 1;
    if ((xr > rn ? xr : rn) <= cx0 || (xl < ln ? xl : ln) >= cx1)
        return 0;
    edge_terms(l, &sl, &dl, &rl);
    edge_terms(r, &sr, &dr, &rr);
    ql = floordiv64(sl, dl); sl -= ql * dl;        /* per row: q whole columns and s/D more */
    qr = floordiv64(sr, dr); sr -= qr * dr;
    for (k = 1; k < n - 1; k++) {
        xl += ql; rl += sl;
        if (rl >= dl) { rl -= dl; xl++; }
        xr += qr; rr += sr;
        if (rr >= dr) { rr -= dr; xr++; }
        if ((xl > cx0 ? xl : cx0) < (xr < cx1 ? xr : cx1))
            return 1;
    }
    return 0;
}

/* Exact mode: column covered when its centre is inside (top-left rule). */
static void edge_exact(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb, int32_t ys, mga_edge *e)
{
    int64_t dY = (int64_t)Yb - Ya, dX = (int64_t)Xb - Xa;
    int64_t P = (int64_t)Xa * dY + ((int64_t)16 * ys + 8 - Ya) * dX;    /* X(ys) * dY */
    /* ceil(X/16 - 1/2) = floor((P - 8dY + 16dY - 1) / 16dY) */
    edge_from_line(P - 8 * dY + 16 * dY - 1, 16 * dX, 16 * dY, e);
}

void setup_edge(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb, int32_t ys, mga_edge *e)
{
    edge_exact(Xa, Ya, Xb, Yb, ys, e);
    edge_reduce(e, ar_lim());
}

/* The Voodoo's 16.16 slope, truncated toward zero: (dX << 16) / dY with
 * dY > 0, from a double estimate corrected in integers (as floordiv_pos). */
static int64_t voodoo_slope(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb)
{
    int64_t n = ((int64_t)Xb - Xa) << 16, dY = (int64_t)Yb - Ya;
    if (dY > 0 && n > -4503599627370496LL && n < 4503599627370496LL) {
        int64_t q = mga_itrunc64((double)n / (double)dY), rem = n - q * dY;
        if (n >= 0) {
            if (rem < 0) { q--; rem += dY; }
            else if (rem >= dY) { q++; rem -= dY; }
            if (rem >= 0 && rem < dY)
                return q;
        } else {
            if (rem > 0) { q++; rem -= dY; }
            else if (rem <= -dY) { q--; rem += dY; }
            if (rem <= 0 && rem > -dY)
                return q;
        }
    }
    return n / dY;                                  /* truncates toward zero */
}

/* A Voodoo edge with its slope d known; x0 = floor(N0 / 65536) is a shift
 * and prod >> 4 is floor(prod / 16) (arithmetic shifts on every target). */
static void edge_voodoo(int32_t Xa, int32_t Ya, int64_t d, int32_t ys, mga_edge *e)
{
    int64_t c0 = (int64_t)16 * ys + 8 - Ya;
    int64_t prod = d * c0;
    int64_t N0 = ((int64_t)Xa << 12) + (prod >> 4) + 0x7000;
    int64_t x0 = N0 >> 16;
    edge_set(x0, N0 - x0 * 65536, d, 65536, e);
}

/* Voodoo mode: the Voodoo evaluates each edge in 16.16 fixed point with a
 * truncated slope and takes columns [floor(xl + 7/16), floor(xr + 7/16)),
 * sampling at x + 9/16. Matching it keeps shared edges identical to the
 * reference frames. */
void setup_edge_voodoo(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb, int32_t ys, mga_edge *e)
{
    edge_voodoo(Xa, Ya, voodoo_slope(Xa, Ya, Xb, Yb), ys, e);
    edge_reduce(e, ar_lim());
}

/* Plane A(x, y) = A0 + dx*(x - x0) + dy*(y - y0) in pixel units. */
typedef struct { double a0, dx, dy; } plane;

/* The sorted triangle in pixels relative to v0, computed once for every
 * plane (multiples of 1/16: exact). */
typedef struct { double x0, y0, x1, y1, x2, y2, inv; } geom;

static void make_plane(plane *p, const geom *g, double a0, double a1, double a2)
{
    double d1 = a1 - a0, d2 = a2 - a0;
    p->a0 = a0;
    p->dx = (d1 * g->y2 - d2 * g->y1) * g->inv;
    p->dy = (g->x1 * d2 - g->x2 * d1) * g->inv;
}

/* The plane at the pixel centre (x0 + ex, y0 + ey); ex and ey are exact. */
static double eval(const plane *p, double ex, double ey)
{
    return p->a0 + p->dx * ex + p->dy * ey;
}

/* The texture prescale K = 2^k: the largest k in [-8, 15] with
 * ms * 2^k < 2047 and mq * 2^k < 32767 (TMR6 12.20, TMR8 16.16), from exact
 * thresholds (scaling by 2^k is exact, so the comparisons are those of
 * multiplying ms and mq through, NaN and infinity included). */
static int prescale(double ms, double mq, double *K)
{
    static double ts[24], tq[24], tk[24];
    int k;
    if (tk[0] == 0.0) {
        for (k = -8; k <= 15; k++) {
            double m = 1.0;
            int j;
            for (j = 0; j < (k < 0 ? -k : k); j++)
                m *= 2.0;
            tk[k + 8] = k < 0 ? 1.0 / m : m;
            ts[k + 8] = 2047.0 / tk[k + 8];
            tq[k + 8] = 32767.0 / tk[k + 8];
        }
    }
    k = 15;
    while (k > -8 && (ms >= ts[k + 8] || mq >= tq[k + 8]))
        k--;
    *K = tk[k + 8];
    return k;
}

/* Each register write takes its FIFO slot as it goes (fifo_need is inline):
 * a FIFOSTATUS read happens only when the slots the last one found free
 * are used up, never because a group did not fit what was left. */
#define WR(reg, val) do { fifo_need(1); MGA_WR32((reg), (val)); } while (0)

static void split4(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx);

#ifdef MGA_PROF
/* The setup in three stages (planes, increments, trapezoids), returning to
 * the caller's stage. */
static void setup_tri(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx);
void setup_triangle(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx)
{
    int o = PROF_SWITCH(PROF_SPLANE);
    setup_tri(a, b, c, ctx);
    PROF_BACK(o);
}
#  define SETUP_TRI static void setup_tri
#  define SETUP_SELF setup_tri
#else
#  define SETUP_TRI void setup_triangle
#  define SETUP_SELF setup_triangle
#endif

/* The midpoint of two vertices: position rounded down to 1/16 pixel (the
 * same whichever comes first), attributes averaged; all are linear in
 * screen space (s, t and q are s/w, t/w, 1/w). */
static void midpoint(const mga_svtx *p, const mga_svtx *q, mga_svtx *m)
{
    m->X16 = (int32_t)(((int64_t)p->X16 + q->X16) >> 1);
    m->Y16 = (int32_t)(((int64_t)p->Y16 + q->Y16) >> 1);
    m->z = (p->z + q->z) * 0.5;
    m->r = (p->r + q->r) * 0.5f; m->g = (p->g + q->g) * 0.5f;
    m->b = (p->b + q->b) * 0.5f; m->a = (p->a + q->a) * 0.5f;
    m->fog = (p->fog + q->fog) * 0.5f;
    m->sr = (p->sr + q->sr) * 0.5f; m->sg = (p->sg + q->sg) * 0.5f; m->sb = (p->sb + q->sb) * 0.5f;
    m->s = (p->s + q->s) * 0.5f; m->t = (p->t + q->t) * 0.5f; m->q = (p->q + q->q) * 0.5f;
    m->s1 = (p->s1 + q->s1) * 0.5f; m->t1 = (p->t1 + q->t1) * 0.5f;
}

static void split4(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx)
{
    mga_svtx ab, bc, ca;
    midpoint(a, b, &ab);
    midpoint(b, c, &bc);
    midpoint(c, a, &ca);
    setup_stats.splits++;
    SETUP_SELF(a, &ab, &ca, ctx);
    SETUP_SELF(&ab, b, &bc, ctx);
    SETUP_SELF(&ca, &bc, c, ctx);
    SETUP_SELF(&ab, &bc, &ca, ctx);
}

SETUP_TRI(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx)
{
    if (setup_stale) {                 /* the WARP wrote the registers since */
        setup_valid = 0;
        setup_stale = 0;
    }
    const mga_svtx *v[3], *t;
    int64_t area2, cross;
    int mid_right;
    double inv;
    plane pz, pr, pg, pb, pa, pf, ps, pt, pq, psr, psg, psb, ps1, pt1, pq1;
    geom g;
    int64_t d_long = 0;
    double K = 1.0;
    int map1_sizes = 1;
    int32_t y_top, y_mid, y_bot, part;
    int reduce, stall_test = 0, cx0 = 0, cx1 = 0;
    uint32_t flags = ctx->flags;

    area2 = (int64_t)(b->X16 - a->X16) * (c->Y16 - a->Y16) - (int64_t)(c->X16 - a->X16) * (b->Y16 - a->Y16);
    if (area2 == 0) {
        setup_stats.culled_empty++;
        return;
    }
    /* Sort top to bottom (ties by x) so v0 is top, v2 bottom. */
    v[0] = a; v[1] = b; v[2] = c;
#define LESS(p, q) ((p)->Y16 < (q)->Y16 || ((p)->Y16 == (q)->Y16 && (p)->X16 < (q)->X16))
    if (LESS(v[1], v[0])) { t = v[0]; v[0] = v[1]; v[1] = t; }
    if (LESS(v[2], v[1])) { t = v[1]; v[1] = v[2]; v[2] = t; }
    if (LESS(v[1], v[0])) { t = v[0]; v[0] = v[1]; v[1] = t; }
#undef LESS
    cross = (int64_t)(v[2]->X16 - v[0]->X16) * (v[1]->Y16 - v[0]->Y16) -
            (int64_t)(v[2]->Y16 - v[0]->Y16) * (v[1]->X16 - v[0]->X16);
    if (cross == 0) {
        setup_stats.culled_empty++;
        return;
    }
    mid_right = cross < 0;
    {
        /* Exact edges have S = 16 dX16 and D = 16 dY16: within 1/16 of the
         * AR limit, every edge fits as it is (the usual case: nothing to
         * do); within the limit, it fits divided by 16; beyond it (over
         * 8191 pixels on the G100/G200, only from callers that do not clip:
         * DOS-GL's guard band keeps edges within 4000 pixels, Glide's
         * vertices within about 2048), the triangle is drawn as four, split
         * at its edges' midpoints. */
        int32_t x0 = v[0]->X16, x1 = v[1]->X16, x2 = v[2]->X16, xmin, xmax, ey, ex, lim;
        xmin = x0 < x1 ? x0 : x1;
        xmax = x0 < x1 ? x1 : x0;
        if (x2 < xmin) xmin = x2;
        if (x2 > xmax) xmax = x2;
        ey = v[2]->Y16 - v[0]->Y16;
        ex = xmax - xmin;
        lim = ar_lim();
        if (ey >= lim || ex > lim) {
            split4(v[0], v[1], v[2], ctx);
            return;
        }
        reduce = ey >= lim >> 4 || ex > lim >> 4;
    }
    setup_stats.tris++;

    /* Plane gradients in pixel units (area of the sorted triangle). */
    {
        double x1 = (v[1]->X16 - v[0]->X16) / 16.0, y1 = (v[1]->Y16 - v[0]->Y16) / 16.0;
        double x2 = (v[2]->X16 - v[0]->X16) / 16.0, y2 = (v[2]->Y16 - v[0]->Y16) / 16.0;
        inv = 1.0 / (x1 * y2 - x2 * y1);
    }
    g.x0 = v[0]->X16 / 16.0; g.y0 = v[0]->Y16 / 16.0;
    g.x1 = v[1]->X16 / 16.0 - g.x0; g.y1 = v[1]->Y16 / 16.0 - g.y0;
    g.x2 = v[2]->X16 / 16.0 - g.x0; g.y2 = v[2]->Y16 / 16.0 - g.y0;
    g.inv = inv;
    if (flags & MGA_S_Z)
        make_plane(&pz, &g, v[0]->z, v[1]->z, v[2]->z);
    if (flags & MGA_S_COLOR) {
        make_plane(&pr, &g, v[0]->r, v[1]->r, v[2]->r);
        make_plane(&pg, &g, v[0]->g, v[1]->g, v[2]->g);
        make_plane(&pb, &g, v[0]->b, v[1]->b, v[2]->b);
    }
    if (flags & MGA_S_ALPHA)
        make_plane(&pa, &g, v[0]->a, v[1]->a, v[2]->a);
    if (flags & MGA_S_FOG)
        make_plane(&pf, &g, v[0]->fog, v[1]->fog, v[2]->fog);
    if (flags & MGA_S_SPEC) {
        make_plane(&psr, &g, v[0]->sr, v[1]->sr, v[2]->sr);
        make_plane(&psg, &g, v[0]->sg, v[1]->sg, v[2]->sg);
        make_plane(&psb, &g, v[0]->sb, v[1]->sb, v[2]->sb);
    }
    if (flags & MGA_S_TEX) {
        /* Prescale K = 2^k keeps the most bits through the engine's
         * truncation of s/w before the divide by q (TMR6 12.20 < 2048,
         * TMR8 16.16 < 32768). */
        double ms = 0, mq = 0;
        int i;
        for (i = 0; i < 3; i++) {
            double as = v[i]->s < 0 ? -v[i]->s : v[i]->s, at = v[i]->t < 0 ? -v[i]->t : v[i]->t;
            if (as > ms) ms = as;
            if (at > ms) ms = at;
            if (v[i]->q > mq) mq = v[i]->q;
        }
        prescale(ms, mq, &K);
        make_plane(&ps, &g, v[0]->s * K, v[1]->s * K, v[2]->s * K);
        make_plane(&pt, &g, v[0]->t * K, v[1]->t * K, v[2]->t * K);
        make_plane(&pq, &g, v[0]->q * K, v[1]->q * K, v[2]->q * K);
    }
    if (flags & MGA_S_TEX2) {
        /* Map 1 (G400) gets its own prescale: its coordinates can span far
         * more (lightmaps) or less than map 0's; q is shared. */
        double ms = 0, mq = 0, K1;
        int i;
        for (i = 0; i < 3; i++) {
            double as = v[i]->s1 < 0 ? -v[i]->s1 : v[i]->s1, at = v[i]->t1 < 0 ? -v[i]->t1 : v[i]->t1;
            if (as > ms) ms = as;
            if (at > ms) ms = at;
            if (v[i]->q > mq) mq = v[i]->q;
        }
        prescale(ms, mq, &K1);
        make_plane(&ps1, &g, v[0]->s1 * K1, v[1]->s1 * K1, v[2]->s1 * K1);
        make_plane(&pt1, &g, v[0]->t1 * K1, v[1]->t1 * K1, v[2]->t1 * K1);
        make_plane(&pq1, &g, v[0]->q * K1, v[1]->q * K1, v[2]->q * K1);
    }

    /* Per-triangle increments: values a register already holds are left
     * out (the shadows above); otherwise the writes and their order are
     * those of the full setup. */
    PROF_SWITCH(PROF_SINC);
    {
        uint32_t zi[4], ci[6], ai[2], fi[2], si[6], wh[2], tm[6];
        int zn = 0, w_dwg, w_z = 0, w_c, w_a = 0, w_f = 0, w_s = 0, w_wh = 0, w_tm = 0;
        w_dwg = !(setup_valid & SH_DWG) || sh.dwgctl != ctx->dwgctl || (ctx->dwgctl & DWG_WRITE_TIME);
        if (flags & MGA_S_Z) {
            if (flags & MGA_S_Z32) {
                int64_t zx = z32_inc(pz.dx), zy = z32_inc(pz.dy);
                zi[0] = (uint32_t)zx;
                zi[1] = (uint32_t)((uint64_t)zx >> 32) & 0xFFFF;
                zi[2] = (uint32_t)zy;
                zi[3] = (uint32_t)((uint64_t)zy >> 32) & 0xFFFF;
                zn = 4;
            } else {
                zi[0] = (uint32_t)fx(pz.dx, 32768.0);
                zi[1] = (uint32_t)fx(pz.dy, 32768.0);
                zn = 2;
            }
            w_z = !(setup_valid & SH_Z) || sh.zn != zn || !same(sh.z, zi, zn);
        }
        if (flags & MGA_S_COLOR) {
            ci[0] = (uint32_t)fx(pr.dx, 32768.0);
            ci[1] = (uint32_t)fx(pr.dy, 32768.0);
            ci[2] = (uint32_t)fx(pg.dx, 32768.0);
            ci[3] = (uint32_t)fx(pg.dy, 32768.0);
            ci[4] = (uint32_t)fx(pb.dx, 32768.0);
            ci[5] = (uint32_t)fx(pb.dy, 32768.0);
        } else
            ci[0] = ci[1] = ci[2] = ci[3] = ci[4] = ci[5] = 0;
        w_c = !(setup_valid & SH_COL) || !same(sh.col, ci, 6);
        if (flags & MGA_S_ALPHA) {
            ai[0] = (uint32_t)fx(pa.dx, 32768.0) & 0xFFFFFF;
            ai[1] = (uint32_t)fx(pa.dy, 32768.0) & 0xFFFFFF;
            w_a = !(setup_valid & SH_ALPHA) || !same(sh.alpha, ai, 2);
        }
        if (flags & MGA_S_FOG) {
            fi[0] = (uint32_t)fx(pf.dx, 32768.0) & 0xFFFFFF;
            fi[1] = (uint32_t)fx(pf.dy, 32768.0) & 0xFFFFFF;
            w_f = !(setup_valid & SH_FOG) || !same(sh.fog, fi, 2);
        }
        if (flags & MGA_S_SPEC) {
            si[0] = (uint32_t)fx(psr.dx, 32768.0) & 0xFFFFFF;
            si[1] = (uint32_t)fx(psr.dy, 32768.0) & 0xFFFFFF;
            si[2] = (uint32_t)fx(psg.dx, 32768.0) & 0xFFFFFF;
            si[3] = (uint32_t)fx(psg.dy, 32768.0) & 0xFFFFFF;
            si[4] = (uint32_t)fx(psb.dx, 32768.0) & 0xFFFFFF;
            si[5] = (uint32_t)fx(psb.dy, 32768.0) & 0xFFFFFF;
            w_s = !(setup_valid & SH_SPEC) || !same(sh.spec, si, 6);
        }
        if (flags & MGA_S_TEX) {
            /* rfw = 8 - log2(size), whatever the prescale: the
             * specification's 8 - log2 - (q fraction bits - 16) would take
             * k off, but on a real G400 that misplaced the texels of
             * prescaled triangles in perspective (bilinear lightmaps by
             * whole faces; DOS-GL TEXEXP part 2, 2026-10-27), and with
             * 8 - log2 they match the WARP's, Mesa's and 86Box's (which
             * ignores rfw). The sizes then change only with the texture. */
            int tw = ctx->tex_tw, th = ctx->tex_th;
            wh[0] = TEXWH(tw, 8 - tw, (1u << tw) - 1);
            wh[1] = TEXWH(th, 8 - th, (1u << th) - 1);
            tm[0] = (uint32_t)fx(ps.dx, 1048576.0);
            tm[1] = (uint32_t)fx(ps.dy, 1048576.0);
            tm[2] = (uint32_t)fx(pt.dx, 1048576.0);
            tm[3] = (uint32_t)fx(pt.dy, 1048576.0);
            tm[4] = (uint32_t)fx(pq.dx, 65536.0);
            tm[5] = (uint32_t)fx(pq.dy, 65536.0);
            w_wh = tex_changes(SH_WH0, SH_WH1, sh.wh[0], sh.wh[1], wh, 2);
            w_tm = tex_changes(SH_TM0, SH_TM1, sh.tm[0], sh.tm[1], tm, 6);
        }

        if (w_dwg) {
            WR(MGAREG_DWGCTL, ctx->dwgctl);
            sh.dwgctl = ctx->dwgctl;
            setup_valid |= SH_DWG;
        }
        if (w_z) {
            if (zn == 4) {
                WR(MGAREG_DR2_Z32LSB, zi[0]);
                WR(MGAREG_DR2_Z32MSB, zi[1]);
                WR(MGAREG_DR3_Z32LSB, zi[2]);
                WR(MGAREG_DR3_Z32MSB, zi[3]);
            } else {
                WR(MGAREG_DR2, zi[0]);
                WR(MGAREG_DR3, zi[1]);
            }
            keep(sh.z, zi, zn);
            sh.zn = zn;
            setup_valid |= SH_Z;
        }
        if (w_c) {
            WR(MGAREG_DR6, ci[0]);
            WR(MGAREG_DR7, ci[1]);
            WR(MGAREG_DR10, ci[2]);
            WR(MGAREG_DR11, ci[3]);
            WR(MGAREG_DR14, ci[4]);
            WR(MGAREG_DR15, ci[5]);
            keep(sh.col, ci, 6);
            setup_valid |= SH_COL;
        }
        if (w_a) {
            WR(MGAREG_ALPHAXINC, ai[0]);
            WR(MGAREG_ALPHAYINC, ai[1]);
            keep(sh.alpha, ai, 2);
            setup_valid |= SH_ALPHA;
        }
        if (w_f) {
            WR(MGAREG_FOGXINC, fi[0]);
            WR(MGAREG_FOGYINC, fi[1]);
            keep(sh.fog, fi, 2);
            setup_valid |= SH_FOG;
        }
        if (w_s) {
            WR(MGAREG_SPECRXINC, si[0]);
            WR(MGAREG_SPECRYINC, si[1]);
            WR(MGAREG_SPECGXINC, si[2]);
            WR(MGAREG_SPECGYINC, si[3]);
            WR(MGAREG_SPECBXINC, si[4]);
            WR(MGAREG_SPECBYINC, si[5]);
            keep(sh.spec, si, 6);
            setup_valid |= SH_SPEC;
        }
        if (w_wh) {                     /* tmap0dis is clear here: both maps */
            setup_wrote_texsize = 1;
            WR(MGAREG_TEXWIDTH, wh[0]);
            WR(MGAREG_TEXHEIGHT, wh[1]);
            keep(sh.wh[0], wh, 2);
            keep(sh.wh[1], wh, 2);
            setup_valid |= SH_WH0 | SH_WH1;
        }
        if (w_tm) {
            WR(MGAREG_TMR(0), tm[0]);
            WR(MGAREG_TMR(1), tm[1]);
            WR(MGAREG_TMR(2), tm[2]);
            WR(MGAREG_TMR(3), tm[3]);
            WR(MGAREG_TMR(4), tm[4]);
            WR(MGAREG_TMR(5), tm[5]);
            keep(sh.tm[0], tm, 6);
            keep(sh.tm[1], tm, 6);
            setup_valid |= SH_TM0 | SH_TM1;
        }
    }

    PROF_SWITCH(PROF_STRAP);
    y_top = first_row(v[0]->Y16);
    y_mid = first_row(v[1]->Y16);
    y_bot = first_row(v[2]->Y16);
    if (flags & MGA_S_VOODOO_EDGES)
        d_long = voodoo_slope(v[0]->X16, v[0]->Y16, v[2]->X16, v[2]->Y16);   /* both trapezoids' long edge */

    if (mga.textrap_empty_stall && (ctx->dwgctl & 0xF) == DWG_OPCOD_TEXTURE_TRAP) {
        int cy0, cy1;
        engine_get_clip(&cx0, &cy0, &cx1, &cy1);
        stall_test = 1;
    }
    for (part = 0; part < 2; part++) {
        int32_t ys = part ? y_mid : y_top, ye = part ? y_bot : y_mid;
        const mga_svtx *sa = part ? v[1] : v[0], *sb = part ? v[2] : v[1];
        mga_edge el, er, lng, sht;
        double ex, ey;
        if (ys < ctx->clip_y0) ys = ctx->clip_y0;
        if (ye > ctx->clip_y1) ye = ctx->clip_y1;
        if (ye <= ys)
            continue;
        if (flags & MGA_S_VOODOO_EDGES) {
            /* An edge whose Voodoo slope does not fit, even reduced (flatter
             * than 2 pixels a row on the G100/G200, 32 on the G400), takes
             * the exact centre rule, which always does. The choice is the
             * edge's own, so a triangle sharing it chooses alike: no cracks. */
            int32_t vl = ar_lim();
            edge_voodoo(v[0]->X16, v[0]->Y16, d_long, ys, &lng);
            if (!edge_fits(&lng, vl)) {
                edge_reduce(&lng, vl);
                if (!edge_fits(&lng, vl)) {
                    edge_exact(v[0]->X16, v[0]->Y16, v[2]->X16, v[2]->Y16, ys, &lng);
                    edge_reduce(&lng, vl);
                    setup_stats.vfallback++;
                }
            }
            edge_voodoo(sa->X16, sa->Y16, voodoo_slope(sa->X16, sa->Y16, sb->X16, sb->Y16), ys, &sht);
            if (!edge_fits(&sht, vl)) {
                edge_reduce(&sht, vl);
                if (!edge_fits(&sht, vl)) {
                    edge_exact(sa->X16, sa->Y16, sb->X16, sb->Y16, ys, &sht);
                    edge_reduce(&sht, vl);
                    setup_stats.vfallback++;
                }
            }
        } else {
            edge_exact(v[0]->X16, v[0]->Y16, v[2]->X16, v[2]->Y16, ys, &lng);
            edge_exact(sa->X16, sa->Y16, sb->X16, sb->Y16, ys, &sht);
            if (reduce) {
                edge_reduce(&lng, ar_lim());
                edge_reduce(&sht, ar_lim());
            }
        }
        if (mid_right) { el = lng; er = sht; } else { el = sht; er = lng; }
        if (stall_test && !trap_covers(&el, &er, ye - ys, cx0, cx1)) {
            setup_stats.culled_empty++;     /* (mga.textrap_empty_stall) */
            continue;
        }
        setup_stats.traps++;
        ex = (el.x + 0.5) - g.x0;          /* exact: multiples of 1/16 */
        ey = (ys + 0.5) - g.y0;
        WR(MGAREG_AR0, (uint32_t)el.ar_step);
        WR(MGAREG_AR1, (uint32_t)el.ar_err);
        WR(MGAREG_AR2, (uint32_t)el.ar_dec);
        WR(MGAREG_AR4, (uint32_t)er.ar_err);
        WR(MGAREG_AR5, (uint32_t)er.ar_dec);
        WR(MGAREG_AR6, (uint32_t)er.ar_step);
        WR(MGAREG_SGN, (el.neg ? SGN_SDXL : 0) | (er.neg ? SGN_SDXR : 0));
        WR(MGAREG_FXBNDRY, ((uint32_t)(er.x & 0xFFFF) << 16) | (uint32_t)(el.x & 0xFFFF));
        if (flags & MGA_S_Z) {
            double z = eval(&pz, ex, ey);
            if (flags & MGA_S_Z32) {
                uint64_t zs = z32_start(z);
                WR(MGAREG_DR0_Z32LSB, (uint32_t)zs);
                WR(MGAREG_DR0_Z32MSB, (uint32_t)(zs >> 32) & 0xFFFF);
            } else {
                WR(MGAREG_DR0, z16_start(z));
            }
        }
        if (flags & MGA_S_COLOR) {
            WR(MGAREG_DR4, col_start(eval(&pr, ex, ey)));
            WR(MGAREG_DR8, col_start(eval(&pg, ex, ey)));
            WR(MGAREG_DR12, col_start(eval(&pb, ex, ey)));
        } else {
            WR(MGAREG_DR4, col_start(v[0]->r));
            WR(MGAREG_DR8, col_start(v[0]->g));
            WR(MGAREG_DR12, col_start(v[0]->b));
        }
        if (flags & MGA_S_ALPHA) {
            WR(MGAREG_ALPHASTART, col_start(eval(&pa, ex, ey)));
        }
        if (flags & MGA_S_FOG) {
            WR(MGAREG_FOGSTART, col_start(eval(&pf, ex, ey)));
        }
        if (flags & MGA_S_SPEC) {
            WR(MGAREG_SPECRSTART, col_start(eval(&psr, ex, ey)));
            WR(MGAREG_SPECGSTART, col_start(eval(&psg, ex, ey)));
            WR(MGAREG_SPECBSTART, col_start(eval(&psb, ex, ey)));
        }
        if (flags & MGA_S_TEX) {
            WR(MGAREG_TMR(6), (uint32_t)fx(eval(&ps, ex, ey), 1048576.0));
            WR(MGAREG_TMR(7), (uint32_t)fx(eval(&pt, ex, ey), 1048576.0));
            WR(MGAREG_TMR(8), (uint32_t)fx(eval(&pq, ex, ey), 65536.0));
        }
        if (flags & MGA_S_TEX2) {
            /* The G400's second programming step (specification §4.5.5.5):
             * everything above reached both maps; now map 1 alone, then
             * start the engine. Its sizes and increments go with the first
             * trapezoid drawn and stay for the second. */
            int tw = ctx->tex_tw1, th = ctx->tex_th1;
            WR(MGAREG_TEXCTL2, ctx->texctl2_1 | TEXCTL2_MAP1);
            if (map1_sizes) {
                uint32_t wh1[2], tm1[6];
                int w_wh1, w_tm1;
                wh1[0] = TEXWH(tw, 8 - tw, (1u << tw) - 1);      /* (rfw as map 0's) */
                wh1[1] = TEXWH(th, 8 - th, (1u << th) - 1);
                tm1[0] = (uint32_t)fx(ps1.dx, 1048576.0);
                tm1[1] = (uint32_t)fx(ps1.dy, 1048576.0);
                tm1[2] = (uint32_t)fx(pt1.dx, 1048576.0);
                tm1[3] = (uint32_t)fx(pt1.dy, 1048576.0);
                tm1[4] = (uint32_t)fx(pq1.dx, 65536.0);
                tm1[5] = (uint32_t)fx(pq1.dy, 65536.0);
                w_wh1 = !(setup_valid & SH_WH1) || !same(sh.wh[1], wh1, 2);
                w_tm1 = !(setup_valid & SH_TM1) || !same(sh.tm[1], tm1, 6);
                if (w_wh1) {            /* map 1 only */
                    setup_wrote_texsize = 1;
                    WR(MGAREG_TEXWIDTH, wh1[0]);
                    WR(MGAREG_TEXHEIGHT, wh1[1]);
                    keep(sh.wh[1], wh1, 2);
                    setup_valid |= SH_WH1;
                }
                if (w_tm1) {
                    WR(MGAREG_TMR(0), tm1[0]);
                    WR(MGAREG_TMR(1), tm1[1]);
                    WR(MGAREG_TMR(2), tm1[2]);
                    WR(MGAREG_TMR(3), tm1[3]);
                    WR(MGAREG_TMR(4), tm1[4]);
                    WR(MGAREG_TMR(5), tm1[5]);
                    keep(sh.tm[1], tm1, 6);
                    setup_valid |= SH_TM1;
                }
                map1_sizes = 0;
            }
            WR(MGAREG_TMR(6), (uint32_t)fx(eval(&ps1, ex, ey), 1048576.0));
            WR(MGAREG_TMR(7), (uint32_t)fx(eval(&pt1, ex, ey), 1048576.0));
            WR(MGAREG_TMR(8), (uint32_t)fx(eval(&pq1, ex, ey), 65536.0));
            WR(MGAREG_TEXCTL2, ctx->texctl2_1);   /* map 1 only; broadcast from the next write */
        }
        WR(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)(ys & 0xFFFF) << 16) | (uint32_t)(ye - ys));
    }
}
