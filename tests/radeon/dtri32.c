/* dtri32.c - DTRI32: the Radeon's 3D at 32 bits a pixel (docs/radeon.md, RD2), as RTRI32 is the Rage 128's: the
 * target ARGB8888 with a Z24S8 buffer, by plain register writes. One Gouraud triangle (coverage against OpenGL's
 * rule with vertices at quarter pixels; the corners' colours), three overlapping flat triangles with LEQUAL and Z
 * writes (each pixel's nearest; a Z value read back to say where the chip keeps the 24 bits), alpha 128 over a known
 * colour, the stencil (a triangle marks it, a quad draws only where marked), the alpha test (GEQUAL 128 along an
 * alpha ramp), and the three texture units multiplied together. Everything from megabyte 16 of the card on.
 *
 *   DTRI32 [base=MB]
 *
 * HX-TEST lines: map, mode, tri, coverage, colours, zbuf, zbits, blend, stencil, stbits, atest, units. */
#include "hx.h"
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "radeon/regs3d_radeon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

static uint32_t base = 16u << 20, zoff, toff;
static int pitch, pitch_px;
static volatile uint32_t *fb;

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* Inside, with the top-left rule for a pixel centre exactly on an edge, as DTRI's reference has it (clockwise on
 * the screen, y down: a top edge is horizontal going right, a left edge goes up). RTRI32's rule had the other
 * sense, and the Radeon left out the 24 pixels this scene has exactly on a right edge. */
static int inside(const radeon_vertex *v, double px, double py)
{
    double area = edge(v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y), s = area > 0 ? 1 : -1;
    int i;
    for (i = 0; i < 3; i++) {
        const radeon_vertex *a = &v[i], *b = &v[(i + 1) % 3];
        double e = s * edge(a->x, a->y, b->x, b->y, px, py);
        if (e < 0)
            return 0;
        if (e == 0) {
            double dx = s * (b->x - a->x), dy = s * (b->y - a->y);
            if (!(dy < 0 || (dy == 0 && dx > 0)))
                return 0;
        }
    }
    return 1;
}

/* The colour buffer to colour; the depth buffer to the farthest depth with stencil 0 (zs). */
static void clear(uint32_t colour, uint32_t zs)
{
    radeon_set_target(zoff, pitch, 32);
    radeon_set_clip(0, 0, W, H);
    radeon_fill(0, 0, W, H, zs);
    radeon_set_target(base, pitch, 32);
    radeon_fill(0, 0, W, H, colour);
}

static void flat(radeon_vertex *v, float x0, float y0, float x1, float y1, float x2, float y2, float z, uint32_t argb)
{
    int i;
    memset(v, 0, 3 * sizeof *v);
    v[0].x = x0, v[0].y = y0, v[1].x = x1, v[1].y = y1, v[2].x = x2, v[2].y = y2;
    for (i = 0; i < 3; i++)
        v[i].z = z, v[i].rhw = 1, v[i].argb = argb, v[i].spec = 0xFF000000u;
}

/* An n x n ARGB8888 texture at off (rows padded to 32 bytes), unit u set to it, NEAREST, wrapped. */
static uint32_t texture(int u, uint32_t off, int log2n, const uint32_t *texels)
{
    int n = 1 << log2n, stride = (n * 4 + 31) & ~31, x, y;
    volatile uint32_t *d = (volatile uint32_t *)(radeon.fb + off);
    for (y = 0; y < n; y++)
        for (x = 0; x < stride / 4; x++)
            d[y * (stride / 4) + x] = x < n ? texels[y * n + x] : 0;
    radeon_3d.tex[u].txfilter = RADEON_MAG_FILTER_NEAREST | RADEON_MIN_FILTER_NEAREST;
    radeon_3d.tex[u].txformat = RADEON_TXFORMAT_ARGB8888 | RADEON_TXFORMAT_ALPHA_IN_MAP |
                                (uint32_t)log2n << RADEON_TXFORMAT_WIDTH_SHIFT | (uint32_t)log2n << RADEON_TXFORMAT_HEIGHT_SHIFT |
                                (uint32_t)u << 24 | RADEON_TXFORMAT_PERSPECTIVE_ENABLE;
    radeon_3d.tex[u].txoffset = radeon.fb_location + off;
    return (off + (uint32_t)(stride * n) + 31u) & ~31u;
}

int main(int argc, char **argv)
{
    radeon_mode m;
    int x, y, k, want = 0, extra = 0, missing = 0;
    radeon_vertex v[3];
    radeon_ctx3d *c = &radeon_3d;
    uint32_t zfar = 0x00FFFFFFu;
    for (k = 1; k < argc; k++)
        if (!strncmp(argv[k], "base=", 5)) base = (uint32_t)atoi(argv[k] + 5) << 20;
    hx_init(1, argv, "dtri32");
    if (radeon_find() != 0 || radeon_map() != 0) {
        hx_test("map", 0, "no Radeon");
        hx_done(1);
    }
    hx_test("map", 1, "%s %04x", radeon.name, radeon.device_id);
    if (radeon_find_mode(W, H, 32, &m) != 0 || radeon_set_mode(&m) != 0) {
        hx_test("mode", 0, "no 640x480x32 mode");
        hx_done(1);
    }
    pitch = m.pitch_bytes, pitch_px = pitch / 4;
    hx_test("mode", 1, "mode %03x, pitch %d", m.mode, pitch);
    zoff = base + 0x200000u, toff = base + 0x400000u;
    fb = (volatile uint32_t *)(radeon.fb + base);
    radeon_set_start(base);
    radeon_3d_format(RADEON_VTX_RHW | RADEON_VTX_ARGB | RADEON_VTX_SPEC);

    clear(0, zfar);
    radeon_3d_init(base, pitch, 32, zoff, pitch, 24, W, H);
    c->rb3d_cntl &= ~RADEON_Z_ENABLE;
    radeon_3d_dirty = 1;
    memset(v, 0, sizeof v);
    v[0].x = 100.25f, v[0].y = 100.5f, v[0].argb = 0xFFFF0000u;
    v[1].x = 500.5f, v[1].y = 120.25f, v[1].argb = 0xFF00FF00u;
    v[2].x = 300.75f, v[2].y = 400.5f, v[2].argb = 0xFF0000FFu;
    for (x = 0; x < 3; x++)
        v[x].z = 0.5f, v[x].rhw = 1, v[x].spec = 0xFF000000u;
    radeon_tri(&v[0], &v[1], &v[2]);
    if (radeon_sync(1000000) != 0) {
        hx_test("tri", 0, "the engine didn't go idle (RBBM_STATUS %08lx)", (unsigned long)radeon_rd(RADEON_RBBM_STATUS));
        goto out;
    }
    hx_test("tri", 1, "drawn");
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int in = inside(v, x + 0.5, y + 0.5), lit = (fb[y * pitch_px + x] & 0xFFFFFF) != 0;
            want += in;
            if (lit && !in) extra++;
            if (in && !lit) missing++;
        }
    hx_test("coverage", extra + missing <= 8, "%d expected: %d outside, %d missing", want, extra, missing);
    {
        static const int px[3][2] = { { 130, 108 }, { 470, 125 }, { 300, 370 } };
        char buf[160] = "";
        int ok = 1;
        for (k = 0; k < 3; k++) {
            uint32_t p = fb[px[k][1] * pitch_px + px[k][0]];
            int cc[3] = { (int)(p >> 16 & 255), (int)(p >> 8 & 255), (int)(p & 255) };
            sprintf(buf + strlen(buf), "%06lx; ", (unsigned long)(p & 0xFFFFFF));
            if (cc[k] < 160 || cc[(k + 1) % 3] > 96 || cc[(k + 2) % 3] > 96)
                ok = 0;
        }
        hx_test("colours", ok, "red, green, blue corners: %s", buf);
    }
    {                                           /* Z24 */
        static const uint32_t col[3] = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu };
        static const float z[3] = { 0.3f, 0.6f, 0.45f };
        static const float pts[3][3][2] = { { { 120, 80 }, { 420, 90 }, { 200, 380 } },
                                            { { 260, 60 }, { 560, 200 }, { 240, 420 } },
                                            { { 80, 250 }, { 520, 300 }, { 330, 460 } } };
        radeon_vertex t[3][3];
        int bad = 0;
        uint32_t zr, z24 = (uint32_t)(0.3 * 0xFFFFFF);
        clear(0, zfar);
        c->rb3d_cntl |= RADEON_Z_ENABLE;
        c->rb3d_zstencilcntl = (c->rb3d_zstencilcntl & ~RADEON_Z_TEST_MASK) | RADEON_Z_TEST_LEQUAL | RADEON_Z_WRITE_ENABLE;
        radeon_3d_dirty = 1;
        for (k = 0; k < 3; k++) {
            flat(t[k], pts[k][0][0], pts[k][0][1], pts[k][1][0], pts[k][1][1], pts[k][2][0], pts[k][2][1], z[k], col[k]);
            radeon_tri(&t[k][0], &t[k][1], &t[k][2]);
        }
        if (radeon_sync(1000000) != 0) {
            hx_test("zbuf", 0, "the engine didn't go idle");
            goto out;
        }
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                float best = 2;
                uint32_t wantc = 0;
                for (k = 0; k < 3; k++)
                    if (inside(t[k], x + 0.5, y + 0.5) && z[k] <= best)
                        best = z[k], wantc = col[k] & 0xFFFFFF;
                if ((fb[y * pitch_px + x] & 0xFFFFFF) != wantc) {
                    if (bad < 10)
                        hx_log("DTRI32-PIX %d,%d: %06lx, want %06lx (Z word %08lx)", x, y,
                               (unsigned long)(fb[y * pitch_px + x] & 0xFFFFFF), (unsigned long)wantc,
                               (unsigned long)*(volatile uint32_t *)(radeon.fb + zoff + (uint32_t)(y * pitch + x * 4)));
                    bad++;
                }
            }
        hx_test("zbuf", bad <= 8, "%d pixels differ", bad);
        zr = *(volatile uint32_t *)(radeon.fb + zoff + (uint32_t)(150 * pitch + 200 * 4));
        hx_test("zbits", (zr & 0xFFFFFF) - z24 + 2 <= 4 || (zr >> 8) - z24 + 2 <= 4,
                "Z read at 200,150 (depth 0.3): %08lx (0.3 x 0xFFFFFF = %06lx: in bits %s)", (unsigned long)zr,
                (unsigned long)z24, (zr & 0xFFFFFF) - z24 + 2 <= 4 ? "23-0" : (zr >> 8) - z24 + 2 <= 4 ? "31-8" : "?");
    }
    {                                           /* blending */
        radeon_vertex b[3];
        uint32_t p;
        clear(0x0000FFu, zfar);
        c->rb3d_cntl = (c->rb3d_cntl & ~RADEON_Z_ENABLE) | RADEON_ALPHA_BLEND_ENABLE;
        c->rb3d_blendcntl = RADEON_COMB_FCN_ADD_CLAMP | RADEON_SRC_BLEND_GL_SRC_ALPHA | RADEON_DST_BLEND_GL_ONE_MINUS_SRC_ALPHA;
        radeon_3d_dirty = 1;
        flat(b, 50, 50, 250, 50, 50, 250, 0.5f, 0x80FF0000u);
        radeon_tri(&b[0], &b[1], &b[2]);
        radeon_sync(1000000);
        p = fb[80 * pitch_px + 80] & 0xFFFFFF;
        hx_test("blend", (p >> 16) >= 125 && (p >> 16) <= 130 && (p >> 8 & 255) <= 2 && (p & 255) >= 125 &&
                (p & 255) <= 130, "red alpha 128 over blue: %06lx", (unsigned long)p);
        c->rb3d_cntl &= ~RADEON_ALPHA_BLEND_ENABLE;
        c->rb3d_blendcntl = RADEON_COMB_FCN_ADD_CLAMP | RADEON_SRC_BLEND_GL_ONE | RADEON_DST_BLEND_GL_ZERO;
    }
    {                                           /* the stencil: a red triangle writes 5, a green quad draws where 5 */
        radeon_vertex a[3], q[3];
        uint32_t in, out, sr;
        clear(0, zfar);
        c->rb3d_cntl |= RADEON_STENCIL_ENABLE;
        c->rb3d_zstencilcntl = (c->rb3d_zstencilcntl & 0xFu) | RADEON_Z_TEST_ALWAYS | RADEON_STENCIL_TEST_ALWAYS |
                               RADEON_STENCIL_FAIL_KEEP | RADEON_STENCIL_ZPASS_REPLACE | RADEON_STENCIL_ZFAIL_KEEP;
        c->rb3d_stencilrefmask = 5u << RADEON_STENCIL_REF_SHIFT | 0xFFu << RADEON_STENCIL_MASK_SHIFT |
                                 0xFFu << RADEON_STENCIL_WRITEMASK_SHIFT;
        radeon_3d_dirty = 1;
        flat(a, 200, 100, 400, 100, 300, 300, 0.5f, 0xFFFF0000u);
        radeon_tri(&a[0], &a[1], &a[2]);
        c->rb3d_zstencilcntl = (c->rb3d_zstencilcntl & 0xFu) | RADEON_Z_TEST_ALWAYS | RADEON_STENCIL_TEST_EQUAL |
                               RADEON_STENCIL_FAIL_KEEP | RADEON_STENCIL_ZPASS_KEEP | RADEON_STENCIL_ZFAIL_KEEP;
        radeon_3d_dirty = 1;
        flat(q, 100, 50, 500, 50, 100, 450, 0.5f, 0xFF00FF00u);
        radeon_tri(&q[0], &q[1], &q[2]);
        radeon_sync(1000000);
        in = fb[150 * pitch_px + 300] & 0xFFFFFF, out = fb[150 * pitch_px + 150] & 0xFFFFFF;
        sr = *(volatile uint32_t *)(radeon.fb + zoff + (uint32_t)(150 * pitch + 300 * 4));
        hx_test("stencil", in == 0x00FF00 && out == 0, "inside the marked triangle %06lx (want 00ff00), outside it %06lx "
                "(want 000000)", (unsigned long)in, (unsigned long)out);
        hx_test("stbits", (sr >> 24) == 5 || (sr & 0xFF) == 5, "the depth word there: %08lx (the stencil's 5 in bits %s)",
                (unsigned long)sr, (sr >> 24) == 5 ? "31-24" : (sr & 0xFF) == 5 ? "7-0" : "?");
        c->rb3d_cntl &= ~RADEON_STENCIL_ENABLE;
    }
    {                                           /* the alpha test: GEQUAL 128 along an alpha ramp from 0 to 255 */
        radeon_vertex r[4];
        int lo, hi;
        clear(0, zfar);
        c->pp_cntl |= RADEON_ALPHA_TEST_ENABLE;
        c->pp_misc = (c->pp_misc & ~(RADEON_ALPHA_TEST_OP_MASK | RADEON_REF_ALPHA_MASK)) | RADEON_ALPHA_TEST_GEQUAL | 128u;
        radeon_3d_dirty = 1;
        memset(r, 0, sizeof r);
        for (k = 0; k < 4; k++) {
            r[k].x = (k == 1 || k == 2) ? 356.0f : 100.0f, r[k].y = k >= 2 ? 300.0f : 100.0f, r[k].z = 0.5f, r[k].rhw = 1;
            r[k].argb = ((k == 1 || k == 2) ? 0xFFu : 0u) << 24 | 0xFFFFFFu, r[k].spec = 0xFF000000u;
        }
        radeon_tri(&r[0], &r[1], &r[2]);
        radeon_tri(&r[0], &r[2], &r[3]);
        radeon_sync(1000000);
        /* (alpha at pixel x is about x - 100: the first drawn pixel of a row should be near 228) */
        for (x = 100; x < 356 && !(fb[200 * pitch_px + x] & 0xFFFFFF); x++)
            ;
        lo = x;
        for (hi = 355; hi > 100 && !(fb[200 * pitch_px + hi] & 0xFFFFFF); hi--)
            ;
        hx_test("atest", lo >= 224 && lo <= 232 && hi == 355, "the row at 200 is drawn from x=%d to %d (want 228 to 355)", lo, hi);
        c->pp_cntl &= ~RADEON_ALPHA_TEST_ENABLE;
        c->pp_misc = (c->pp_misc & ~RADEON_ALPHA_TEST_OP_MASK) | RADEON_ALPHA_TEST_PASS;
    }
    {                                           /* three units: T0 x T1 x T2 */
        static const uint32_t t0[4] = { 0xFFC86432u, 0xFFC86432u, 0xFFC86432u, 0xFFC86432u };    /* (200, 100, 50) */
        static const uint32_t t1[4] = { 0xFF80FF80u, 0xFF80FF80u, 0xFF80FF80u, 0xFF80FF80u };    /* (128, 255, 128) */
        static const uint32_t t2[4] = { 0xFFFFFFFFu, 0xFF0000FFu, 0xFFFFFFFFu, 0xFF0000FFu };    /* white | blue */
        radeon_vertex r[4];
        uint32_t off, l, rr;
        clear(0, zfar);
        radeon_sync(1000000);
        off = texture(0, toff, 1, t0);
        off = texture(1, off, 1, t1);
        off = texture(2, off, 1, t2);
        radeon_vram_written(off - 4);
        c->pp_cntl |= RADEON_TEX_0_ENABLE | RADEON_TEX_1_ENABLE | RADEON_TEX_2_ENABLE | RADEON_TEX_BLEND_0_ENABLE |
                      RADEON_TEX_BLEND_1_ENABLE | RADEON_TEX_BLEND_2_ENABLE;
        c->tex[0].txcblend = RADEON_COLOR_ARG_A_ZERO | RADEON_COLOR_ARG_B_ZERO | RADEON_COLOR_ARG_C_T0_COLOR |
                             RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        c->tex[1].txcblend = RADEON_COLOR_ARG_A_T1_COLOR | RADEON_COLOR_ARG_B_CURRENT_COLOR | RADEON_COLOR_ARG_C_ZERO |
                             RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        c->tex[2].txcblend = RADEON_COLOR_ARG_A_T2_COLOR | RADEON_COLOR_ARG_B_CURRENT_COLOR | RADEON_COLOR_ARG_C_ZERO |
                             RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        radeon_3d_format(RADEON_VTX_RHW | RADEON_VTX_ARGB | RADEON_VTX_SPEC | RADEON_VTX_ST | RADEON_VTX_ST2 | RADEON_VTX_ST3);
        radeon_3d_dirty = 1;
        memset(r, 0, sizeof r);
        for (k = 0; k < 4; k++) {
            float u = (k == 1 || k == 2) ? 1.0f : 0.0f, w = k >= 2 ? 1.0f : 0.0f;
            r[k].x = 100.0f + 400.0f * u, r[k].y = 100.0f + 200.0f * w, r[k].z = 0.5f, r[k].rhw = 1;
            r[k].argb = 0xFFFFFFFFu, r[k].spec = 0xFF000000u;
            r[k].s = u * 3, r[k].t = w * 3, r[k].s2 = u * 5, r[k].t2 = w * 5, r[k].s3 = u, r[k].t3 = w;
        }
        radeon_tri(&r[0], &r[1], &r[2]);
        radeon_tri(&r[0], &r[2], &r[3]);
        radeon_sync(1000000);
        l = fb[200 * pitch_px + 200] & 0xFFFFFF, rr = fb[200 * pitch_px + 400] & 0xFFFFFF;
        /* (the left half: (200, 100, 50) x (128, 255, 128) / 255 = (100, 100, 25); the right: that x blue = (0, 0, 25)) */
        hx_test("units", abs((int)(l >> 16) - 100) <= 3 && abs((int)(l >> 8 & 255) - 100) <= 3 && abs((int)(l & 255) - 25) <= 3 &&
                (rr >> 16) <= 3 && (rr >> 8 & 255) <= 3 && abs((int)(rr & 255) - 25) <= 3,
                "three textures multiplied: left %06lx (want 646419), right %06lx (want 000019)", (unsigned long)l,
                (unsigned long)rr);
    }
out:
    radeon_sync(200000);
    radeon_text_mode();
    hx_done(hx_failures() ? 1 : 0);
}
