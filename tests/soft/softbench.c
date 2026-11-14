/* softbench.c - the software core's speed by class of state (gl/docs/b2.md
 * B2.8, B2.9): a 320x240 RGB565 screen covered by a jittered mesh of
 * triangles (about 320 pixels each), drawn again and again in each state, on
 * the reference spans, the integer ones in C and with MMX; millions of
 * pixels a second and nanoseconds a pixel. Built 32-bit with x87 maths and
 * the DOS library's flags (make softbench), so the code is close to what a
 * Pentium II runs; the host's numbers say which routines matter and by how
 * much one path beats another, not what a PII does (B2.9 measures that).
 *
 * softbench [SECONDS-A-CASE]   (default 0.5) */
#include "soft/soft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 320
#define H 240
#define GX 12                           /* mesh cells across and down: 2 triangles each */
#define GY 10

static uint16_t color[W * H], depth[W * H];
static uint8_t texels[2][64 * 64 * 4 * 2];  /* (a 64x64 level 0 and its smaller levels) */
static soft_texture tex[2];
static soft_vertex mesh[(GX + 1) * (GY + 1)];
static int levels0;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void make_texture(int k)
{
    int l, x, y, w = 64, off = 0;
    memset(&tex[k], 0, sizeof tex[k]);
    for (l = 0; w >= 1; l++, w /= 2) {
        uint8_t *p = texels[k] + off;
        for (y = 0; y < w; y++)
            for (x = 0; x < w; x++) {
                uint8_t *t = p + (y * w + x) * 4;
                int on = ((x >> 2) + (y >> 2) + k) & 1;
                t[0] = (uint8_t)(on ? 240 : 40 + x * 3), t[1] = (uint8_t)(on ? 200 : 60 + y * 3);
                t[2] = (uint8_t)(on ? 60 : 160), t[3] = (uint8_t)(on ? 255 : 128);
            }
        tex[k].level[l].rgba = p, tex[k].level[l].w = w, tex[k].level[l].h = w;
        off += w * w * 4;
    }
    tex[k].levels = levels0 = l;
    tex[k].wrap_s = tex[k].wrap_t = SOFT_REPEAT;
}

static void make_mesh(void)
{
    int i, j;
    unsigned seed = 7;
    for (j = 0; j <= GY; j++)
        for (i = 0; i <= GX; i++) {
            soft_vertex *v = &mesh[j * (GX + 1) + i];
            int jitter = i && j && i < GX && j < GY;
            memset(v, 0, sizeof *v);
            seed = seed * 1103515245u + 12345u;
            v->x = (float)(i * W) / GX + (jitter ? (float)((seed >> 8) % 9) - 4.0f : 0.0f);
            seed = seed * 1103515245u + 12345u;
            v->y = (float)(j * H) / GY + (jitter ? (float)((seed >> 8) % 9) - 4.0f : 0.0f);
            v->z = 0.3f + 0.4f * (float)i / GX;
            v->oow = 1.0f / (1.0f + 0.8f * (float)j / GY);     /* (a floor going away: perspective) */
            v->r = (float)i / GX, v->g = (float)j / GY, v->b = 0.5f, v->a = 0.6f;
            v->tex[0][0] = (float)i * 1.5f, v->tex[0][1] = (float)j * 1.5f, v->tex[0][2] = 1;
            v->tex[1][0] = (float)i * 0.25f, v->tex[1][1] = (float)j * 0.25f, v->tex[1][2] = 1;
            v->fog = (float)j;
        }
}

static void draw_mesh(void)
{
    int i, j;
    for (j = 0; j < GY; j++)
        for (i = 0; i < GX; i++) {
            const soft_vertex *a = &mesh[j * (GX + 1) + i], *b = a + 1, *c = a + GX + 1, *d = c + 1;
            soft_triangle(a, b, c);
            soft_triangle(b, d, c);
        }
}

enum { C_FLAT, C_SMOOTH_Z, C_TEX_NEAREST, C_TEX_LINEAR, C_TEX_MIP, C_BLEND, C_ADD, C_TWO_UNITS, C_COUNT };
static const char *const names[C_COUNT] = {
    "flat, no depth", "smooth, depth", "texture nearest, modulate, depth", "texture bilinear, modulate, depth",
    "texture trilinear mipmaps, depth", "smooth, blended (alpha)", "texture bilinear, added (ONE, ONE)",
    "two units bilinear (lightmap), depth",
};

static void modulate(soft_stage *st, const soft_texture *t, int coord)
{
    st->tex = t, st->coord = coord;
    st->rgb.func = st->alpha.func = SOFT_C_MODULATE;
    st->rgb.src[0] = st->alpha.src[0] = SOFT_S_TEXTURE;
    st->rgb.src[1] = st->alpha.src[1] = SOFT_S_PREVIOUS;
    st->rgb.op[1] = SOFT_O_COLOR, st->alpha.op[0] = st->alpha.op[1] = SOFT_O_ALPHA;
    st->rgb.scale = st->alpha.scale = 1.0f;
}

static void state_for(int c, soft_state *s)
{
    memset(s, 0, sizeof *s);
    s->scissor[2] = W, s->scissor[3] = H, s->color_mask = 15, s->fill_rule = SOFT_FILL_BOTTOM_LEFT, s->dither = 1;
    s->smooth = c != C_FLAT;
    if (c != C_FLAT && c != C_BLEND && c != C_ADD)
        s->depth_test = 1, s->depth_func = SOFT_ALWAYS, s->depth_write = 1;
    tex[0].min_filter = tex[0].mag_filter = SOFT_LINEAR;
    tex[1].min_filter = tex[1].mag_filter = SOFT_LINEAR;
    tex[0].levels = c == C_TEX_MIP ? levels0 : 1;  /* (one level without a mipmap filter, as GL's back end sets it) */
    tex[1].levels = 1;
    if (c == C_TEX_NEAREST)
        tex[0].min_filter = tex[0].mag_filter = SOFT_NEAREST;
    if (c == C_TEX_MIP)
        tex[0].min_filter = SOFT_LINEAR_MIPMAP_LINEAR;
    if (c >= C_TEX_NEAREST && c != C_BLEND)
        modulate(&s->stage[0], &tex[0], 0);
    if (c == C_TWO_UNITS)
        modulate(&s->stage[1], &tex[1], 1);
    if (c == C_BLEND)
        s->blend = 1, s->blend_src = SOFT_B_SRC_ALPHA, s->blend_dst = SOFT_B_ONE_MINUS_SRC_ALPHA;
    if (c == C_ADD)
        s->blend = 1, s->blend_src = SOFT_B_ONE, s->blend_dst = SOFT_B_ONE;
}

/* Millions of pixels a second for case c on the given spans. */
static double run(int c, int exact, int mmx, double secs)
{
    soft_state s;
    uint32_t px0;
    double t0, t;
    int frames = 0;
    soft_exact(exact);
    soft_mmx(mmx);
    state_for(c, &s);
    soft_state_set(&s);
    px0 = soft_count.pixels;
    t0 = now();
    do {
        draw_mesh();
        soft_flush();
        frames++;
    } while ((t = now() - t0) < secs);
    return (double)(soft_count.pixels - px0) / t / 1e6;
}

int main(int argc, char **argv)
{
    double secs = argc > 1 ? atof(argv[1]) : 0.5;
    soft_target tg;
    int c;
    make_texture(0), make_texture(1), make_mesh();
    memset(&tg, 0, sizeof tg);
    tg.color = (uint8_t *)color, tg.color_pitch = W * 2, tg.format = SOFT_RGB565, tg.width = W, tg.height = H;
    tg.depth = (uint8_t *)depth, tg.depth_pitch = W * 2, tg.depth_bits = 16;
    soft_target_set(&tg);
    soft_tiles(64);
    printf("softbench: %dx%d RGB565, %d triangles a frame, tiles of 64\n", W, H, 2 * GX * GY);
    printf("  %-38s %12s %12s %12s %8s\n", "state", "reference", "integer C", "MMX", "MMX/ref");
    for (c = 0; c < C_COUNT; c++) {
        double ref = run(c, 1, 0, secs), fast = run(c, 0, 0, secs), mmx = run(c, 0, 1, secs);
        printf("  %-38s %8.1f Mp/s %8.1f Mp/s %8.1f Mp/s %7.2fx\n", names[c], ref, fast, mmx, mmx / ref);
    }
    return 0;
}
