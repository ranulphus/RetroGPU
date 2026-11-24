/* dtcl0.c - DTCL0: the Radeon's transform engine's first triangles (gl/docs/dx7.md, X3's bring-up), on a chip with
 * one (the 7500, RV200; the 7200, R100). DTRI0's three overlapping triangles, given in object coordinates and
 * taken to the window by the chip: the model-view-projection matrix in the engine's vector memory (Mesa's
 * upload_matrix: its rows), the viewport transform in SE_VPORT_*, the vertices fetched by the chip from an array
 * in video memory (3D_LOAD_VBPNTR, then 3D_DRAW_VBUF with TCL_ENABLE: Mesa's radeonEmitAOS and radeonEmitVbufPrim),
 * the TCL registers as Mesa's radeonInitState sets them. The frame is compared with DTRI0's reference, the
 * vertices taken through the same matrix in doubles.
 *
 *   DTCL0 [rot=DEG] [immd]
 *
 * rot: the triangles turned about the screen's centre by the matrix (the vertices given turned the other way).
 * immd: the vertices in the command stream (3D_DRAW_IMMD with TCL_ENABLE) instead of fetched from the array.
 * perf=N: after the checks, a 64 x 64 grid of small triangles (7,938, as a list) from an array, drawn N times
 * through the transform engine and timed (DTCL0-PERF: triangles and vertices a second).
 * Needs the command processor. */
#include "hx.h"
#include "radeon/radeon.h"
#include "radeon/regs3d_radeon.h"
#include "radeon/regs_radeon.h"
#include "mga/sys.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define BASE (16u << 20)

/* The TCL registers (X.org's radeon_reg.h; Linux's radeon_drv.h for the state flush) */
#define SE_VPORT_XSCALE          0x1d98     /* .. XOFFSET, YSCALE, YOFFSET, ZSCALE, ZOFFSET, 4 bytes on each */
#define SE_TCL_VECTOR_INDX_REG   0x2200
#define SE_TCL_VECTOR_DATA_REG   0x2204
#define SE_TCL_SCALAR_INDX_REG   0x2208
#define SE_TCL_SCALAR_DATA_REG   0x220c
#define SE_TCL_OUTPUT_VTX_FMT    0x2254
#define SE_TCL_OUTPUT_VTX_SEL    0x2258
#define SE_TCL_MATRIX_SELECT_0   0x225c
#define SE_TCL_MATRIX_SELECT_1   0x2260
#define SE_TCL_UCP_VERT_BLEND_CTL 0x2264
#define SE_TCL_TEXTURE_PROC_CTL  0x2268
#define SE_TCL_LIGHT_MODEL_CTL   0x226c
#define SE_TCL_PER_LIGHT_CTL_0   0x2270     /* .. _3 */
#define SE_TCL_STATE_FLUSH       0x2284
#define TCL_VTX_W0               (1u << 0)
#define TCL_VTX_PK_DIFFUSE       (1u << 3)
#define TCL_VTX_Z0               (1u << 31)
#define TCL_COMPUTE_XYZW         (1u << 0)
#define UCP_IN_CLIP_SPACE        (1u << 0)
#define CULL_FRONT_IS_CCW        (1u << 28)
#define VPORT_XY_XFORM_ENABLE    (1u << 24)
#define VPORT_Z_XFORM_ENABLE     (1u << 25)
#define VTX_W0_IS_NOT_1_OVER_W0  (1u << 16)
#define SS_VERT_GUARD_CLIP_ADJ   48         /* (and the next three: Mesa sets all four to 1.0) */
/* Mesa's matrix slots (each 4 vectors): MODEL_PROJ 0, MODEL 1, MODEL_IT 2, TEXMAT_0..2 3..5 */
#define LOAD_VBPNTR              0x00002F00u
#define DRAW_VBUF                0x00002800u
#define VC_FRMT_PKCOLOR          0x00000008u
#define VC_FRMT_Z                0x80000000u
#define VC_TRI_LIST              0x00000004u
#define VC_WALK_LIST             0x00000020u
#define VC_MAOS_ENABLE           0x00000080u
#define VC_RADEON_MODE           0x00000100u
#define VC_TCL_ENABLE            0x00000200u

typedef struct { double x, y, z; double r, g, b; } V;

static const V win[3][3] = {        /* DTRI0's triangles, in the window (y down, z 0..1) */
    { { 100.1875, 100.3125, 0.5, 255, 0, 0 }, { 500.4375, 120.0625, 0.5, 0, 255, 0 }, { 300.5625, 400.6875, 0.5, 0, 0, 255 } },
    { { 200.3125, 150.1875, 0.25, 255, 255, 255 }, { 450.0625, 160.5625, 0.25, 255, 255, 255 }, { 320.6875, 350.4375, 0.25, 255, 255, 255 } },
    { { 150.5625, 300.0625, 0.75, 255, 255, 0 }, { 250.1875, 50.4375, 0.75, 255, 255, 0 }, { 400.3125, 380.5625, 0.75, 255, 255, 0 } },
};
static V tris[3][3];                /* the reference's triangles: the object's taken through the matrix */
static double M[4][4];              /* model-view-projection, rows: clip = M x object */

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static int inside(const V *t, double px, double py, double l[3])
{
    double area = edge(t[0].x, t[0].y, t[1].x, t[1].y, t[2].x, t[2].y), e0, e1, e2;
    if (area == 0)
        return 0;
    e0 = edge(t[1].x, t[1].y, t[2].x, t[2].y, px, py) / area;
    e1 = edge(t[2].x, t[2].y, t[0].x, t[0].y, px, py) / area;
    e2 = edge(t[0].x, t[0].y, t[1].x, t[1].y, px, py) / area;
    l[0] = e0, l[1] = e1, l[2] = e2;
    return e0 >= 0 && e1 >= 0 && e2 >= 0;
}

static signed char *who;
static uint16_t *zref;

static void reference(double off)
{
    int x, y, k;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            who[y * W + x] = -1, zref[y * W + x] = 0xFFFF;
    for (k = 0; k < 3; k++)
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                double l[3];
                uint16_t z;
                if (!inside(tris[k], x + off, y + off, l))
                    continue;
                z = (uint16_t)((l[0] * tris[k][0].z + l[1] * tris[k][1].z + l[2] * tris[k][2].z) * 65535.0);
                if (z < zref[y * W + x])
                    zref[y * W + x] = z, who[y * W + x] = (signed char)k;
            }
}

static uint32_t fb(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

static void reg(uint32_t r, uint32_t v) { radeon_cp_reg(r, v); }

/* count words to a register in turn (a type-0 packet with ONE_REG_WR) */
static void table(uint32_t r, const uint32_t *v, int count)
{
    uint32_t *d = radeon_cp_reserve(count + 1);
    int i;
    d[0] = RADEON_CP_PACKET0(r, count - 1) | RADEON_CP_ONE_REG_WR;
    for (i = 0; i < count; i++)
        d[1 + i] = v[i];
    radeon_cp_advance(count + 1);
}

static void mat_mul(double r[4][4], double a[4][4], double b[4][4])
{
    double t[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            t[i][j] = 0;
            for (k = 0; k < 4; k++)
                t[i][j] += a[i][k] * b[k][j];
        }
    memcpy(r, t, sizeof t);
}

int main(int argc, char **argv)
{
    radeon_mode m;
    uint32_t zoff, coff, aoff, vec[16], one = fb(1.0f);
    double rot = 0, P[4][4], R[4][4], obj[3][3][4];
    int immd = 0, perf = 0, k, i, j, x, y, cov_half = 0, cov_whole = 0, worst = 0, wrong_winner = 0, drawn = 0, hist[256];
    volatile uint32_t *arr;
    char where[96] = "";
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "rot=", 4)) rot = atof(argv[i] + 4);
        else if (!strcmp(argv[i], "immd")) immd = 1;
        else if (!strncmp(argv[i], "perf=", 5)) perf = atoi(argv[i] + 5);
    hx_init(1, argv, "dtcl0");
    if (radeon_find() < 0 || radeon_map() < 0) {
        hx_test("map", 0, "no Radeon mapped");
        hx_done(1);
    }
    hx_test("map", 1, "%s %04x, %u KB%s", radeon.name, radeon.device_id, (unsigned)(radeon.vram_bytes >> 10),
            radeon.flags & RADEON_HAS_TCL ? ", T&L" : "");
    if (!(radeon.flags & RADEON_HAS_TCL)) {
        hx_test("tcl", 0, "this chip has no transform engine");
        hx_done(1);
    }
    if (radeon_find_mode(W, H, 16, &m) < 0 || radeon_set_mode(&m) < 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    coff = BASE;
    zoff = BASE + (((uint32_t)m.pitch_bytes * H + 4095u) & ~4095u);
    aoff = zoff + (((uint32_t)m.pitch_bytes * H + 4095u) & ~4095u);
    radeon_set_start(coff);
    radeon_set_target(coff, m.pitch_bytes, 16);
    radeon_set_clip(0, 0, W, H);
    radeon_fill(0, 0, W, H, 0);
    radeon_set_target(zoff, m.pitch_bytes, 16);
    radeon_fill(0, 0, W, H, 0xFFFF);
    radeon_sync(1000000);
    if (radeon_cp_init(NULL, 65536u) != 0) {
        hx_test("cp", 0, "the command processor did not start");
        hx_done(1);
    }

    /* The matrix: the window's coordinates from DTRI0's (y down, z 0..1) as clip coordinates (orthographic), turned
     * about the screen's centre by rot first. */
    memset(P, 0, sizeof P);
    P[0][0] = 2.0 / W, P[0][3] = -1;
    P[1][1] = -2.0 / H, P[1][3] = 1;
    P[2][2] = 2, P[2][3] = -1;
    P[3][3] = 1;
    memset(R, 0, sizeof R);
    {
        double c = cos(rot * 3.14159265358979 / 180), s = sin(rot * 3.14159265358979 / 180);
        R[0][0] = c, R[0][1] = -s, R[0][3] = W / 2.0 - c * W / 2.0 + s * H / 2.0;
        R[1][0] = s, R[1][1] = c, R[1][3] = H / 2.0 - s * W / 2.0 - c * H / 2.0;
        R[2][2] = 1, R[3][3] = 1;
    }
    mat_mul(M, P, R);
    for (k = 0; k < 3; k++)
        for (i = 0; i < 3; i++) {
            /* The object's vertex: DTRI0's window position turned back by -rot (which the matrix undoes). */
            double c = cos(rot * 3.14159265358979 / 180), s = sin(rot * 3.14159265358979 / 180);
            double dx = win[k][i].x - W / 2.0, dy = win[k][i].y - H / 2.0;
            obj[k][i][0] = c * dx + s * dy + W / 2.0;
            obj[k][i][1] = -s * dx + c * dy + H / 2.0;
            obj[k][i][2] = win[k][i].z;
            obj[k][i][3] = 1;
        }
    for (k = 0; k < 3; k++)
        for (i = 0; i < 3; i++) {
            double c4[4];
            for (j = 0; j < 4; j++)
                c4[j] = M[j][0] * obj[k][i][0] + M[j][1] * obj[k][i][1] + M[j][2] * obj[k][i][2] + M[j][3] * obj[k][i][3];
            tris[k][i] = win[k][i];
            tris[k][i].x = (c4[0] / c4[3]) * W / 2.0 + W / 2.0;
            tris[k][i].y = -(c4[1] / c4[3]) * H / 2.0 + H / 2.0;
            tris[k][i].z = (c4[2] / c4[3]) * 0.5 + 0.5;
        }

    /* The vertices in video memory: x, y, z (w 1: the format has none) and the colour. */
    arr = (volatile uint32_t *)(radeon.fb + aoff);
    for (k = 0; k < 3; k++)
        for (i = 0; i < 3; i++) {
            volatile uint32_t *v = arr + (k * 3 + i) * 4;
            v[0] = fb((float)obj[k][i][0]), v[1] = fb((float)obj[k][i][1]), v[2] = fb((float)obj[k][i][2]);
            v[3] = 0xFF000000u | (uint32_t)win[k][i].r << 16 | (uint32_t)win[k][i].g << 8 | (uint32_t)win[k][i].b;
        }
    (void)arr[35];                          /* (the CPU's writes to video memory out before the chip reads them) */

    /* The state: DTRI0's (radeon_3d_init), with the vertices' w as clip w and the viewport transform on. */
    radeon_3d_init(coff, m.pitch_bytes, 16, zoff, m.pitch_bytes, 16, W, H);
    radeon_3d.se_coord_fmt = VTX_W0_IS_NOT_1_OVER_W0;
    radeon_3d.se_cntl |= VPORT_XY_XFORM_ENABLE | VPORT_Z_XFORM_ENABLE;
    radeon_3d.se_cntl_status = 0;           /* (TCL not bypassed) */
    radeon_3d_dirty = 1;
    radeon_3d_emit();
    {
        uint32_t vp[6];
        vp[0] = fb(W / 2.0f), vp[1] = fb(W / 2.0f), vp[2] = fb(-H / 2.0f), vp[3] = fb(H / 2.0f);
        vp[4] = fb(0.5f), vp[5] = fb(0.5f);
        for (i = 0; i < 6; i++)
            reg(SE_VPORT_XSCALE + 4u * (uint32_t)i, vp[i]);
    }
    reg(SE_TCL_OUTPUT_VTX_FMT, TCL_VTX_Z0 | TCL_VTX_W0 | TCL_VTX_PK_DIFFUSE);
    reg(SE_TCL_OUTPUT_VTX_SEL, TCL_COMPUTE_XYZW | 0u << 24 | 1u << 28);
    reg(SE_TCL_MATRIX_SELECT_0, 1u << 0 | 2u << 16);
    reg(SE_TCL_MATRIX_SELECT_1, 0u << 0 | 3u << 16 | 4u << 20 | 5u << 24);
    reg(SE_TCL_UCP_VERT_BLEND_CTL, UCP_IN_CLIP_SPACE | CULL_FRONT_IS_CCW);
    reg(SE_TCL_TEXTURE_PROC_CTL, 0);
    reg(SE_TCL_LIGHT_MODEL_CTL, 0);
    for (i = 0; i < 4; i++)
        reg(SE_TCL_PER_LIGHT_CTL_0 + 4u * (uint32_t)i, 0);
    /* The model-view-projection matrix, slot 0 (vector 0): its rows (Mesa's upload_matrix). */
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            vec[i * 4 + j] = fb((float)M[i][j]);
    reg(SE_TCL_STATE_FLUSH, 0);
    reg(SE_TCL_VECTOR_INDX_REG, 0u | 1u << 16);
    table(SE_TCL_VECTOR_DATA_REG, vec, 16);
    {
        uint32_t g[4] = { one, one, one, one };
        reg(SE_TCL_SCALAR_INDX_REG, SS_VERT_GUARD_CLIP_ADJ | 1u << 16);
        table(SE_TCL_SCALAR_DATA_REG, g, 4);
    }

    /* The draw: the array (one component of 4 words a vertex), then nine vertices as three triangles; or (immd)
     * the nine vertices' words in the packet. */
    if (immd) {
        uint32_t *d = radeon_cp_reserve(3 + 36);
        d[0] = RADEON_CP_PACKET3(RADEON_3D_DRAW_IMMD, 36 + 1);
        d[1] = VC_FRMT_Z | VC_FRMT_PKCOLOR;
        d[2] = VC_TRI_LIST | RADEON_CP_VC_WALK_RING | VC_RADEON_MODE | VC_TCL_ENABLE | 9u << 16;
        for (i = 0; i < 36; i++)
            d[3 + i] = arr[i];
        radeon_cp_advance(3 + 36);
    } else {
        uint32_t *d = radeon_cp_reserve(4 + 3);
        d[0] = RADEON_CP_PACKET3(LOAD_VBPNTR, 2);
        d[1] = 1;
        d[2] = 4u << 8 | 4u;                /* (stride, size: in dwords) */
        d[3] = radeon.fb_location + aoff;
        d[4] = RADEON_CP_PACKET3(DRAW_VBUF, 1);
        d[5] = VC_FRMT_Z | VC_FRMT_PKCOLOR;
        d[6] = VC_TRI_LIST | VC_WALK_LIST | VC_MAOS_ENABLE | VC_RADEON_MODE | VC_TCL_ENABLE | 9u << 16;
        radeon_cp_advance(7);
    }
    radeon_cp_flush();
    if (radeon_sync(2000000) != 0)
        hx_log("DTCL0 the engine did not go idle (rbbm_status=%08lx)", (unsigned long)radeon_rd(0x0E40));
    hx_log("DTCL0-STAT rot=%g %s rbbm_status=%08lx", rot, immd ? "immd" : "array", (unsigned long)radeon_rd(0x0E40));
    for (k = 0; k < 3; k++)
        hx_log("DTCL0-REF triangle %d: (%.3f, %.3f, %.4f) (%.3f, %.3f, %.4f) (%.3f, %.3f, %.4f)", k, tris[k][0].x,
               tris[k][0].y, tris[k][0].z, tris[k][1].x, tris[k][1].y, tris[k][1].z, tris[k][2].x, tris[k][2].y,
               tris[k][2].z);

    who = malloc(W * H);
    zref = malloc(W * H * 2);
    if (!who || !zref)
        hx_done(1);
    memset(hist, 0, sizeof hist);
    for (k = 0; k < 2; k++) {
        int miss = 0;
        reference(k ? 0.0 : 0.5);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                uint16_t p = *(volatile uint16_t *)(radeon.fb + coff + (uint32_t)y * m.pitch_bytes + (uint32_t)x * 2);
                miss += (p != 0) != (who[y * W + x] >= 0);
            }
        if (k) cov_whole = miss; else cov_half = miss;
    }
    reference(cov_whole < cov_half ? 0.0 : 0.5);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint16_t p = *(volatile uint16_t *)(radeon.fb + coff + (uint32_t)y * m.pitch_bytes + (uint32_t)x * 2);
            int w = who[y * W + x], r, g, b, e, er, eg, eb;
            double l[3], off = cov_whole < cov_half ? 0.0 : 0.5;
            if (w < 0 || p == 0)
                continue;
            drawn++;
            r = ((p >> 11) & 31) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, b = (p & 31) * 255 / 31;
            inside(tris[w], x + off, y + off, l);
            er = abs(r - (int)(l[0] * tris[w][0].r + l[1] * tris[w][1].r + l[2] * tris[w][2].r));
            eg = abs(g - (int)(l[0] * tris[w][0].g + l[1] * tris[w][1].g + l[2] * tris[w][2].g));
            eb = abs(b - (int)(l[0] * tris[w][0].b + l[1] * tris[w][1].b + l[2] * tris[w][2].b));
            e = er > eg ? er : eg;
            e = eb > e ? eb : e;
            hist[e > 255 ? 255 : e]++;
            if (e > 40)
                wrong_winner++;
            else if (e > worst) {
                worst = e;
                sprintf(where, "at %d,%d (triangle %d): %04x", x, y, w, p);
            }
        }
    {
        int miss = cov_whole < cov_half ? cov_whole : cov_half;
        hx_test("coverage", miss <= 3 * 64, "%d pixels differ from the reference's coverage (centres %s), %d drawn",
                miss, cov_whole < cov_half ? "whole" : "half", drawn);
        hx_test("colour", drawn > 1000 && worst <= 24 && wrong_winner <= drawn / 200, "worst %d %s; %d beyond 40 "
                "(another triangle's colour: depth or order)", worst, where, wrong_winner);
    }
    if (perf > 0) {
        /* A 64 x 64 grid of vertices over the middle of the screen (63 x 63 cells, two triangles each, 3 vertices a
         * triangle in the list), each cell 4 pixels: the engine's vertex rate with little to fill. */
        uint32_t goff = aoff + 4096u, t0, us, n = 0, words, base_addr;
        volatile uint32_t *g = (volatile uint32_t *)(radeon.fb + goff);
        int r, cx, cy, c;
        static const int corner[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
        for (cy = 0; cy < 63; cy++)
            for (cx = 0; cx < 63; cx++)
                for (c = 0; c < 6; c++) {
                    double ox = 192 + (cx + corner[c][0]) * 4.0, oy = 112 + (cy + corner[c][1]) * 4.0;
                    volatile uint32_t *v = g + n * 4;
                    double dx = ox - W / 2.0, dy = oy - H / 2.0, cr = cos(rot * 3.14159265358979 / 180),
                           sr = sin(rot * 3.14159265358979 / 180);
                    v[0] = fb((float)(cr * dx + sr * dy + W / 2.0)), v[1] = fb((float)(-sr * dx + cr * dy + H / 2.0));
                    v[2] = fb(0.1f);
                    v[3] = 0xFF000000u | (uint32_t)(cx * 4) << 16 | (uint32_t)(cy * 4) << 8 | 0x80u;
                    n++;
                }
        (void)g[n * 4 - 1];
        words = n;
        base_addr = radeon.fb_location + goff;
        radeon_sync(1000000);
        t0 = sys_time_us();
        for (r = 0; r < perf; r++) {
            /* (in pieces of at most 0xFFFF vertices: the count's field; 23,814 here, one piece) */
            uint32_t *d = radeon_cp_reserve(7);
            d[0] = RADEON_CP_PACKET3(LOAD_VBPNTR, 2);
            d[1] = 1;
            d[2] = 4u << 8 | 4u;
            d[3] = base_addr;
            d[4] = RADEON_CP_PACKET3(DRAW_VBUF, 1);
            d[5] = VC_FRMT_Z | VC_FRMT_PKCOLOR;
            d[6] = VC_TRI_LIST | VC_WALK_LIST | VC_MAOS_ENABLE | VC_RADEON_MODE | VC_TCL_ENABLE | words << 16;
            radeon_cp_advance(7);
            radeon_cp_flush();
        }
        radeon_sync(5000000);
        us = sys_time_us() - t0;
        hx_log("DTCL0-PERF %d draws of %lu triangles (%lu vertices) in %lu us: %lu triangles a second, %lu vertices",
               perf, (unsigned long)(words / 3), (unsigned long)words, (unsigned long)us,
               us ? (unsigned long)((double)perf * (words / 3) * 1e6 / us) : 0ul,
               us ? (unsigned long)((double)perf * words * 1e6 / us) : 0ul);
    }
    radeon_cp_stop();
    radeon_text_mode();
    radeon_unmap();
    hx_done(hx_failures() ? 1 : 0);
    return 0;
}
