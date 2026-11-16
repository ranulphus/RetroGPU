/* vfloor.c - VFLOOR: the floor of DOS-GL's per-vertex work on this CPU (gl/docs/g4path.md, V0).
 *
 * 4,500 vertices laid out as immediate mode stores them (position xyzw, colour, two texture coordinate pairs:
 * 48 bytes), a modelview-projection matrix and a 640x480 viewport, put through the fast path's arithmetic
 * (fast_get: the transform stored as floats, the clip codes, the projection in doubles with the viewport's
 * integers, X16/Y16, the WARP vertex, the texture coordinates' floors) in tight loops over the whole batch,
 * with the FPU as DOS-GL sets it (FPU_ENTER: 0x027F). Cycles per vertex by RDTSC, the best of 20 runs:
 *   xform   the transform and the clip codes only
 *   full    the transform, clip codes, projection and WARP form (fast_get's work, batched)
 *   konst   as full, the viewport and depth range as doubles computed once (V5's idea)
 *   fncall  as full, one vertex per call through a cache lookup, as fast_get is called now
 *   nv      as konst, the transform kept in registers (no volatile), clip codes on the stored floats
 *   sign    as nv, the clip codes from the sign bits of the stored sums (integer tests, no x87 compares)
 *   single  as sign, the reciprocal under single precision (the control word set once around the loop)
 *   lean    as single, texture coordinates' range tests by their exponent bits
 * HX-STAT vfloor lines. Built by RetroGPU's root Makefile's DOS rule:
 *   make build/djgpp/VFLOOR.EXE DJ_SRC_VFLOOR=gl/tools/perf/vfloor.c \
 *        'DJ_CFLAGS=-std=gnu99 -O2 -march=i586 -mtune=pentium2 -Wall -Wextra -Werror -Ihal/include -Iglide/include -DMGA_DJGPP=1'
 * (DOS-GL's own flags), then run on the bench with tools/bench/run.py. */
#include "hx.h"
#include "mga/fp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NV 4500
#define RUNS 20
#define GUARD 5.25f

typedef struct { float pos[4], col[4], tex[2], tex1[2]; } vin;
typedef struct { float x, y, z, rhw; uint32_t bgra, spec; float u0, v0; } wvtx;
typedef struct { int key; unsigned gen, oc; int32_t X16, Y16, fu, fv; wvtx w; } slot;

static vin V[NV];
static wvtx W[NV];
static int32_t X16[NV], Y16[NV], FU[NV], FV[NV];
static unsigned OC[NV];
static float CX[NV], CY[NV], CZ[NV], CWW[NV];
static slot S[256];
static unsigned gen = 1;
static float M[16];
static volatile int vp[4] = { 0, 0, 640, 480 };
static volatile int height = 480;
static volatile double dnear = 0.0, dfar = 1.0;
static volatile float tss = 1.0f, tst = 1.0f;

static uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void make_data(void)
{
    int i;
    /* A perspective view of points spread in front of the eye: all inside. */
    static const float m[16] = { 1.2f, 0, 0, 0,  0, 1.6f, 0, 0,  0, 0, -1.002f, -1,  0, 0, -8.0f, 0 };
    memcpy(M, m, sizeof M);
    srand(1);
    for (i = 0; i < NV; i++) {
        V[i].pos[0] = (float)(rand() % 2000 - 1000) * 0.1f;
        V[i].pos[1] = (float)(rand() % 1500 - 750) * 0.1f;
        V[i].pos[2] = -(float)(rand() % 1500 + 150);
        V[i].pos[3] = 1.0f;
        V[i].col[0] = V[i].col[1] = V[i].col[2] = V[i].col[3] = 1.0f;
        V[i].tex[0] = (float)(rand() % 4000) * 0.01f;
        V[i].tex[1] = (float)(rand() % 4000) * 0.01f;
    }
}

static void xform(int i)
{
    const float *p = V[i].pos, *m = M;
    volatile float x, y, z, w;
    x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12] * p[3];
    y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13] * p[3];
    z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] * p[3];
    w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15] * p[3];
    OC[i] = (z + w < 0) | ((w - z < 0) << 1) | ((x + GUARD * w < 0) << 2) | ((GUARD * w - x < 0) << 3) |
            ((y + GUARD * w < 0) << 4) | ((GUARD * w - y < 0) << 5);
    CX[i] = x; CY[i] = y; CZ[i] = z; CWW[i] = w;
}

static void project(int i, int32_t *x16, int32_t *y16, int32_t *fu, int32_t *fv, wvtx *o)
{
    double iw = 1.0 / CWW[i], xw, yw, zw;
    xw = vp[0] + (CX[i] * iw + 1.0) * 0.5 * vp[2];
    yw = height - (vp[1] + (CY[i] * iw + 1.0) * 0.5 * vp[3]);
    zw = dnear + (CZ[i] * iw + 1.0) * 0.5 * (dfar - dnear);
    *x16 = mga_irint_nearest(xw * 16.0);
    *y16 = mga_irint_nearest(yw * 16.0);
    o->x = (float)xw - 0.5f;
    o->y = (float)yw - 0.375f;
    o->z = (float)(zw < 0 ? 0 : zw > 1 ? 1 : zw);
    o->rhw = (float)iw;
    o->bgra = 0xFFFFFFFFu;
    o->spec = 0xFF000000u;
    o->u0 = V[i].tex[0] * tss;
    o->v0 = V[i].tex[1] * tst;
    *fu = o->u0 > -1e9f && o->u0 < 1e9f ? mga_ifloor(o->u0) : INT32_MIN;
    *fv = o->v0 > -1e9f && o->v0 < 1e9f ? mga_ifloor(o->v0) : INT32_MIN;
}

static void project_k(int i, double sx, double ox, double sy, double oy, double sz, double oz)
{
    double iw = 1.0 / CWW[i], xw, yw, zw;
    xw = ox + CX[i] * iw * sx;
    yw = oy + CY[i] * iw * sy;
    zw = oz + CZ[i] * iw * sz;
    X16[i] = mga_irint_nearest(xw * 16.0);
    Y16[i] = mga_irint_nearest(yw * 16.0);
    W[i].x = (float)xw - 0.5f;
    W[i].y = (float)yw - 0.375f;
    W[i].z = (float)(zw < 0 ? 0 : zw > 1 ? 1 : zw);
    W[i].rhw = (float)iw;
    W[i].bgra = 0xFFFFFFFFu;
    W[i].spec = 0xFF000000u;
    W[i].u0 = V[i].tex[0] * tss;
    W[i].v0 = V[i].tex[1] * tst;
    FU[i] = W[i].u0 > -1e9f && W[i].u0 < 1e9f ? mga_ifloor(W[i].u0) : INT32_MIN;
    FV[i] = W[i].v0 > -1e9f && W[i].v0 < 1e9f ? mga_ifloor(W[i].v0) : INT32_MIN;
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* The transform in registers, stored once as floats; clip codes from the stored floats' comparisons. */
static void xform_nv(int i)
{
    const float *p = V[i].pos, *m = M;
    float x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12] * p[3];
    float y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13] * p[3];
    float z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] * p[3];
    float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15] * p[3];
    CX[i] = x; CY[i] = y; CZ[i] = z; CWW[i] = w;
    x = CX[i]; y = CY[i]; z = CZ[i]; w = CWW[i];
    OC[i] = (z + w < 0) | ((w - z < 0) << 1) | ((x + GUARD * w < 0) << 2) | ((GUARD * w - x < 0) << 3) |
            ((y + GUARD * w < 0) << 4) | ((GUARD * w - y < 0) << 5);
}

/* Clip codes from sign bits: each sum stored as a float, its sign bit read as an integer. */
static void xform_sign(int i)
{
    const float *p = V[i].pos, *m = M;
    float x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12] * p[3];
    float y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13] * p[3];
    float z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] * p[3];
    float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15] * p[3];
    float s[6];
    CX[i] = x; CY[i] = y; CZ[i] = z; CWW[i] = w;
    x = CX[i]; y = CY[i]; z = CZ[i]; w = CWW[i];
    s[0] = z + w; s[1] = w - z; s[2] = x + GUARD * w; s[3] = GUARD * w - x; s[4] = y + GUARD * w; s[5] = GUARD * w - y;
    {
        uint32_t u[6];
        memcpy(u, s, sizeof u);
        /* (a negative zero counts as outside here, where the comparison says inside: the general path decides) */
        OC[i] = (u[0] >> 31) | ((u[1] >> 31) << 1) | ((u[2] >> 31) << 2) | ((u[3] >> 31) << 3) |
                ((u[4] >> 31) << 4) | ((u[5] >> 31) << 5);
    }
}

static void project_lean(int i, double sx, double ox, double sy, double oy, double sz, double oz)
{
    double iw = 1.0 / CWW[i], xw, yw, zw;
    xw = ox + CX[i] * iw * sx;
    yw = oy + CY[i] * iw * sy;
    zw = oz + CZ[i] * iw * sz;
    X16[i] = mga_irint_nearest(xw * 16.0);
    Y16[i] = mga_irint_nearest(yw * 16.0);
    W[i].x = (float)xw - 0.5f;
    W[i].y = (float)yw - 0.375f;
    W[i].z = (float)(zw < 0 ? 0 : zw > 1 ? 1 : zw);
    W[i].rhw = (float)iw;
    W[i].bgra = 0xFFFFFFFFu;
    W[i].spec = 0xFF000000u;
    W[i].u0 = V[i].tex[0] * tss;
    W[i].v0 = V[i].tex[1] * tst;
    /* |u| < 2^30 by the exponent alone (an integer test), then the floor */
    FU[i] = ((fbits(W[i].u0) >> 23) & 0xFF) < 157 ? mga_ifloor(W[i].u0) : INT32_MIN;
    FV[i] = ((fbits(W[i].v0) >> 23) & 0xFF) < 157 ? mga_ifloor(W[i].v0) : INT32_MIN;
}

static __attribute__((noinline)) const slot *get(int i)
{
    slot *s = &S[(unsigned)i & 255];
    if (s->gen == gen && s->key == i)
        return s;
    s->key = i;
    s->gen = gen;
    xform(i);
    s->oc = OC[i];
    if (!s->oc)
        project(i, &s->X16, &s->Y16, &s->fu, &s->fv, &s->w);
    return s;
}

static double best(uint64_t *t)
{
    uint64_t b = t[0];
    int k;
    for (k = 1; k < RUNS; k++)
        if (t[k] < b)
            b = t[k];
    return (double)b / NV;
}

int main(int argc, char **argv)
{
    uint64_t tx[RUNS], tf[RUNS], tk[RUNS], tc[RUNS], tn[RUNS], ts[RUNS], tsg[RUNS], tl[RUNS], t0;
    int r, i;
    unsigned sink = 0;
    hx_init(argc, argv, "vfloor");
    make_data();
    {
        FPU_ENTER();
        for (r = 0; r < RUNS; r++) {
            t0 = rdtsc();
            for (i = 0; i < NV; i++)
                xform(i);
            tx[r] = rdtsc() - t0;

            t0 = rdtsc();
            for (i = 0; i < NV; i++)
                xform(i);
            for (i = 0; i < NV; i++)
                if (!OC[i])
                    project(i, &X16[i], &Y16[i], &FU[i], &FV[i], &W[i]);
            tf[r] = rdtsc() - t0;

            t0 = rdtsc();
            {
                const double sx = 0.5 * vp[2], ox = vp[0] + sx, sy = -0.5 * vp[3], oy = height - vp[1] - 0.5 * vp[3],
                             sz = 0.5 * (dfar - dnear), oz = dnear + sz;
                for (i = 0; i < NV; i++)
                    xform(i);
                for (i = 0; i < NV; i++)
                    if (!OC[i])
                        project_k(i, sx, ox, sy, oy, sz, oz);
            }
            tk[r] = rdtsc() - t0;

            {
                const double sx = 0.5 * vp[2], ox = vp[0] + sx, sy = -0.5 * vp[3], oy = height - vp[1] - 0.5 * vp[3],
                             sz = 0.5 * (dfar - dnear), oz = dnear + sz;
                t0 = rdtsc();
                for (i = 0; i < NV; i++)
                    xform_nv(i);
                for (i = 0; i < NV; i++)
                    if (!OC[i])
                        project_k(i, sx, ox, sy, oy, sz, oz);
                tn[r] = rdtsc() - t0;
                t0 = rdtsc();
                for (i = 0; i < NV; i++)
                    xform_sign(i);
                for (i = 0; i < NV; i++)
                    if (!OC[i])
                        project_k(i, sx, ox, sy, oy, sz, oz);
                ts[r] = rdtsc() - t0;
                t0 = rdtsc();
                fp_set_cw(0x007F);              /* (single precision: the reciprocal's latency) */
                for (i = 0; i < NV; i++)
                    xform_sign(i);
                for (i = 0; i < NV; i++)
                    if (!OC[i])
                        project_k(i, sx, ox, sy, oy, sz, oz);
                fp_set_cw(0x027F);
                tsg[r] = rdtsc() - t0;
                t0 = rdtsc();
                fp_set_cw(0x007F);
                for (i = 0; i < NV; i++)
                    xform_sign(i);
                for (i = 0; i < NV; i++)
                    if (!OC[i])
                        project_lean(i, sx, ox, sy, oy, sz, oz);
                fp_set_cw(0x027F);
                tl[r] = rdtsc() - t0;
            }
            gen++;
            t0 = rdtsc();
            for (i = 0; i < NV; i++)
                sink += get(i)->X16;
            tc[r] = rdtsc() - t0;
        }
        FPU_LEAVE();
    }
    for (i = 0; i < NV; i++)
        sink += OC[i];
    hx_log("HX-STAT vfloor cycles_per_vertex xform=%.0f full=%.0f konst=%.0f fncall=%.0f nv=%.0f sign=%.0f single=%.0f "
           "lean=%.0f (sink=%u)", best(tx), best(tf), best(tk), best(tc), best(tn), best(ts), best(tsg), best(tl), sink);
    hx_test("vfloor", 1, "measured");
    hx_done(0);
}
