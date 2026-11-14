/* virge.c - the S3 ViRGE/DX device (rg/device.h; docs/virge.md V3): the
 * ViRGE layer (hal/src/virge, libvirge's calls) behind the device table, the
 * second card behind it after the Matrox one.
 *
 * Modes are the BIOS's (VBE; the window to the framebuffer opened by the
 * layer after each mode set), in 15, 16 or 24 bits a pixel: the 2D engine
 * draws 8, 16 (555 or 565 alike) and 24 packed, the 3D engine 15 and 24, and
 * neither 32. VRAM layout: colour buffer A at 0, colour buffer B (double
 * buffering), the 16-bit depth buffer (the 3D engine's Z, a 16-bit pixel of
 * the width), then what is left for drawing (textures); each 4 KB aligned.
 * A swap shows the hidden buffer by the display start (the CRTC's), in the
 * vertical blank when vsync is on. No GLOS window yet (B3's software path
 * shows a window's pixels for cards other than Matrox ones). */
#include "rg/device.h"
#include "virge/virge.h"
#include "mga/sys.h"
#include <stdlib.h>
#include <string.h>

#define ALIGN4K(x) (((x) + 4095u) & ~4095u)
#define MAX_MODES 32
#define TEXTURE_RESERVE (512u * 1024u)   /* kept for textures when judging fit */

static struct {
    int      active;
    int      width, height, pitch_bytes, bpp, z_pitch;
    int      double_buffer, depth_bits;
    uint32_t front_off, back_off, z_off, heap_off, heap_end;
    int      front_is_a, draw_front;
    virge_mode mode;
    rg_open  hooks;
} st;
static int found;                       /* 0 not yet, 1 found, -1 none */
static int mapped, graphics;
static rg_info info;
static rg_mode modes[MAX_MODES];
static virge_mode vmodes[MAX_MODES];
static int nmodes;
static uint32_t retrace_us;

static int bytes_of(int bpp) { return bpp == 24 ? 3 : bpp == 32 ? 4 : bpp == 8 ? 1 : 2; }

/* ---- Finding and modes --------------------------------------------------- */

static int probe(void)
{
    if (found) {
        if (found < 0)
            rg_set_error("no S3 ViRGE/DX found");   /* (asked again: the same reason) */
        return found > 0 ? 0 : -1;
    }
    found = -1;
    if (virge_find() != 0) {
        rg_set_error("no S3 ViRGE/DX found");
        return -1;
    }
    memset(&info, 0, sizeof info);
    info.chip_name = "S3 ViRGE/DX";
    info.device_id = virge.device_id;
    info.revision = virge.revision;
    info.vram_bytes = virge.vram_bytes;
    info.fb_phys = virge.bar0;
    info.mmio_phys = virge.bar0 + 0x1000000u;
    found = 1;
    RG_INFO("DGL-DEVICE %s id=%04x rev=%02x vram=%lu", info.chip_name, info.device_id, info.revision,
            (unsigned long)info.vram_bytes);
    return 0;
}

static void caps(rg_caps *c)
{
    memset(c, 0, sizeof *c);
    c->vram_bytes = info.vram_bytes;
    c->max_color_bits = 24;
    c->cursor = 1;
}

/* Whether double buffering and a depth buffer fit, with room for textures. */
static void judge(rg_mode *d)
{
    uint32_t colour = (uint32_t)d->pitch_px * (uint32_t)bytes_of(d->color_bits) * (uint32_t)d->height;
    uint32_t z = (uint32_t)((d->width * 2 + 7) & ~7) * (uint32_t)d->height;
    d->can_double_buffer = 2u * ALIGN4K(colour) + TEXTURE_RESERVE <= info.vram_bytes;
    d->max_depth_bits = (d->can_double_buffer ? 2u : 1u) * ALIGN4K(colour) + ALIGN4K(z) + TEXTURE_RESERVE <=
                        info.vram_bytes ? 16 : 0;
}

static int list_modes(rg_mode *out, int max)
{
    virge_mode all[64];
    int n, i;
    if (probe() != 0)
        return 0;
    if (!nmodes) {
        n = virge_list_modes(all, 64);
        for (i = 0; i < n && i < 64 && nmodes < MAX_MODES; i++) {
            rg_mode d;
            if (all[i].bpp != 15 && all[i].bpp != 16 && all[i].bpp != 24)
                continue;                   /* (8: palettized; 32: neither engine draws it) */
            memset(&d, 0, sizeof d);
            d.width = d.display_width = all[i].width;
            d.height = d.display_height = all[i].height;
            d.color_bits = all[i].bpp;
            d.bios_mode = all[i].mode;
            d.pitch_px = all[i].pitch_bytes / bytes_of(all[i].bpp);
            judge(&d);
            if ((uint32_t)all[i].pitch_bytes * all[i].height > info.vram_bytes)
                continue;
            vmodes[nmodes] = all[i];
            modes[nmodes++] = d;
        }
    }
    for (i = 0; i < nmodes && i < max && out; i++)
        out[i] = modes[i];
    return nmodes;
}

/* ---- Buffers and showing ------------------------------------------------- */

static uint32_t color_off(int front)
{
    if (!st.double_buffer)
        return st.front_off;
    return st.front_is_a == !!front ? st.front_off : st.back_off;
}

static int surface(int which, rg_surface *s)
{
    if (!st.active)
        return -1;
    s->off = color_off(which == RG_SURF_FRONT);
    s->pitch_bytes = st.pitch_bytes;
    s->bpp = st.bpp;
    s->width = st.width;
    s->height = st.height;
    return 0;
}

static void draw_buffer(int front) { st.draw_front = front; }

static int idle(void) { return !mapped || virge_idle(); }

static int sync(void)
{
    if (virge_sync(500000) != 0)
        return -1;
    if (st.hooks.on_sync)
        st.hooks.on_sync();
    return 0;
}

static int swap(int vsync)
{
    uint32_t show;
    if (!st.active || !st.double_buffer)
        return 0;
    show = st.front_is_a ? st.back_off : st.front_off;
    if (vsync) {
        uint32_t t0 = sys_time_us();
        if (st.hooks.wait)
            st.hooks.wait();
        virge_vsync_wait(50000);
        retrace_us += sys_time_us() - t0;
    }
    virge_set_start(show);
    st.front_is_a = !st.front_is_a;
    return RG_SWAP_TARGET;
}

static int present_front(void) { return 0; }   /* (no scaled modes) */

/* ---- 2D and the cursor (the ViRGE layer's) ------------------------------- */

static void target(const rg_surface *s)
{
    virge_set_target(s->off, s->pitch_bytes, s->bpp == 15 ? 16 : s->bpp);
    virge_set_clip(0, 0, s->width, s->height);
}

static void copy_from(const rg_surface *src, int sx, int sy, int dx, int dy, int w, int h)
{
    virge_copy_from(src->off, src->pitch_bytes, sx, sy, dx, dy, w, h);
}

static void mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, int flags)
{
    virge_mono(x, y, w, h, bits, stride, fg, bg, (flags & RG_MONO_TRANSPARENT) ? VIRGE_MONO_TRANSPARENT : 0u);
}

/* ---- Opening and closing ------------------------------------------------- */

static int open_screen(const rg_open *o, rg_opened *out)
{
    int bpp = o->color_bits ? o->color_bits : 16, i;
    uint32_t colour, z;
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
    st.hooks = *o;
    st.mode = vmodes[i];
    st.width = o->width;
    st.height = o->height;
    st.bpp = bpp;
    st.pitch_bytes = vmodes[i].pitch_bytes;
    st.double_buffer = o->double_buffer != 0;
    st.depth_bits = o->depth_bits ? 16 : 0;
    if (!mapped) {
        if (virge_map() != 0) {
            rg_set_error("cannot map the ViRGE's framebuffer and registers");
            return -1;
        }
        mapped = 1;
    }
    graphics = 1;                       /* (about to leave text mode) */
    if (virge_set_mode(&st.mode) != 0) {
        rg_set_error("VBE mode %03x failed", st.mode.mode);
        return -1;                      /* (the caller closes: the screen goes back) */
    }
    colour = (uint32_t)st.pitch_bytes * (uint32_t)st.height;
    st.z_pitch = (st.width * 2 + 7) & ~7;       /* (the 2D engine's strides: multiples of 8 bytes) */
    z = (uint32_t)st.z_pitch * (uint32_t)st.height;
    st.front_off = 0;
    st.back_off = st.double_buffer ? ALIGN4K(colour) : 0;
    st.z_off = st.depth_bits ? ALIGN4K((st.double_buffer ? st.back_off : 0) + colour) : 0;
    st.heap_off = ALIGN4K(st.depth_bits ? st.z_off + z : (st.double_buffer ? st.back_off : 0) + colour);
    if (st.heap_off > virge.vram_bytes) {
        rg_set_error("%dx%dx%d with these buffers needs %lu bytes of VRAM; the card has %lu", st.width, st.height,
                     bpp, (unsigned long)st.heap_off, (unsigned long)virge.vram_bytes);
        return -1;
    }
    st.heap_end = virge.vram_bytes;
    if (o->heap_kb && st.heap_off + o->heap_kb * 1024u < st.heap_end)
        st.heap_end = st.heap_off + o->heap_kb * 1024u;
    st.active = 1;
    /* Everything clear: both colour buffers black, depth at its farthest;
     * then the first buffer shown. */
    st.front_is_a = 1;
    for (i = 0; i < (st.double_buffer ? 2 : 1); i++) {
        s.off = i ? st.back_off : st.front_off;
        s.pitch_bytes = st.pitch_bytes, s.bpp = st.bpp, s.width = st.width, s.height = st.height;
        target(&s);
        virge_fill(0, 0, st.width, st.height, 0);
    }
    if (st.depth_bits) {
        s.off = st.z_off, s.pitch_bytes = st.z_pitch, s.bpp = 16, s.width = st.width, s.height = st.height;
        target(&s);
        virge_fill(0, 0, st.width, st.height, 0xFFFF);
    }
    virge_sync(500000);
    virge_set_start(st.front_off);
    memset(out, 0, sizeof *out);
    out->width = st.width;
    out->height = st.height;
    out->pitch_px = st.pitch_bytes / bytes_of(bpp);
    out->double_buffer = st.double_buffer;
    out->depth_bits = st.depth_bits;
    out->vsync = o->vsync != 0;
    out->mem_start = st.heap_off;
    out->mem_end = st.heap_end;
    out->vram_bytes = virge.vram_bytes;
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
        virge_sync(100000);
    virge_text_mode();
    st.active = 0;
}

static int window_ok(void) { return 0; }
static int window_modes(rg_mode *out, int max) { (void)out; (void)max; return 0; }
static int open_glos(const rg_open *o, rg_opened *out) { (void)o; (void)out; return 1; }

/* ---- Reading and diagnostics -------------------------------------------- */

/* The buffer's pixels as RGBA8 (bit replication to 8 bits), rows top down
 * from (x, y); outside the buffer black. */
static int read_px(int front, int x, int y, int w, int h, uint8_t *rgba, int stride)
{
    const volatile uint8_t *fb;
    int i, j, bpb = bytes_of(st.bpp);
    if (!st.active)
        return -1;
    fb = virge.fb + color_off(front);
    for (j = 0; j < h; j++, rgba += stride) {
        int sy = y + j;
        uint8_t *p = rgba;
        if ((j & 15) == 15 && st.hooks.yield)
            st.hooks.yield();
        for (i = 0; i < w; i++, p += 4) {
            int sx = x + i;
            unsigned r = 0, g = 0, b = 0;
            if (sx >= 0 && sx < st.width && sy >= 0 && sy < st.height) {
                const volatile uint8_t *q = fb + (uint32_t)sy * (uint32_t)st.pitch_bytes + (uint32_t)sx * (uint32_t)bpb;
                if (st.bpp == 24) {
                    b = q[0], g = q[1], r = q[2];
                } else {
                    unsigned c = q[0] | (unsigned)q[1] << 8;
                    if (st.bpp == 15) {
                        r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                        r = (r << 3) | (r >> 2), g = (g << 3) | (g >> 2), b = (b << 3) | (b >> 2);
                    } else {
                        r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                        r = (r << 3) | (r >> 2), g = (g << 2) | (g >> 4), b = (b << 3) | (b >> 2);
                    }
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

const rg_device rg_virge_device = {
    .name = "virge",
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
    .clip = virge_set_clip,
    .fill = virge_fill,
    .copy = virge_copy,
    .copy_from = copy_from,
    .image = virge_image,
    .mono = mono,
    .cursor_load = virge_cursor_load,
    .cursor_colours = virge_cursor_colours,     /* (0xRRGGBB: the layer converts to the mode's pixels) */
    .cursor_show = virge_cursor_show,
    .cursor_move = virge_cursor_move,
    .idle = idle,
    .sync = sync,
    .read = read_px,
    .info = get_info,
    .stats = get_stats,
    .crash = crash_lines,
};
