/* vbe.c - mode setting through the card's VBE BIOS (PRD §7.7). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include <string.h>

static uint8_t *vbe_buf;
static uint16_t vbe_seg;
static int vbe_ver;

static int vbe_call(sys_rmregs *r)
{
    if (sys_rm_int(0x10, r) < 0)
        return -1;
    return (r->eax & 0xFFFF) == 0x004F ? 0 : -1;
}

static int vbe_init(void)
{
    sys_rmregs r;
    if (vbe_buf)
        return 0;
    vbe_buf = (uint8_t *)sys_dos_alloc(1024, &vbe_seg);
    if (!vbe_buf)
        return -1;
    memset(vbe_buf, 0, 512);
    memcpy(vbe_buf, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00;
    r.es = vbe_seg;
    r.edi = 0;
    if (vbe_call(&r) < 0 || memcmp(vbe_buf, "VESA", 4) != 0)
        return -1;
    vbe_ver = vbe_buf[4] | (vbe_buf[5] << 8);
    return 0;
}

int vbe_version(void) { return vbe_init() == 0 ? vbe_ver : 0; }

/* Video memory the BIOS reports (VBE info block TotalMemory, 64 KB units):
 * safe before any mode is set, unlike mga_probe_vram(), which writes VRAM. */
uint32_t vbe_total_memory(void)
{
    if (vbe_init() < 0)
        return 0;
    return (uint32_t)(vbe_buf[0x12] | (vbe_buf[0x13] << 8)) << 16;
}

static int mode_info(uint16_t mode, mga_vbe_mode *m)
{
    sys_rmregs r;
    uint8_t *b = vbe_buf + 512;
    memset(b, 0, 256);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F01;
    r.ecx = mode;
    r.es = vbe_seg;
    r.edi = 512;
    if (vbe_call(&r) < 0)
        return -1;
    memset(m, 0, sizeof *m);
    m->mode = mode;
    m->pitch_bytes = (uint16_t)(b[0x10] | (b[0x11] << 8));
    m->width = (uint16_t)(b[0x12] | (b[0x13] << 8));
    m->height = (uint16_t)(b[0x14] | (b[0x15] << 8));
    m->bpp = b[0x19];
    m->red_size = b[0x1F]; m->red_pos = b[0x20];
    m->green_size = b[0x21]; m->green_pos = b[0x22];
    m->blue_size = b[0x23]; m->blue_pos = b[0x24];
    m->lfb_phys = (uint32_t)b[0x28] | ((uint32_t)b[0x29] << 8) | ((uint32_t)b[0x2A] << 16) | ((uint32_t)b[0x2B] << 24);
    /* Usable: supported, graphics, linear framebuffer, direct colour (or
     * packed pixels at 8 bits: 2D drawing only, never planned). */
    if ((b[0] & 0x91) != 0x91 || !(b[0x1B] == 6 || (b[0x1B] == 4 && m->bpp == 8)))
        return 1;
    return 0;
}

int vbe_enumerate(void (*cb)(const mga_vbe_mode *m, void *ctx), void *ctx)
{
    uint16_t list[128], *p;
    int n = 0, i;
    if (vbe_init() < 0)
        return -1;
    p = (uint16_t *)sys_real_ptr((uint16_t)(vbe_buf[0x10] | (vbe_buf[0x11] << 8)),
                                 (uint16_t)(vbe_buf[0x0E] | (vbe_buf[0x0F] << 8)));
    while (p && *p != 0xFFFF && n < 128)
        list[n++] = *p++;
    for (i = 0; i < n; i++) {
        mga_vbe_mode m;
        if (mode_info(list[i], &m) == 0)
            cb(&m, ctx);
    }
    return n;
}

typedef struct { int w, h, bpp; mga_vbe_mode best; int found; } find_ctx;

static void find_cb(const mga_vbe_mode *m, void *ctx)
{
    find_ctx *f = (find_ctx *)ctx;
    if (m->width != f->w || m->height != f->h || m->bpp != f->bpp || f->found)
        return;
    if (f->bpp == 16 && !(m->red_size == 5 && m->green_size == 6 && m->blue_size == 5))
        return;
    f->best = *m;
    f->found = 1;
}

int vbe_find_mode(int w, int h, int bpp, mga_vbe_mode *out)
{
    find_ctx f;
    memset(&f, 0, sizeof f);
    f.w = w; f.h = h; f.bpp = bpp;
    if (vbe_enumerate(find_cb, &f) < 0 || !f.found)
        return -1;
    *out = f.best;
    return 0;
}

/* A mode for the software renderer: supported, colour, graphics; direct colour, or packed pixels at 8 bits;
   with a linear framebuffer or a window A that can be written. 1: not usable. */
static int mode_info_any(uint16_t mode, vbe_any_mode *m)
{
    sys_rmregs r;
    uint8_t *b = vbe_buf + 512;
    memset(b, 0, 256);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F01;
    r.ecx = mode;
    r.es = vbe_seg;
    r.edi = 512;
    if (vbe_call(&r) < 0)
        return -1;
    memset(m, 0, sizeof *m);
    m->mode = mode;
    m->pitch_bytes = (uint16_t)(b[0x10] | (b[0x11] << 8));
    m->width = (uint16_t)(b[0x12] | (b[0x13] << 8));
    m->height = (uint16_t)(b[0x14] | (b[0x15] << 8));
    m->bpp = b[0x19];
    m->red_size = b[0x1F]; m->red_pos = b[0x20];
    m->green_size = b[0x21]; m->green_pos = b[0x22];
    m->blue_size = b[0x23]; m->blue_pos = b[0x24];
    if ((b[0] & 0x80) && vbe_ver >= 0x0200)     /* (a linear framebuffer: VBE 2.0's; 1.2 has only the window) */
        m->lfb_phys = (uint32_t)b[0x28] | ((uint32_t)b[0x29] << 8) | ((uint32_t)b[0x2A] << 16) |
                      ((uint32_t)b[0x2B] << 24);
    m->win_seg = (uint16_t)(b[0x08] | (b[0x09] << 8));
    m->gran_kb = (uint16_t)(b[0x04] | (b[0x05] << 8));
    m->win_kb = (uint16_t)(b[0x06] | (b[0x07] << 8));
    if ((b[0] & 0x19) != 0x19 || !(b[0x1B] == 6 || (b[0x1B] == 4 && m->bpp == 8)))
        return 1;
    if (!m->lfb_phys && ((b[0x02] & 0x05) != 0x05 || !m->win_seg || !m->gran_kb || !m->win_kb || (b[0] & 0x40)))
        return 1;                       /* (no linear framebuffer and no writable window A) */
    return 0;
}

int vbe_enumerate_any(void (*cb)(const vbe_any_mode *m, void *ctx), void *ctx)
{
    uint16_t list[128], *p;
    int n = 0, i;
    if (vbe_init() < 0)
        return -1;
    p = (uint16_t *)sys_real_ptr((uint16_t)(vbe_buf[0x10] | (vbe_buf[0x11] << 8)),
                                 (uint16_t)(vbe_buf[0x0E] | (vbe_buf[0x0F] << 8)));
    while (p && *p != 0xFFFF && n < 128)
        list[n++] = *p++;
    for (i = 0; i < n; i++) {
        vbe_any_mode m;
        if (mode_info_any(list[i], &m) == 0)
            cb(&m, ctx);
    }
    return n;
}

int vbe_set_mode_any(const vbe_any_mode *m)
{
    sys_rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = (m->lfb_phys ? 0x4000u : 0u) | m->mode;
    return vbe_call(&r);
}

int vbe_set_bank(unsigned bank)
{
    sys_rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F05;
    r.ebx = 0;                          /* window A, set */
    r.edx = bank;
    return vbe_call(&r);
}

int vbe_set_mode(const mga_vbe_mode *m, int pitch_px, int *actual_pitch_px)
{
    sys_rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = 0x4000u | m->mode;          /* linear framebuffer */
    if (vbe_call(&r) < 0)
        return -1;
    *actual_pitch_px = m->pitch_bytes / (m->bpp / 8);
    if (pitch_px > 0) {
        memset(&r, 0, sizeof r);
        r.eax = 0x4F06;
        r.ebx = 0;                       /* set scan line length in pixels */
        r.ecx = (uint32_t)pitch_px;
        if (vbe_call(&r) == 0)
            *actual_pitch_px = (int)((r.ebx & 0xFFFF) / (uint32_t)(m->bpp / 8));
    }
    return 0;
}

int vbe_set_display_start(uint32_t byte_offset, int pitch_bytes, int bpp)
{
    sys_rmregs r;
    uint32_t bytes_pp = (uint32_t)bpp / 8;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F07;
    r.ebx = 0x0000;
    r.ecx = (byte_offset % (uint32_t)pitch_bytes) / bytes_pp;
    r.edx = byte_offset / (uint32_t)pitch_bytes;
    return vbe_call(&r);
}

/* ---- The mode planner ---------------------------------------------------- */

static const struct { int w, h; } crt_sizes[] = {
    { 320, 200 }, { 640, 200 }, { 640, 350 }, { 640, 400 }, { 400, 256 }, { 512, 256 },
};

const char *mga_fit_name(int fit)
{
    static const char *const n[] = { "native", "zoom", "integer", "fill", "aspect", "topleft" };
    return fit >= 0 && fit <= MGA_FIT_TOPLEFT ? n[fit] : "?";
}

int mga_pow2_pitch(int w)
{
    int p = 32;
    while (p < w)
        p <<= 1;
    return p;
}

static int usable(const mga_vbe_mode *m, int bpp)
{
    if (m->bpp != bpp)
        return 0;
    return bpp != 16 || (m->red_size == 5 && m->green_size == 6 && m->blue_size == 5);
}

/* The smallest usable mode (by area) at least w x h, and 4:3 if asked. */
static const mga_vbe_mode *smallest(const mga_vbe_mode *l, int n, int w, int h, int bpp, int four_three)
{
    const mga_vbe_mode *best = NULL;
    int i;
    for (i = 0; i < n; i++) {
        const mga_vbe_mode *m = &l[i];
        if (!usable(m, bpp) || m->width < w || m->height < h || (four_three && m->width * 3 != m->height * 4))
            continue;
        if (!best || (long)m->width * m->height < (long)best->width * best->height)
            best = m;
    }
    return best;
}

static const mga_vbe_mode *exact(const mga_vbe_mode *l, int n, int w, int h, int bpp)
{
    int i;
    for (i = 0; i < n; i++)
        if (usable(&l[i], bpp) && l[i].width == w && l[i].height == h)
            return &l[i];
    return NULL;
}

int mga_plan_from_list(const mga_vbe_mode *l, int n, int w, int h, int bpp, unsigned flags, mga_mode_plan *p)
{
    const mga_vbe_mode *m;
    int k, i;
    if (w <= 0 || h <= 0)
        return -1;
    memset(p, 0, sizeof *p);
    p->w = w; p->h = h; p->zoom = 1;
    if ((m = exact(l, n, w, h, bpp)) != NULL) {
        p->disp = *m;
        p->fit = (flags & MGA_PLAN_FORCE) ? MGA_FIT_INTEGER : MGA_FIT_NATIVE;
        p->dw = w; p->dh = h;
        return 0;
    }
    if ((flags & MGA_PLAN_ZOOM) && (m = exact(l, n, 2 * w, 2 * h, bpp)) != NULL) {
        p->disp = *m;
        p->fit = MGA_FIT_ZOOM;
        p->zoom = 2;
        p->dw = 2 * w; p->dh = 2 * h;
        return 0;
    }
    if (flags & MGA_PLAN_TOPLEFT) {
        if ((m = smallest(l, n, w, h, bpp, 0)) == NULL)
            return -1;
        p->disp = *m;
        p->fit = MGA_FIT_TOPLEFT;
        p->dw = w; p->dh = h;
        return 0;
    }
    for (k = 2; k <= 4; k++)
        if ((m = exact(l, n, k * w, k * h, bpp)) != NULL) {
            p->disp = *m;
            p->fit = MGA_FIT_INTEGER;
            p->dw = k * w; p->dh = k * h;
            return 0;
        }
    for (i = 0; i < (int)(sizeof crt_sizes / sizeof crt_sizes[0]); i++)
        if (crt_sizes[i].w == w && crt_sizes[i].h == h && (m = smallest(l, n, w, h, bpp, 1)) != NULL) {
            p->disp = *m;
            p->fit = MGA_FIT_FILL;
            p->dw = m->width; p->dh = m->height;
            return 0;
        }
    if ((m = smallest(l, n, w, h, bpp, 0)) == NULL)
        return -1;
    p->disp = *m;
    p->fit = MGA_FIT_ASPECT;
    /* Scale evenly by the smaller ratio; centre on even pixels. */
    if ((long)m->width * h <= (long)m->height * w) {
        p->dw = m->width;
        p->dh = (int)(((long)h * m->width + w / 2) / w);
    } else {
        p->dh = m->height;
        p->dw = (int)(((long)w * m->height + h / 2) / h);
    }
    p->dx = ((m->width - p->dw) / 2) & ~1;
    p->dy = ((m->height - p->dh) / 2) & ~1;
    return 0;
}

typedef struct { mga_vbe_mode list[64]; int n; } list_ctx;

static void list_cb(const mga_vbe_mode *m, void *ctx)
{
    list_ctx *l = (list_ctx *)ctx;
    if (l->n < 64 && m->bpp > 8 && (!mga.fb_phys || m->lfb_phys == mga.fb_phys))
        l->list[l->n++] = *m;
}

int vbe_plan_mode(int w, int h, int bpp, unsigned flags, mga_mode_plan *p)
{
    static list_ctx l;
    l.n = 0;
    if (vbe_enumerate(list_cb, &l) < 0)
        return -1;
    return mga_plan_from_list(l.list, l.n, w, h, bpp, flags, p);
}

/* ---- Zoom ---------------------------------------------------------------- */

static int zoom_on;

void vbe_set_zoom(int factor)
{
    uint8_t c9;
    if (!mga_mmio || (factor <= 1 && !zoom_on))
        return;
    if (factor != 2 && factor != 4)
        factor = 1;
    /* In power-graphics mode CRTC9's maxscan repeats each line maxscan+1
     * times; its top bits (line compare, vertical blank bit 9) stay. */
    MGA_WR8(MGAREG_CRTC_INDEX, 0x09);
    c9 = MGA_RD8(MGAREG_CRTC_DATA);
    MGA_WR8(MGAREG_CRTC_DATA, (uint8_t)((c9 & 0xE0) | (factor - 1)));
    MGA_WR8(MGAREG_PALWTADD, 0x38);                 /* XZOOMCTRL: hzoom 00 1x, 01 2x, 11 4x */
    MGA_WR8(MGAREG_X_DATAREG, (uint8_t)(factor == 4 ? 3 : factor == 2 ? 1 : 0));
    zoom_on = factor > 1;
}

void vbe_set_text_mode(void)
{
    sys_rmregs r;
    vbe_set_zoom(1);                                /* the BIOS's text mode set leaves XZOOMCTRL alone */
    memset(&r, 0, sizeof r);
    r.eax = 0x0003;
    sys_rm_int(0x10, &r);
}
