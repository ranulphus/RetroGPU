/* dctnl.c - DCTNL: Celsius's (0x96) own transform on the NV1x, against the path DOS-GL draws by now (the Quadro2 plan's
 * Q5). Each case is drawn twice into the same 640x480x16 screen: first as the CPU's path sends it (clip-space
 * vertices the CPU transformed, the projection the viewport alone: conformance-proven), read back; then with the
 * object-space vertices and the whole matrix on the chip (PROJECTION = viewport x P x MV, MODELVIEW = MV for eye
 * space), read back; the two frames compared.
 *   tri    - a perspective triangle, turned and moved away;
 *   near   - a triangle with one corner behind the eye (the CPU's path clips it at the near plane first);
 *   fog    - tri with linear fog: the CPU's path gives each vertex's eye distance, the chip computes it from MODELVIEW
 *            (FOG_COORD 3, the eye-plane distance's magnitude).
 *
 *   DCTNL
 *
 * HX-TEST lines: map, mode, accel, celsius, and one per case. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define S7 7

typedef struct { float x, y, z, w; uint32_t col; float s, t, s1, t1, fog; } cvert;

static int pitch_px;
static uint32_t vb, zoff;
static uint16_t ref[W * H];

static void m1(uint32_t mthd, uint32_t v) { tnt2_methods(S7, mthd, &v, 1); }
static uint32_t fb(float f) { union { float f; uint32_t u; } c; c.f = f; return c.u; }

static int trap_ok(const char *name)
{
    uint32_t a, d, i = tnt2_3d_trap(&a, &d);
    if (i)
        hx_test(name, 0, "PGRAPH_INTR %08lx: trapped %08lx (subchannel %lu, method %03lx), data %08lx",
                (unsigned long)i, (unsigned long)a, (unsigned long)((a >> 16) & 7), (unsigned long)(a & 0x1FFC),
                (unsigned long)d);
    return i == 0;
}

/* GL's column-major 4x4 matrices: r = a b */
static void mul(float *r, const float *a, const float *b)
{
    float t[16];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float s = 0;
            for (k = 0; k < 4; k++)
                s += a[k * 4 + j] * b[i * 4 + k];
            t[i * 4 + j] = s;
        }
    memcpy(r, t, sizeof t);
}

/* A column-major matrix to the chip's sixteen words (row-major). */
static void send_matrix(uint32_t mthd, const float *m)
{
    uint32_t w[16];
    int r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            w[r * 4 + c] = fb(m[c * 4 + r]);
    tnt2_methods(S7, mthd, w, 16);
}

static float VPS[16] = { W / 2.0f, 0, 0, 0, 0, -H / 2.0f, 0, 0, 0, 0, 65535.0f / 2.0f, 0, 0, 0, 0, 1 };
static float P[16], MV[16], MVP[16];

static void setup_common(void)
{
    m1(0x6E8, fb(W / 2.0f - 2048.0f));
    m1(0x6EC, fb(H / 2.0f - 2048.0f));
    m1(0x6F0, fb(65535.0f / 2.0f));
    m1(0x6F4, 0);
    m1(0x394, fb(0.0f));
    m1(0x398, fb(65535.0f));
    m1(0x30C, 0); m1(0x35C, 0); m1(0x304, 0); m1(0x314, 0);
    m1(0x228, 0); m1(0x22C, 0);
    m1(0x260, 0); m1(0x268, 0); m1(0x270, 0); m1(0x278, 0); m1(0x280, 0); m1(0x284, 0x18000000);
}

static void fog_on(int on, uint32_t coord)
{
    float s = 2.0f, e = 12.0f, k[3];
    m1(0x29C, 0x2601);
    m1(0x2A0, coord);
    m1(0x2A4, (uint32_t)on);
    m1(0x2A8, 0xFF000000u);                             /* black */
    k[0] = 2.0f + s / (e - s), k[1] = -1.0f / (e - s), k[2] = 0;
    {
        uint32_t w[3] = { fb(k[0]), fb(k[1]), fb(k[2]) };
        tnt2_methods(S7, 0x680, w, 3);
    }
    if (on)
        m1(0x288, 0x13040300), m1(0x28C, 0x13001400);
    else
        m1(0x288, 0x20042000), m1(0x28C, 0x20001400);
}

static void formats(int homogeneous_clip)
{
    uint32_t fmt[8] = { 0x01002842, 0x2840, 0x0002, 0x2822, 0x2822, 0x0002, 0x0002, 0x2812 };
    static const uint32_t at[8] = { 0, 16, 0, 20, 28, 0, 0, 36 };
    int i;
    if (!homogeneous_clip)
        fmt[0] = 0x2832;                                /* (object space: x y z, w 1) */
    for (i = 0; i < 8; i++)
        m1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    for (i = 0; i < 8; i++)
        if (fmt[i] & 0xF0)
            m1(0xD00 + 8u * (uint32_t)i, vb + at[i]);
}

static void draw(const cvert *v, int n)
{
    memcpy((void *)(tnt2_fb + vb), v, (size_t)n * sizeof *v);
    (void)tnt2_fb[vb];
    m1(0xCF0, 0);
    m1(0x13FC, 5);
    m1(0x1400, (uint32_t)(n - 1) << 24);
    m1(0x13FC, 0);
    tnt2_kick();
    tnt2_sync(1000000);
}

static void clear(void)
{
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_fill(0, 0, W, H, 0x0841);
    tnt2_sync(1000000);
    tnt2_celsius_target(0, zoff, pitch_px * 2, W, H, 16);
}

static void grab(uint16_t *to)
{
    int y;
    for (y = 0; y < H; y++)
        memcpy(to + y * W, (const void *)(tnt2_fb + (uint32_t)y * (uint32_t)pitch_px * 2u), W * 2);
}

/* The CPU's path: clip coordinates, clipped at the near plane (z > -w), as a fan of triangles. */
static int cpu_verts(const float obj[3][3], const uint32_t col[3], int fog, cvert *out)
{
    float c[3][4], e[3], p[6][4], pe[6];
    uint32_t pc[6];
    int i, n = 0;
    for (i = 0; i < 3; i++) {
        const float *o = obj[i];
        int r;
        for (r = 0; r < 4; r++)
            c[i][r] = MVP[0 * 4 + r] * o[0] + MVP[1 * 4 + r] * o[1] + MVP[2 * 4 + r] * o[2] + MVP[3 * 4 + r];
        e[i] = -(MV[2] * o[0] + MV[6] * o[1] + MV[10] * o[2] + MV[14]);
    }
    for (i = 0; i < 3; i++) {                          /* (Sutherland-Hodgman against z + w >= 0) */
        int j = (i + 1) % 3;
        float di = c[i][2] + c[i][3], dj = c[j][2] + c[j][3];
        if (di >= 0)
            memcpy(p[n], c[i], sizeof p[n]), pe[n] = e[i], pc[n] = col[i], n++;
        if ((di >= 0) != (dj >= 0)) {
            float t = di / (di - dj);
            int r;
            for (r = 0; r < 4; r++)
                p[n][r] = c[i][r] + (c[j][r] - c[i][r]) * t;
            pe[n] = e[i] + (e[j] - e[i]) * t;
            pc[n] = t < 0.5f ? col[i] : col[j];         /* (flat colours in these cases) */
            n++;
        }
    }
    for (i = 1; i + 1 < n; i++) {
        int k, idx[3] = { 0, i, i + 1 };
        for (k = 0; k < 3; k++) {
            cvert *v = &out[(i - 1) * 3 + k];
            v->x = p[idx[k]][0], v->y = p[idx[k]][1], v->z = p[idx[k]][2], v->w = p[idx[k]][3];
            v->col = pc[idx[k]], v->s = v->t = v->s1 = v->t1 = 0, v->fog = fog ? pe[idx[k]] : 0;
        }
    }
    return n >= 3 ? (n - 2) * 3 : 0;
}

static void run_case(const char *name, const float obj[3][3], const uint32_t col[3], int fog)
{
    static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    cvert v[12];
    float vpm[16];
    int n, i, bad = 0, drawn = 0, worst = 0;
    static uint16_t got[W * H];
    /* the CPU's path */
    clear();
    setup_common();
    send_matrix(0x500, VPS);
    send_matrix(0x400, I);
    fog_on(fog, 0);
    formats(1);
    n = cpu_verts(obj, col, fog, v);
    if (n)
        draw(v, n);
    if (!trap_ok(name))
        return;
    grab(ref);
    /* the chip's transform */
    clear();
    setup_common();
    mul(vpm, VPS, MVP);
    send_matrix(0x500, vpm);
    send_matrix(0x400, MV);
    fog_on(fog, 3);
    formats(0);
    for (i = 0; i < 3; i++) {
        v[i].x = obj[i][0], v[i].y = obj[i][1], v[i].z = obj[i][2], v[i].w = 1;
        v[i].col = col[i], v[i].s = v[i].t = v[i].s1 = v[i].t1 = v[i].fog = 0;
    }
    draw(v, 3);
    if (!trap_ok(name))
        return;
    grab(got);
    for (i = 0; i < W * H; i++) {
        int a = ref[i], b = got[i], d;
        if (a != 0x0841)
            drawn++;
        d = abs(((a >> 5) & 63) - ((b >> 5) & 63)) + abs((a >> 11) - (b >> 11)) + abs((a & 31) - (b & 31));
        if (d > worst) worst = d;
        if (d > 4) bad++;
    }
    hx_test(name, drawn > 1000 && bad < drawn / 100, "%d pixels drawn by the CPU's path; %d differ (worst %d)", drawn,
            bad, worst);
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    hx_init(1, argv, "dctnl");
    (void)argc;
    if (tnt2_find() != 0 || tnt2_map() != 0 || tnt2.arch != 10) {
        hx_test("map", 0, "no NV1x chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s", tnt2.name);
    if (vbe_find_mode(W, H, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    zoff = ((uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    vb = (zoff + (uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_set_clip(0, 0, W, H);
    if (tnt2_3d_init() != 0 || tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 set up");
    {
        /* P: glFrustum(-1, 1, -0.75, 0.75, 1, 30); MV: turned 30 degrees about y, then 6 away */
        float n = 1, f = 30, a = 0.5235988f, R[16] = { 0 }, T[16] = { 0 };
        memset(P, 0, sizeof P);
        P[0] = 2 * n / 2.0f, P[5] = 2 * n / 1.5f, P[10] = -(f + n) / (f - n), P[11] = -1, P[14] = -2 * f * n / (f - n);
        R[0] = cosf(a), R[2] = -sinf(a), R[5] = 1, R[8] = sinf(a), R[10] = cosf(a), R[15] = 1;
        T[0] = T[5] = T[10] = T[15] = 1, T[14] = -6;
        mul(MV, T, R);
        mul(MVP, P, MV);
    }
    {
        static const float tri[3][3] = { { -2, -1.5f, 0 }, { 2, -1, -1 }, { 0, 2, 1 } };
        static const float near_[3][3] = { { -1, -1, 0 }, { 1, -1, 0 }, { 0, 1, 8 } };   /* (z 8: behind the eye) */
        static const uint32_t col[3] = { 0xFFFF8000u, 0xFFFF8000u, 0xFFFF8000u };
        run_case("tri", tri, col, 0);
        run_case("near", near_, col, 0);
        run_case("fog", tri, col, 1);
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
