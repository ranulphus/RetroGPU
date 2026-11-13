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
 * side 02 on the first line; its Y delta follows side 02 up a line.
 *
 * Textures (data book 15.4.8; the scales from 86Box's model, which bench C
 * checks: docs/virge.md). U and V are the texel position in the largest
 * level, 2^(27 - s) to a texel (s: the mipmap size field), the engine taking
 * the texel from the integer part, wrapped, and its LINEAR weights from the
 * fraction, with no half-texel offset: so OpenGL's is put into the planes.
 * With perspective, U and V are the texel position times W, W being 1/w
 * scaled, and the texel position the engine works out is U/W; D, the mipmap
 * level (its integer part: 0 the largest), is a plane too. */
#include "virge/virge.h"
#include "virge/regs_virge.h"
#include "mga/sys.h"
#include <math.h>

#define WR(off, v) (*(volatile uint32_t *)(virge.mmio + (off)) = (uint32_t)(v))
#define RD(off)    (*(volatile uint32_t *)(virge.mmio + (off)))


static uint32_t d_off, z_off, d_fmt;
static int d_stride, z_stride, d_w, d_h;
static uint32_t z_bits;             /* the command's Z part */
static virge_texture tex;
static int tex_on, blend_mode, fog_on;
static uint32_t fog_rgb;

static const int texel_bytes[3] = { 4, 2, 2 };  /* ARGB8888, ARGB4444, ARGB1555 */

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
    z_bits = zfunc < 0 ? VIRGE_3D_ZB_NONE
                       : VIRGE_3D_ZB_NORMAL | VIRGE_3D_ZCMP((uint32_t)zfunc) | (zwrite ? VIRGE_3D_ZUP : 0);
}

void virge_3d_texture(const virge_texture *t)
{
    tex_on = t != 0;
    if (t)
        tex = *t;
}

void virge_3d_blend(int mode) { blend_mode = mode; }

void virge_3d_fog(int on, uint32_t rgb888)
{
    fog_on = on;
    fog_rgb = rgb888 & 0xFFFFFFu;
}

uint32_t virge_tex_level_offset(int fmt, int log2size, int level)
{
    uint32_t off = 0;
    int k;
    for (k = 0; k < level && k <= log2size; k++)
        off += (uint32_t)texel_bytes[fmt] << (2 * (log2size - k));
    return off;
}

uint32_t virge_tex_bytes(int fmt, int log2size, int levels) { return virge_tex_level_offset(fmt, log2size, levels); }

void virge_tex_load(const virge_texture *t, int level, const void *texels)
{
    uint32_t n = (uint32_t)texel_bytes[t->fmt] << (2 * (t->log2size - level)), i;
    volatile uint8_t *dst = virge.fb + t->offset + virge_tex_level_offset(t->fmt, t->log2size, level);
    const uint8_t *src = (const uint8_t *)texels;
    virge_sync(1000000);                        /* (not under a triangle still drawing from it) */
    if (n & 3) {
        for (i = 0; i < n; i += 2)
            *(volatile uint16_t *)(dst + i) = (uint16_t)(src[i] | src[i + 1] << 8);
    } else {
        for (i = 0; i < n; i += 4)
            *(volatile uint32_t *)(dst + i) = (uint32_t)src[i] | (uint32_t)src[i + 1] << 8 |
                                              (uint32_t)src[i + 2] << 16 | (uint32_t)src[i + 3] << 24;
    }
}

/* Fixed point: S11.20 (x), S8.7 (colour, 16-bit fields), S16.15 (z), and
 * any number of fractional bits (fxn). */
static int32_t fx20(double v) { return (int32_t)floor(v * 1048576.0 + 0.5); }
static int32_t fx7(double v) { return (int32_t)floor(v * 128.0 + 0.5); }
static int32_t fx15(double v) { return (int32_t)floor(v * 32768.0 + 0.5); }
static int32_t fxn(double v, int bits) { return (int32_t)floor(ldexp(v, bits) + 0.5); }
static uint32_t pair(int32_t hi, int32_t lo) { return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFF); }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The attributes, each a plane over the screen. */
enum { A_R, A_G, A_B, A_A, A_Z, A_U, A_V, A_W, A_D, NATTR };

/* With perspective, U = u W and W = 1/w scaled so the triangle's largest is
 * 2^wbits; the engine (86Box's ViRGE/DX model) takes the texel position as
 * U/W, so U is the position times W over 2^11: U's fraction 2^(wbits - 11). */
#define PERSP_UW_SHIFT 11

void virge_tri(const virge_vertex *a, const virge_vertex *b, const virge_vertex *c)
{
    const virge_vertex *v[3] = { a, b, c }, *t;
    double x0, y0, x1, y1, x2, y2, area, yc0, x02, dx02, dx01 = 0, dx12 = 0, x01 = 0, x12 = 0;
    double val[3][NATTR];           /* each attribute at each vertex */
    double grad[NATTR][3];          /* d/dx, d/dy, the value at side 02 on the first line */
    double sx, size = 0, maxd = 0;
    int j0, jm, j2, n01, n12, left, k, i, L = 0, uvbits = 0, wbits = 30, nattr = A_U;
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

    for (i = 0; i < 3; i++) {
        val[i][A_R] = v[i]->r, val[i][A_G] = v[i]->g, val[i][A_B] = v[i]->b, val[i][A_A] = v[i]->a;
        val[i][A_Z] = v[i]->z;
    }
    if (tex_on) {
        /* Texel positions in the largest level: OpenGL's LINEAR samples about
         * the position less half a texel, the engine's about the position;
         * moved by whole textures so none is negative (the engine wraps). */
        double half = tex.linear ? 0.5 : 0, umin, vmin, qmax = 0, umax = 0;
        L = tex.log2size;
        size = ldexp(1.0, L);
        for (i = 0; i < 3; i++) {
            val[i][A_U] = v[i]->s * size - half;
            val[i][A_V] = v[i]->t * size - half;
        }
        umin = val[0][A_U], vmin = val[0][A_V];
        for (i = 1; i < 3; i++) {
            if (val[i][A_U] < umin) umin = val[i][A_U];
            if (val[i][A_V] < vmin) vmin = val[i][A_V];
        }
        umin = floor(umin / size) * size, vmin = floor(vmin / size) * size;
        for (i = 0; i < 3; i++) {
            val[i][A_U] -= umin, val[i][A_V] -= vmin;
            if (val[i][A_U] > umax) umax = val[i][A_U];
            if (val[i][A_V] > umax) umax = val[i][A_V];
            if (v[i]->oow > qmax) qmax = v[i]->oow;
        }
        if (tex.persp && qmax > 0) {
            /* W's largest 2^wbits, unless U would not fit 31 bits; none
             * below 2^16 (86Box's model divides 2^46 by W into 32 bits). */
            while (wbits > 16 && ldexp(umax, wbits - PERSP_UW_SHIFT) >= 2147483647.0)
                wbits--;
            for (i = 0; i < 3; i++) {
                double qn = clampd(v[i]->oow / qmax, ldexp(1.0, 16 - wbits), 1.0);
                val[i][A_W] = qn;
                val[i][A_U] *= qn, val[i][A_V] *= qn;
            }
            uvbits = wbits - PERSP_UW_SHIFT;
        } else {
            for (i = 0; i < 3; i++)
                val[i][A_W] = 1;
            uvbits = 27 - L;
        }
        nattr = A_D;
    }

    /* Each attribute's plane: a = a0 + dadx (x - x0) + dady (y - y0). */
    for (k = 0; k < nattr; k++) {
        double d1 = val[1][k] - val[0][k], d2 = val[2][k] - val[0][k];
        grad[k][0] = (d1 * (y2 - y0) - d2 * (y1 - y0)) / area;
        grad[k][1] = (d2 * (x1 - x0) - d1 * (x2 - x0)) / area;
    }

    /* D: OpenGL's level of detail (log2 of the texels a pixel spans, from the
     * texel position's derivatives), plus half for the nearest level, at
     * each vertex (with perspective it varies), held to the levels there are. */
    if (tex_on) {
        maxd = tex.levels > 1 ? tex.levels - 1 + 0.999 : 0;
        for (i = 0; i < 3; i++) {
            double dudx = grad[A_U][0], dudy = grad[A_U][1], dvdx = grad[A_V][0], dvdy = grad[A_V][1], rho2, lod;
            if (tex.persp) {
                double w = val[i][A_W], u = val[i][A_U] / w, vv = val[i][A_V] / w;
                dudx = (dudx - u * grad[A_W][0]) / w, dudy = (dudy - u * grad[A_W][1]) / w;
                dvdx = (dvdx - vv * grad[A_W][0]) / w, dvdy = (dvdy - vv * grad[A_W][1]) / w;
            }
            rho2 = dudx * dudx + dvdx * dvdx;
            if (dudy * dudy + dvdy * dvdy > rho2)
                rho2 = dudy * dudy + dvdy * dvdy;
            lod = rho2 > 0 ? 0.5 * log(rho2) / log(2.0) : -16;
            val[i][A_D] = clampd(lod + 0.5, 0, maxd);
        }
        {
            double d1 = val[1][A_D] - val[0][A_D], d2 = val[2][A_D] - val[0][A_D];
            grad[A_D][0] = (d1 * (y2 - y0) - d2 * (y1 - y0)) / area;
            grad[A_D][1] = (d2 * (x1 - x0) - d1 * (x2 - x0)) / area;
        }
        nattr = NATTR;
    }
    for (k = 0; k < nattr; k++)
        grad[k][2] = val[0][k] + grad[k][0] * (x02 - x0) + grad[k][1] * (yc0 - y0);

    sx = left ? 1.0 : -1.0;                         /* (X deltas in the drawing direction) */
    cmd = VIRGE_CMD_3D | z_bits | d_fmt | VIRGE_CMD_HC;
    if (tex_on) {
        int lit = tex.env != VIRGE_TEXENV_DECAL || fog_on || blend_mode == VIRGE_BLEND_SOURCE;
        uint32_t filt = tex.levels > 1 ? (tex.linear ? 2u : 0u) : (tex.linear ? 6u : 4u);
        cmd |= tex.persp ? (lit ? VIRGE_3D_LIT_TEX_PERSP : VIRGE_3D_UNLIT_TEX_PERSP)
                         : (lit ? VIRGE_3D_LIT_TEX : VIRGE_3D_UNLIT_TEX);
        if (lit)
            cmd |= VIRGE_3D_TB((uint32_t)tex.env);
        cmd |= VIRGE_3D_TWE | VIRGE_3D_FILTER(filt) | VIRGE_3D_MIPSIZE((uint32_t)L) | VIRGE_3D_TEXFMT((uint32_t)tex.fmt);
        if (fog_on && blend_mode != VIRGE_BLEND_SOURCE)
            cmd |= VIRGE_3D_FOG;
    } else
        cmd |= VIRGE_3D_GOURAUD;
    if (blend_mode == VIRGE_BLEND_TEXTURE && tex_on)
        cmd |= VIRGE_3D_ABC_TEXTURE;
    else if (blend_mode == VIRGE_BLEND_SOURCE)
        cmd |= VIRGE_3D_ABC_SOURCE;

    fifo_wait(16);
    WR(VIRGE_3D_Z_BASE, z_off);
    WR(VIRGE_3D_DEST_BASE, d_off);
    WR(VIRGE_3D_CLIP_LR, ((uint32_t)0 << 16) | (uint32_t)(d_w - 1));
    WR(VIRGE_3D_CLIP_TB, ((uint32_t)0 << 16) | (uint32_t)(d_h - 1));
    /* (the source stride: a flat texture's row, 8 bytes at least) */
    WR(VIRGE_3D_STRIDE, ((uint32_t)d_stride << 16) |
                        (tex_on ? ((uint32_t)(texel_bytes[tex.fmt] << L) + 7u) & 0xFF8u : 0));
    WR(VIRGE_3D_Z_STRIDE, (uint32_t)z_stride);
    if (tex_on) {
        WR(VIRGE_3D_TEX_BASE, tex.offset);
        if (cmd & VIRGE_3D_FOG)
            WR(VIRGE_3D_FOG_COLOR, fog_rgb);
        WR(VIRGE_3D_BV, 0);
        WR(VIRGE_3D_BU, 0);
        WR(VIRGE_3D_DWDX, fxn(sx * grad[A_W][0], wbits));
        WR(VIRGE_3D_DWDY, fxn(grad[A_W][0] * dx02 - grad[A_W][1], wbits));
        WR(VIRGE_3D_WS, fxn(grad[A_W][2], wbits));
        WR(VIRGE_3D_DDDX, fxn(sx * grad[A_D][0], 27));
        fifo_wait(16);
        WR(VIRGE_3D_DVDX, fxn(sx * grad[A_V][0], uvbits));
        WR(VIRGE_3D_DUDX, fxn(sx * grad[A_U][0], uvbits));
        WR(VIRGE_3D_DDDY, fxn(grad[A_D][0] * dx02 - grad[A_D][1], 27));
        WR(VIRGE_3D_DVDY, fxn(grad[A_V][0] * dx02 - grad[A_V][1], uvbits));
        WR(VIRGE_3D_DUDY, fxn(grad[A_U][0] * dx02 - grad[A_U][1], uvbits));
        WR(VIRGE_3D_DS, fxn(clampd(grad[A_D][2], 0, maxd), 27));
        WR(VIRGE_3D_VS, fxn(grad[A_V][2] > 0 ? grad[A_V][2] : 0, uvbits));
        WR(VIRGE_3D_US, fxn(grad[A_U][2] > 0 ? grad[A_U][2] : 0, uvbits));
    }
    fifo_wait(16);
    /* Colours: S8.7, B and G, R and A; starts clamped into 0..255.99. */
    WR(VIRGE_3D_DGDX_DBDX, pair(fx7(sx * grad[A_G][0]), fx7(sx * grad[A_B][0])));
    WR(VIRGE_3D_DADX_DRDX, pair(fx7(sx * grad[A_A][0]), fx7(sx * grad[A_R][0])));
    WR(VIRGE_3D_DGDY_DBDY, pair(fx7(grad[A_G][0] * dx02 - grad[A_G][1]), fx7(grad[A_B][0] * dx02 - grad[A_B][1])));
    WR(VIRGE_3D_DADY_DRDY, pair(fx7(grad[A_A][0] * dx02 - grad[A_A][1]), fx7(grad[A_R][0] * dx02 - grad[A_R][1])));
    WR(VIRGE_3D_GS_BS, pair(fx7(clampd(grad[A_G][2], 0, 255.99)), fx7(clampd(grad[A_B][2], 0, 255.99))));
    WR(VIRGE_3D_AS_RS, pair(fx7(clampd(grad[A_A][2], 0, 255.99)), fx7(clampd(grad[A_R][2], 0, 255.99))));
    /* (Z along a line: 16 fractional bits in 86Box's model, which starts each
     * line's Z at TZS << 1 and adds TdZdX to that; the data book says S16.15
     * for TdZdX as for TZS and TdZdY. Bench C decides: docs/virge.md.) */
    WR(VIRGE_3D_DZDX, (int32_t)floor(sx * grad[A_Z][0] * 65536.0 + 0.5));
    WR(VIRGE_3D_DZDY, fx15(grad[A_Z][0] * dx02 - grad[A_Z][1]));
    WR(VIRGE_3D_ZS, fx15(clampd(grad[A_Z][2], 0, 65535.99)));
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
