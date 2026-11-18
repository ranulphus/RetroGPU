/* vbe_soft.c - the VBE device (gl/docs/backends.md, B3): the software renderer
 * full-screen on any card with a VBE 1.2 or later BIOS. Its buffers are system memory
 * (rg/softbuf.h, mem.c's); each swap copies the finished frame to the screen,
 * through the linear framebuffer when the mode has one, else through the
 * banked window A (vbe_set_bank between its pieces). 8-bit contexts set the
 * DAC to their RGB 3-3-2 colours. Chosen by name (DGL_DEVICE=vbe), or when the
 * card found has no drawing back end of its own (DOS-GL's dglInit).
 * DGL_VBE_BANKED=1 uses the window even where there is a linear framebuffer
 * (a test of the banked path on a card that has both). */
#include "rg/softbuf.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdlib.h>
#include <string.h>

#if defined(__DJGPP__) && !defined(DGL_RIG)

static vbe_any_mode cur;                /* the mode shown */
static volatile uint8_t *lfb;           /* its linear framebuffer, mapped; NULL: banked */
static uint32_t lfb_bytes;
static int shown;                       /* a mode set by open */
unsigned long rg_vbe_copies, rg_vbe_banks;

/* The same size and bits, or nothing. 16 bits means RGB565 (15-bit modes are skipped: the buffers are 565). */
typedef struct { int w, h, bpp, found; vbe_any_mode m; } want_t;

static void want_cb(const vbe_any_mode *m, void *ctx)
{
    want_t *f = (want_t *)ctx;
    if (f->found || m->width != f->w || m->height != f->h)
        return;
    if (f->bpp == 16 && !(m->bpp == 16 && m->green_size == 6))
        return;
    if (f->bpp == 32 && !(m->bpp == 32 && m->red_pos == 16 && m->green_pos == 8 && m->blue_pos == 0))
        return;
    if (f->bpp == 8 && m->bpp != 8)
        return;
    f->m = *m;
    f->found = 1;
}

static int find(int w, int h, int bpp, vbe_any_mode *out)
{
    want_t f;
    memset(&f, 0, sizeof f);
    f.w = w, f.h = h, f.bpp = bpp;
    if (vbe_enumerate_any(want_cb, &f) < 0 || !f.found)
        return -1;
    *out = f.m;
    return 0;
}

static int probe(void)
{
    vbe_any_mode m;
    if (vbe_version() < 0x0102) {       /* (1.2: the banked window only; 2.0: a linear framebuffer too) */
        rg_set_error("no VBE 1.2 or later BIOS");
        return -1;
    }
    if (find(640, 480, 16, &m) != 0 && find(640, 480, 32, &m) != 0 && find(640, 480, 8, &m) != 0 &&
        find(320, 200, 8, &m) != 0) {
        rg_set_error("the VBE BIOS has no mode the software renderer can draw in");
        return -1;
    }
    return 0;
}

static void caps(rg_caps *c)
{
    memset(c, 0, sizeof *c);
    c->max_color_bits = 32;
}

typedef struct { rg_mode *out; int max, n; } list_t;

static void list_cb(const vbe_any_mode *m, void *ctx)
{
    list_t *l = (list_t *)ctx;
    int i, bpp = m->bpp == 16 && m->green_size == 6 ? 16 : m->bpp == 32 && m->red_pos == 16 ? 32 : m->bpp == 8 ? 8 : 0;
    if (!bpp || m->width < 320 || m->height < 200)
        return;
    for (i = 0; i < l->n && i < l->max; i++)       /* (each size and depth once) */
        if (l->out && l->out[i].width == m->width && l->out[i].height == m->height && l->out[i].color_bits == bpp)
            return;
    if (l->out && l->n < l->max) {
        memset(&l->out[l->n], 0, sizeof l->out[l->n]);
        l->out[l->n].width = l->out[l->n].display_width = m->width;
        l->out[l->n].height = l->out[l->n].display_height = m->height;
        l->out[l->n].color_bits = bpp;
        l->out[l->n].pitch_px = m->width;
        l->out[l->n].can_double_buffer = 1;
        l->out[l->n].max_depth_bits = 32;
    }
    l->n++;
}

static int modes(rg_mode *out, int max)
{
    list_t l;
    l.out = out, l.max = max, l.n = 0;
    if (vbe_enumerate_any(list_cb, &l) < 0)
        return 0;
    return l.n;
}

/* The front buffer to the screen. */
static void show(void)
{
    const uint8_t *src = rg_sb_color(1);
    const int row = rg_sb.width * (rg_sb.bpp / 8);
    int y;
    if (!shown || !rg_sb.active)
        return;
    if (lfb) {
        for (y = 0; y < rg_sb.height; y++)
            memcpy((void *)(lfb + (uint32_t)y * cur.pitch_bytes), src + (size_t)y * rg_sb.pitch, (size_t)row);
    } else {
        /* Window A, gran_kb steps: each row in the pieces the window boundaries cut it into. */
        volatile uint8_t *win = (volatile uint8_t *)sys_real_ptr(cur.win_seg, 0);
        const uint32_t gran = (uint32_t)cur.gran_kb * 1024u, wsize = (uint32_t)cur.win_kb * 1024u;
        uint32_t bank = 0xFFFFFFFFu;
        for (y = 0; y < rg_sb.height; y++) {
            uint32_t at = (uint32_t)y * cur.pitch_bytes, done = 0;
            while (done < (uint32_t)row) {
                uint32_t b = (at + done) / gran, off = (at + done) - b * gran, n = (uint32_t)row - done;
                if (n > wsize - off)
                    n = wsize - off;
                if (b != bank) {
                    vbe_set_bank(b);
                    bank = b;
                    rg_vbe_banks++;
                }
                memcpy((void *)(win + off), src + (size_t)y * rg_sb.pitch + done, n);
                done += n;
            }
        }
    }
    rg_vbe_copies++;
}

static void close_dev(void)
{
    rg_sb_close();
    if (lfb)
        sys_unmap_phys(lfb, lfb_bytes);
    lfb = NULL;
    if (shown)
        vbe_set_text_mode();
    shown = 0;
}

static int open_vbe(const rg_open *o, rg_opened *out)
{
    int bpp = o->color_bits ? o->color_bits : 16, i;
    close_dev();
    if (find(o->width, o->height, bpp, &cur) != 0) {
        rg_set_error("the VBE BIOS has no %dx%d mode at %d bits", o->width, o->height, bpp);
        return -1;
    }
    if (rg_sb_open(o, out) != 0)
        return -1;
    {
        const char *e = getenv("DGL_VBE_BANKED");
        if (e && *e == '1' && cur.win_seg && cur.win_kb && cur.gran_kb)
            cur.lfb_phys = 0;
    }
    if (cur.lfb_phys) {
        lfb_bytes = (uint32_t)cur.pitch_bytes * cur.height;
        lfb = (volatile uint8_t *)sys_map_phys(cur.lfb_phys, lfb_bytes);
        if (!lfb) {
            rg_sb_close();
            rg_set_error("the linear framebuffer at %08lx could not be mapped", (unsigned long)cur.lfb_phys);
            return -1;
        }
    }
    if (vbe_set_mode_any(&cur) != 0) {
        close_dev();
        rg_set_error("VBE mode %03Xh would not set", (unsigned)cur.mode);
        return -1;
    }
    shown = 1;
    if (bpp == 8) {                     /* (RGB 3-3-2 in the DAC, 6 bits a colour) */
        sys_outb(0x3C8, 0);
        for (i = 0; i < 256; i++) {
            sys_outb(0x3C9, (uint8_t)((i >> 5) * 63 / 7));
            sys_outb(0x3C9, (uint8_t)(((i >> 2) & 7) * 63 / 7));
            sys_outb(0x3C9, (uint8_t)((i & 3) * 63 / 3));
        }
    }
    show();                             /* (the cleared buffer: never the BIOS's garbage) */
    out->fit = lfb ? "VBE linear" : "VBE banked";
    return 0;
}

static int swap(int vsync)
{
    (void)vsync;
    if (rg_sb.flush)
        rg_sb.flush();
    if (rg_sb.color[1])
        rg_sb.front_is_a = !rg_sb.front_is_a;
    show();
    return 0;
}

static int present_front(void)
{
    if (rg_sb.flush)
        rg_sb.flush();
    show();
    return 0;
}

static void info(rg_info *i)
{
    memset(i, 0, sizeof *i);
    i->chip_name = "Software (VBE)";
    i->display_width = rg_sb.active ? rg_sb.width : 0;
    i->display_height = rg_sb.active ? rg_sb.height : 0;
    i->picture_width = i->display_width;
    i->picture_height = i->display_height;
    i->fit = rg_sb.active ? (lfb ? "VBE linear" : "VBE banked") : NULL;
}

static void crash(void) { if (shown) vbe_set_text_mode(); shown = 0; }

#else   /* the rig and the host: no VBE */

static int probe(void) { rg_set_error("no VBE here"); return -1; }
static void caps(rg_caps *c) { memset(c, 0, sizeof *c); }
static int modes(rg_mode *out, int max) { (void)out; (void)max; return 0; }
static void close_dev(void) { rg_sb_close(); }
static int open_vbe(const rg_open *o, rg_opened *out) { (void)o; (void)out; rg_set_error("no VBE here"); return -1; }
static int swap(int vsync) { (void)vsync; return 0; }
static int present_front(void) { return 0; }
static void info(rg_info *i) { memset(i, 0, sizeof *i); i->chip_name = "Software (VBE)"; }
static void crash(void) { }

#endif

static void draw_buffer(int front) { rg_sb.draw_front = front; }
static int idle(void) { return 1; }
static int sync(void)
{
    if (rg_sb.flush)
        rg_sb.flush();
    return 0;
}
static void stats(rg_stats *s) { memset(s, 0, sizeof *s); }
static int window_ok(void) { return 0; }      /* (GLOS windows are the memory device's) */
static int window_modes(rg_mode *out, int max) { (void)out; (void)max; return 0; }
static int open_glos(const rg_open *o, rg_opened *out) { (void)o; (void)out; return 1; }

const rg_device rg_vbe_device = {
    .name = "vbe",
    .flags = RG_DEV_BY_NAME,
    .probe = probe,
    .caps = caps,
    .modes = modes,
    .open = open_vbe,
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
