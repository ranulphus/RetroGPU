/* mga.c - the Matrox device (rg/device.h, rg/mga_dev.h; gl/docs/b1.md): the
 * Matrox HAL behind the device table. Moved from DOS-GL's src/dgl (device.c's
 * discovery and modes, context.c's buffers, display and swaps, crash.c's
 * teardown, readpix.c's read) in B1, with nothing changed in what it writes.
 * Its GLOS window is mga_glos.c.
 *
 * Discovery reads PCI configuration space and the VBE information block
 * only: nothing is mapped and VRAM is not touched, so the modes can be
 * listed in text mode before a context opens.
 *
 * VRAM layout: colour buffer A at 0, colour buffer B (double buffering),
 * the 16-bit depth buffer, then what is left (textures); each 4 KB aligned.
 * The engine always draws into the hidden buffer; a swap shows it through
 * VBE 4F07h, after the vertical retrace when vsync is on.
 *
 * Scaled modes (a size the BIOS lacks, shown in a larger BIOS mode; the
 * HAL's mode planner decides): two display buffers of the BIOS mode, then
 * one render buffer that GL draws into, its depth buffer and the rest. A
 * swap scales the render buffer into the hidden display buffer
 * (engine_present) and flips that. Zoomed modes use the layout above at the
 * BIOS mode's pitch, and the chip doubles lines and pixels. */
#include "rg/mga_dev.h"
#include "rg/glos.h"
#include "mga/mmio.h"
#include "mga/warp.h"
#include "mga/sys.h"
#include "mga/prof.h"
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <sys/nearptr.h>
#endif
#ifdef MGA_DJGPP
#  include "mga/fp.h"
#  define RG_FPU_ENTER() FPU_ENTER()
#  define RG_FPU_LEAVE() FPU_LEAVE()
#else
#  define RG_FPU_ENTER() ((void)0)
#  define RG_FPU_LEAVE() ((void)0)
#endif

rg_mga_state rg_mga;

#define ALIGN4K(x) (((x) + 4095u) & ~4095u)
#define MAX_MODES 32
#define TEXTURE_RESERVE (512u * 1024u)   /* VRAM kept for textures when judging fit */

static int discovered;                   /* 0 not yet, 1 found, -1 none */
static char why_not[96];                 /* ... and why not */
static rg_info info;
static mga_vbe_mode vbe_all[MAX_MODES];  /* the BIOS's usable 16-bit modes on this card */
static int nvbe;
static rg_mode modes[MAX_MODES];
static mga_mode_plan plans[MAX_MODES];
static int nmodes;
static unsigned plan_flags;               /* DGL_ZOOM=1: zoom; DGL_PRESENT=force: scale even native sizes */
static int graphics;                      /* left text mode: close has the screen to give back */
static uint32_t present_us, retrace_us;

/* Sizes offered beyond the BIOS's own when the planner can show them. */
static const int virtual_sizes[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 512 } };

int rg_mga_pitch_for(int width)
{
    /* The drawing engine wants a pitch that is a multiple of 32 pixels, at
     * most 4096 (FR-HAL-8). DOS-GL programs it with VBE 4F06h rather than
     * trusting the BIOS's choice. */
    int p = (width + 31) & ~31;
    return p <= 4096 ? p : 0;
}

/* ---- Discovery and modes ------------------------------------------------- */

#ifndef DGL_RIG
/* open found more VRAM than the BIOS reported (a probe in graphics mode). */
static void note_vram(uint32_t bytes)
{
    info.vram_bytes = bytes;
    mga.vram_bytes = bytes;
}
#endif

/* The VRAM a mode needs before the depth buffer and textures: the display
 * buffers (one or two) and, for a scaled mode, the render buffer. */
static uint32_t colour_bytes(const rg_mode *d, int buffers)
{
    uint32_t disp = (uint32_t)rg_mga_pitch_for(d->display_width) * (uint32_t)d->display_height * 2u;
    if (d->scaled)
        return 2u * disp + (uint32_t)d->pitch_px * (uint32_t)d->height * 2u;
    return (uint32_t)buffers * (uint32_t)d->pitch_px * (uint32_t)d->height * 2u;
}

/* Advisory: whether double buffering and a depth buffer fit, against the
 * best VRAM figure known (the BIOS's, or the probe's once dglInit ran). */
static void judge(rg_mode *d, uint32_t vram)
{
    uint32_t z = (uint32_t)d->pitch_px * (uint32_t)d->height * 2u;
    d->can_double_buffer = colour_bytes(d, 2) + TEXTURE_RESERVE <= vram;
    d->max_depth_bits = colour_bytes(d, d->can_double_buffer ? 2 : 1) + z + TEXTURE_RESERVE <= vram ? 16 : 0;
}

/* The most VRAM known: the probe's (dglInit), DGL_VRAM_KB, or the BIOS's. */
static uint32_t known_vram(void)
{
    const char *e = getenv("DGL_VRAM_KB");
    uint32_t kb = e ? (uint32_t)strtoul(e, NULL, 10) : 0;
    return kb * 1024u > info.vram_bytes ? kb * 1024u : info.vram_bytes;
}

#ifndef DGL_RIG
static void mode_cb(const mga_vbe_mode *m, void *ctx)
{
    (void)ctx;
    /* Linear, direct colour (vbe.c already required both), 16 bpp RGB565. */
    if (m->bpp != 16 || m->red_size != 5 || m->green_size != 6 || m->blue_size != 5 || !m->lfb_phys)
        return;
    if (m->lfb_phys != mga.fb_phys || !rg_mga_pitch_for(m->width))
        return;                          /* not this card's aperture, or too wide */
    if (nvbe < MAX_MODES)
        vbe_all[nvbe++] = *m;
}
#endif

/* Add w x h as the planner would show it (native, zoomed or scaled). */
static void add_mode(int w, int h)
{
    rg_mode d;
    mga_mode_plan p;
    int i;
    if (nmodes == MAX_MODES || mga_plan_from_list(vbe_all, nvbe, w, h, 16, plan_flags, &p) != 0)
        return;
    for (i = 0; i < nmodes; i++)
        if (modes[i].width == w && modes[i].height == h)
            return;
    memset(&d, 0, sizeof d);
    d.width = w;
    d.height = h;
    d.color_bits = 16;
    d.bios_mode = p.disp.mode;
    d.display_width = p.disp.width;
    d.display_height = p.disp.height;
    d.zoomed = p.fit == MGA_FIT_ZOOM;
    d.scaled = p.fit != MGA_FIT_NATIVE && p.fit != MGA_FIT_ZOOM;
    /* Scaled: a render buffer engine_present can sample (a power-of-two
     * pitch); zoomed and native: the display pitch. */
    d.pitch_px = d.scaled ? mga_pow2_pitch(w) : rg_mga_pitch_for(p.disp.width);
    if (d.scaled && d.pitch_px > 2048)
        return;
    /* Not dropped when it seems not to fit: the BIOS can under-report VRAM
     * (the G200's says 2 MB of 8); dglInit probes and decides. */
    judge(&d, known_vram());
    plans[nmodes] = p;
    modes[nmodes++] = d;
}

static int probe(void)
{
    int i, j;
    if (rg_lock.on) {
        /* In a GLOS window: the chip is what GLOS described (mga_glos.c), and
         * the PCI bus is GLOS's, not the program's to probe (the probe would
         * find nothing and wipe `mga` on its way). */
        memset(&info, 0, sizeof info);
        info.chip_name = mga.name;
        info.device_id = mga.device_id;
        info.revision = mga.revision;
        info.fb_phys = mga.fb_phys;
        info.mmio_phys = mga.mmio_phys;
        info.fifo_depth = mga.fifo_depth;
        info.vram_bytes = mga.vram_bytes;
        return 0;
    }
    if (discovered) {
        if (discovered < 0)
            rg_set_error("%s", why_not);        /* (asked again: the same reason) */
        return discovered > 0 ? 0 : -1;
    }
    discovered = -1;
    strcpy(why_not, "no supported Matrox card found (G400, G450 or G200)");
    if (mga_find(&mga) != 0) {
        rg_set_error("%s", why_not);
        return -1;
    }
    memset(&info, 0, sizeof info);
    info.chip_name = mga.name;
    info.device_id = mga.device_id;
    info.revision = mga.revision;
    info.fb_phys = mga.fb_phys;
    info.mmio_phys = mga.mmio_phys;
    info.iload_phys = mga.iload_phys;
    info.fifo_depth = mga.fifo_depth;
#ifdef DGL_RIG
    /* Loop C (a Linux program on a card the host's own driver may be
     * showing): no BIOS to ask. VRAM is DGL_RIG_VRAM_KB (default 8 MB), and
     * the "BIOS modes" are the usual 16-bit sizes, which are drawn into
     * VRAM without ever being shown (open's DGL_RIG_VRAM_BASE). */
    {
        static const int sizes[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 } };
        const char *e = getenv("DGL_RIG_VRAM_KB");
        uint32_t kb = e ? (uint32_t)strtoul(e, NULL, 10) : 8192u;
        info.emulated = 0;
        info.vram_bytes = kb * 1024u;
        nvbe = 0;
        for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++) {
            mga_vbe_mode *m = &vbe_all[nvbe++];
            memset(m, 0, sizeof *m);
            m->mode = (uint16_t)(0x100 + i);
            m->width = (uint16_t)sizes[i][0];
            m->height = (uint16_t)sizes[i][1];
            m->bpp = 16;
            m->pitch_bytes = (uint16_t)(rg_mga_pitch_for(sizes[i][0]) * 2);
            m->lfb_phys = mga.fb_phys;
            m->red_size = 5; m->red_pos = 11; m->green_size = 6; m->green_pos = 5; m->blue_size = 5;
        }
    }
#else
    info.emulated = mga_detect_emulator();
    info.vram_bytes = vbe_total_memory();
#endif
    if (!info.vram_bytes) {
        strcpy(why_not, "the card's VBE BIOS did not answer (VBE 2.0 or later needed)");
        rg_set_error("%s", why_not);
        return -1;
    }
    mga.vram_bytes = info.vram_bytes;
    {
        const char *e = getenv("DGL_ZOOM");
        plan_flags = e && *e && *e != '0' ? MGA_PLAN_ZOOM : 0;
        e = getenv("DGL_PRESENT");
        if (e && !strcmp(e, "force"))
            plan_flags |= MGA_PLAN_FORCE;
    }
#ifdef DGL_RIG
    nmodes = 0;
#else
    nvbe = nmodes = 0;
    vbe_enumerate(mode_cb, NULL);
#endif
    for (i = 0; i < nvbe; i++)
        add_mode(vbe_all[i].width, vbe_all[i].height);
    for (i = 0; i < (int)(sizeof virtual_sizes / sizeof virtual_sizes[0]); i++)
        add_mode(virtual_sizes[i][0], virtual_sizes[i][1]);
    /* Smallest first. */
    for (i = 1; i < nmodes; i++)
        for (j = i; j > 0 && modes[j].width * modes[j].height < modes[j - 1].width * modes[j - 1].height; j--) {
            rg_mode t = modes[j]; mga_mode_plan v = plans[j];
            modes[j] = modes[j - 1]; plans[j] = plans[j - 1];
            modes[j - 1] = t; plans[j - 1] = v;
        }
    if (mga.family == MGA_FAMILY_G100)
        RG_WARN("DGL-WARN %s is a development-only card: no GL blending (PRD D13)", mga.name);
    RG_INFO("DGL-DEVICE %s id=%04x rev=%02x vram=%lu modes=%d", mga.name, mga.device_id, mga.revision,
            (unsigned long)info.vram_bytes, nmodes);
    discovered = 1;
    return 0;
}

static void caps(rg_caps *c)
{
    memset(c, 0, sizeof *c);
    c->vram_bytes = info.vram_bytes;
    c->max_color_bits = 16;
    c->windows = 1;
    c->cursor = 1;
}

static int list_modes(rg_mode *out, int max)
{
    int i;
    if (probe() != 0)
        return 0;
    for (i = 0; i < nmodes; i++)
        judge(&modes[i], known_vram());          /* a probe since discovery may have found more */
    for (i = 0; i < nmodes && i < max && out; i++)
        out[i] = modes[i];
    return nmodes;
}

/* The enumerated mode for width x height, and how it is shown. */
static const rg_mode *mode_for(int width, int height, const mga_mode_plan **plan)
{
    int i;
    for (i = 0; i < nmodes; i++)
        if (modes[i].width == width && modes[i].height == height) {
            *plan = &plans[i];
            return &modes[i];
        }
    return NULL;
}

/* ---- Buffers and showing ------------------------------------------------- */

uint32_t rg_mga_color_off(int front)
{
    if (rg_mga.scaled || !rg_mga.double_buffer)
        return rg_mga.front_off;
    return rg_mga.front_is_a == !!front ? rg_mga.front_off : rg_mga.back_off;
}

static void set_target(void)
{
    mga_target t;
    RG_GPU_ENTER();
    memset(&t, 0, sizeof t);
    t.color_off = rg_mga_color_off(rg_mga.draw_front);
    t.z_off = rg_mga.z_off;
    t.pitch_px = rg_mga.pitch_px;
    t.bpp = 16;
    t.zbits = rg_mga.z_off ? 16 : 0;
    engine_set_target(&t);
    engine_set_clip(0, 0, rg_mga.width, rg_mga.height);
}

static void draw_buffer(int front)
{
    rg_mga.draw_front = front;
    if (rg_mga.active)
        set_target();                   /* through the FIFO, so queued draws keep their buffer */
}

/* Where the display starts. Loop C (DGL_RIG) shows nothing: the host's
 * driver may own the screen. */
static uint32_t rig_base;
static void show_start(uint32_t off, int pitch_bytes)
{
#ifdef DGL_RIG
    (void)off;
    (void)pitch_bytes;
#else
    vbe_set_display_start(off, pitch_bytes, 16);
#endif
}

static int sync(void)
{
    RG_GPU_ENTER();
    if (engine_sync(500000) != 0)
        return -1;
    if (rg_mga.hooks.on_sync)
        rg_mga.hooks.on_sync();
    return 0;
}

static int idle(void) { return !mga_mmio || engine_idle(); }

/* Scale the render buffer into a display buffer: the hidden one for a
 * swap, the one on screen for GL_FRONT or single buffering. The engine's
 * texture, blend and mask registers are overwritten: the caller writes its
 * state again (RG_SWAP_ENGINE). */
static void present(int to_front)
{
    mga_surface rs, ds;
    uint32_t t0 = sys_time_us();
    rs.off = rg_mga.front_off; rs.w = rg_mga.width; rs.h = rg_mga.height; rs.pitch_px = rg_mga.pitch_px;
    ds.off = rg_mga.disp_off[to_front ? rg_mga.disp_front : !rg_mga.disp_front];
    ds.w = rg_mga.plan.disp.width; ds.h = rg_mga.plan.disp.height; ds.pitch_px = rg_mga.disp_pitch_px;
    {
        RG_FPU_ENTER();
        engine_present(&rs, &ds, rg_mga.plan.dx, rg_mga.plan.dy, rg_mga.plan.dw, rg_mga.plan.dh,
                       rg_mga.filter ? MGA_PRESENT_BILINEAR : MGA_PRESENT_NEAREST);
        RG_FPU_LEAVE();
    }
    sync();
    present_us += sys_time_us() - t0;
}

static int present_front(void)
{
    if (!rg_mga.active || !rg_mga.scaled)
        return 0;
    sync();
    present(1);
    return RG_SWAP_ENGINE;
}

/* The retrace wait: the user's hook runs once before it, never during it (a
 * hook that ran past the start of the blank would cost a whole frame). */
static void retrace_wait(void)
{
    uint32_t t0 = sys_time_us();
    PROF_SCOPE(PROF_D_VSYNC);
    if (rg_mga.hooks.wait)
        rg_mga.hooks.wait();
    engine_vsync_wait(50000);
    retrace_us += sys_time_us() - t0;
}

void rg_mga_glos_show(uint32_t off);     /* mga_glos.c */

static int swap(int vsync)
{
    uint32_t show;
    rg_mga.vsync = vsync;
    if (rg_lock.on) {
        /* In a GLOS window: it shows the finished buffer from now on (the
         * desktop puts it on the screen), and drawing goes into the other. */
        if (!rg_mga.double_buffer) {
            rg_mga_glos_show(rg_mga.front_off);
            return 0;
        }
        show = rg_mga.front_is_a ? rg_mga.back_off : rg_mga.front_off;
        rg_mga_glos_show(show);
        rg_mga.front_is_a = !rg_mga.front_is_a;
        set_target();
        RG_GPU_QUIET();                 /* (the program's own work until the next frame's drawing) */
        return RG_SWAP_TARGET;
    }
    if (rg_mga.scaled) {
        /* Scale the finished frame into the hidden display buffer, then
         * flip the display (single buffering: into the one on screen). */
        present(!rg_mga.double_buffer);
        if (!rg_mga.double_buffer)
            return RG_SWAP_ENGINE;
        if (rg_mga.vsync)
            retrace_wait();
        rg_mga.disp_front = !rg_mga.disp_front;
        show_start(rg_mga.disp_off[rg_mga.disp_front], rg_mga.disp_pitch_px * 2);
        set_target();
        return RG_SWAP_ENGINE;
    }
    if (!rg_mga.double_buffer)
        return 0;
    show = rg_mga.front_is_a ? rg_mga.back_off : rg_mga.front_off;
    if (rg_mga.vsync)
        retrace_wait();
    show_start(show, rg_mga.pitch_px * 2);
    rg_mga.front_is_a = !rg_mga.front_is_a;
    set_target();
    return RG_SWAP_TARGET;                      /* the target write reset MACCESS */
}

/* ---- Opening and closing ------------------------------------------------- */

/* Clear both colour buffers and the depth buffer, then draw into the hidden
 * buffer (the end of every open). */
void rg_mga_clear_all(void)
{
    rg_mga.front_is_a = 0; set_target(); engine_fill(0, 0, rg_mga.width, rg_mga.height, 0);
    rg_mga.front_is_a = 1; set_target(); engine_fill(0, 0, rg_mga.width, rg_mga.height, 0);
    if (rg_mga.z_off)
        engine_fill_depth(0, 0, rg_mga.width, rg_mga.height, 0xFFFF);
    engine_sync(500000);
}

static void fill_opened(rg_opened *out, const char *fit)
{
    memset(out, 0, sizeof *out);
    out->width = rg_mga.width;
    out->height = rg_mga.height;
    out->pitch_px = rg_mga.pitch_px;
    out->double_buffer = rg_mga.double_buffer;
    out->depth_bits = rg_mga.depth_bits;
    out->vsync = rg_mga.vsync;
    out->mem_start = rg_mga.heap_off;
    out->mem_end = rg_mga.heap_end;
    out->vram_bytes = rg_mga.vram_bytes;
    out->display_width = rg_mga.plan.disp.width;
    out->display_height = rg_mga.plan.disp.height;
    out->fit = fit;
}

static int open_screen(const rg_open *o, rg_opened *out)
{
    const rg_mode *md;
    const mga_mode_plan *pl;
    int pitch = 0;
    uint32_t fb, render;
    if (o->color_bits && o->color_bits != 16) {
        rg_set_error("only 16-bit colour is supported");
        return -1;
    }
    if (probe() != 0)
        return -1;
    md = mode_for(o->width, o->height, &pl);
    if (!md) {
        rg_set_error("no usable %dx%dx16 mode on this card", o->width, o->height);
        return -1;
    }
    memset(&rg_mga, 0, sizeof rg_mga);
    rg_mga.hooks = *o;
    rg_mga.width = o->width;
    rg_mga.height = o->height;
    rg_mga.plan = *pl;
    rg_mga.scaled = md->scaled;
    rg_mga.zoomed = md->zoomed;
    rg_mga.pitch_px = md->pitch_px;            /* the render buffer's (scaled) or the display's */
    rg_mga.disp_pitch_px = rg_mga_pitch_for(md->display_width);
    rg_mga.filter = o->scale_filter;
    rg_mga.double_buffer = o->double_buffer != 0;
    rg_mga.depth_bits = o->depth_bits ? 16 : 0;
    rg_mga.vsync = o->vsync != 0;
    rg_mga.vram_bytes = mga.vram_bytes;
    /* Map at least 16 MB (the most a G200 carries; every supported card's
     * aperture is that big or bigger), so the probe below can find VRAM the
     * BIOS did not report. */
    mga.fb_size = mga.vram_bytes > (16u << 20) ? mga.vram_bytes : (16u << 20);
    if (mga_map(&mga) != 0) {
        rg_set_error("cannot map the card's apertures (DPMI 0800h)");
        return -1;
    }
    graphics = 1;                       /* (about to leave text mode) */
#ifdef DGL_RIG
    /* Loop C: nothing is shown. The buffers start at DGL_RIG_VRAM_BASE (KB;
     * above the host's console when its driver is still bound), and neither
     * the mode, the DAC nor the display start is touched. */
    (void)pitch;
    {
        const char *e = getenv("DGL_RIG_VRAM_BASE");
        rig_base = e ? (uint32_t)strtoul(e, NULL, 10) * 1024u : 0;
    }
#else
    if (vbe_set_mode(&pl->disp, rg_mga.disp_pitch_px, &pitch) != 0 || pitch != rg_mga.disp_pitch_px) {
        rg_set_error("VBE mode %03x with pitch %d failed (BIOS gave %d)", pl->disp.mode, rg_mga.disp_pitch_px,
                     pitch);
        return -1;                      /* (the caller closes: the screen goes back) */
    }
    vbe_set_zoom(pl->zoom);
#endif
#ifndef DGL_RIG
    {
        /* VBE's total memory can be short: Matrox's G200 BIOS reports 2 MB of
         * 8 in 86Box. In graphics mode VRAM can be written freely, so probe it
         * and keep the larger size for the texture heap. */
        uint32_t probed = mga_probe_vram();
        if (probed > rg_mga.vram_bytes) {
            RG_WARN("DGL-VRAM the BIOS reports %lu KB, the card has %lu KB", (unsigned long)(rg_mga.vram_bytes >> 10),
                    (unsigned long)(probed >> 10));
            rg_mga.vram_bytes = probed;
            note_vram(probed);
        }
    }
#endif
    /* The layout, checked against the VRAM found (the BIOS can under-report). */
    fb = (uint32_t)rg_mga.pitch_px * (uint32_t)o->height * 2u;
    render = rig_base;
    if (rg_mga.scaled) {
        uint32_t disp = (uint32_t)rg_mga.disp_pitch_px * (uint32_t)pl->disp.height * 2u;
        rg_mga.disp_off[0] = rig_base;
        rg_mga.disp_off[1] = ALIGN4K(rig_base + disp);
        render = ALIGN4K(rg_mga.disp_off[1] + disp);
    }
    rg_mga.front_off = render;
    rg_mga.back_off = rg_mga.double_buffer && !rg_mga.scaled ? ALIGN4K(render + fb) : render;
    rg_mga.z_off = rg_mga.depth_bits ? ALIGN4K(rg_mga.back_off + fb) : 0;
    rg_mga.heap_off = ALIGN4K((rg_mga.z_off ? rg_mga.z_off : rg_mga.back_off) + fb);
    if (rg_mga.heap_off > rg_mga.vram_bytes) {
        rg_set_error("%dx%d with these buffers needs %lu bytes of VRAM; the card has %lu", o->width, o->height,
                     (unsigned long)rg_mga.heap_off, (unsigned long)rg_mga.vram_bytes);
        return -1;
    }
    /* DGL_TEXHEAP_KB (heap_kb) caps what is left (tests of eviction, or a
     * smaller card's memory on a bigger one). */
    rg_mga.heap_end = rg_mga.vram_bytes;
    if (o->heap_kb && rg_mga.heap_off + o->heap_kb * 1024u < rg_mga.heap_end)
        rg_mga.heap_end = rg_mga.heap_off + o->heap_kb * 1024u;
    engine_init(rg_mga.pitch_px, 16);
#ifndef DGL_RIG
    {
        /* 16-bit pixels index the DAC palette on these chips, and the BIOS
         * leaves it non-linear: load an identity ramp. */
        uint8_t ramp[256];
        int i;
        for (i = 0; i < 256; i++)
            ramp[i] = (uint8_t)i;
        dac_set_ramp(ramp);
    }
#endif
    rg_mga.front_is_a = 1;
    rg_mga.active = 1;
    /* Clear everything the context owns, then draw into the hidden buffer. */
    if (rg_mga.scaled) {
        mga_target t;
        int i;
        memset(&t, 0, sizeof t);
        t.pitch_px = rg_mga.disp_pitch_px; t.bpp = 16;
        for (i = 0; i < 2; i++) {
            t.color_off = rg_mga.disp_off[i];
            engine_set_target(&t);
            engine_set_clip(0, 0, pl->disp.width, pl->disp.height);
            engine_fill(0, 0, pl->disp.width, pl->disp.height, 0);
        }
    }
    rg_mga_clear_all();
    show_start(rg_mga.scaled ? rg_mga.disp_off[0] : rg_mga.front_off,
               (rg_mga.scaled ? rg_mga.disp_pitch_px : rg_mga.pitch_px) * 2);
    fill_opened(out, mga_fit_name(pl->fit));
    return 0;
}

void rg_mga_glos_close(void);            /* mga_glos.c */
int  rg_mga_glos_open(const rg_open *o, rg_opened *out);
int  rg_mga_window_ok(void);
int  rg_mga_window_modes(rg_mode *out, int max);
void rg_mga_glos_slow(void);

static void close_dev(void)
{
    if (!graphics)
        return;
    graphics = 0;
    if (warp_tris || warp_failures) {           /* DGL_WARP: the DMA window back to general purpose, the WARP suspended */
        warp_stop();
        RG_ERR("DGL-WARP tris=%lu failures=%lu kicks=%lu ring=%d/%luK wc=%d resets=%lu timeouts=%lu fence_stuck=%lu "
               "seqs=%lu/%lu clock_backs=%lu", (unsigned long)warp_tris, (unsigned long)warp_failures,
               (unsigned long)warp_dma_kicks, warp_ring_mode, (unsigned long)warp_ring_kb, warp_wc_status,
               (unsigned long)engine_resets, (unsigned long)engine_timeouts, (unsigned long)engine_fence_stuck,
               (unsigned long)warp_seqs, (unsigned long)warp_seqs_t2, (unsigned long)sys_time_backs);
    }
    if (rg_lock.on) {                           /* a GLOS window: GLOS has the screen and the engine's state */
        if (*rg_lock.held)
            engine_sync(100000);
        rg_mga_glos_close();
        rg_mga.active = 0;
        return;
    }
    if (mga_mmio && engine_sync(100000) != 0)
        engine_reset();
#ifndef DGL_RIG
    vbe_set_text_mode();                        /* Loop C never left the host's mode */
#endif
    rg_mga.active = 0;
}

/* Called by mga_glos.c once its window is open: the device holds the screen's
 * equivalent (a teardown has a window to close). */
void rg_mga_graphics(void) { graphics = 1; }

/* ---- 2D and the cursor (the HAL's engine_* and cursor_*) ------------------ */

static int t_bytes = 2;                 /* the 2D target's bytes a pixel */

static int surface(int which, rg_surface *s)
{
    if (!rg_mga.active)
        return -1;
    s->off = rg_mga_color_off(which == RG_SURF_FRONT);
    s->pitch_bytes = rg_mga.pitch_px * 2;
    s->bpp = 16;
    s->width = rg_mga.width;
    s->height = rg_mga.height;
    return 0;
}

static void target(const rg_surface *s)
{
    mga_target t;
    RG_GPU_ENTER();
    memset(&t, 0, sizeof t);
    t.bpp = s->bpp == 15 ? 16 : s->bpp;
    t_bytes = t.bpp / 8;
    t.color_off = s->off;
    t.pitch_px = s->pitch_bytes / t_bytes;
    engine_set_target(&t);
    engine_set_clip(0, 0, s->width, s->height);
}

static void clip(int x0, int y0, int x1, int y1) { engine_set_clip(x0, y0, x1, y1); }
static void fill(int x, int y, int w, int h, uint32_t pixel) { engine_fill(x, y, w, h, pixel); }
static void copy(int sx, int sy, int dx, int dy, int w, int h) { engine_copy(sx, sy, dx, dy, w, h); }

static void copy_from(const rg_surface *src, int sx, int sy, int dx, int dy, int w, int h)
{
    engine_copy_from(src->off, src->pitch_bytes / t_bytes, sx, sy, dx, dy, w, h);
}

/* Through the ILOAD: each row packed into whole dwords. */
static void image(int x, int y, int w, int h, const void *px, int stride)
{
    static uint32_t row[2048];
    const uint8_t *p = (const uint8_t *)px;
    int dw = engine_image_begin(x, y, w, h), j;
    if (!dw || dw > 2048)
        return;
    for (j = 0; j < h; j++, p += stride) {
        row[dw - 1] = 0;
        memcpy(row, p, (size_t)w * (size_t)t_bytes);
        engine_iload_row(row);
    }
    engine_iload_end();
}

static void mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, int flags)
{
    engine_mono(x, y, w, h, bits, stride, fg, bg, (flags & RG_MONO_TRANSPARENT) ? MGA_MONO_TRANSPARENT : 0u);
}

/* ---- Reading and diagnostics -------------------------------------------- */

/* RGB565 expanded to 8 bits a channel by bit replication, alpha 255; rows top
 * down from (x, y), pixels outside the buffer black. */
static int read_px(int front, int x, int y, int w, int h, uint8_t *rgba, int stride)
{
    const volatile uint16_t *fb;
    int i, j;
    if (!rg_mga.active)
        return -1;
    RG_GPU_QUIET();                         /* (the CPU's work from here) */
    fb = (const volatile uint16_t *)(mga_fb + rg_mga_color_off(front));
    for (j = 0; j < h; j++, rgba += stride) {
        int sy = y + j;
        uint8_t *p = rgba;
        if ((j & 15) == 15 && rg_mga.hooks.yield)
            rg_mga.hooks.yield();           /* (a whole frame read back takes a while: the sound's turn) */
        for (i = 0; i < w; i++, p += 4) {
            int sx = x + i;
            unsigned r = 0, g = 0, b = 0;
            if (sx >= 0 && sx < rg_mga.width && sy >= 0 && sy < rg_mga.height) {
                uint16_t c = fb[sy * rg_mga.pitch_px + sx];
                r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31;
                r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
            }
            p[0] = (uint8_t)r, p[1] = (uint8_t)g, p[2] = (uint8_t)b, p[3] = 255;
        }
    }
    return 0;
}

static void get_info(rg_info *i)
{
    *i = info;
    i->display_width = rg_mga.active ? rg_mga.plan.disp.width : 0;
    i->display_height = rg_mga.active ? rg_mga.plan.disp.height : 0;
    i->picture_x = rg_mga.plan.dx;
    i->picture_y = rg_mga.plan.dy;
    i->picture_width = rg_mga.active ? rg_mga.plan.dw : 0;
    i->picture_height = rg_mga.active ? rg_mga.plan.dh : 0;
    i->fit = rg_mga.active ? mga_fit_name(rg_mga.plan.fit) : NULL;
}

static void get_stats(rg_stats *s)
{
    s->resets = engine_resets;
    s->timeouts = engine_timeouts;
    s->fence_stuck = engine_fence_stuck;
    s->present_us = present_us;
    s->retrace_us = retrace_us;
}

static void crash_lines(void)
{
#if defined(DGL_DEBUG) || defined(MGA_TRACE_MMIO)
    mga_trace_dump(rg_log_line, 32);
#endif
}

const rg_device rg_mga_device = {
    .name = "mga",
    .probe = probe,
    .caps = caps,
    .modes = list_modes,
    .open = open_screen,
    .window_ok = rg_mga_window_ok,
    .window_modes = rg_mga_window_modes,
    .open_glos = rg_mga_glos_open,
    .close = close_dev,
    .swap = swap,
    .present_front = present_front,
    .draw_buffer = draw_buffer,
    .surface = surface,
    .target = target,
    .clip = clip,
    .fill = fill,
    .copy = copy,
    .copy_from = copy_from,
    .image = image,
    .mono = mono,
    .cursor_load = cursor_load,
    .cursor_colours = cursor_colours,
    .cursor_show = cursor_show,
    .cursor_move = cursor_move,
    .idle = idle,
    .sync = sync,
    .read = read_px,
    .info = get_info,
    .stats = get_stats,
    .crash = crash_lines,
};
