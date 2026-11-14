/* mem.c - the memory device (rg/device.h, rg/softbuf.h; gl/docs/b2.md): its
 * buffers are system memory and it shows nothing. The software renderer's
 * host tests draw on it (DGL_DEVICE=mem), and so can a DOS program whose
 * frames are only read back (glReadPixels). Chosen only by name. */
#include "rg/softbuf.h"
#include <stdlib.h>
#include <string.h>

rg_softbuf rg_sb;

uint8_t *rg_sb_color(int front)
{
    if (!rg_sb.color[1])
        return rg_sb.color[0];
    return rg_sb.front_is_a == !!front ? rg_sb.color[0] : rg_sb.color[1];
}

static int probe(void) { return 0; }

static void caps(rg_caps *c)
{
    memset(c, 0, sizeof *c);
    c->max_color_bits = 32;
}

/* Any size; 8, 16 or 32 bits a pixel. */
static int modes(rg_mode *out, int max)
{
    static const int sizes[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 480 },
                                    { 800, 600 }, { 1024, 768 } };
    int n = 0, i;
    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++) {
        if (out && n < max) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].width = out[n].display_width = sizes[i][0];
            out[n].height = out[n].display_height = sizes[i][1];
            out[n].color_bits = 16;
            out[n].pitch_px = sizes[i][0];
            out[n].can_double_buffer = 1;
            out[n].max_depth_bits = 32;
        }
        n++;
    }
    return n;
}

static void close_dev(void)
{
    free(rg_sb.color[0]);
    free(rg_sb.color[1]);
    free(rg_sb.depth);
    free(rg_sb.stencil);
    memset(&rg_sb, 0, sizeof rg_sb);
}

static int open_mem(const rg_open *o, rg_opened *out)
{
    int bpp = o->color_bits ? o->color_bits : 16;
    size_t colour;
    if (bpp != 8 && bpp != 16 && bpp != 32) {
        rg_set_error("no %d-bit colour in memory (8, 16 or 32)", bpp);
        return -1;
    }
    if (o->width < 1 || o->height < 1 || o->width > 4096 || o->height > 4096) {
        rg_set_error("no %dx%d buffers", o->width, o->height);
        return -1;
    }
    close_dev();
    rg_sb.width = o->width;
    rg_sb.height = o->height;
    rg_sb.bpp = bpp;
    rg_sb.pitch = (o->width * (bpp / 8) + 7) & ~7;
    colour = (size_t)rg_sb.pitch * (size_t)o->height;
    rg_sb.color[0] = calloc(1, colour);
    if (o->double_buffer)
        rg_sb.color[1] = calloc(1, colour);
    if (o->depth_bits) {
        rg_sb.depth_bits = o->depth_bits > 16 ? 32 : 16;
        rg_sb.depth_pitch = o->width * (rg_sb.depth_bits / 8);
        rg_sb.depth = malloc((size_t)rg_sb.depth_pitch * (size_t)o->height);
        if (rg_sb.depth)
            memset(rg_sb.depth, 0xFF, (size_t)rg_sb.depth_pitch * (size_t)o->height);   /* (the farthest) */
    }
    if (o->stencil_bits) {
        rg_sb.stencil_pitch = o->width;
        rg_sb.stencil = calloc(1, (size_t)o->width * (size_t)o->height);
    }
    if (!rg_sb.color[0] || (o->double_buffer && !rg_sb.color[1]) || (o->depth_bits && !rg_sb.depth) ||
        (o->stencil_bits && !rg_sb.stencil)) {
        close_dev();
        rg_set_error("no memory for %dx%d buffers", o->width, o->height);
        return -1;
    }
    rg_sb.front_is_a = 1;
    rg_sb.active = 1;
    memset(out, 0, sizeof *out);
    out->width = o->width;
    out->height = o->height;
    out->pitch_px = rg_sb.pitch / (bpp / 8);
    out->double_buffer = o->double_buffer != 0;
    out->depth_bits = rg_sb.depth_bits;
    out->vsync = 0;
    out->display_width = o->width;
    out->display_height = o->height;
    out->fit = "memory";
    return 0;
}

static int swap(int vsync)
{
    (void)vsync;
    if (rg_sb.color[1])
        rg_sb.front_is_a = !rg_sb.front_is_a;
    return 0;
}

static int present_front(void) { return 0; }
static void draw_buffer(int front) { rg_sb.draw_front = front; }
static int idle(void) { return 1; }
static int sync(void) { return 0; }

static int read_px(int front, int x, int y, int w, int h, uint8_t *rgba, int stride)
{
    const uint8_t *buf = rg_sb_color(front);
    int i, j;
    if (!rg_sb.active)
        return -1;
    for (j = 0; j < h; j++, rgba += stride) {
        int sy = y + j;
        uint8_t *p = rgba;
        for (i = 0; i < w; i++, p += 4) {
            int sx = x + i;
            unsigned r = 0, g = 0, b = 0;
            if (sx >= 0 && sx < rg_sb.width && sy >= 0 && sy < rg_sb.height) {
                const uint8_t *q = buf + (size_t)sy * rg_sb.pitch;
                if (rg_sb.bpp == 16) {
                    unsigned c = ((const uint16_t *)q)[sx];
                    r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                    r = (r << 3) | (r >> 2), g = (g << 2) | (g >> 4), b = (b << 3) | (b >> 2);
                } else if (rg_sb.bpp == 32) {
                    uint32_t c = ((const uint32_t *)q)[sx];
                    r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                } else {
                    unsigned c = q[sx];
                    r = (c >> 5) * 255 / 7, g = ((c >> 2) & 7) * 255 / 7, b = (c & 3) * 255 / 3;
                }
            }
            p[0] = (uint8_t)r, p[1] = (uint8_t)g, p[2] = (uint8_t)b, p[3] = 255;
        }
    }
    return 0;
}

static void info(rg_info *i)
{
    memset(i, 0, sizeof *i);
    i->chip_name = "Software (memory)";
    i->display_width = rg_sb.active ? rg_sb.width : 0;
    i->display_height = rg_sb.active ? rg_sb.height : 0;
    i->picture_width = i->display_width;
    i->picture_height = i->display_height;
    i->fit = rg_sb.active ? "memory" : NULL;
}

static void stats(rg_stats *s) { memset(s, 0, sizeof *s); }
static void crash(void) { }
static int window_ok(void) { return 0; }
static int window_modes(rg_mode *out, int max) { (void)out; (void)max; return 0; }
static int open_glos(const rg_open *o, rg_opened *out) { (void)o; (void)out; return 1; }

const rg_device rg_mem_device = {
    .name = "mem",
    .flags = RG_DEV_BY_NAME,
    .probe = probe,
    .caps = caps,
    .modes = modes,
    .open = open_mem,
    .window_ok = window_ok,
    .window_modes = window_modes,
    .open_glos = open_glos,
    .close = close_dev,
    .swap = swap,
    .present_front = present_front,
    .draw_buffer = draw_buffer,
    .idle = idle,
    .sync = sync,
    .read = read_px,
    .info = info,
    .stats = stats,
    .crash = crash,
};
