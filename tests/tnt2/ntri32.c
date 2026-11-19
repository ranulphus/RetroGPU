/* ntri32.c - NTRI32: the Riva TNT2's 3D at 32 bits a pixel (docs/tnt2.md, T2): the target X8R8G8B8 with a Z24S8
 * buffer (the chip pairs the depth buffer's width with the colour's). One Gouraud triangle (coverage against OpenGL's
 * rule with vertices on the chip's 1/16-pixel grid; the corners' colours), three overlapping flat triangles with
 * LEQUAL and Z writes (each pixel's nearest; a Z value read back), and alpha 128 over a known colour.
 *
 *   NTRI32
 *
 * HX-TEST lines: map, mode, accel, 3d, tri, coverage, colours, zbuf, blend. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "tnt2/tnt2.h"
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static int inside(const tnt2_vertex *v, double px, double py)
{
    double area = edge(v[0].sx, v[0].sy, v[1].sx, v[1].sy, v[2].sx, v[2].sy), s = area > 0 ? 1 : -1;
    int i;
    for (i = 0; i < 3; i++) {
        const tnt2_vertex *a = &v[i], *b = &v[(i + 1) % 3];
        double e = s * edge(a->sx, a->sy, b->sx, b->sy, px, py);
        if (e < 0)
            return 0;
        if (e == 0) {
            double dx = s * (b->sx - a->sx), dy = s * (b->sy - a->sy);
            if (!((dy == 0 && dx > 0) || dy < 0))
                return 0;
        }
    }
    return 1;
}

static int trap_ok(const char *name)
{
    uint32_t a, d, i = tnt2_3d_trap(&a, &d);
    if (i)
        hx_test(name, 0, "PGRAPH_INTR %08lx: trapped method %08lx, data %08lx", (unsigned long)i, (unsigned long)a,
                (unsigned long)d);
    return i == 0;
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch_px = 0, pitch, x, y, k, want = 0, extra = 0, missing = 0;
    uint32_t zoff;
    volatile uint32_t *fb;
    tnt2_vertex v[3];
    (void)argc;
    hx_init(1, argv, "ntri32");
    if (tnt2_find() != 0 || tnt2_map() != 0) {
        hx_test("map", 0, "no NV4-family chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s", tnt2.name);
    if (vbe_find_mode(W, H, 32, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x32 mode");
        hx_done(1);
    }
    pitch = pitch_px * 4;
    hx_test("mode", 1, "mode %03x, pitch %d", m.mode, pitch);
    if (tnt2_accel_init() != 0 || tnt2_3d_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring or the 3D objects didn't start");
        hx_done(1);
    }
    hx_test("accel", 1, "ring and 3D objects up");
    zoff = ((uint32_t)pitch * H + 4095u) & ~4095u;
    tnt2_set_target(zoff, pitch, 32);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0xFFFFFFFFu);
    tnt2_set_target(0, pitch, 32);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_3d_target(0, zoff, pitch, 32, W, H);
    hx_test("3d", tnt2_sync(1000000) == 0 && trap_ok("3d"), "X8R8G8B8 at 0, Z24S8 at %lu", (unsigned long)zoff);
    fb = (volatile uint32_t *)tnt2_fb;

    tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x120001A4u, 0x40182800u, 0);
    memset(v, 0, sizeof v);
    v[0].sx = 100.25f, v[0].sy = 100.5f, v[0].color = 0xFFFF0000u;
    v[1].sx = 500.5f, v[1].sy = 120.25f, v[1].color = 0xFF00FF00u;
    v[2].sx = 300.75f, v[2].sy = 400.5f, v[2].color = 0xFF0000FFu;
    for (x = 0; x < 3; x++)
        v[x].sz = 0.5f, v[x].rhw = 1, v[x].specular = 0xFF000000u;
    tnt2_tri(&v[0], &v[1], &v[2]);
    if (tnt2_sync(1000000) != 0 || !trap_ok("tri")) {
        hx_test("tri", 0, "the engine didn't go idle, or trapped");
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
    hx_test("coverage", extra == 0 && missing == 0, "%d expected: %d outside, %d missing", want, extra, missing);
    {
        static const int px[3][2] = { { 130, 108 }, { 470, 125 }, { 300, 370 } };
        char buf[160] = "";
        int ok = 1;
        for (k = 0; k < 3; k++) {
            uint32_t p = fb[px[k][1] * pitch_px + px[k][0]];
            int c[3] = { (int)(p >> 16 & 255), (int)(p >> 8 & 255), (int)(p & 255) };
            sprintf(buf + strlen(buf), "%06lx; ", (unsigned long)(p & 0xFFFFFF));
            if (c[k] < 160 || c[(k + 1) % 3] > 96 || c[(k + 2) % 3] > 96)
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
        tnt2_vertex t[3][3];
        int bad = 0;
        uint32_t zr;
        tnt2_set_target(zoff, pitch, 32);         /* (Y32: all 32 bits, Z's top byte too) */
        tnt2_fill(0, 0, W, H, 0xFFFFFFFFu);
        tnt2_set_target(0, pitch, 32);
        tnt2_fill(0, 0, W, H, 0);
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
                uint32_t wantc = 0;
                for (k = 0; k < 3; k++)
                    if (inside(t[k], x + 0.5, y + 0.5) && z[k] <= best)
                        best = z[k], wantc = col[k] & 0xFFFFFF;
                if ((fb[y * pitch_px + x] & 0xFFFFFF) != wantc)
                    bad++;
            }
        zr = *(volatile uint32_t *)(tnt2_fb + zoff + (uint32_t)(150 * pitch + 200 * 4));
        hx_test("zbuf", bad == 0, "%d pixels differ; Z read at 200,150 (depth 0.3): %08lx (0.3 x 0xFFFFFF = %06lx, "
                "in bits 31-8: %08lx)", bad, (unsigned long)zr, (unsigned long)(0.3 * 0xFFFFFF),
                (unsigned long)((uint32_t)(0.3 * 0xFFFFFF) << 8));
    }
    {                                           /* blending */
        tnt2_vertex b[3];
        uint32_t p;
        tnt2_fill(0, 0, W, H, 0x0000FFu);
        memset(b, 0, sizeof b);
        b[0].sx = 50, b[0].sy = 50; b[1].sx = 250, b[1].sy = 50; b[2].sx = 50, b[2].sy = 250;
        for (x = 0; x < 3; x++)
            b[x].sz = 0.5f, b[x].rhw = 1, b[x].color = 0x80FF0000u, b[x].specular = 0xFF000000u;
        tnt2_3d_state(tnt2_tex_dummy_off(), 0x110016A1u, 0x11001010u, 0x651001A4u, 0x40182800u, 0);
        tnt2_tri(&b[0], &b[1], &b[2]);
        tnt2_sync(1000000);
        p = fb[80 * pitch_px + 80] & 0xFFFFFF;
        hx_test("blend", (p >> 16) >= 125 && (p >> 16) <= 130 && (p >> 8 & 255) <= 2 && (p & 255) >= 125 &&
                (p & 255) <= 130 && trap_ok("blend"), "red alpha 128 over blue: %06lx", (unsigned long)p);
    }
out:
    tnt2_sync(200000);
    vbe_set_text_mode();
    hx_done(0);
}
