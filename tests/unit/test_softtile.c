/* test_softtile.c - the software core's tiles (hal/src/soft/raster.c;
 * gl/docs/b2.md B2.7): random sequences of triangles, lines, points and
 * clears, each under a random state (scissor, colour mask, depth, stencil,
 * alpha test, blending, fog, a texture, flat or smooth, dither, both fill
 * rules), drawn at once and through tiles of several sizes (64, 16, 7, 1),
 * must leave the colour, depth and stencil buffers the same byte for byte,
 * in each colour format. Small stores are not tested here: a full store
 * flushes and goes on, the same as a flush the caller asks for. */
#include "unit.h"
#include "soft/soft.h"
#include <string.h>

#define W 96
#define H 80

static uint8_t color[W * H * 4], depth[W * H * 2], stencil[W * H];
static uint8_t keep_color[W * H * 4], keep_depth[W * H * 2], keep_stencil[W * H];
static uint8_t texels[8 * 8 * 4];
static soft_texture tex;
static unsigned seed;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

static float rndf(void) { return rnd(10001) / 10000.0f; }

static void random_state(soft_state *s, int format)
{
    memset(s, 0, sizeof *s);
    s->scissor[0] = (int)rnd(3) ? 0 : (int)rnd(W / 2);
    s->scissor[1] = (int)rnd(3) ? 0 : (int)rnd(H / 2);
    s->scissor[2] = (int)rnd(3) ? W : W / 2 + (int)rnd(W / 2 + 1);
    s->scissor[3] = (int)rnd(3) ? H : H / 2 + (int)rnd(H / 2 + 1);
    s->fill_rule = (int)rnd(2);
    s->color_mask = rnd(4) ? 15 : rnd(16);
    s->dither = format == SOFT_RGB332 && rnd(4);
    s->smooth = (int)rnd(4) != 0;
    s->depth_test = (int)rnd(2), s->depth_func = (int)rnd(8), s->depth_write = (int)rnd(3) != 0;
    if (rnd(3) == 0) {
        s->stage[0].tex = &tex;
        s->stage[0].coord = 0;
        s->stage[0].rgb.func = (int)rnd(2) ? SOFT_C_MODULATE : SOFT_C_REPLACE;
        s->stage[0].rgb.src[0] = SOFT_S_TEXTURE, s->stage[0].rgb.src[1] = SOFT_S_PRIMARY;
        s->stage[0].alpha = s->stage[0].rgb;
        s->stage[0].rgb.scale = s->stage[0].alpha.scale = 1.0f;
    }
    if (rnd(4) == 0) {
        s->fog = 1, s->fog_mode = (int)rnd(3);
        s->fog_start = 0.5f, s->fog_end = 3.0f, s->fog_density = 0.7f;
        s->fog_color[0] = 0.2f, s->fog_color[1] = 0.5f, s->fog_color[2] = 0.9f;
    }
    if (rnd(4) == 0)
        s->alpha_test = 1, s->alpha_func = (int)rnd(8), s->alpha_ref = rndf();
    if (rnd(4) == 0) {
        s->stencil_test = 1, s->stencil_func = (int)rnd(8);
        s->stencil_fail = (int)rnd(6), s->stencil_zfail = (int)rnd(6), s->stencil_zpass = (int)rnd(6);
        s->stencil_ref = rnd(256), s->stencil_mask = rnd(256), s->stencil_writemask = rnd(256);
    }
    if (rnd(3) == 0)
        s->blend = 1, s->blend_src = (int)rnd(11), s->blend_dst = (int)rnd(10);
}

static void random_vertex(soft_vertex *v)
{
    int i;
    memset(v, 0, sizeof *v);
    v->x = (float)((int)rnd((W + 40) * 16) - 20 * 16) / 16.0f + (rnd(2) ? 0.0f : rndf() / 16.0f);
    v->y = (float)((int)rnd((H + 40) * 16) - 20 * 16) / 16.0f;
    v->z = rndf(), v->oow = 0.5f + rndf();
    v->r = rndf(), v->g = rndf(), v->b = rndf(), v->a = rndf();
    for (i = 0; i < 2; i++)
        v->tex[i][0] = rndf() * 3 - 1, v->tex[i][1] = rndf() * 3 - 1, v->tex[i][2] = 1;
    v->fog = rndf() * 4;
}

/* One sequence, replayed from its seed, drawn through tiles of `tile` (0: at once). */
static void run(unsigned s0, int format, int tile)
{
    soft_target t;
    soft_state st;
    int n, ops;
    memset(&t, 0, sizeof t);
    memset(color, 0x5A, sizeof color), memset(depth, 0xFF, sizeof depth), memset(stencil, 0, sizeof stencil);
    t.color = color, t.format = format, t.width = W, t.height = H;
    t.color_pitch = W * (format == SOFT_XRGB8888 ? 4 : format == SOFT_RGB565 ? 2 : 1);
    t.depth = depth, t.depth_pitch = W * 2, t.depth_bits = 16;
    t.stencil = stencil, t.stencil_pitch = W;
    soft_target_set(&t);
    soft_tiles(tile);
    seed = s0;
    ops = 20 + (int)rnd(60);
    for (n = 0; n < ops; n++) {
        soft_vertex v[3];
        if (n == 0 || rnd(3) == 0) {
            random_state(&st, format);
            soft_state_set(&st);
        }
        switch (rnd(8)) {
        case 0: {
            float c[4];
            c[0] = rndf(), c[1] = rndf(), c[2] = rndf(), c[3] = rndf();
            soft_clear(1u + rnd(7), c, rndf(), rnd(256));
            break;
        }
        case 1:
        case 2:
            random_vertex(&v[0]), random_vertex(&v[1]);
            soft_line(&v[0], &v[1], (float)(1 + rnd(5)) + (rnd(2) ? 0.5f : 0.0f));
            break;
        case 3:
            random_vertex(&v[0]);
            soft_point(&v[0], (float)(1 + rnd(7)) + (rnd(3) ? 0.0f : 0.4f));
            break;
        default:
            random_vertex(&v[0]), random_vertex(&v[1]), random_vertex(&v[2]);
            soft_triangle(&v[0], &v[1], &v[2]);
            break;
        }
    }
    soft_flush();
    soft_tiles(0);
}

int unit_main(void)
{
    static const int formats[3] = { SOFT_RGB565, SOFT_XRGB8888, SOFT_RGB332 }, tiles[4] = { 64, 16, 7, 1 };
    int f, k, i, bad = 0, runs = 0;
    unsigned s0;
    uint32_t flushes0;
    for (i = 0; i < 8 * 8 * 4; i++)
        texels[i] = (uint8_t)(i * 37 + (i >> 2) * 11);
    memset(&tex, 0, sizeof tex);
    tex.level[0].rgba = texels, tex.level[0].w = 8, tex.level[0].h = 8, tex.levels = 1;
    tex.wrap_s = SOFT_REPEAT, tex.wrap_t = SOFT_CLAMP_TO_EDGE, tex.min_filter = SOFT_LINEAR, tex.mag_filter = SOFT_NEAREST;
    flushes0 = soft_count.flushes;
    for (f = 0; f < 3; f++)
        for (s0 = 1; s0 <= 60; s0++) {
            run(s0 * 7919u + (unsigned)f, formats[f], 0);
            memcpy(keep_color, color, sizeof color), memcpy(keep_depth, depth, sizeof depth);
            memcpy(keep_stencil, stencil, sizeof stencil);
            for (k = 0; k < 4; k++) {
                run(s0 * 7919u + (unsigned)f, formats[f], tiles[k]);
                bad += memcmp(keep_color, color, sizeof color) != 0 || memcmp(keep_depth, depth, sizeof depth) != 0 ||
                       memcmp(keep_stencil, stencil, sizeof stencil) != 0;
                runs++;
            }
        }
    CHECK_EQ(bad, 0);
    CHECK_EQ(runs, 3 * 60 * 4);
    CHECK(soft_count.flushes - flushes0 >= (uint32_t)(3 * 60 * 4));   /* (the tiled runs did keep and flush) */
    CHECK_EQ(soft_pending(), 0);
    return 0;
}
