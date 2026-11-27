/* dcfog.c - DCFOG: what Celsius (0x96) does with a vertex's fog value, measured on the NV1x (the Quadro2 plan's Q4:
 * DOS-GL's Celsius back end hands the chip either a fog factor or a fog coordinate, and Mesa's constants for exp and
 * exp2 are approximations). Each case draws 32 bands across the screen, white, fogged towards black, each band's fog
 * value c the same at its four corners, and reads the green channel back at the band's centre: f = green / max.
 *
 * The model under test (envytools' hwtest, nvhw/pgraph_celsius_xfrm.c, with the transform not bypassed, 0x3E8 = 6
 * as Mesa's init sets it): v = k0 * C + k1 * c + k2 * c * c with k = FOG_COEFF(0..2) and C a constant; then FOG_MODE
 * in the rasteriser: linear (0x2601) f = v - 1 (Mesa's k0 = 2 + s / (e - s)); exp (0x800) and exp2 (0x803) unknown.
 * The vertex's fog value is the secondary colour's alpha (vertex buffer slot 7 writes it there).
 *
 * Two more: a quad whose fog goes 0..1 left to right while w goes 1..4, and the same with green going 0..1 and no fog:
 * whether the chip interpolates fog and colour in screen space or in perspective.
 *
 *   DCFOG [16] [persp]   (32 bits a pixel unless 16 is given; persp: only the interpolation cases)
 *
 * HX-TEST lines: map, mode, accel, celsius, and one per case (drawn, nothing trapped). The measurements are
 * "DCFOG case ..." lines. */
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
#define BANDS 32

static int bpp = 32, pitch_px;
static uint32_t vb, zoff;

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

/* The green channel at a window position (y up) as a fraction of its largest value. */
static double green(int x, int wy)
{
    int y = H - 1 - wy;
    if (bpp == 32) {
        uint32_t p = ((volatile uint32_t *)tnt2_fb)[y * pitch_px + x];
        return ((p >> 8) & 255) / 255.0;
    } else {
        uint16_t p = ((volatile uint16_t *)tnt2_fb)[y * pitch_px + x];
        return ((p >> 5) & 63) / 63.0;
    }
}

/* Clear, then the viewport and a projection that takes window coordinates (y up) as they are (DCEL1's). */
static void frame(void)
{
    uint32_t pm[16];
    int i;
    tnt2_set_target(0, pitch_px * (bpp / 8), bpp);
    tnt2_fill(0, 0, W, H, bpp == 32 ? 0x00404040u : 0x2104u);
    tnt2_sync(1000000);
    tnt2_celsius_target(0, zoff, pitch_px * (bpp / 8), W, H, bpp);
    m1(0x6E8, fb(-2048.0f + W / 2.0f));
    m1(0x6EC, fb(-2048.0f + H / 2.0f));
    m1(0x6F0, fb(0.0f));
    m1(0x6F4, 0);
    for (i = 0; i < 16; i++)
        pm[i] = 0;
    pm[0] = fb(1.0f), pm[3] = fb(-W / 2.0f);
    pm[5] = fb(-1.0f), pm[7] = fb(H / 2.0f);
    pm[10] = fb(1.0f);
    pm[15] = fb(1.0f);
    tnt2_methods(S7, 0x500, pm, 16);
    m1(0x30C, 0);                                       /* DEPTH_TEST_ENABLE off */
    m1(0x35C, 0);                                       /* DEPTH_WRITE_ENABLE off */
    m1(0x228, 0);                                       /* texture 0 off */
    m1(0x260, 0); m1(0x268, 0); m1(0x270, 0); m1(0x278, 0); m1(0x280, 0);
}

static void fog(int on, uint32_t mode, float k0, float k1, float k2)
{
    m1(0x29C, mode);                                    /* FOG_MODE */
    m1(0x2A0, 0);                                       /* FOG_COORD: the vertex's fog value */
    m1(0x2A4, (uint32_t)on);                            /* FOG_ENABLE */
    m1(0x2A8, 0xFF000000u);                             /* FOG_COLOR: black (R in bits 7:0) */
    m1(0x680, fb(k0)); m1(0x684, fb(k1)); m1(0x688, fb(k2));
    if (on) {                                           /* (Mesa's final combiner with fog: A fog's alpha, B the */
        m1(0x288, 0x13040300);                          /* primary colour, C the fog colour; G the primary's alpha) */
        m1(0x28C, 0x13001400);
    } else {
        m1(0x288, 0x20042000);
        m1(0x28C, 0x20001400);
    }
}

static void vtxbuf(const uint32_t fmt[8], const uint32_t off[8])
{
    int i;
    for (i = 0; i < 8; i++)
        m1(0xD04 + 8u * (uint32_t)i, fmt[i]);
    for (i = 0; i < 8; i++)
        if (fmt[i] & 0xF0)
            m1(0xD00 + 8u * (uint32_t)i, off[i]);
    m1(0xCF0, 0);                                       /* VTXBUF_VALIDATE */
}

static void draw_quads(int n)
{
    m1(0x13FC, 8);                                      /* GL_QUADS + 1 */
    m1(0x1400, (uint32_t)(n - 1) << 24);
    m1(0x13FC, 0);
    tnt2_kick();
}

struct fcase { const char *name; uint32_t mode; float k0, k1, k2, c0, c1; const char *model; };

/* The models' f for a case at c (clamped to 0..1), for the log beside the measurement. */
static double model_f(const struct fcase *k, double c)
{
    double v = k->k0 + k->k1 * c + k->k2 * c * c, f;
    if (k->mode == 0x2601)
        f = v - 1.0;
    else if (k->mode == 0x800 || k->mode == 0x802)
        f = pow(2.0, 16.0 * v - 24.0);                  /* (a guess: Mesa's k1 = -log2(e) / 16 per unit density) */
    else
        f = pow(2.0, -(16.0 * v - 24.0) * (16.0 * v - 24.0) / 8.0);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static int run_bands(const struct fcase *k)
{
    float q[BANDS * 4][6];
    static const uint32_t fmt[8] = { 0x1822, 0x1832, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x1812 };
    uint32_t off[8] = { 0 };
    char line[200];
    int i, j;
    double worst = 0;
    frame();
    fog(1, k->mode, k->k0, k->k1, k->k2);
    for (i = 0; i < BANDS; i++) {
        float c = k->c0 + (k->c1 - k->c0) * (float)i / (BANDS - 1);
        float y0 = (float)(i * (H / BANDS)), y1 = y0 + (float)(H / BANDS);
        static const float xs[4] = { 0, W, W, 0 };
        for (j = 0; j < 4; j++) {
            float *v = q[i * 4 + j];
            v[0] = xs[j], v[1] = j < 2 ? y0 : y1;
            v[2] = v[3] = v[4] = 1.0f;
            v[5] = c;
        }
    }
    memcpy((void *)(tnt2_fb + vb), q, sizeof q);
    (void)tnt2_fb[vb];
    off[0] = vb, off[1] = vb + 8, off[7] = vb + 20;
    vtxbuf(fmt, off);
    draw_quads(BANDS * 4);
    if (tnt2_sync(1000000) != 0 || !trap_ok(k->name)) {
        hx_test(k->name, 0, "the engine didn't go idle, or trapped");
        return -1;
    }
    for (i = 0; i < BANDS; i++) {
        double c = k->c0 + (k->c1 - k->c0) * i / (BANDS - 1);
        double f = green(W / 2, i * (H / BANDS) + H / BANDS / 2), m = model_f(k, c);
        snprintf(line, sizeof line, "DCFOG %s c=%.4f f=%.4f model=%.4f", k->name, c, f, m);
        hx_log("%s", line);
        if (fabs(f - m) > worst)
            worst = fabs(f - m);
    }
    hx_test(k->name, 1, "drawn; mode %04lx k %g %g %g, c %g..%g; largest difference from %s: %.4f",
            (unsigned long)k->mode, k->k0, k->k1, k->k2, k->c0, k->c1, k->model, worst);
    return 0;
}

/* A quad from x 64 to 576 whose w goes 1 to 4 left to right; fog (or green) 0 to 1 the same way. (A position with w
 * needs VTXBUF_FMT's homogeneous bit 24: without it four fields trap, PGRAPH_INTR bit 20, as envytools' hwtest says.) */
static int run_persp(const char *name, int with_fog)
{
    static const float xs[4] = { 64, 576, 576, 64 }, ys[4] = { 160, 160, 320, 320 }, ws[4] = { 1, 4, 4, 1 };
    static const uint32_t fmt[8] = { 0x01002042, 0x2032, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x2012 };
    float q[4][8];
    uint32_t off[8] = { 0 };
    double worst_lin = 0, worst_persp = 0;
    int i;
    frame();
    fog(with_fog, 0x2601, 1.0f, 1.0f, 0.0f);            /* (f = c if the linear model holds) */
    for (i = 0; i < 4; i++) {
        float t = xs[i] > 100 ? 1.0f : 0.0f;
        q[i][0] = xs[i] * ws[i], q[i][1] = ys[i] * ws[i], q[i][2] = 0, q[i][3] = ws[i];
        q[i][4] = with_fog ? 1.0f : 0.0f, q[i][5] = with_fog ? 1.0f : t, q[i][6] = with_fog ? 1.0f : 0.0f;
        q[i][7] = t;
    }
    memcpy((void *)(tnt2_fb + vb), q, sizeof q);
    (void)tnt2_fb[vb];
    off[0] = vb, off[1] = vb + 16, off[7] = vb + 28;
    vtxbuf(fmt, off);
    draw_quads(4);
    if (tnt2_sync(1000000) != 0 || !trap_ok(name)) {
        hx_test(name, 0, "the engine didn't go idle, or trapped");
        return -1;
    }
    for (i = 1; i < 10; i++) {
        int x = 64 + i * 512 / 10;
        double s = (x + 0.5 - 64) / 512.0;
        double lin = s, persp = (s / 4.0) / ((1.0 - s) + s / 4.0);
        double g = green(x, 240);
        hx_log("DCFOG %s s=%.4f got=%.4f screen=%.4f persp=%.4f", name, s, g, lin, persp);
        if (fabs(g - lin) > worst_lin) worst_lin = fabs(g - lin);
        if (fabs(g - persp) > worst_persp) worst_persp = fabs(g - persp);
    }
    hx_test(name, 1, "drawn; largest difference from screen-space %.4f, from perspective %.4f (%s)", worst_lin,
            worst_persp, worst_persp < worst_lin ? "perspective" : "screen space");
    return 0;
}

/* Where exp and exp2 are applied: a quad (w 1) whose fog value goes c0..c1 left to right. Applied to each pixel's
 * interpolated v, the middle shows the curve at the middle c; applied at the vertices, the curve's ends averaged. */
static int run_grad(const struct fcase *k)
{
    static const float xs[4] = { 64, 576, 576, 64 }, ys[4] = { 160, 160, 320, 320 };
    static const uint32_t fmt[8] = { 0x1822, 0x1832, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x1812 };
    float q[4][6];
    uint32_t off[8] = { 0 };
    double worst_px = 0, worst_vx = 0, f0, f1;
    int i;
    frame();
    fog(1, k->mode, k->k0, k->k1, k->k2);
    for (i = 0; i < 4; i++) {
        q[i][0] = xs[i], q[i][1] = ys[i];
        q[i][2] = q[i][3] = q[i][4] = 1.0f;
        q[i][5] = xs[i] > 100 ? k->c1 : k->c0;
    }
    memcpy((void *)(tnt2_fb + vb), q, sizeof q);
    (void)tnt2_fb[vb];
    off[0] = vb, off[1] = vb + 8, off[7] = vb + 20;
    vtxbuf(fmt, off);
    draw_quads(4);
    if (tnt2_sync(1000000) != 0 || !trap_ok(k->name)) {
        hx_test(k->name, 0, "the engine didn't go idle, or trapped");
        return -1;
    }
    f0 = model_f(k, k->c0), f1 = model_f(k, k->c1);
    for (i = 1; i < 10; i++) {
        int x = 64 + i * 512 / 10;
        double s = (x + 0.5 - 64) / 512.0, c = k->c0 + (k->c1 - k->c0) * s;
        double px = model_f(k, c), vx = f0 + (f1 - f0) * s, g = green(x, 240);
        hx_log("DCFOG %s s=%.4f got=%.4f per-pixel=%.4f per-vertex=%.4f", k->name, s, g, px, vx);
        if (fabs(g - px) > worst_px) worst_px = fabs(g - px);
        if (fabs(g - vx) > worst_vx) worst_vx = fabs(g - vx);
    }
    hx_test(k->name, 1, "drawn; largest difference from per-pixel %.4f, from per-vertex %.4f (%s)", worst_px,
            worst_vx, worst_px < worst_vx ? "per pixel" : "per vertex");
    return 0;
}

int main(int argc, char **argv)
{
    static const struct fcase cases_all[] = {
        { "lin-1-1", 0x2601, 1.0f, 1.0f, 0.0f, -0.5f, 2.5f, "f = c" },
        { "lin-2-m1", 0x2601, 2.0f, -1.0f, 0.0f, -0.5f, 2.5f, "f = 1 - c" },
        { "lin-0-1", 0x2601, 0.0f, 1.0f, 0.0f, 0.5f, 3.5f, "f = c - 1" },
        { "lin-.5-.5", 0x2601, 1.5f, 0.5f, 0.0f, -1.5f, 1.5f, "f = 0.5 + c / 2" },
        { "lin-k2", 0x2601, 1.0f, 0.0f, 1.0f, -1.5f, 1.5f, "f = c * c" },
        { "exp", 0x800, 1.5f, -0.0901684f, 0.0f, 0.0f, 6.4f, "f = exp(-c)" },
        { "exp-k0", 0x800, 1.25f, -0.0901684f, 0.0f, 0.0f, 6.4f, "f = 2^(16 v - 24)" },
        { "exp-abs", 0x802, 1.5f, -0.0901684f, 0.0f, -3.2f, 3.2f, "f = exp(-|c|)?" },
        { "exp2", 0x803, 1.5f, -0.2123305f, 0.0f, 0.0f, 3.2f, "f = exp(-c * c)" },
    };
    const struct fcase *cases = cases_all;
    unsigned ncases = sizeof cases_all / sizeof cases_all[0];
    mga_vbe_mode m;
    unsigned i;
    int a;
    hx_init(1, argv, "dcfog");
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "16"))
            bpp = 16;
        else if (!strcmp(argv[a], "persp"))
            ncases = 0;                                 /* (only the interpolation cases) */
    }
    if (tnt2_find() != 0 || tnt2_map() != 0 || tnt2.arch != 10) {
        hx_test("map", 0, "no NV1x chip");
        hx_done(1);
    }
    hx_test("map", 1, "%s, %lu MB", tnt2.name, (unsigned long)(tnt2.fb_size >> 20));
    if (vbe_find_mode(W, H, bpp, &m) != 0 || vbe_set_mode(&m, 0, &pitch_px) != 0) {
        hx_test("mode", 0, "no 640x480x%d mode", bpp);
        hx_done(1);
    }
    hx_test("mode", 1, "mode %03x, %d bits, pitch %d bytes", m.mode, bpp, pitch_px * (bpp / 8));
    if (tnt2_accel_init() != 0) {
        vbe_set_text_mode();
        hx_test("accel", 0, "the ring never started");
        hx_done(1);
    }
    hx_test("accel", 1, "the ring started");
    zoff = ((uint32_t)pitch_px * (uint32_t)(bpp / 8) * H + 4095u) & ~4095u;
    vb = tnt2.fb_usable + 0x8000u;
    tnt2_set_target(0, pitch_px * (bpp / 8), bpp);
    tnt2_set_clip(0, 0, W, H);
    tnt2_fill(0, 0, W, H, 0);
    tnt2_sync(1000000);
    if (tnt2_3d_init() != 0 || tnt2_celsius_init() != 0 || !trap_ok("celsius")) {
        hx_test("celsius", 0, "Celsius's set-up didn't finish, or trapped");
        goto out;
    }
    hx_test("celsius", 1, "0x96 set up");
    for (i = 0; i < ncases; i++)
        if (run_bands(&cases[i]) != 0)
            goto out;
    if (run_persp("persp-fog", 1) == 0 && run_persp("persp-colour", 0) == 0) {
        static const struct fcase grads[] = {
            { "grad-exp", 0x800, 1.5f, -0.0901684f, 0.0f, 0.0f, 6.4f, "" },
            { "grad-exp2", 0x803, 1.5f, -0.2123305f, 0.0f, 0.0f, 3.2f, "" },
        };
        for (i = 0; i < 2; i++)
            if (run_grad(&grads[i]) != 0)
                break;
    }
out:
    tnt2_sync(1000000);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
