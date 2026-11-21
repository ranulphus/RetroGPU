/* rmip.c - RMIP: which mipmap level the Rage 128 samples (docs/rage128.md, R2): a 128 x 128 RGB565 texture whose
 * eight levels are each one colour, at PRIM_TEX_n_OFFSET_C for level n from the largest, drawn NEAREST_MIPMAP_NEAREST
 * on quads of 128 down to 1 pixels a side (1 to 128 texels a pixel along both axes): the colour at each quad's
 * centre says which level the chip took; the levels' slots are also filled the other way round in a second pass
 * (offset n holding level 7 - n) to tell the two orders apart.
 *
 *   RMIP [chipbias=B]        (TEX_CNTL_C's LOD bias byte, 63 by default)
 *
 * Lines: RMIP-QUAD k=TEXELS_A_PIXEL got=LEVEL (or the raw pixel). */
#include "hx.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define TOFF 0x200000u

static const uint16_t col[8] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x8410 };

static int level_of(uint16_t p)
{
    int i;
    for (i = 0; i < 8; i++)
        if (col[i] == p)
            return i;
    return -1;
}

static void quad(float x, float y, float s)
{
    r128_vertex v[4];
    int i;
    memset(v, 0, sizeof v);
    for (i = 0; i < 4; i++) {
        v[i].x = x + ((i == 1 || i == 2) ? s : 0), v[i].y = y + (i >= 2 ? s : 0), v[i].z = 0.5f, v[i].rhw = 1.0f;
        v[i].argb = 0xFFFFFFFFu, v[i].spec = 0xFF000000u;
        v[i].s = (i == 1 || i == 2) ? 1.0f : 0.0f, v[i].t = i >= 2 ? 1.0f : 0.0f;
    }
    r128_tri(&v[0], &v[1], &v[2]);
    r128_tri(&v[0], &v[2], &v[3]);
}

int main(int argc, char **argv)
{
    r128_mode m;
    uint32_t off[8], o = TOFF;
    int i, pass, n, bias = 0x3F;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "chipbias=", 9))
            bias = atoi(argv[i] + 9);
    hx_init(1, argv, "rmip");
    if (r128_find() < 0 || r128_map() < 0 || r128_find_mode(W, H, 16, &m) < 0 || r128_set_mode(&m) < 0 ||
        r128_cce_init(NULL) != 0) {
        hx_test("start", 0, "no Rage 128, mode or CCE");
        hx_done(1);
    }
    for (n = 0; n < 8; n++) {                   /* the levels: 128 .. 1 texels a side, 32-byte aligned */
        int sz = 128 >> n;
        off[n] = o;
        o = (o + (uint32_t)(sz * sz * 2) + 31u) & ~31u;
    }
    for (pass = 0; pass < 2; pass++) {
        r128_sync(1000000);
        for (n = 0; n < 8; n++) {               /* pass 0: slot n holds level n; pass 1: level 7 - n */
            int lv = pass ? 7 - n : n, sz = 128 >> lv, j;
            volatile uint16_t *d = (volatile uint16_t *)(r128.fb + off[n]);
            for (j = 0; j < sz * sz; j++)
                d[j] = col[lv];
        }
        r128_set_target(0, m.pitch_bytes, 16);
        r128_set_clip(0, 0, W, H);
        r128_fill(0, 0, W, H, 0);
        r128_3d_init(0, m.pitch_bytes, 16, 0x100000u, W * 2, 16);
        r128_3d.tex_cntl_c = R128_TEXMAP_ENABLE | R128_TEX_CACHE_FLUSH | ((uint32_t)bias & 0xFFu) << R128_LOD_BIAS_SHIFT;
        r128_3d.prim_tex_cntl_c = 4u << 16 | 2u << 1 | 0u << 4;            /* RGB565, MIPNEAREST, mag NEAREST */
        r128_3d.prim_texture_combine_cntl_c = 4u << 10 | 2u << 25 | 4u << 4 | 6u << 18;   /* the texel */
        r128_3d.tex_size_pitch_c = 7u | 7u << 4 | 7u << 8 | 0u << 12;
        for (n = 0; n < 11; n++)
            r128_3d.prim_tex_offset_c[n] = off[n < 8 ? (pass ? 7 - n : n) : 0];
        r128_3d_format(R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB | R128_VC_FRMT_SPEC_FRGB | R128_VC_FRMT_S_T);
        r128_3d_emit();
        for (n = 0; n < 8; n++)                 /* quads 128 >> n pixels a side: 2^n texels a pixel */
            quad(10.0f + (float)n * 75.0f, 100.0f, (float)(128 >> n));
        r128_sync(2000000);
        for (n = 0; n < 8; n++) {
            int s = 128 >> n, cx = 10 + n * 75 + s / 2, cy = 100 + s / 2;
            uint16_t p = *(volatile uint16_t *)(r128.fb + (uint32_t)cy * m.pitch_bytes + (uint32_t)cx * 2);
            int lv = level_of(p);
            hx_log("RMIP-QUAD pass=%d bias=%d k=%d got=%d raw=%04x", pass, bias, 1 << n, lv, p);
        }
    }
    r128_text_mode();
    hx_test("ran", 1, "both passes");
    hx_done(0);
}
