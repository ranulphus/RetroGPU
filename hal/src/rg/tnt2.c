/* tnt2.c - the Riva TNT2 device (rg/device.h; docs/tnt2.md T3): the TNT2 layer (hal/src/tnt2, libtnt2's calls)
 * behind the device table, as the ViRGE/DX's (rg/virge.c).
 *
 * Modes are the BIOS's (VBE, with a linear framebuffer) in 16 and 32 bits a pixel: the 3D engine draws R5G6B5 and
 * X8R8G8B8 (8-bit modes are left to the 2D desktop, not offered here). VRAM layout: colour buffer A at 0, colour
 * buffer B (double buffering), the depth buffer (the chip pairs its width with the colour's: 16-bit Z with 16-bit
 * colour, Z24S8 with 32), each 4 KB aligned, at the colour's pitch; then what is left for drawing (textures), below
 * the 128 KB the layer keeps at the top (its command ring, image scratch and the white texel). The engine is set up
 * after the BIOS's mode set. A swap shows the hidden buffer by the display start (PCRTC), in the vertical blank when
 * vsync is on. No GLOS window (B3's software path shows a window's pixels for cards other than Matrox ones). */
#include "rg/device.h"
#include "rg/tnt2_dev.h"
#include "tnt2/tnt2.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdlib.h>
#include <string.h>

#define ALIGN4K(x) (((x) + 4095u) & ~4095u)
#define MAX_MODES 32
#define TEXTURE_RESERVE (1024u * 1024u)  /* kept for textures when judging fit */

rg_tnt2_ctx rg_tnt2;                    /* (the open context: rg/tnt2_dev.h, for the drawing back end) */
#define st rg_tnt2
static mga_vbe_mode st_mode;
static rg_open st_hooks;
static int found;                       /* 0 not yet, 1 found, -1 none */
static int mapped, graphics;
static rg_info info;
static rg_mode modes[MAX_MODES];
static mga_vbe_mode vmodes[MAX_MODES];
static int nmodes;
static uint32_t retrace_us;

static int bytes_of(int bpp) { return bpp == 32 ? 4 : bpp == 8 ? 1 : 2; }

/* ---- Finding and modes --------------------------------------------------- */

static int probe(void)
{
    if (found) {
        if (found < 0)
            rg_set_error("no Riva TNT or TNT2 found");
        return found > 0 ? 0 : -1;
    }
    found = -1;
    if (tnt2_find() != 0) {
        rg_set_error("no Riva TNT or TNT2 found");
        return -1;
    }
    if (tnt2_map() != 0) {                      /* (the memory's size is the chip's register) */
        rg_set_error("cannot map the %s's registers and framebuffer", tnt2.name);
        return -1;
    }
    mapped = 1;
    memset(&info, 0, sizeof info);
    info.chip_name = tnt2.name;
    info.device_id = tnt2.device;
    info.revision = tnt2.revision;
    info.vram_bytes = tnt2.fb_size;
    info.fb_phys = tnt2.bar1;
    info.mmio_phys = tnt2.bar0;
    found = 1;
    RG_INFO("DGL-DEVICE %s id=%04x rev=%02x vram=%lu", info.chip_name, info.device_id, info.revision,
            (unsigned long)info.vram_bytes);
    return 0;
}

static void caps(rg_caps *c)
{
    memset(c, 0, sizeof *c);
    c->vram_bytes = info.vram_bytes;
    c->max_color_bits = 32;
    c->cursor = 1;
}

/* Whether double buffering and a depth buffer fit, with room for textures. */
static void judge(rg_mode *d)
{
    uint32_t colour = (uint32_t)d->pitch_px * (uint32_t)bytes_of(d->color_bits) * (uint32_t)d->height;
    uint32_t room = tnt2.fb_usable;
    d->can_double_buffer = 2u * ALIGN4K(colour) + TEXTURE_RESERVE <= room;
    d->max_depth_bits = ((d->can_double_buffer ? 2u : 1u) * ALIGN4K(colour) + ALIGN4K(colour) + TEXTURE_RESERVE <= room)
                        ? (d->color_bits == 32 ? 24 : 16) : 0;
}

static void take_mode(const mga_vbe_mode *m, void *ctx)
{
    rg_mode d;
    (void)ctx;
    if (nmodes >= MAX_MODES || !m->lfb_phys || (m->bpp != 16 && m->bpp != 32) || m->width < 320)
        return;
    if (m->bpp == 16 && m->green_size != 6)
        return;                                 /* (R5G6B5 only: the 3D engine's 16-bit format) */
    if ((uint32_t)m->pitch_bytes * m->height > tnt2.fb_usable)
        return;
    memset(&d, 0, sizeof d);
    d.width = d.display_width = m->width;
    d.height = d.display_height = m->height;
    d.color_bits = m->bpp;
    d.bios_mode = m->mode;
    d.pitch_px = m->pitch_bytes / bytes_of(m->bpp);
    judge(&d);
    vmodes[nmodes] = *m;
    modes[nmodes++] = d;
}

static int list_modes(rg_mode *out, int max)
{
    int i;
    if (probe() != 0)
        return 0;
    if (!nmodes)
        vbe_enumerate(take_mode, NULL);
    for (i = 0; i < nmodes && i < max && out; i++)
        out[i] = modes[i];
    return nmodes;
}

/* ---- Buffers and showing ------------------------------------------------- */

uint32_t rg_tnt2_color_off(int front)
{
    if (!st.double_buffer)
        return st.front_off;
    return st.front_is_a == !!front ? st.front_off : st.back_off;
}

static int surface(int which, rg_surface *s)
{
    if (!st.active)
        return -1;
    s->off = rg_tnt2_color_off(which == RG_SURF_FRONT);
    s->pitch_bytes = st.pitch_bytes;
    s->bpp = st.bpp;
    s->width = st.width;
    s->height = st.height;
    return 0;
}

static void draw_buffer(int front) { st.draw_front = front; }

static int idle(void) { return !mapped || tnt2_idle(); }

static int sync(void)
{
    if (tnt2_sync(500000) != 0)
        return -1;
    if (st_hooks.on_sync)
        st_hooks.on_sync();
    return 0;
}

static int swap(int vsync)
{
    uint32_t show;
    if (!st.active || !st.double_buffer)
        return 0;
    show = st.front_is_a ? st.back_off : st.front_off;
    tnt2_kick();
    if (vsync) {
        uint32_t t0 = sys_time_us();
        if (st_hooks.wait)
            st_hooks.wait();
        tnt2_vsync_wait(50000);
        retrace_us += sys_time_us() - t0;
    }
    tnt2_set_start(show);
    st.front_is_a = !st.front_is_a;
    return RG_SWAP_TARGET;
}

static int present_front(void) { return 0; }   /* (no scaled modes) */

/* ---- 2D and the cursor (the TNT2 layer's) -------------------------------- */

static void target(const rg_surface *s)
{
    tnt2_set_target(s->off, s->pitch_bytes, s->bpp);
    tnt2_set_clip(0, 0, s->width, s->height);
}

static void copy_from(const rg_surface *src, int sx, int sy, int dx, int dy, int w, int h)
{
    tnt2_copy_from(src->off, src->pitch_bytes, sx, sy, dx, dy, w, h);
}

static void mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, int flags)
{
    tnt2_mono(x, y, w, h, bits, stride, fg, bg, (flags & RG_MONO_TRANSPARENT) ? TNT2_MONO_TRANSPARENT : 0u);
}

/* The NV4 family's cursor is in instance memory, not VRAM: off is not used (docs/tnt2.md). */
static int dev_cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512])
{
    (void)off;
    return tnt2_cursor_load(and_mask, xor_mask);
}

/* ---- Opening and closing ------------------------------------------------- */

static int open_screen(const rg_open *o, rg_opened *out)
{
    int bpp = o->color_bits ? o->color_bits : 16, i, pitch_px = 0;
    uint32_t colour;
    rg_surface s;
    if (probe() != 0)
        return -1;
    list_modes(NULL, 0);
    for (i = 0; i < nmodes; i++)
        if (modes[i].width == o->width && modes[i].height == o->height && modes[i].color_bits == bpp)
            break;
    if (i == nmodes) {
        rg_set_error("no usable %dx%dx%d mode on this card", o->width, o->height, bpp);
        return -1;
    }
    memset(&st, 0, sizeof st);
    memset(&st_hooks, 0, sizeof st_hooks);
    st_hooks = *o;
    st_mode = vmodes[i];
    st.width = o->width;
    st.height = o->height;
    st.bpp = bpp;
    st.double_buffer = o->double_buffer != 0;
    st.depth_bits = o->depth_bits ? (bpp == 32 ? 24 : 16) : 0;
    graphics = 1;                               /* (about to leave text mode) */
    if (vbe_set_mode(&st_mode, 0, &pitch_px) != 0) {
        rg_set_error("VBE mode %03x failed", st_mode.mode);
        return -1;                              /* (the caller closes: the screen goes back) */
    }
    st.pitch_bytes = pitch_px * bytes_of(bpp);
    if (tnt2_accel_init() != 0) {
        rg_set_error("the %s's engine didn't start (its command ring was never read)", tnt2.name);
        return -1;
    }
    if (tnt2_3d_init() != 0) {              /* (the 3D objects, for the drawing back end) */
        rg_set_error("the %s's 3D objects didn't start", tnt2.name);
        return -1;
    }
    colour = (uint32_t)st.pitch_bytes * (uint32_t)st.height;
    st.front_off = 0;
    st.back_off = st.double_buffer ? ALIGN4K(colour) : 0;
    st.z_off = st.depth_bits ? ALIGN4K((st.double_buffer ? st.back_off : 0) + colour) : 0;
    st.heap_off = ALIGN4K(st.depth_bits ? st.z_off + colour : (st.double_buffer ? st.back_off : 0) + colour);
    if (st.heap_off > tnt2.fb_usable) {
        rg_set_error("%dx%dx%d with these buffers needs %lu bytes of VRAM; %lu are usable", st.width, st.height, bpp,
                     (unsigned long)st.heap_off, (unsigned long)tnt2.fb_usable);
        return -1;
    }
    st.heap_end = tnt2.fb_usable;
    if (o->heap_kb && st.heap_off + o->heap_kb * 1024u < st.heap_end)
        st.heap_end = st.heap_off + o->heap_kb * 1024u;
    st.active = 1;
    /* Everything clear: both colour buffers black, depth at its farthest (all ones: Z24S8's top three bytes are
     * its Z); then the first buffer shown. */
    st.front_is_a = 1;
    for (i = 0; i < (st.double_buffer ? 2 : 1); i++) {
        s.off = i ? st.back_off : st.front_off;
        s.pitch_bytes = st.pitch_bytes, s.bpp = st.bpp, s.width = st.width, s.height = st.height;
        target(&s);
        tnt2_fill(0, 0, st.width, st.height, 0);
    }
    if (st.depth_bits) {
        s.off = st.z_off, s.pitch_bytes = st.pitch_bytes, s.bpp = st.bpp, s.width = st.width, s.height = st.height;
        target(&s);
        tnt2_fill(0, 0, st.width, st.height, 0xFFFFFFFFu);
    }
    tnt2_sync(500000);
    tnt2_set_start(st.front_off);
    memset(out, 0, sizeof *out);
    out->width = st.width;
    out->height = st.height;
    out->pitch_px = pitch_px;
    out->double_buffer = st.double_buffer;
    out->depth_bits = st.depth_bits;
    out->vsync = o->vsync != 0;
    out->mem_start = st.heap_off;
    out->mem_end = st.heap_end;
    out->vram_bytes = tnt2.fb_size;
    out->display_width = st.width;
    out->display_height = st.height;
    out->fit = "native";
    return 0;
}

static void close_dev(void)
{
    if (!graphics)
        return;
    graphics = 0;
    if (mapped)
        tnt2_sync(100000);
    vbe_set_text_mode();
    st.active = 0;
}

static int window_ok(void) { return 0; }
static int window_modes(rg_mode *out, int max) { (void)out; (void)max; return 0; }
static int open_glos(const rg_open *o, rg_opened *out) { (void)o; (void)out; return 1; }

/* ---- Reading and diagnostics -------------------------------------------- */

/* The buffer's pixels as RGBA8 (bit replication to 8 bits), rows top down from (x, y); outside the buffer black. */
static int read_px(int front, int x, int y, int w, int h, uint8_t *rgba, int stride)
{
    const volatile uint8_t *fb;
    int i, j, bpb = bytes_of(st.bpp);
    if (!st.active)
        return -1;
    tnt2_sync(500000);
    fb = tnt2_fb + rg_tnt2_color_off(front);
    for (j = 0; j < h; j++, rgba += stride) {
        int sy = y + j;
        uint8_t *p = rgba;
        if ((j & 15) == 15 && st_hooks.yield)
            st_hooks.yield();
        for (i = 0; i < w; i++, p += 4) {
            int sx = x + i;
            unsigned r = 0, g = 0, b = 0;
            if (sx >= 0 && sx < st.width && sy >= 0 && sy < st.height) {
                const volatile uint8_t *q = fb + (uint32_t)sy * (uint32_t)st.pitch_bytes + (uint32_t)sx * (uint32_t)bpb;
                if (st.bpp == 32) {
                    b = q[0], g = q[1], r = q[2];
                } else {
                    unsigned c = q[0] | (unsigned)q[1] << 8;
                    r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                    r = (r << 3) | (r >> 2), g = (g << 2) | (g >> 4), b = (b << 3) | (b >> 2);
                }
            }
            p[0] = (uint8_t)r, p[1] = (uint8_t)g, p[2] = (uint8_t)b, p[3] = 255;
        }
    }
    return 0;
}

static void get_info(rg_info *i)
{
    *i = info;
    i->display_width = st.active ? st.width : 0;
    i->display_height = st.active ? st.height : 0;
    i->picture_width = st.active ? st.width : 0;
    i->picture_height = st.active ? st.height : 0;
    i->fit = st.active ? "native" : NULL;
}

static void get_stats(rg_stats *s)
{
    memset(s, 0, sizeof *s);
    s->retrace_us = retrace_us;
}

static void crash_lines(void) { }

const rg_device rg_tnt2_device = {
    .name = "tnt2",
    .probe = probe,
    .caps = caps,
    .modes = list_modes,
    .open = open_screen,
    .window_ok = window_ok,
    .window_modes = window_modes,
    .open_glos = open_glos,
    .close = close_dev,
    .swap = swap,
    .present_front = present_front,
    .draw_buffer = draw_buffer,
    .surface = surface,
    .target = target,
    .clip = tnt2_set_clip,
    .fill = tnt2_fill,
    .copy = tnt2_copy,
    .copy_from = copy_from,
    .image = tnt2_image,
    .mono = mono,
    .cursor_load = dev_cursor_load,
    .cursor_colours = tnt2_cursor_colours,      /* (0xRRGGBB: the layer converts to its A1R5G5B5) */
    .cursor_show = tnt2_cursor_show,
    .cursor_move = tnt2_cursor_move,
    .idle = idle,
    .sync = sync,
    .read = read_px,
    .info = get_info,
    .stats = get_stats,
    .crash = crash_lines,
};
