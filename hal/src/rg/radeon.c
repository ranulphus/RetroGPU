/* radeon.c - the ATI Radeon device (rg/device.h; docs/radeon.md RD4): the Radeon layer (hal/src/radeon: the R100
 * family) behind the device table, as the Rage 128's (rg/r128.c), from which it is made.
 *
 * Modes are the BIOS's (VBE, with the linear window) in 16 (R5G6B5) and 32 bits a pixel: what the 3D engine draws.
 * A pitch other than the mode's (Glide's 1024 pixels) is set in the CRTC and taken by every surface. VRAM layout:
 * colour buffer A, colour buffer B (double buffering), the depth buffer (16-bit with 16-bit colour, Z24S8 with 32,
 * at the colour's pitch in pixels), each 4 KB aligned; then what is left for drawing (textures) to the top.
 *
 * The command processor is started at open from a ring in system memory (radeon_cp_init), with the R100 microcode
 * embedded in the layer or R100_CP.BIN's (linux-firmware's radeon/R100_cp.bin: 512 big-endian dwords, high word
 * first) from the program's folder when it is there. If it doesn't start, drawing goes by plain register writes:
 * slower, the same pictures. DGL_RADEON_RING=0: not tried.
 *
 * A swap with the command processor is a flip in the stream (the next frame's drawing held until the display has
 * taken the new start); without it, the engine waited for, the start written, then the retrace wait (the chip
 * takes a start at the end of the vertical blank: DFLIP).
 *
 * DGL_RADEON_VRAM=A-B (megabytes): everything this device uses lies in megabytes A to B of the card (B not
 * included), for a card with bad memory elsewhere: the P3's has megabytes 2-15 and 34-47 faulty, and 16-33 is
 * its setting there. Programs get the same through rg_radeon_vram_mb before the open. No GLOS window. */
#include "rg/device.h"
#include "rg/radeon_dev.h"
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "mga/sys.h"
#include <string.h>
#if !defined(__WATCOMC__)
#include <stdio.h>
#include <stdlib.h>
#endif

#define ALIGN4K(x) (((x) + 4095u) & ~4095u)
#define MAX_MODES 32
#define TEXTURE_RESERVE (1024u * 1024u)  /* kept for textures when judging fit */

rg_radeon_ctx rg_radeon;                /* (the open context: rg/radeon_dev.h, for the drawing back ends) */
int rg_radeon_vram_mb[2];               /* (megabytes A and B: the card's memory this device may use; 0, 0: all) */
#define st rg_radeon
static radeon_mode st_mode;
static rg_open st_hooks;
static int found;                       /* 0 not yet, 1 found, -1 none */
static int mapped, graphics;
static rg_info info;
static rg_mode modes[MAX_MODES];
static radeon_mode rmodes[MAX_MODES];
static int nmodes;
static uint32_t retrace_us, timeouts;
static int ring_on;                     /* (the command processor from a ring) */
static uint32_t win_start, win_end;     /* the card's memory in use (DGL_RADEON_VRAM): bytes */
static int line_flips;                  /* (FLIP_CNTL set: swaps without the vertical sync) */

static int bytes_of(int bpp) { return bpp == 32 ? 4 : bpp == 8 ? 1 : 2; }

/* ---- Finding and modes --------------------------------------------------- */

static int probe(void)
{
    if (found) {
        if (found < 0)
            rg_set_error("no ATI Radeon (R100 family) found");
        return found > 0 ? 0 : -1;
    }
    found = -1;
    if (radeon_find() != 0) {
        rg_set_error("no ATI Radeon (R100 family) found");
        return -1;
    }
    if (radeon_map() != 0) {
        rg_set_error("cannot map the Radeon's registers and framebuffer");
        return -1;
    }
    mapped = 1;
    memset(&info, 0, sizeof info);
    info.chip_name = radeon.name;
    info.device_id = radeon.device_id;
    info.revision = radeon.revision;
    info.vram_bytes = radeon.vram_bytes;
    info.fb_phys = radeon.fb_phys;
    info.mmio_phys = radeon.mmio_phys;
    info.fifo_depth = 64;
    win_start = 0, win_end = radeon.vram_bytes;
#if !defined(__WATCOMC__)
    {
        const char *e = getenv("DGL_RADEON_VRAM");
        int a = 0, b = 0;
        if (e && sscanf(e, "%d-%d", &a, &b) == 2)
            rg_radeon_vram_mb[0] = a, rg_radeon_vram_mb[1] = b;
    }
#endif
    if (rg_radeon_vram_mb[1] > rg_radeon_vram_mb[0] && rg_radeon_vram_mb[0] >= 0 &&
        ((uint32_t)rg_radeon_vram_mb[1] << 20) <= radeon.vram_bytes) {
        win_start = (uint32_t)rg_radeon_vram_mb[0] << 20, win_end = (uint32_t)rg_radeon_vram_mb[1] << 20;
        RG_WARN("DGL-RADEON megabytes %d to %d of the card's %lu in use (DGL_RADEON_VRAM)", rg_radeon_vram_mb[0],
                rg_radeon_vram_mb[1], (unsigned long)(radeon.vram_bytes >> 20));     /* (always said) */
    }
    info.vram_bytes = win_end - win_start;
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
    uint32_t room = win_end - win_start;
    d->can_double_buffer = 2u * ALIGN4K(colour) + TEXTURE_RESERVE <= room;
    d->max_depth_bits = ((d->can_double_buffer ? 2u : 1u) * ALIGN4K(colour) + ALIGN4K(colour) + TEXTURE_RESERVE <= room)
                        ? (d->color_bits == 32 ? 24 : 16) : 0;
}

static int list_modes(rg_mode *out, int max)
{
    static radeon_mode all[96];
    int i, n;
    if (probe() != 0)
        return 0;
    if (!nmodes) {
        n = radeon_list_modes(all, 96);
        for (i = 0; i < n && i < 96 && nmodes < MAX_MODES; i++) {
            const radeon_mode *m = &all[i];
            rg_mode d;
            if ((m->bpp != 16 && m->bpp != 32) || m->width < 320 || (m->width & 7))
                continue;
            if ((uint32_t)m->pitch_bytes * m->height > win_end - win_start)
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
/* R100_CP.BIN (linux-firmware's radeon/R100_cp.bin: 512 big-endian dwords, each entry's high word first) in the
 * program's folder, else the current one: its words as the layer takes them (low, high), or NULL for the layer's
 * embedded copy. (Not in Glide's Open Watcom runtime: no C library there; the embedded copy.) */
#ifdef __DJGPP__
extern char *__dos_argv0;
#endif
static uint32_t file_pairs[512];

static const uint32_t *microcode(void)
{
    char path[160] = "R100_CP.BIN";
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
        strcpy(e ? e + 1 : path, "R100_CP.BIN");
    }
#endif
    f = fopen(path, "rb");
    if (!f && strcmp(path, "R100_CP.BIN") != 0)
        f = fopen(strcpy(path, "R100_CP.BIN"), "rb");
    if (!f)
        return NULL;
    n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n != 2048) {
        RG_WARN("DGL-RADEON %s is %lu bytes, not 2048: the embedded microcode used", path, (unsigned long)n);
        return NULL;
    }
    for (i = 0; i < 256; i++) {
        const unsigned char *e = b + i * 8;
        file_pairs[i * 2 + 1] = (uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 | (uint32_t)e[2] << 8 | e[3];
        file_pairs[i * 2] = (uint32_t)e[4] << 24 | (uint32_t)e[5] << 16 | (uint32_t)e[6] << 8 | e[7];
    }
    RG_WARN("DGL-RADEON the command processor's microcode from %s", path);     /* (always said: not the embedded copy) */
    return file_pairs;
}
#else
static const uint32_t *microcode(void) { return NULL; }
#endif

/* ---- Buffers and showing ------------------------------------------------- */

uint32_t rg_radeon_color_off(int front)
{
    if (!st.double_buffer)
        return st.front_off;
    return st.front_is_a == !!front ? st.front_off : st.back_off;
}

static int surface(int which, rg_surface *s)
{
    if (!st.active)
        return -1;
    s->off = rg_radeon_color_off(which == RG_SURF_FRONT);
    s->pitch_bytes = st.pitch_bytes;
    s->bpp = st.bpp;
    s->width = st.width;
    s->height = st.height;
    return 0;
}

static void draw_buffer(int front) { st.draw_front = front; }

static int idle(void) { return !mapped || radeon_idle(); }

static int sync(void)
{
    if (radeon_sync(500000) != 0) {
        timeouts++;
        return -1;
    }
    if (st_hooks.on_sync)
        st_hooks.on_sync();
    return 0;
}

static uint32_t flip_seq;                /* the last flip queued in the command stream */

static int swap(int vsync)
{
    uint32_t show;
    if (!st.active || !st.double_buffer)
        return 0;
    show = st.front_is_a ? st.back_off : st.front_off;
    /* Without the vertical sync a start is taken at the next scan line (FLIP_CNTL): changed with the engine idle. */
    if (line_flips != !vsync) {
        radeon_sync(500000);
        radeon_flip_at_line(!vsync);
        line_flips = !vsync;
    }
    /* The command processor running: the flip in the command stream (radeon_flip_queue); the CPU neither drains the
     * engine nor waits for the retrace. With the retrace, the stream held until the new buffer is shown, the next
     * frame's drawing reaching the old one only then, and one flip in flight (the one before shown first). */
    if (radeon_cp_running()) {
        uint32_t t0 = sys_time_us(), tw = t0;
        while (vsync && (int32_t)(flip_seq - radeon_flip_seen()) > 0) {
            if (st_hooks.wait && sys_time_us() - tw > 2000u) {
                st_hooks.wait();
                tw = sys_time_us();
            }
            if (sys_time_us() - t0 > 500000u) {
                timeouts++;
                break;
            }
        }
        retrace_us += sys_time_us() - t0;
        if (radeon_flip_queue(show, flip_seq + 1u, vsync) == 0) {
            flip_seq++;
            st.front_is_a = !st.front_is_a;
            return RG_SWAP_TARGET;
        }
    }
    /* (register writes: the frame finished first, the start written, then the retrace: the chip takes the start at
     * the end of the vertical blank) */
    if (radeon_sync(500000) != 0)
        timeouts++;
    radeon_set_start(show);
    if (vsync) {
        uint32_t t0 = sys_time_us();
        if (st_hooks.wait)
            st_hooks.wait();
        radeon_vsync_wait(50000);
        retrace_us += sys_time_us() - t0;
    }
    st.front_is_a = !st.front_is_a;
    return RG_SWAP_TARGET;
}

static int present_front(void) { return 0; }   /* (no scaled modes) */

/* ---- 2D and the cursor (the layer's) ------------------------------------- */

static void target(const rg_surface *s)
{
    radeon_set_target(s->off, s->pitch_bytes, s->bpp);
    radeon_set_clip(0, 0, s->width, s->height);
}

static void copy_from(const rg_surface *src, int sx, int sy, int dx, int dy, int w, int h)
{
    radeon_copy_from(src->off, src->pitch_bytes, sx, sy, dx, dy, w, h);
}

static void mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, int flags)
{
    radeon_mono(x, y, w, h, bits, stride, fg, bg, (flags & RG_MONO_TRANSPARENT) ? RADEON_MONO_TRANSPARENT : 0u);
}

/* ---- Opening and closing ------------------------------------------------- */

static int open_screen(const rg_open *o, rg_opened *out)
{
    int bpp = o->color_bits ? o->color_bits : 16, i, pitch_px;
    uint32_t colour, zcolour;
    rg_surface s;
    if (probe() != 0)
        return -1;
    {   /* An engine an earlier program left stuck: the BIOS's mode set, or the first write to it, would hold the PCI
         * bus (a write to a full FIFO) and freeze the PC, as it did the P3 twice (2026-11-25, -26). Half a second for
         * it to be idle, else the recovery reset (registers below 1000h only until it is free); still stuck: no open. */
        uint32_t t0 = sys_time_us();
        int check = 1;
#if !defined(__WATCOMC__)
        const char *e = getenv("DGL_TEST_NOCHECK");     /* (tests only: 1 skips it, so GLOS's own guard is what's tried) */
        check = !(e && *e == '1');
#endif
        while (check && !radeon_idle() && sys_time_us() - t0 < 500000u)
            ;
        if (check && !radeon_idle() && radeon_engine_recover() != 0) {
            rg_set_error("the card's engine is stuck, left so by an earlier program (RBBM_STATUS %08lx): reboot the "
                         "PC or switch it off and on", (unsigned long)radeon_recover_status);
            return -1;
        }
    }
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
        if (o->pitch_px < st_mode.width || (o->pitch_px & 7) || (o->pitch_px * bytes_of(bpp)) % 64) {
            rg_set_error("a pitch of %d pixels at %dx%dx%d: rows are multiples of 8 pixels and 64 bytes, at least "
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
    /* (a chip that tiles depth, RADEON_TILED_Z: its depth buffer in whole tiles, 32 pixels wide and 16 rows high at
     * 16 bits, 16 by 16 at 32: Mesa's radeon_mba_z16 and _z32; so its pitch a multiple of 32 pixels and its rows of
     * 16) */
    st.z_rows = st.height;
    if (radeon.flags & RADEON_TILED_Z)
        st.z_pitch_bytes = ((pitch_px + 31) & ~31) * (st.depth_bits == 16 ? 2 : 4), st.z_rows = (st.height + 15) & ~15;
    else
        st.z_pitch_bytes = pitch_px * (st.depth_bits == 16 ? 2 : 4);
    graphics = 1;                               /* (about to leave text mode) */
    if (radeon_set_mode(&st_mode) != 0) {
        rg_set_error("VBE mode %03x failed", st_mode.mode);
        return -1;                              /* (the caller closes: the screen goes back) */
    }
    if (st.pitch_bytes != (int)rmodes[i].pitch_bytes) {
        RG_INFO("DGL-RADEON pitch %d px: the BIOS's %d, the CRTC's set", pitch_px,
                rmodes[i].pitch_bytes / bytes_of(bpp));
        radeon_set_pitch(st.pitch_bytes);
    }
    /* The command processor from a ring in system memory (radeon_cp_init: the CPU writes memory, the chip as far
     * behind as the ring holds), else plain register writes. DGL_RADEON_RING=0: not tried. */
    line_flips = 0;
    radeon_cp_varea_want = 0;
#if !defined(__WATCOMC__)
    /* With the transform engine (gl/docs/dx7.md X3; on unless DGL_TNL=0), its draws' vertices in video memory by
     * default; DGL_RADEON_VAREA=n: an area of n kilobytes beside the ring instead, through the chip's PCI GART. The
     * engine fetching vertices from system memory over the P3's bus made it the bottleneck: DTMIX's textured stress
     * mode 25.5 fps with that area, 65.0 with video memory, 11.9 on the CPU (2026-11-26). */
    if ((radeon.flags & RADEON_HAS_TCL) && !(getenv("DGL_TNL") && *getenv("DGL_TNL") == '0')) {
        const char *e = getenv("DGL_RADEON_VAREA");
        long kb = e && *e ? strtol(e, NULL, 10) : 0;
        radeon_cp_varea_want = kb > 0 && kb <= 1024 ? (uint32_t)kb * 1024u : 0u;
    }
    if (getenv("DGL_RADEON_RING") && *getenv("DGL_RADEON_RING") == '0')
        ring_on = 0;
    else
#endif
    ring_on = radeon_cp_init(microcode(), 0x10000u) == 0;
    if (ring_on) {
        RG_WARN("DGL-RADEON the command processor from a 64 KB ring in system memory");     /* (always said) */
        if (radeon_cp_varea)
            RG_INFO("DGL-RADEON a %u KB vertex area for the transform engine beside it", (unsigned)(radeon_cp_varea_bytes / 1024u));
        radeon_cp_reg(RADEON_GUI_SCRATCH_REG0, 0);      /* (the flips' sequence number: it keeps its last value) */
    } else
        RG_WARN("DGL-RADEON no command processor: drawing by plain register writes");
    colour = (uint32_t)st.pitch_bytes * (uint32_t)st.height;
    zcolour = (uint32_t)st.z_pitch_bytes * (uint32_t)st.z_rows;
    st.front_off = win_start;
    st.back_off = st.double_buffer ? win_start + ALIGN4K(colour) : win_start;
    st.z_off = st.depth_bits ? ALIGN4K(st.back_off + colour) : 0;
    st.heap_off = ALIGN4K(st.depth_bits ? st.z_off + zcolour : st.back_off + colour);
    if (st.heap_off > win_end) {
        rg_set_error("%dx%dx%d with these buffers needs %lu bytes of VRAM; the card has %lu to use", st.width,
                     st.height, bpp, (unsigned long)(st.heap_off - win_start), (unsigned long)(win_end - win_start));
        return -1;
    }
    st.heap_end = win_end;
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
        radeon_fill(0, 0, st.width, st.height, 0);
    }
    if (st.depth_bits) {
        /* (all ones whatever the layout: the whole buffer, its padding too; then, where depth is tiled, the surface
         * that shows it to the CPU linear: Glide's LFB reads and writes of its aux buffer) */
        s.off = st.z_off, s.pitch_bytes = st.z_pitch_bytes, s.bpp = st.depth_bits == 16 ? 16 : 32;
        s.width = st.z_pitch_bytes / (st.depth_bits == 16 ? 2 : 4), s.height = st.z_rows;
        target(&s);
        radeon_fill(0, 0, s.width, s.height, 0xFFFFFFFFu);
        if ((radeon.flags & RADEON_TILED_Z) &&
            radeon_depth_surface(st.z_off, (zcolour + 1023u) & ~1023u, st.z_pitch_bytes, st.depth_bits == 16 ? 16 : 32) == 0)
            RG_INFO("DGL-RADEON its depth buffer tiled, the CPU's view through a surface");
    }
    radeon_sync(500000);
    radeon_set_start(st.front_off);
    flip_seq = ring_on ? radeon_flip_seen() : 0;
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
    out->vram_bytes = win_end - win_start;
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
    if (radeon.flags & RADEON_TILED_Z)
        radeon_depth_surface_off();
    radeon_text_mode();                         /* (the engine waited for, the command processor stopped) */
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
    radeon_sync(500000);
    fb = radeon.fb + rg_radeon_color_off(front);
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
        RG_ERR("DGL-RADEON RBBM_STATUS=%08lx CP_RB_RPTR=%08lx command processor %s", (unsigned long)radeon_rd(RADEON_RBBM_STATUS),
               (unsigned long)radeon_rd(RADEON_CP_RB_RPTR), radeon_cp_running() ? "running" : "off");
}

const rg_device rg_radeon_device = {
    .name = "radeon",
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
    .clip = radeon_set_clip,
    .fill = radeon_fill,
    .copy = radeon_copy,
    .copy_from = copy_from,
    .image = radeon_image,
    .mono = mono,
    .cursor_load = radeon_cursor_load,
    .cursor_colours = radeon_cursor_colours,
    .cursor_show = radeon_cursor_show,
    .cursor_move = radeon_cursor_move,
    .idle = idle,
    .sync = sync,
    .read = read_px,
    .info = get_info,
    .stats = get_stats,
    .crash = crash_lines,
};
