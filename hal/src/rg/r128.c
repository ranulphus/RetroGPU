/* r128.c - the ATI Rage 128 device (rg/device.h; docs/rage128.md R3): the Rage 128 layer (hal/src/r128) behind the
 * device table, as the TNT2's (rg/tnt2.c).
 *
 * Modes are the BIOS's (VBE, with the linear window) in 16 (R5G6B5) and 32 bits a pixel: what the 3D engine draws
 * (8- and 15-bit modes are left to the 2D desktop). A pitch other than the mode's (Glide's 1024 pixels) is set in
 * the CRTC and taken by every surface. VRAM layout: colour buffer A at 0, colour buffer B (double buffering), the
 * depth buffer (16-bit with 16-bit colour, Z24S8 with 32, linear, at the colour's pitch in pixels), each 4 KB
 * aligned; then what is left for drawing (textures) to the top. The CCE is started at open (the 3D engine draws only
 * through it; from then on the 2D engine's writes go as packets too), with ATI's microcode embedded in the layer or
 * R128_CCE.BIN's (linux-firmware's r128/r128_cce.bin, big-endian) from the program's folder when it is there. A swap
 * waits for the engine, then shows the hidden buffer by the display start, written before the retrace wait (the
 * TNT2's order: the start a frame late otherwise, if the chip takes it at the retrace's start; RFLIP watches it). No
 * GLOS window (B3's software path shows a window's pixels for cards other than Matrox ones). */
#include "rg/device.h"
#include "rg/r128_dev.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/sys.h"
#include <string.h>
#if !defined(__WATCOMC__)
#include <stdio.h>
#endif

#define ALIGN4K(x) (((x) + 4095u) & ~4095u)
#define MAX_MODES 32
#define TEXTURE_RESERVE (1024u * 1024u)  /* kept for textures when judging fit */

rg_r128_ctx rg_r128;                    /* (the open context: rg/r128_dev.h, for the drawing back ends) */
#define st rg_r128
static r128_mode st_mode;
static rg_open st_hooks;
static int found;                       /* 0 not yet, 1 found, -1 none */
static int mapped, graphics;
static rg_info info;
static rg_mode modes[MAX_MODES];
static r128_mode rmodes[MAX_MODES];
static int nmodes;
static uint32_t retrace_us, timeouts;

static int bytes_of(int bpp) { return bpp == 32 ? 4 : bpp == 8 ? 1 : 2; }

/* ---- Finding and modes --------------------------------------------------- */

static int probe(void)
{
    if (found) {
        if (found < 0)
            rg_set_error("no ATI Rage 128 found");
        return found > 0 ? 0 : -1;
    }
    found = -1;
    if (r128_find() != 0) {
        rg_set_error("no ATI Rage 128 found");
        return -1;
    }
    if (r128_map() != 0) {
        rg_set_error("cannot map the Rage 128's registers and framebuffer");
        return -1;
    }
    mapped = 1;
    memset(&info, 0, sizeof info);
    info.chip_name = "ATI Rage 128";
    info.device_id = r128.device_id;
    info.revision = r128.revision;
    info.vram_bytes = r128.vram_bytes;
    info.fb_phys = r128.fb_phys;
    info.mmio_phys = r128.mmio_phys;
    info.fifo_depth = 192;
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
    uint32_t room = r128.vram_bytes;
    d->can_double_buffer = 2u * ALIGN4K(colour) + TEXTURE_RESERVE <= room;
    d->max_depth_bits = ((d->can_double_buffer ? 2u : 1u) * ALIGN4K(colour) + ALIGN4K(colour) + TEXTURE_RESERVE <= room)
                        ? (d->color_bits == 32 ? 24 : 16) : 0;
}

static int list_modes(rg_mode *out, int max)
{
    static r128_mode all[96];
    int i, n;
    if (probe() != 0)
        return 0;
    if (!nmodes) {
        n = r128_list_modes(all, 96);
        for (i = 0; i < n && i < 96 && nmodes < MAX_MODES; i++) {
            const r128_mode *m = &all[i];
            rg_mode d;
            if ((m->bpp != 16 && m->bpp != 32) || m->width < 320 || (m->width & 7))
                continue;
            if ((uint32_t)m->pitch_bytes * m->height > r128.vram_bytes)
                continue;
            memset(&d, 0, sizeof d);
            d.width = d.display_width = m->width;
            d.height = d.display_height = m->height;
            d.color_bits = m->bpp;
            d.bios_mode = m->mode;
            d.pitch_px = m->pitch_bytes / bytes_of(m->bpp);
            judge(&d);
            rmodes[nmodes] = *m;
            modes[nmodes++] = d;
        }
    }
    for (i = 0; i < nmodes && i < max && out; i++)
        out[i] = modes[i];
    return nmodes;
}

/* ---- The microcode ------------------------------------------------------- */

#if !defined(__WATCOMC__)
/* R128_CCE.BIN (512 big-endian dwords, linux-firmware's r128/r128_cce.bin) in the program's folder, else the
 * current one: its words, or NULL for the layer's embedded copy. (Not in Glide's Open Watcom runtime: no C
 * library there; the embedded copy.) */
#ifdef __DJGPP__
extern char *__dos_argv0;
#endif
static uint32_t file_pairs[512];

static const uint32_t *microcode(void)
{
    char path[160] = "R128_CCE.BIN";
    unsigned char b[2048 + 1];
    size_t n;
    FILE *f;
    int i;
#ifdef __DJGPP__
    if (__dos_argv0 && strlen(__dos_argv0) < sizeof path - 16) {
        char *e;
        strcpy(path, __dos_argv0);
        e = strrchr(path, '\\');
        if (!e || (strrchr(path, '/') && strrchr(path, '/') > e))
            e = strrchr(path, '/');
        strcpy(e ? e + 1 : path, "R128_CCE.BIN");
    }
#endif
    f = fopen(path, "rb");
    if (!f && strcmp(path, "R128_CCE.BIN") != 0)
        f = fopen(strcpy(path, "R128_CCE.BIN"), "rb");
    if (!f)
        return NULL;
    n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n != 2048) {
        RG_WARN("DGL-R128 %s is %lu bytes, not 2048: the embedded microcode used", path, (unsigned long)n);
        return NULL;
    }
    for (i = 0; i < 512; i++)
        file_pairs[i] = (uint32_t)b[i * 4] << 24 | (uint32_t)b[i * 4 + 1] << 16 | (uint32_t)b[i * 4 + 2] << 8 | b[i * 4 + 3];
    RG_WARN("DGL-R128 the CCE's microcode from %s", path);     /* (always said: not the embedded copy) */
    return file_pairs;
}
#else
static const uint32_t *microcode(void) { return NULL; }
#endif

/* ---- Buffers and showing ------------------------------------------------- */

uint32_t rg_r128_color_off(int front)
{
    if (!st.double_buffer)
        return st.front_off;
    return st.front_is_a == !!front ? st.front_off : st.back_off;
}

static int surface(int which, rg_surface *s)
{
    if (!st.active)
        return -1;
    s->off = rg_r128_color_off(which == RG_SURF_FRONT);
    s->pitch_bytes = st.pitch_bytes;
    s->bpp = st.bpp;
    s->width = st.width;
    s->height = st.height;
    return 0;
}

static void draw_buffer(int front) { st.draw_front = front; }

static int idle(void) { return !mapped || r128_idle(); }

static int sync(void)
{
    if (r128_sync(500000) != 0) {
        timeouts++;
        return -1;
    }
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
    /* (the frame finished first: in PIO mode the chip is never more than its 192-entry FIFO behind, so the wait
     * is short, and a start shown before the last triangles land would show them late) */
    if (r128_sync(500000) != 0)
        timeouts++;
    r128_set_start(show);
    if (vsync) {
        uint32_t t0 = sys_time_us();
        if (st_hooks.wait)
            st_hooks.wait();
        r128_vsync_wait(50000);
        retrace_us += sys_time_us() - t0;
    }
    st.front_is_a = !st.front_is_a;
    return RG_SWAP_TARGET;
}

static int present_front(void) { return 0; }   /* (no scaled modes) */

/* ---- 2D and the cursor (the layer's) ------------------------------------- */

static void target(const rg_surface *s)
{
    r128_set_target(s->off, s->pitch_bytes, s->bpp);
    r128_set_clip(0, 0, s->width, s->height);
}

static void copy_from(const rg_surface *src, int sx, int sy, int dx, int dy, int w, int h)
{
    r128_copy_from(src->off, src->pitch_bytes, sx, sy, dx, dy, w, h);
}

static void mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, int flags)
{
    r128_mono(x, y, w, h, bits, stride, fg, bg, (flags & RG_MONO_TRANSPARENT) ? R128_MONO_TRANSPARENT : 0u);
}

/* ---- Opening and closing ------------------------------------------------- */

static int open_screen(const rg_open *o, rg_opened *out)
{
    int bpp = o->color_bits ? o->color_bits : 16, i, pitch_px;
    uint32_t colour, zcolour;
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
    st_mode = rmodes[i];
    pitch_px = st_mode.pitch_bytes / bytes_of(bpp);
    if (o->pitch_px > 0 && o->pitch_px != pitch_px) {
        if (o->pitch_px < st_mode.width || (o->pitch_px & 7) || (o->pitch_px * bytes_of(bpp)) % 16) {
            rg_set_error("a pitch of %d pixels at %dx%dx%d: rows are multiples of 8 pixels and 16 bytes, at least "
                         "the width", o->pitch_px, o->width, o->height, bpp);
            return -1;
        }
        st_mode.pitch_bytes = (uint16_t)(o->pitch_px * bytes_of(bpp));    /* (the engine's default pitch too) */
        pitch_px = o->pitch_px;
    }
    st.width = o->width;
    st.height = o->height;
    st.bpp = bpp;
    st.pitch_bytes = pitch_px * bytes_of(bpp);
    st.double_buffer = o->double_buffer != 0;
    st.depth_bits = o->depth_bits ? (bpp == 32 ? 24 : 16) : 0;
    st.z_pitch_bytes = pitch_px * (st.depth_bits == 16 ? 2 : 4);
    graphics = 1;                               /* (about to leave text mode) */
    if (r128_set_mode(&st_mode) != 0) {
        rg_set_error("VBE mode %03x failed", st_mode.mode);
        return -1;                              /* (the caller closes: the screen goes back) */
    }
    if (st.pitch_bytes != (int)rmodes[i].pitch_bytes) {
        RG_INFO("DGL-R128 pitch %d px: the BIOS's %d, the CRTC's set", pitch_px,
                rmodes[i].pitch_bytes / bytes_of(bpp));
        r128_set_pitch(st.pitch_bytes);
    }
    if (r128_cce_init(microcode()) != 0) {
        rg_set_error("the Rage 128's CCE didn't start (the engine never went idle)");
        return -1;
    }
    colour = (uint32_t)st.pitch_bytes * (uint32_t)st.height;
    zcolour = (uint32_t)st.z_pitch_bytes * (uint32_t)st.height;
    st.front_off = 0;
    st.back_off = st.double_buffer ? ALIGN4K(colour) : 0;
    st.z_off = st.depth_bits ? ALIGN4K((st.double_buffer ? st.back_off : 0) + colour) : 0;
    st.heap_off = ALIGN4K(st.depth_bits ? st.z_off + zcolour : (st.double_buffer ? st.back_off : 0) + colour);
    if (st.heap_off > r128.vram_bytes) {
        rg_set_error("%dx%dx%d with these buffers needs %lu bytes of VRAM; the card has %lu", st.width, st.height, bpp,
                     (unsigned long)st.heap_off, (unsigned long)r128.vram_bytes);
        return -1;
    }
    st.heap_end = r128.vram_bytes;
    if (o->heap_kb && st.heap_off + o->heap_kb * 1024u < st.heap_end)
        st.heap_end = st.heap_off + o->heap_kb * 1024u;
    st.active = 1;
    /* Everything clear: both colour buffers black, depth at its farthest (all ones: Z24S8's low three bytes are its
     * Z, the stencil's byte above them ones too); then the first buffer shown. */
    st.front_is_a = 1;
    for (i = 0; i < (st.double_buffer ? 2 : 1); i++) {
        s.off = i ? st.back_off : st.front_off;
        s.pitch_bytes = st.pitch_bytes, s.bpp = st.bpp, s.width = st.width, s.height = st.height;
        target(&s);
        r128_fill(0, 0, st.width, st.height, 0);
    }
    if (st.depth_bits) {
        s.off = st.z_off, s.pitch_bytes = st.z_pitch_bytes, s.bpp = st.depth_bits == 16 ? 16 : 32;
        s.width = st.width, s.height = st.height;
        target(&s);
        r128_fill(0, 0, st.width, st.height, 0xFFFFFFFFu);
    }
    r128_sync(500000);
    r128_set_start(st.front_off);
    memset(out, 0, sizeof *out);
    out->width = st.width;
    out->height = st.height;
    out->pitch_px = pitch_px;
    out->double_buffer = st.double_buffer;
    out->depth_bits = st.depth_bits;
    out->stencil_bits = st.depth_bits == 24 ? 8 : 0;
    out->vsync = o->vsync != 0;
    out->mem_start = st.heap_off;
    out->mem_end = st.heap_end;
    out->vram_bytes = r128.vram_bytes;
    out->display_width = st.width;
    out->display_height = st.height;
    out->fit = "native";
    out->color_bits = bpp;
    return 0;
}

static void close_dev(void)
{
    if (!graphics)
        return;
    graphics = 0;
    r128_text_mode();                           /* (the engine waited for, the CCE stopped: MMIO as the BIOS left it) */
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
    r128_sync(500000);
    fb = r128.fb + rg_r128_color_off(front);
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
    s->timeouts = timeouts;
}

static void crash_lines(void)
{
    if (mapped)
        RG_ERR("DGL-R128 GUI_STAT=%08lx PM4_STAT=%08lx CCE %s", (unsigned long)r128_rd(R128_GUI_STAT),
               (unsigned long)r128_rd(R128_PM4_STAT), r128_cce_running() ? "running" : "off");
}

const rg_device rg_r128_device = {
    .name = "r128",
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
    .clip = r128_set_clip,
    .fill = r128_fill,
    .copy = r128_copy,
    .copy_from = copy_from,
    .image = r128_image,
    .mono = mono,
    .cursor_load = r128_cursor_load,
    .cursor_colours = r128_cursor_colours,
    .cursor_show = r128_cursor_show,
    .cursor_move = r128_cursor_move,
    .idle = idle,
    .sync = sync,
    .read = read_px,
    .info = get_info,
    .stats = get_stats,
    .crash = crash_lines,
};
