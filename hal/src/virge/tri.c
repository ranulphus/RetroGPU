/* tri.c - the ViRGE/DX's S3d engine, 3D triangles (docs/virge.md, V2): the
 * host's set-up of one triangle into the 3D triangle register group (data
 * book 15.4.5.2 and 19.4), Gouraud shaded with an optional Z test, drawn
 * as OpenGL rasterises: the pixels whose centres are inside, by the top-left
 * rule, each coloured by the plane through the vertices at its centre.
 *
 * How the engine walks a triangle (the data book's figure 15-6, and Loop A):
 * vertices 0, 1, 2 from the bottom up; it starts on the bottom scan line
 * (TYS) and goes up, side 02 on one hand all the way and side 01 then 12 on
 * the other; each line draws from ceil(x02) to ceil(x01|x12), exclusive, in
 * the direction bit 31 of TY01_Y12 gives, with every attribute starting at
 * x02 on that line and stepped by its X delta to the first pixel and on: the
 * X deltas are per pixel in the drawing direction, so negated right to left. Edges are
 * set half a pixel left, so ceil() picks the pixels whose centres are at or
 * past the edge: OpenGL's rule. Each start value is the attribute's plane at
 * side 02 on the first line; its Y delta follows side 02 up a line. */
#include "virge/virge.h"
#include "virge/regs_virge.h"
#include "mga/sys.h"
#include <math.h>

#define WR(off, v) (*(volatile uint32_t *)(virge.mmio + (off)) = (uint32_t)(v))
#define RD(off)    (*(volatile uint32_t *)(virge.mmio + (off)))

static uint32_t d_off, z_off, d_fmt;
static int d_stride, z_stride, d_w, d_h;
static uint32_t cmd_bits;           /* the Z and blending part of the command */

static void fifo_wait(int n)
{
    uint32_t t0 = sys_time_us();
    while ((int)((RD(VIRGE_SUBSYS_STAT) >> VIRGE_STAT_FIFO_SHIFT) & VIRGE_STAT_FIFO_MASK) < n &&
           sys_time_us() - t0 < 200000)
        ;
}

void virge_3d_target(uint32_t dest_off, int dest_stride, int bpp, int w, int h, uint32_t zbuf_off, int zbuf_stride)
{
    d_off = dest_off;
    d_stride = dest_stride;
    d_fmt = bpp == 24 ? VIRGE_CMD_FMT_24 : VIRGE_CMD_FMT_16;
    d_w = w, d_h = h;
    z_off = zbuf_off;
    z_stride = zbuf_stride;
}

void virge_3d_state(int zfunc, int zwrite)
{
    cmd_bits = zfunc < 0 ? VIRGE_3D_ZB_NONE
                         : VIRGE_3D_ZB_NORMAL | VIRGE_3D_ZCMP((uint32_t)zfunc) | (zwrite ? VIRGE_3D_ZUP : 0);
}

/* Fixed point: S11.20 (x), S8.7 (colour, 16-bit fields), S16.15 (z). */
static int32_t fx20(double v) { return (int32_t)floor(v * 1048576.0 + 0.5); }
static int32_t fx7(double v) { return (int32_t)floor(v * 128.0 + 0.5); }
static int32_t fx15(double v) { return (int32_t)floor(v * 32768.0 + 0.5); }
static uint32_t pair(int32_t hi, int32_t lo) { return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFF); }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

void virge_tri(const virge_vertex *a, const virge_vertex *b, const virge_vertex *c)
{
    const virge_vertex *v[3] = { a, b, c }, *t;
    double x0, y0, x1, y1, x2, y2, area, yc0, x02, dx02, dx01 = 0, dx12 = 0, x01 = 0, x12 = 0;
    double grad[5][3];              /* r, g, b, a, z: d/dx, d/dy, the value at side 02 on the first line */
    double sx;
    int j0, jm, j2, n01, n12, left, k;
    uint32_t cmd;

    /* From the bottom (largest y) up. */
    if (v[0]->y < v[1]->y) t = v[0], v[0] = v[1], v[1] = t;
    if (v[1]->y < v[2]->y) t = v[1], v[1] = v[2], v[2] = t;
    if (v[0]->y < v[1]->y) t = v[0], v[0] = v[1], v[1] = t;
    x0 = v[0]->x, y0 = v[0]->y, x1 = v[1]->x, y1 = v[1]->y, x2 = v[2]->x, y2 = v[2]->y;
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0 || y0 == y2)
        return;
    /* The lines whose centres (j + 0.5) are in [y2, y0): 01's part from the
     * bottom one (j0) to jm, then 12's up to j2. */
    j0 = (int)ceil(y0 - 0.5) - 1;
    jm = (int)ceil(y1 - 0.5);
    j2 = (int)ceil(y2 - 0.5);
    n01 = j0 - jm + 1;
    n12 = jm - j2;
    if (n01 < 0) n01 = 0;
    if (n12 < 0) n12 = 0;
    if (n01 + n12 == 0)
        return;
    yc0 = j0 + 0.5;
    dx02 = (x2 - x0) / (y0 - y2);                   /* per line up */
    x02 = x0 + (y0 - yc0) * dx02;
    if (y0 != y1) {
        dx01 = (x1 - x0) / (y0 - y1);
        x01 = x0 + (y0 - yc0) * dx01;
    }
    if (y1 != y2) {
        dx12 = (x2 - x1) / (y1 - y2);
        x12 = x1 + (y1 - (jm - 0.5)) * dx12;        /* on the first line of 12's part */
    }
    left = x0 + (y0 - y1) * dx02 < x1;              /* side 02 on the left: draw left to right */

    /* Each attribute's plane: a = a0 + dadx (x - x0) + dady (y - y0). */
    for (k = 0; k < 5; k++) {
        double a0, a1, a2, d1, d2;
        if (k < 4) {
            a0 = (&v[0]->r)[k], a1 = (&v[1]->r)[k], a2 = (&v[2]->r)[k];
        } else {
            a0 = v[0]->z, a1 = v[1]->z, a2 = v[2]->z;
        }
        d1 = a1 - a0, d2 = a2 - a0;
        grad[k][0] = (d1 * (y2 - y0) - d2 * (y1 - y0)) / area;
        grad[k][1] = (d2 * (x1 - x0) - d1 * (x2 - x0)) / area;
        grad[k][2] = a0 + grad[k][0] * (x02 - x0) + grad[k][1] * (yc0 - y0);
    }

    sx = left ? 1.0 : -1.0;                         /* (X deltas in the drawing direction) */
    cmd = VIRGE_CMD_3D | VIRGE_3D_GOURAUD | cmd_bits | d_fmt | VIRGE_CMD_HC;
    fifo_wait(16);
    WR(VIRGE_3D_Z_BASE, z_off);
    WR(VIRGE_3D_DEST_BASE, d_off);
    WR(VIRGE_3D_CLIP_LR, ((uint32_t)0 << 16) | (uint32_t)(d_w - 1));
    WR(VIRGE_3D_CLIP_TB, ((uint32_t)0 << 16) | (uint32_t)(d_h - 1));
    WR(VIRGE_3D_STRIDE, ((uint32_t)d_stride << 16));
    WR(VIRGE_3D_Z_STRIDE, (uint32_t)z_stride);
    /* Colours: S8.7, B and G, R and A; starts clamped into 0..255.99. */
    WR(VIRGE_3D_DGDX_DBDX, pair(fx7(sx * grad[1][0]), fx7(sx * grad[2][0])));
    WR(VIRGE_3D_DADX_DRDX, pair(fx7(sx * grad[3][0]), fx7(sx * grad[0][0])));
    WR(VIRGE_3D_DGDY_DBDY, pair(fx7(grad[1][0] * dx02 - grad[1][1]), fx7(grad[2][0] * dx02 - grad[2][1])));
    WR(VIRGE_3D_DADY_DRDY, pair(fx7(grad[3][0] * dx02 - grad[3][1]), fx7(grad[0][0] * dx02 - grad[0][1])));
    WR(VIRGE_3D_GS_BS, pair(fx7(clampd(grad[1][2], 0, 255.99)), fx7(clampd(grad[2][2], 0, 255.99))));
    WR(VIRGE_3D_AS_RS, pair(fx7(clampd(grad[3][2], 0, 255.99)), fx7(clampd(grad[0][2], 0, 255.99))));
    /* (Z along a line: 16 fractional bits in 86Box's model, which starts each
     * line's Z at TZS << 1 and adds TdZdX to that; the data book says S16.15
     * for TdZdX as for TZS and TdZdY. Bench C decides: docs/virge.md.) */
    WR(VIRGE_3D_DZDX, (int32_t)floor(sx * grad[4][0] * 65536.0 + 0.5));
    WR(VIRGE_3D_DZDY, fx15(grad[4][0] * dx02 - grad[4][1]));
    WR(VIRGE_3D_ZS, fx15(clampd(grad[4][2], 0, 65535.99)));
    fifo_wait(9);
    WR(VIRGE_3D_DXDY12, fx20(dx12));
    WR(VIRGE_3D_XEND12, fx20(x12 - 0.5));
    WR(VIRGE_3D_DXDY01, fx20(dx01));
    WR(VIRGE_3D_XEND01, fx20(x01 - 0.5));
    WR(VIRGE_3D_DXDY02, fx20(dx02));
    WR(VIRGE_3D_XS, fx20(x02 - 0.5));
    WR(VIRGE_3D_YS, (uint32_t)j0);
    WR(VIRGE_3D_Y01_Y12, (left ? 0x80000000u : 0) | ((uint32_t)n01 << 16) | (uint32_t)n12);
    WR(VIRGE_3D_CMD, cmd);                          /* (autoexecute off: this write draws) */
}
