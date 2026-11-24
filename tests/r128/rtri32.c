/* rtri32.c - RTRI32: the Rage 128's 3D at 32 bits a pixel (docs/rage128.md, R2), as NTRI32 is the TNT2's: the
 * target ARGB8888 with a Z24S8 buffer. One Gouraud triangle (coverage against OpenGL's rule with vertices at
 * quarter pixels; the corners' colours), three overlapping flat triangles with LEQUAL and Z writes (each pixel's
 * nearest; a Z value read back to say where the chip keeps the 24 bits), and alpha 128 over a known colour.
 *
 *   RTRI32
 *
 * HX-TEST lines: map, mode, cce, tri, coverage, colours, zbuf, zbits, blend. */
#include "hx.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* Inside with OpenGL's top-left rule for pixels on an edge. */
static int inside(const r128_vertex *v, double px, double py)
{
    double area = edge(v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y), s = area > 0 ? 1 : -1;
    int i;
    for (i = 0; i < 3; i++) {
        const r128_vertex *a = &v[i], *b = &v[(i + 1) % 3];
        double e = s * edge(a->x, a->y, b->x, b->y, px, py);
        if (e < 0)
            return 0;
        if (e == 0) {
            double dx = s * (b->x - a->x), dy = s * (b->y - a->y);
            if (!((dy == 0 && dx > 0) || dy < 0))
                return 0;
        }
    }
    return 1;
}

static void clear(uint32_t zoff, int pitch, uint32_t colour)
{
    r128_set_target(zoff, W * 4, 32);
    r128_set_clip(0, 0, W, H);
    r128_fill(0, 0, W, H, 0xFFFFFFFFu);
    r128_set_target(0, pitch, 32);
    r128_fill(0, 0, W, H, colour);
}

int main(int argc, char **argv)
{
    r128_mode m;
    int pitch_px, pitch, x, y, k, want = 0, extra = 0, missing = 0;
    uint32_t zoff;
    volatile uint32_t *fb;
    r128_vertex v[3];
    (void)argc;
    hx_init(1, argv, "rtri32");
    if (r128_find() != 0 || r128_map() != 0) {
        hx_test("map", 0, "no Rage 128");
        hx_done(1);
    }
    hx_test("map", 1, "Rage 128 %04x", r128.device_id);
    if (r128_find_mode(W, H, 32, &m) != 0 || r128_set_mode(&m) != 0) {
        hx_test("mode", 0, "no 640x480x32 mode");
        hx_done(1);
    }
    pitch = m.pitch_bytes, pitch_px = pitch / 4;
    hx_test("mode", 1, "mode %03x, pitch %d", m.mode, pitch);
    if (r128_cce_init(NULL) != 0) {
        r128_text_mode();
        hx_test("cce", 0, "the CCE did not start");
        hx_done(1);
    }
    hx_test("cce", 1, "192PIO");
    zoff = ((uint32_t)pitch * H + 4095u) & ~4095u;
    fb = (volatile uint32_t *)r128.fb;
    r128_3d_format(R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB | R128_VC_FRMT_SPEC_FRGB);

    clear(zoff, pitch, 0);
    r128_3d_init(0, pitch, 32, zoff, W * 4, 24);
    memset(v, 0, sizeof v);
    v[0].x = 100.25f, v[0].y = 100.5f, v[0].argb = 0xFFFF0000u;
    v[1].x = 500.5f, v[1].y = 120.25f, v[1].argb = 0xFF00FF00u;
    v[2].x = 300.75f, v[2].y = 400.5f, v[2].argb = 0xFF0000FFu;
    for (x = 0; x < 3; x++)
        v[x].z = 0.5f, v[x].rhw = 1, v[x].spec = 0xFF000000u;
    r128_tri(&v[0], &v[1], &v[2]);
    if (r128_sync(1000000) != 0) {
        hx_test("tri", 0, "the engine didn't go idle");
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
        r128_vertex t[3][3];
        int bad = 0;
        uint32_t zr, z24 = (uint32_t)(0.3 * 0xFFFFFF);
        clear(zoff, pitch, 0);
        r128_3d.tex_cntl_c |= R128_Z_ENABLE;
        r128_3d.z_sten_cntl_c = (r128_3d.z_sten_cntl_c & ~(7u << R128_Z_TEST_SHIFT)) | 2u << R128_Z_TEST_SHIFT;
        r128_3d_emit();
        memset(t, 0, sizeof t);
        for (k = 0; k < 3; k++) {
            for (x = 0; x < 3; x++) {
                t[k][x].x = pts[k][x][0], t[k][x].y = pts[k][x][1], t[k][x].z = z[k], t[k][x].rhw = 1;
                t[k][x].argb = col[k], t[k][x].spec = 0xFF000000u;
            }
            r128_tri(&t[k][0], &t[k][1], &t[k][2]);
        }
        if (r128_sync(1000000) != 0) {
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
                if ((fb[y * pitch_px + x] & 0xFFFFFF) != wantc)
                    bad++;
            }
        hx_test("zbuf", bad == 0, "%d pixels differ", bad);
        zr = *(volatile uint32_t *)(r128.fb + zoff + (uint32_t)(150 * W * 4 + 200 * 4));
        hx_test("zbits", (zr & 0xFFFFFF) - z24 + 2 <= 4 || (zr >> 8) - z24 + 2 <= 4,
                "Z read at 200,150 (depth 0.3): %08lx (0.3 x 0xFFFFFF = %06lx: in bits %s)", (unsigned long)zr,
                (unsigned long)z24, (zr & 0xFFFFFF) - z24 + 2 <= 4 ? "23-0" : (zr >> 8) - z24 + 2 <= 4 ? "31-8" : "?");
    }
    {                                           /* blending */
        r128_vertex b[3];
        uint32_t p;
        clear(zoff, pitch, 0x0000FFu);
        r128_3d.tex_cntl_c = (r128_3d.tex_cntl_c & ~(uint32_t)R128_Z_ENABLE) | R128_ALPHA_ENABLE;
        r128_3d.misc_3d_state_cntl_reg = (r128_3d.misc_3d_state_cntl_reg & ~(0xFu << R128_ALPHA_BLEND_SRC_SHIFT |
                                          0xFu << R128_ALPHA_BLEND_DST_SHIFT)) |
                                         (uint32_t)R128_BLEND_SRCALPHA << R128_ALPHA_BLEND_SRC_SHIFT |
                                         (uint32_t)R128_BLEND_INVSRCALPHA << R128_ALPHA_BLEND_DST_SHIFT;
        r128_3d_emit();
        memset(b, 0, sizeof b);
        b[0].x = 50, b[0].y = 50; b[1].x = 250, b[1].y = 50; b[2].x = 50, b[2].y = 250;
        for (x = 0; x < 3; x++)
            b[x].z = 0.5f, b[x].rhw = 1, b[x].argb = 0x80FF0000u, b[x].spec = 0xFF000000u;
        r128_tri(&b[0], &b[1], &b[2]);
        r128_sync(1000000);
        p = fb[80 * pitch_px + 80] & 0xFFFFFF;
        hx_test("blend", (p >> 16) >= 125 && (p >> 16) <= 130 && (p >> 8 & 255) <= 2 && (p & 255) >= 125 &&
                (p & 255) <= 130, "red alpha 128 over blue: %06lx", (unsigned long)p);
    }
out:
    hx_snap_screen("RTRI32");
    r128_sync(200000);
    r128_text_mode();
    hx_done(hx_failures() ? 1 : 0);
}
