/* mem.c - the memory device (rg/device.h, rg/softbuf.h; gl/docs/b2.md): its
 * buffers are system memory. Full-screen it shows nothing: the software
 * renderer's host tests draw on it (DGL_DEVICE=mem), and so can a DOS program
 * whose frames are only read back (glReadPixels); chosen only by name there.
 * Under GLOS (B3, gl/docs/backends.md) it is the window device of any desktop
 * the Matrox device can't draw in (another card, or not 16 bits a pixel):
 * the program becomes a GLOS app with a window of the context's size, and
 * each swap copies the finished frame into it (GLOS_DRAW_BLIT, which converts
 * to the screen's format). */
#include "rg/softbuf.h"
#include "rg/glos.h"
#include <stdlib.h>
#include <string.h>

rg_softbuf rg_sb;

static void win_close(void);
static void win_show(void);

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

/* The buffers go (and a GLOS window). Also the VBE device's (vbe_soft.c). */
void rg_sb_close(void)
{
    win_close();
    if (rg_sb.drop)
        rg_sb.drop();
    free(rg_sb.color[0]);
    free(rg_sb.color[1]);
    free(rg_sb.depth);
    free(rg_sb.stencil);
    memset(&rg_sb, 0, sizeof rg_sb);
}
#define close_dev rg_sb_close

/* Buffers in system memory for a context: the memory device's, the GLOS
   window's and the VBE device's. */
int rg_sb_open(const rg_open *o, rg_opened *out)
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
    out->stencil_bits = rg_sb.stencil ? 8 : 0;
    out->color_bits = bpp;
    out->vsync = 0;
    out->display_width = o->width;
    out->display_height = o->height;
    out->fit = "memory";
    return 0;
}

static int swap(int vsync)
{
    (void)vsync;
    if (rg_sb.flush)
        rg_sb.flush();
    if (rg_sb.color[1])
        rg_sb.front_is_a = !rg_sb.front_is_a;
    win_show();                                 /* (a GLOS window: the new front into it) */
    return 0;
}

static int present_front(void)
{
    if (rg_sb.flush)
        rg_sb.flush();
    win_show();
    return 0;
}
static void draw_buffer(int front) { rg_sb.draw_front = front; }
static int idle(void) { return 1; }           /* (kept drawing is done by sync, never in the background) */

static int sync(void)
{
    if (rg_sb.flush)
        rg_sb.flush();
    return 0;
}

int rg_sb_read(int front, int x, int y, int w, int h, uint8_t *rgba, int stride)
{
    const uint8_t *buf = rg_sb_color(front);
    int i, j;
    if (!rg_sb.active)
        return -1;
    if (rg_sb.flush)
        rg_sb.flush();
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

#if defined(__DJGPP__) && !defined(DGL_RIG)

/* ---- In a GLOS window (B3) ------------------------------------------------ */

extern char **__crt0_argv;

static glos_handle window;
static int full;                        /* shown full screen (GLOS_WS_FULL) */
static uint32_t palette332[256];        /* an 8-bit context's colours (RGB 3-3-2) for GLOS_DRAW_BLIT */
unsigned long rg_mem_blits;             /* frames copied into the window */

/* A GLOS desktop on the screen, of any card and depth (GLOS_DRAW_BLIT draws into a window on any desktop).
   GLOS answers its caps without one too (AUDIO with a mixer, NET with the network up), and a program there
   must take the screen, not open a window nobody sees (Echoes under GLOS 6619e13 with no `glos display on`:
   "DOS-GL on Software" instead of the G200). A GLOS that doesn't say whether its desktop is up gives no
   software windows: its programs go full screen, as before B3. */
#ifndef GLOS_MACHINE_DESKTOP
#define GLOS_MACHINE_DESKTOP 16         /* (proposed to GLOS 2026-11-19; until its api.h has it) */
#endif
static int window_ok(void) { return (rg_glos_caps() & GLOS_MACHINE_DESKTOP) != 0; }

/* The usual sizes that fit in the desktop's work area with a frame and a title bar, then those up to the screen's
   size (shown full screen); 8, 16 or 32 bits a pixel
   (the context's: GLOS converts each frame to the screen's format). */
static int window_modes(rg_mode *out, int max)
{
    static const int sizes[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 400 },
                                    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 } };
    unsigned i;
    int n = 0;
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        if (!rg_glos_fits(sizes[i][0], sizes[i][1]))     /* (with a frame, or full screen up to the screen's size) */
            continue;
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

static void win_close(void)
{
    struct glos_close c;
    if (!window)
        return;
    c.size = sizeof c;
    c.handle = window;
    rg_glos_call(GLOS_CORE_CLOSE, &c);
    window = 0;
}

/* The front buffer into the window: GLOS converts it to the screen's format. */
static void win_show(void)
{
    struct glos_blit b;
    if (!window || !rg_sb.active)
        return;
    memset(&b, 0, sizeof b);
    b.size = sizeof b;
    b.handle = window;
    b.pixels = (unsigned)(uintptr_t)rg_sb_color(1);    /* (DJGPP: a pointer is an offset in DS) */
    b.pitch = (unsigned)rg_sb.pitch;
    b.format.bpp = (unsigned)rg_sb.bpp;
    if (rg_sb.bpp == 16) {
        b.format.rpos = 11, b.format.rsize = 5, b.format.gpos = 5, b.format.gsize = 6, b.format.bpos = 0,
        b.format.bsize = 5;
    } else if (rg_sb.bpp == 32) {
        b.format.rpos = 16, b.format.rsize = 8, b.format.gpos = 8, b.format.gsize = 8, b.format.bpos = 0,
        b.format.bsize = 8;
    } else
        b.palette = (unsigned)(uintptr_t)palette332;
    b.w = rg_sb.width;
    b.h = rg_sb.height;
    if (rg_glos_call(GLOS_DRAW_BLIT, &b) == 0)
        rg_mem_blits++;
}

/* 0 in a window; 1 when GLOS won't have the program as an app (it runs full-screen, as a session); -1 on an
   error. The buffers are rg_sb_open's. */
static int open_glos(const rg_open *o, rg_opened *out)
{
    static struct glos_init in;
    struct glos_win_create w;
    const char *name = __crt0_argv && __crt0_argv[0] ? __crt0_argv[0] : "DOS-GL", *e;
    unsigned i;
    int r;
    if ((e = getenv("DGL_TITLE")) != NULL && *e)        /* the program's name: its window's title, and the app's */
        name = e;
    else {
        for (e = name; *e; e++)
            if (*e == '\\' || *e == '/' || *e == ':')
                name = e + 1;
    }
    memset(&in, 0, sizeof in);
    in.size = sizeof in;
    in.major = GLOS_MAJOR, in.minor = GLOS_MINOR;
    for (i = 0; i < sizeof in.name - 1 && name[i] && name[i] != '.'; i++)
        in.name[i] = name[i];
    r = rg_glos_call(GLOS_CORE_INIT, &in);
    if (r && !(r == GLOS_E_INIT && in.why == GLOS_WHY_ALREADY))
        return 1;                       /* not to be an app (a session's, a child's): the screen, as on DOS */
    if (rg_sb_open(o, out) != 0)
        return -1;
    for (i = 0; i < 256; i++)           /* (RGB 3-3-2, as rg_sb_read expands it) */
        palette332[i] = (uint32_t)((i >> 5) * 255 / 7) << 16 | (uint32_t)(((i >> 2) & 7) * 255 / 7) << 8 |
                        (uint32_t)((i & 3) * 255 / 3);
    memset(&w, 0, sizeof w);
    w.size = sizeof w;
    w.kind = GLOS_WK_NORMAL;
    w.w = o->width;
    w.h = o->height;
    full = rg_glos_full(o->glos_flags, o->width, o->height);   /* (full screen: no frame either) */
    w.flags = GLOS_WF_FIXED | GLOS_WF_ANYWHERE | ((o->glos_flags & RG_GLOS_BARE) || full ? GLOS_WF_BARE : 0);
    for (i = 0; i < sizeof w.title - 1 && name[i] && name[i] != '.'; i++)
        w.title[i] = name[i];
    if ((r = rg_glos_call(GLOS_WIN_CREATE, &w)) != 0) {
        close_dev();
        rg_set_error("GLOS made no %dx%d window (error %d)", o->width, o->height, r);
        return -1;
    }
    window = w.handle;
    win_show();                         /* (the cleared buffer: the window never shows garbage) */
    if (!(o->glos_flags & RG_GLOS_HIDDEN)) {
        struct glos_win_show v;
        v.size = sizeof v;
        v.handle = window;
        v.state = full ? GLOS_WS_FULL : GLOS_WS_NORMAL;
        v.flags = 0;
        rg_glos_call(GLOS_WIN_SHOW, &v);
    }
    out->fit = "window";
    out->window = window;
    return 0;
}

#else   /* the rig and the host: no GLOS */

static void win_close(void) { }
static void win_show(void) { }
static int window_ok(void) { return 0; }
static int window_modes(rg_mode *out, int max) { (void)out; (void)max; return 0; }
static int open_glos(const rg_open *o, rg_opened *out) { (void)o; (void)out; return 1; }

#endif

const rg_device rg_mem_device = {
    .name = "mem",
    .flags = RG_DEV_BY_NAME,
    .probe = probe,
    .caps = caps,
    .modes = modes,
    .open = rg_sb_open,
    .window_ok = window_ok,
    .window_modes = window_modes,
    .open_glos = open_glos,
    .close = close_dev,
    .swap = swap,
    .present_front = present_front,
    .draw_buffer = draw_buffer,
    .idle = idle,
    .sync = sync,
    .read = rg_sb_read,
    .info = info,
    .stats = stats,
    .crash = crash,
};
