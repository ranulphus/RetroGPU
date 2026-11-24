/* dmip.c - DMIP: which mipmap level the Radeon samples (docs/radeon.md, RD2), after RMIP: a 128 x 128 RGB565 texture
 * whose eight levels are each one colour, drawn NEAREST_MIPMAP_NEAREST on 22-pixel quads with a chosen footprint: the
 * texture's derivatives (du/dx, du/dy, dv/dx, dv/dy, in texels a pixel) the same over the quad. The colour at a
 * quad's centre says which level the chip took. Five sweeps, the scale k = 2^(j/8) for j = -4..36 (0.7 to 22.6):
 *
 *   iso      k along both axes                         (every rule gives k)
 *   rot45    the same turned 45 degrees                (the longest derivative k; the largest single one k/1.41;
 *                                                        the sum of absolute ones 1.41 k)
 *   rot30    turned 30 degrees                         (k; 0.87 k; 1.37 k)
 *   xonly    k along x, 1 along y                      (a rule by the larger axis: k; by the area: sqrt k)
 *   yonly    1 along x, k along y
 *
 *   DMIP [chipbias=B] [base=MB]      (PP_TXFILTER's LOD bias byte, 0 by default)
 *
 * Lines: DMIP-SWEEP NAME: a digit for each j (the level, or ? for a pixel that is no level's colour), and
 * DMIP-STEP NAME: the k at which each level is first taken. */
#include "hx.h"
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "radeon/regs3d_radeon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define Q 22
#define NJ 41

static const uint16_t col[8] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x8410 };

static int level_of(uint16_t p)
{
    int i;
    for (i = 0; i < 8; i++)
        if (col[i] == p)
            return i;
    return -1;
}

/* A quad at (x, y), Q pixels a side, its centre the texture's (0.5, 0.5), with the derivatives m (texels a pixel). */
static void quad(float x, float y, const double m[4])
{
    radeon_vertex v[4];
    int i;
    memset(v, 0, sizeof v);
    for (i = 0; i < 4; i++) {
        double dx = ((i == 1 || i == 2) ? Q : 0) - Q / 2.0, dy = (i >= 2 ? Q : 0) - Q / 2.0;
        v[i].x = x + (float)(dx + Q / 2.0), v[i].y = y + (float)(dy + Q / 2.0), v[i].z = 0.5f, v[i].rhw = 1.0f;
        v[i].argb = 0xFFFFFFFFu, v[i].spec = 0xFF000000u;
        v[i].s = (float)(0.5 + (m[0] * dx + m[1] * dy) / 128.0), v[i].t = (float)(0.5 + (m[2] * dx + m[3] * dy) / 128.0);
    }
    radeon_tri(&v[0], &v[1], &v[2]);
    radeon_tri(&v[0], &v[2], &v[3]);
}

int main(int argc, char **argv)
{
    static const char *names[5] = { "iso", "rot45", "rot30", "xonly", "yonly" };
    radeon_mode m;
    uint32_t base = 16u << 20, toff, off;
    int n, i, j, s, bias = 0;
    radeon_ctx3d *c = &radeon_3d;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "chipbias=", 9)) bias = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "base=", 5)) base = (uint32_t)atoi(argv[i] + 5) << 20;
    hx_init(1, argv, "dmip");
    if (radeon_find() < 0 || radeon_map() < 0 || radeon_find_mode(W, H, 16, &m) < 0 || radeon_set_mode(&m) < 0) {
        hx_test("init", 0, "no Radeon or no 640x480x16 mode");
        hx_done(1);
    }
    toff = base + 0x200000u;
    radeon_sync(1000000);
    for (n = 0, off = toff; n < 8; n++) {
        int sz = 128 >> n, stride = (sz * 2 + 31) & ~31;
        volatile uint16_t *d = (volatile uint16_t *)(radeon.fb + off);
        for (i = 0; i < stride / 2 * sz; i++)
            d[i] = col[n];
        off = (off + (uint32_t)(stride * sz) + 31u) & ~31u;
    }
    radeon_vram_written(off - 4);
    radeon_set_start(base);
    radeon_set_target(base, m.pitch_bytes, 16);
    radeon_set_clip(0, 0, W, H);
    radeon_fill(0, 0, W, H, 0);
    radeon_3d_init(base, m.pitch_bytes, 16, 0, 0, 0, W, H);
    c->pp_cntl |= RADEON_TEX_0_ENABLE;
    c->tex[0].txfilter = RADEON_MAG_FILTER_NEAREST | RADEON_MIN_FILTER_NEAREST_MIP_NEAREST |
                         ((uint32_t)bias & 0xFFu) << RADEON_LOD_BIAS_SHIFT | 7u << RADEON_MAX_MIP_LEVEL_SHIFT;
    c->tex[0].txformat = RADEON_TXFORMAT_RGB565 | 7u << RADEON_TXFORMAT_WIDTH_SHIFT | 7u << RADEON_TXFORMAT_HEIGHT_SHIFT |
                         RADEON_TXFORMAT_PERSPECTIVE_ENABLE;
    c->tex[0].txoffset = radeon.fb_location + toff;
    c->tex[0].txcblend = RADEON_COLOR_ARG_A_ZERO | RADEON_COLOR_ARG_B_ZERO | RADEON_COLOR_ARG_C_T0_COLOR |
                         RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
    radeon_3d_format(RADEON_VTX_RHW | RADEON_VTX_ARGB | RADEON_VTX_SPEC | RADEON_VTX_ST);
    radeon_3d_dirty = 1;
    for (s = 0; s < 5; s++)
        for (j = 0; j < NJ; j++) {
            double k = pow(2.0, (j - 4) / 8.0), a = s == 1 ? 0.78539816 : 0.52359878, mm[4];
            int idx = s * NJ + j;
            if (s == 0) mm[0] = k, mm[1] = 0, mm[2] = 0, mm[3] = k;
            else if (s <= 2) mm[0] = k * cos(a), mm[1] = -k * sin(a), mm[2] = k * sin(a), mm[3] = k * cos(a);
            else if (s == 3) mm[0] = k, mm[1] = 0, mm[2] = 0, mm[3] = 1;
            else mm[0] = 1, mm[1] = 0, mm[2] = 0, mm[3] = k;
            quad((float)(2 + (idx % 26) * 24), (float)(2 + (idx / 26) * 24), mm);
        }
    if (radeon_sync(2000000) != 0)
        hx_log("DMIP the engine did not go idle");
    for (s = 0; s < 5; s++) {
        char line[NJ + 1], steps[200];
        int last = -1, pos = 0;
        steps[0] = 0;
        for (j = 0; j < NJ; j++) {
            int idx = s * NJ + j, x = 2 + (idx % 26) * 24 + Q / 2, y = 2 + (idx / 26) * 24 + Q / 2;
            int lv = level_of(*(volatile uint16_t *)(radeon.fb + base + (uint32_t)y * m.pitch_bytes + (uint32_t)x * 2));
            line[j] = lv < 0 ? '?' : (char)('0' + lv);
            if (lv > last && lv >= 0) {
                pos += sprintf(steps + pos, " %d@%.2f", lv, pow(2.0, (j - 4) / 8.0));
                last = lv;
            }
        }
        line[NJ] = 0;
        hx_log("DMIP-SWEEP %-5s: %s", names[s], line);
        hx_log("DMIP-STEP  %-5s:%s", names[s], steps);
    }
    radeon_text_mode();
    hx_test("dmip", 1, "bias byte %d", bias);
    hx_done(0);
}
