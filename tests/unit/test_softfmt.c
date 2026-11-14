/* test_softfmt.c - the software core's colour buffers (hal/src/soft/raster.c;
 * gl/docs/b2.md B2.6): RGB565 and XRGB8888 written exactly as their bits
 * say; RGB332 rounded to the nearest of its levels without dither, and with
 * the 4x4 ordered dither averaging back to the colour over every 4x4 block
 * (within 1/32 of a level); the colour mask kept at 8 bits too. */
#include "unit.h"
#include "soft/soft.h"
#include <math.h>
#include <string.h>

#define W 16
#define H 16

static uint8_t buf[W * H * 4];

static void target(int format, int dither, unsigned mask)
{
    soft_target t;
    soft_state s;
    memset(&t, 0, sizeof t);
    t.color = buf, t.format = format, t.width = W, t.height = H;
    t.color_pitch = W * (format == SOFT_XRGB8888 ? 4 : format == SOFT_RGB565 ? 2 : 1);
    soft_target_set(&t);
    memset(&s, 0, sizeof s);
    s.scissor[2] = W, s.scissor[3] = H, s.color_mask = mask, s.dither = dither;
    soft_state_set(&s);
}

static void clear(int r, int g, int b)
{
    float c[4];
    c[0] = r / 255.0f, c[1] = g / 255.0f, c[2] = b / 255.0f, c[3] = 1;
    soft_clear(SOFT_CLEAR_COLOR, c, 0, 0);
}

int unit_main(void)
{
    int v, bad = 0, x, y, k;
    double worst = 0;

    /* RGB565 and XRGB8888: the top bits, and all of them. */
    target(SOFT_RGB565, 1, 15);
    clear(200, 100, 50);
    CHECK_EQ(((uint16_t *)buf)[0], (200 >> 3) << 11 | (100 >> 2) << 5 | (50 >> 3));
    target(SOFT_XRGB8888, 1, 15);
    clear(200, 100, 50);
    CHECK_EQ(((uint32_t *)buf)[0] & 0xFFFFFFu, 200u << 16 | 100u << 8 | 50u);

    /* RGB332 without dither: each channel the nearest level. */
    target(SOFT_RGB332, 0, 15);
    for (v = 0; v < 256; v++) {
        unsigned p;
        clear(v, v, v);
        p = buf[0];
        bad += (p >> 5) != (unsigned)floor(v * 7 / 255.0 + 0.5) || ((p >> 2) & 7) != (unsigned)floor(v * 7 / 255.0 + 0.5) ||
               (p & 3) != (unsigned)floor(v * 3 / 255.0 + 0.5);
    }
    CHECK_EQ(bad, 0);

    /* RGB332 dithered: every 4x4 block's mean, in levels, within 1/32 of the colour's. */
    target(SOFT_RGB332, 1, 15);
    for (v = 0; v < 256; v++) {
        clear(v, (v * 7) & 255, 255 - v);
        for (y = 0; y + 4 <= H; y++)
            for (x = 0; x + 4 <= W; x++) {
                double sum[3] = { 0, 0, 0 }, want[3];
                int i, j;
                for (j = 0; j < 4; j++)
                    for (i = 0; i < 4; i++) {
                        unsigned p = buf[(y + j) * W + x + i];
                        sum[0] += p >> 5, sum[1] += (p >> 2) & 7, sum[2] += p & 3;
                    }
                want[0] = v * 7 / 255.0, want[1] = ((v * 7) & 255) * 7 / 255.0, want[2] = (255 - v) * 3 / 255.0;
                for (k = 0; k < 3; k++) {
                    double e = fabs(sum[k] / 16 - want[k]);
                    if (e > worst)
                        worst = e;
                }
            }
    }
    CHECK(worst <= 1.0 / 32 + 1e-9);

    /* The colour mask at 8 bits: green and blue kept, red written. */
    target(SOFT_RGB332, 0, 7);
    clear(0, 255, 255);
    target(SOFT_RGB332, 0, 1);
    clear(255, 0, 0);
    CHECK_EQ(buf[0], 0xFF);
    return 0;
}
