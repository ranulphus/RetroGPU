/* ntri0.c - NTRI0: the Riva TNT2's first 3D (docs/tnt2.md, T2's bring-up): the 3D surfaces and textured-triangle
 * objects set up, then one Gouraud triangle (red, green and blue corners; the untextured way: a white texel
 * modulated) on a black 640x480x16 screen. Its coverage is counted against OpenGL's rule (pixel centres inside, the
 * top-left rule), the colours read near each corner, and PGRAPH's interrupt status read after each step: a method
 * that trapped stalls the engine, which is reported rather than waited for.
 *
 *   NTRI0
 *
 * Then a 256x256 quad (two triangles) textured with a 64x64 R5G6B5 texture, NEAREST, DECAL: every pixel must be
 * exactly the texel under its centre, which checks the swizzled layout and the texture's words.
 *
 * Then Z: three overlapping flat triangles at depths 0.3, 0.6 and 0.45 with LEQUAL and Z writes, each pixel's
 * winner against the reference and a Z value read back; blending (source alpha 128 over a known colour); culling
 * (a clockwise and a counter-clockwise triangle under each mode).
 *
 * HX-TEST lines: map, accel, mode, 3d-init, target, tri, coverage, colours, tex, zbuf, blend, cull. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define W 640
#define H 480

static int trap_ok(const char *name)
{
    uint32_t a, d, i = tnt2_3d_trap(&a, &d);
    if (i)
        hx_test(name, 0, "PGRAPH_INTR %08lx: trapped method %08lx (subchannel %lu, method %03lx), data %08lx",
                (unsigned long)i, (unsigned long)a, (unsigned long)((a >> 13) & 7), (unsigned long)(a & 0x1FFC),
                (unsigned long)d);
    return i == 0;
}

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* OpenGL's coverage of a pixel centre (top-left rule for centres on an edge), the triangle either winding. */
static int inside(const tnt2_vertex *v, double px, double py)
{
    double area = edge(v[0].sx, v[0].sy, v[1].sx, v[1].sy, v[2].sx, v[2].sy), s = area > 0 ? 1 : -1;
    int i;
    for (i = 0; i < 3; i++) {
        const tnt2_vertex *a = &v[i], *b = &v[(i + 1) % 3];
        double e = s * edge(a->sx, a->sy, b->sx, b->sy, px, py);
        if (e < 0)
            return 0;
        if (e == 0) {                           /* (top or left edge only) */
            double dx = s * (b->sx - a->sx), dy = s * (b->sy - a->sy);
            if (!((dy == 0 && dx > 0) || dy < 0))
                return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch_px = 0, x, y, want = 0, got = 0, extra = 0, missing = 0;
    uint32_t zoff;
    volatile uint16_t *fb;
    tnt2_vertex v[3];
    hx_init(1, argv, "ntri0");
    (void)argc;
    if (tnt2_find() != 0 || tnt2_map() != 0) {
        hx_test("map", 0, "no NV4-family chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s, %lu MB", tnt2.name, (unsigned long)(tnt2.fb_size >> 20));
    if (vbe_find_mode(W, H, 16, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    hx_test("mode", 1, "mode %03x, pitch %d bytes", m.mode, pitch_px * 2);
    tnt2_set_target(0, pitch_px * 2, 16);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);

    if (tnt2_3d_init() != 0 || !trap_ok("3d-init")) {
        hx_test("3d-init", 0, "the engine didn't go idle, or trapped");
        trap_ok("3d-init-trap");
        goto out;
    }
    hx_test("3d-init", 1, "0x53 on subchannel 4, 0x54 on 7, SURFACES bound");
    zoff = ((uint32_t)pitch_px * 2u * H + 4095u) & ~4095u;
    tnt2_3d_target(0, zoff, pitch_px * 2, 16, W, H);
    if (tnt2_sync(1000000) != 0 || !trap_ok("target")) {
        hx_test("target", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("target", 1, "colour 0, Z %lu, pitch %d", (unsigned long)zoff, pitch_px * 2);

    /* The untextured state (Mesa's): the 1x1 white texel (FORMAT A8R8G8B8, 1 level, repeat, DMA_A), nearest;
     * BLEND MODULATEALPHA | MSB | Gouraud | perspective | ONE, ZERO; CONTROL Z fixed | corner origin | alpha
     * ALWAYS | Z ALWAYS | no culling, no Z test or write. */
    tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x120001A4u, 0x40182800u, 0);
    memset(v, 0, sizeof v);
    v[0].sx = 100.25f, v[0].sy = 100.5f, v[0].color = 0xFFFF0000u;
    v[1].sx = 500.5f,  v[1].sy = 120.25f, v[1].color = 0xFF00FF00u;
    v[2].sx = 300.75f, v[2].sy = 400.5f, v[2].color = 0xFF0000FFu;
    for (x = 0; x < 3; x++)
        v[x].sz = 0.5f, v[x].rhw = 1.0f, v[x].specular = 0xFF000000u;
    tnt2_tri(&v[0], &v[1], &v[2]);
    if (tnt2_sync(1000000) != 0 || !trap_ok("tri")) {
        hx_test("tri", 0, "the engine didn't go idle, or trapped");
        goto out;
    }
    hx_test("tri", 1, "drawn, nothing trapped");

    fb = (volatile uint16_t *)tnt2_fb;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int in = inside(v, x + 0.5, y + 0.5), lit = fb[y * pitch_px + x] != 0;
            want += in, got += lit;
            if (lit && !in) extra++;
            if (in && !lit) missing++;
        }
    hx_test("coverage", extra == 0 && missing == 0, "%d pixels drawn, %d expected: %d outside, %d missing", got, want,
            extra, missing);
    {
        static const int px[3][2] = { { 130, 108 }, { 470, 125 }, { 300, 370 } };
        static const char *what[3] = { "red", "green", "blue" };
        char buf[160] = "";
        int i, ok = 1;
        for (i = 0; i < 3; i++) {
            uint16_t p = fb[px[i][1] * pitch_px + px[i][0]];
            int r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            int big = i == 0 ? r : i == 1 ? g / 2 : b;  /* (5-bit scale) */
            int small = i == 0 ? (g / 2 > b ? g / 2 : b) : i == 1 ? (r > b ? r : b) : (r > g / 2 ? r : g / 2);
            sprintf(buf + strlen(buf), "%s at %d,%d: %04x; ", what[i], px[i][0], px[i][1], p);
            if (big < 20 || small > 12)
                ok = 0;
        }
        hx_test("colours", ok, "%s", buf);
    }
    {
        static uint16_t tex[64 * 64];
        const void *lv[1] = { tex };
        const uint32_t toff = 0x200000u;
        tnt2_vertex q[4];
        int i, bad = 0, bx = -1, by = -1;
        uint16_t bgot = 0, bwant = 0;
        for (i = 0; i < 64 * 64; i++) {          /* a different colour in each texel */
            int tx = i % 64, ty = i / 64;
            tex[i] = (uint16_t)((tx & 31) << 11 | ((ty * 3) & 63) << 5 | ((tx ^ ty) & 31));
        }
        tnt2_sync(1000000);
        tnt2_tex_load(toff, 2, 6, 6, 1, lv);
        tnt2_fill(0, 0, W, H, 0);
        memset(q, 0, sizeof q);
        q[0].sx = 100, q[0].sy = 100, q[0].tu = 0, q[0].tv = 0;
        q[1].sx = 356, q[1].sy = 100, q[1].tu = 1, q[1].tv = 0;
        q[2].sx = 356, q[2].sy = 356, q[2].tu = 1, q[2].tv = 1;
        q[3].sx = 100, q[3].sy = 356, q[3].tu = 0, q[3].tv = 1;
        for (i = 0; i < 4; i++)
            q[i].sz = 0.5f, q[i].rhw = 1.0f, q[i].color = 0xFFFFFFFFu, q[i].specular = 0xFF000000u;
        /* FORMAT: DMA_A, corner origins, R5G6B5, 1 level, 64x64 (log2 6, 6), repeat both ways. FILTER: nearest.
         * BLEND: DECAL | MSB | Gouraud | perspective | ONE, ZERO. */
        tnt2_3d_state(toff, 0x116615A1u, 0x11001010u, 0x120001A1u, 0x40182800u, 0);
        tnt2_tri(&q[0], &q[1], &q[2]);
        tnt2_tri(&q[0], &q[2], &q[3]);
        if (tnt2_sync(1000000) != 0 || !trap_ok("tex")) {
            hx_test("tex", 0, "the engine didn't go idle, or trapped");
            goto out;
        }
        for (y = 100; y < 356; y++)
            for (x = 100; x < 356; x++) {
                uint16_t want = tex[((y - 100) / 4) * 64 + (x - 100) / 4], have = fb[y * pitch_px + x];
                if (have != want) {
                    if (!bad) bx = x, by = y, bgot = have, bwant = want;
                    bad++;
                }
            }
        hx_test("tex", bad == 0, "%d of 65536 pixels differ%s", bad, bad ? "" : " (the swizzle and the words right)");
        if (bad)
            hx_log("NTRI0-TEX first at %d,%d: %04x, want %04x", bx, by, bgot, bwant);
    }
    {                                           /* Z */
        static const uint32_t col[3] = { 0xFFF80000u, 0xFF00FC00u, 0xFF0000F8u };   /* (exact in R5G6B5) */
        static const float z[3] = { 0.3f, 0.6f, 0.45f };
        static const float pts[3][3][2] = { { { 120, 80 }, { 420, 90 }, { 200, 380 } },
                                            { { 260, 60 }, { 560, 200 }, { 240, 420 } },
                                            { { 80, 250 }, { 520, 300 }, { 330, 460 } } };
        tnt2_vertex t[3][3];
        int k, bad = 0, bx = -1, by = -1;
        uint16_t zr;
        tnt2_set_target(zoff, pitch_px * 2, 16);
        tnt2_fill(0, 0, W, H, 0xFFFF);
        tnt2_set_target(0, pitch_px * 2, 16);
        tnt2_fill(0, 0, W, H, 0);
        /* CONTROL: Z fixed | Z write | no culling | Z LEQUAL | Z enable | corner | alpha ALWAYS. */
        tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x120001A4u, 0x41146800u, 0);
        memset(t, 0, sizeof t);
        for (k = 0; k < 3; k++) {
            for (x = 0; x < 3; x++) {
                t[k][x].sx = pts[k][x][0], t[k][x].sy = pts[k][x][1], t[k][x].sz = z[k], t[k][x].rhw = 1;
                t[k][x].color = col[k], t[k][x].specular = 0xFF000000u;
            }
            tnt2_tri(&t[k][0], &t[k][1], &t[k][2]);
        }
        if (tnt2_sync(1000000) != 0 || !trap_ok("zbuf")) {
            hx_test("zbuf", 0, "the engine didn't go idle, or trapped");
            goto out;
        }
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                float best = 2;
                uint16_t want = 0, have = fb[y * pitch_px + x];
                for (k = 0; k < 3; k++)
                    if (inside(t[k], x + 0.5, y + 0.5) && z[k] <= best) {
                        best = z[k];
                        want = (uint16_t)((col[k] >> 8 & 0xF800) | (col[k] >> 5 & 0x07E0) | (col[k] >> 3 & 0x001F));
                    }
                if (have != want) {
                    if (!bad) bx = x, by = y;
                    bad++;
                }
            }
        zr = *(volatile uint16_t *)(tnt2_fb + zoff + (uint32_t)(150 * pitch_px * 2 + 200 * 2));   /* (in 0.3's only) */
        hx_test("zbuf", bad == 0, "%d pixels differ (first at %d,%d); Z read at 200,150 (depth 0.3): %u (0.3 x 65535 = %u)",
                bad, bx, by, zr, (unsigned)(0.3 * 65535 + 0.5));
    }
    {                                           /* blending: alpha 128 over a known colour */
        tnt2_vertex b[3];
        uint16_t p;
        int r, g, bl;
        tnt2_fill(0, 0, W, H, 0x001F);           /* (blue) */
        memset(b, 0, sizeof b);
        b[0].sx = 50, b[0].sy = 50; b[1].sx = 250, b[1].sy = 50; b[2].sx = 50, b[2].sy = 250;
        for (x = 0; x < 3; x++)
            b[x].sz = 0.5f, b[x].rhw = 1, b[x].color = 0x80F80000u, b[x].specular = 0xFF000000u;
        /* BLEND: MODULATEALPHA | MSB | Gouraud | perspective | blend | SRC_ALPHA, ONE_MINUS_SRC_ALPHA. */
        tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x651001A4u, 0x40182800u, 0);
        tnt2_tri(&b[0], &b[1], &b[2]);
        tnt2_sync(1000000);
        p = fb[80 * pitch_px + 80];
        r = p >> 11, g = (p >> 5) & 63, bl = p & 31;
        hx_test("blend", r >= 14 && r <= 17 && g <= 1 && bl >= 14 && bl <= 17 && trap_ok("blend"),
                "red alpha 128 over blue: %04x (r %d g %d b %d; about 15-16 and 15-16 expected)", p, r, g, bl);
    }
    {                                           /* culling: CW and CCW (on the screen, y down) under each mode */
        static const uint32_t mode[3] = { 0x00100000u, 0x00200000u, 0x00300000u };   /* NONE, CW, CCW */
        static const char *mname[3] = { "none", "cw", "ccw" };
        char buf[160] = "";
        tnt2_vertex cw[3], ccw[3];
        int k;
        memset(cw, 0, sizeof cw);
        for (x = 0; x < 3; x++)
            cw[x].sz = 0.5f, cw[x].rhw = 1, cw[x].color = 0xFFFFFFFFu, cw[x].specular = 0xFF000000u;
        memcpy(ccw, cw, sizeof cw);
        cw[0].sx = 300, cw[0].sy = 100; cw[1].sx = 400, cw[1].sy = 100; cw[2].sx = 300, cw[2].sy = 200;   /* (clockwise) */
        ccw[0].sx = 450, ccw[0].sy = 100; ccw[1].sx = 450, ccw[1].sy = 200; ccw[2].sx = 550, ccw[2].sy = 100;
        for (k = 0; k < 3; k++) {
            int a, c;
            tnt2_fill(0, 0, W, H, 0);
            tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x120001A4u,
                          0x40002800u | 0x00080000u | mode[k], 0);
            tnt2_tri(&cw[0], &cw[1], &cw[2]);
            tnt2_tri(&ccw[0], &ccw[1], &ccw[2]);
            tnt2_sync(1000000);
            a = fb[120 * pitch_px + 320] != 0, c = fb[120 * pitch_px + 470] != 0;
            sprintf(buf + strlen(buf), "%s: cw %s ccw %s; ", mname[k], a ? "drawn" : "culled", c ? "drawn" : "culled");
        }
        hx_test("cull", trap_ok("cull"), "%s", buf);
    }
    {                                           /* after 2D: what must be sent again before 3D draws? */
        static const char *vname[4] = { "state", "target+state", "target+surfaces+state", "bind+target+state" };
        char buf[220] = "";
        int k;
        uint32_t zo = zoff;
        for (k = 0; k < 4; k++) {
            tnt2_vertex t[3];
            int lit = 0, xx, yy;
            uint32_t ti, ta, td;
            tnt2_set_target(zo, pitch_px * 2, 16);
            tnt2_fill(0, 0, W, H, 0xFFFF);
            tnt2_set_target(0, pitch_px * 2, 16);
            tnt2_fill(0, 0, W, H, 0);
            if (k == 3)
                tnt2_3d_raw(1, 0, tnt2_3d_handle(1));
            if (k >= 1) {
                tnt2_3d_raw(1, 0x30C, 0);
                tnt2_3d_raw(1, 0x310, zo);
                tnt2_3d_raw(1, 0x300, 0x103);
                tnt2_3d_raw(1, 0x308, (uint32_t)(pitch_px * 2) << 16 | (uint32_t)(pitch_px * 2));
            }
            if (k == 2)
                tnt2_3d_raw(0, 0x18C, tnt2_3d_handle(1));
            tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x120001A4u, 0x40182800u, 0);
            memset(t, 0, sizeof t);
            t[0].sx = 100, t[0].sy = 100; t[1].sx = 300, t[1].sy = 100; t[2].sx = 100, t[2].sy = 300;
            for (x = 0; x < 3; x++)
                t[x].sz = 0.5f, t[x].rhw = 1, t[x].color = 0xFFFFFFFFu, t[x].specular = 0xFF000000u;
            tnt2_tri(&t[0], &t[1], &t[2]);
            tnt2_sync(1000000);
            ti = tnt2_3d_trap(&ta, &td);
            for (yy = 100; yy < 300; yy++)
                for (xx = 100; xx < 300; xx++)
                    lit += fb[yy * pitch_px + xx] != 0;
            sprintf(buf + strlen(buf), "%s: %d lit%s; ", vname[k], lit, ti ? " TRAP" : "");
            if (ti)
                break;
        }
        hx_test("resend", 1, "%s", buf);
    }
    {                                           /* mipsweep: which level the chip uses at k texels a pixel */
        static const uint16_t lc[8] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x8410 };
        static uint16_t lvl[8][128 * 128];
        const void *lv[8];
        const uint32_t toff = 0x200000u;
        static const float aniso[5][3] = { { 1, 1, 0 }, { 1, 0.25f, 0 }, { 0.25f, 1, 0 }, { 1, 1, 45 }, { 1, 1, 30 } };
        int n, a2;
        for (n = 0; n < 8; n++) {
            int sz = 128 >> n, i;
            for (i = 0; i < sz * sz; i++)
                lvl[n][i] = lc[n];
            lv[n] = lvl[n];
        }
        tnt2_sync(1000000);
        tnt2_tex_load(toff, 2, 7, 7, 8, lv);
        for (a2 = 0; a2 < 5; a2++) {
            char buf[400] = "";
            int last = -1, i;
            tnt2_fill(0, 0, W, H, 0);
            /* FORMAT: R5G6B5, 8 levels, 128x128; FILTER: NEAREST_MIPMAP_NEAREST, NEAREST; BLEND: DECAL. */
            tnt2_3d_state(toff, 0x117785A1u, 0x13001010u, 0x120001A1u, 0x40182800u, 0);
            for (i = 0; i < 64; i++) {
                double k = 0.5 * pow(2.0, i / 8.0);
                float x0 = (float)(16 + (i % 32) * 18), y0 = (float)(16 + (i / 32) * 18 + a2 * 40);
                double th = aniso[a2][2] * 3.14159265 / 180, ku = k * aniso[a2][0] / 128, kv = k * aniso[a2][1] / 128;
                tnt2_vertex q[4];
                int j;
                static const int cx[4] = { 0, 16, 16, 0 }, cy[4] = { 0, 0, 16, 16 };
                memset(q, 0, sizeof q);
                for (j = 0; j < 4; j++) {               /* (u, v) = the screen offset rotated by th, scaled */
                    double dx = cx[j], dy = cy[j];
                    q[j].sx = x0 + cx[j], q[j].sy = y0 + cy[j];
                    q[j].tu = (float)(ku * (dx * cos(th) - dy * sin(th)));
                    q[j].tv = (float)(kv * (dx * sin(th) + dy * cos(th)));
                }
                for (j = 0; j < 4; j++)
                    q[j].sz = 0.5f, q[j].rhw = 1, q[j].color = 0xFFFFFFFFu, q[j].specular = 0xFF000000u;
                tnt2_tri(&q[0], &q[1], &q[2]);
                tnt2_tri(&q[0], &q[2], &q[3]);
            }
            tnt2_sync(1000000);
            for (i = 0; i < 64; i++) {
                double k = 0.5 * pow(2.0, i / 8.0);
                int x0 = 16 + (i % 32) * 18, y0 = 16 + (i / 32) * 18 + a2 * 40, j, l = -2;
                (void)k;
                uint16_t p = fb[(y0 + 8) * pitch_px + x0 + 8];
                for (j = 0; j < 8; j++)
                    if (p == lc[j])
                        l = j;
                if (l != last) {
                    sprintf(buf + strlen(buf), "k%.3f:L%d ", k, l);
                    last = l;
                }
            }
            hx_log("NTRI0-MIP %s %s", a2 == 0 ? "iso" : a2 == 1 ? "u-long" : a2 == 2 ? "v-long" : a2 == 3 ? "rot45" : "rot30",
                   buf);
        }
        hx_test("mipsweep", trap_ok("mipsweep"), "levels logged (NTRI0-MIP)");
    }
out:
    tnt2_sync(200000);
    vbe_set_text_mode();
    hx_done(0);
}
