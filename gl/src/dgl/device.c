/* device.c - card discovery and the mode list (PRD FR-HAL-1/3/8, §5.5).
 *
 * Discovery reads PCI configuration space and the VBE information block
 * only: nothing is mapped and VRAM is not touched, so dglEnumModes can run
 * in text mode before dglInit. */
#include "dgl.h"
#include <stdlib.h>
#include <string.h>

#define MAX_MODES 32
#define TEXTURE_RESERVE (512u * 1024u)   /* VRAM kept for textures when judging fit */

static int discovered;                   /* 0 not yet, 1 found, -1 none */
static DGLDeviceInfo info;

/* dglInit found more VRAM than the BIOS reported (a probe in graphics mode). */
void dgl_note_vram(uint32_t bytes)
{
    info.vram_bytes = bytes;
    mga.vram_bytes = bytes;
}
static mga_vbe_mode vbe_all[MAX_MODES];  /* the BIOS's usable 16-bit modes on this card */
static int nvbe;
static DGLMode modes[MAX_MODES];
static mga_mode_plan plans[MAX_MODES];
static int nmodes;
static unsigned plan_flags;               /* DGL_ZOOM=1: zoom; DGL_PRESENT=force: scale even native sizes */

/* Sizes offered beyond the BIOS's own when the planner can show them. */
static const int virtual_sizes[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 512 } };

int dgl_pitch_for(int width)
{
    /* The drawing engine wants a pitch that is a multiple of 32 pixels, at
     * most 4096 (FR-HAL-8). DOS-GL programs it with VBE 4F06h rather than
     * trusting the BIOS's choice. */
    int p = (width + 31) & ~31;
    return p <= 4096 ? p : 0;
}

/* The VRAM a mode needs before the depth buffer and textures: the display
 * buffers (one or two) and, for a scaled mode, the render buffer. */
static uint32_t colour_bytes(const DGLMode *d, int buffers)
{
    uint32_t disp = (uint32_t)dgl_pitch_for(d->display_width) * (uint32_t)d->display_height * 2u;
    if (d->scaled)
        return 2u * disp + (uint32_t)d->pitch_px * (uint32_t)d->height * 2u;
    return (uint32_t)buffers * (uint32_t)d->pitch_px * (uint32_t)d->height * 2u;
}

/* Advisory: whether double buffering and a depth buffer fit, against the
 * best VRAM figure known (the BIOS's, or the probe's once dglInit ran). */
static void judge(DGLMode *d, uint32_t vram)
{
    uint32_t z = (uint32_t)d->pitch_px * (uint32_t)d->height * 2u;
    d->can_double_buffer = colour_bytes(d, 2) + TEXTURE_RESERVE <= vram;
    d->max_depth_bits = colour_bytes(d, d->can_double_buffer ? 2 : 1) + z + TEXTURE_RESERVE <= vram ? 16 : 0;
}

#ifndef DGL_RIG
static void mode_cb(const mga_vbe_mode *m, void *ctx)
{
    (void)ctx;
    /* Linear, direct colour (vbe.c already required both), 16 bpp RGB565. */
    if (m->bpp != 16 || m->red_size != 5 || m->green_size != 6 || m->blue_size != 5 || !m->lfb_phys)
        return;
    if (m->lfb_phys != mga.fb_phys || !dgl_pitch_for(m->width))
        return;                          /* not this card's aperture, or too wide */
    if (nvbe < MAX_MODES)
        vbe_all[nvbe++] = *m;
}
#endif

/* Add w x h as the planner would show it (native, zoomed or scaled). */
static void add_mode(int w, int h)
{
    DGLMode d;
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
    d.vbe_mode = p.disp.mode;
    d.display_width = p.disp.width;
    d.display_height = p.disp.height;
    d.zoomed = p.fit == MGA_FIT_ZOOM;
    d.scaled = p.fit != MGA_FIT_NATIVE && p.fit != MGA_FIT_ZOOM;
    /* Scaled: a render buffer engine_present can sample (a power-of-two
     * pitch); zoomed and native: the display pitch. */
    d.pitch_px = d.scaled ? mga_pow2_pitch(w) : dgl_pitch_for(p.disp.width);
    if (d.scaled && d.pitch_px > 2048)
        return;
    /* Not dropped when it seems not to fit: the BIOS can under-report VRAM
     * (the G200's says 2 MB of 8); dglInit probes and decides. */
    judge(&d, dgl_known_vram());
    plans[nmodes] = p;
    modes[nmodes++] = d;
}

int dgl_discover(void)
{
    int i, j;
    if (dgl_glos.on) {
        /* In a GLOS window: the chip is what GLOS described (glos.c), and the
         * PCI bus is GLOS's, not the program's to probe (the probe would
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
    if (discovered)
        return discovered > 0 ? 0 : -1;
    discovered = -1;
    if (mga_find(&mga) != 0) {
        dgl_set_error("no supported Matrox card found (G400, G450 or G200)");
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
     * the "BIOS modes" are the usual 16-bit sizes, which DOS-GL draws into
     * VRAM without ever showing them (dgl_rig_* in context.c). */
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
            m->pitch_bytes = (uint16_t)(dgl_pitch_for(sizes[i][0]) * 2);
            m->lfb_phys = mga.fb_phys;
            m->red_size = 5; m->red_pos = 11; m->green_size = 6; m->green_pos = 5; m->blue_size = 5;
        }
    }
#else
    info.emulated = mga_detect_emulator();
    info.vram_bytes = vbe_total_memory();
#endif
    if (!info.vram_bytes) {
        dgl_set_error("the card's VBE BIOS did not answer (VBE 2.0 or later needed)");
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
            DGLMode t = modes[j]; mga_mode_plan v = plans[j];
            modes[j] = modes[j - 1]; plans[j] = plans[j - 1];
            modes[j - 1] = t; plans[j - 1] = v;
        }
    if (mga.family == MGA_FAMILY_G100)
        DGL_WARN("DGL-WARN %s is a development-only card: no GL blending (PRD D13)", mga.name);
    DGL_INFO("DGL-DEVICE %s id=%04x rev=%02x vram=%lu modes=%d", mga.name, mga.device_id, mga.revision,
             info.vram_bytes, nmodes);
    discovered = 1;
    return 0;
}

/* The enumerated mode for width x height, and how it is shown. */
const DGLMode *dgl_mode_for(int width, int height, const mga_mode_plan **plan)
{
    int i;
    for (i = 0; i < nmodes; i++)
        if (modes[i].width == width && modes[i].height == height) {
            *plan = &plans[i];
            return &modes[i];
        }
    return NULL;
}

/* The most VRAM known: the probe's (dglInit), DGL_VRAM_KB, or the BIOS's. */
uint32_t dgl_known_vram(void)
{
    const char *e = getenv("DGL_VRAM_KB");
    uint32_t kb = e ? (uint32_t)strtoul(e, NULL, 10) : 0;
    return kb * 1024u > info.vram_bytes ? kb * 1024u : info.vram_bytes;
}

int dglEnumModes(DGLMode *out, int max_modes)
{
    int i;
    if (dgl_glos_want())                        /* under GLOS: the sizes a window can have */
        return dgl_glos_modes(out, max_modes);
    if (dgl_discover() != 0)
        return 0;
    for (i = 0; i < nmodes; i++)
        judge(&modes[i], dgl_known_vram());        /* a probe since discovery may have found more */
    for (i = 0; i < nmodes && i < max_modes && out; i++)
        out[i] = modes[i];
    return nmodes;
}

const DGLDeviceInfo *dglGetDeviceInfo(void)
{
    if (dgl_discover() != 0)
        return NULL;
    info.width = dgl_ctx.active ? dgl_ctx.width : 0;
    info.height = dgl_ctx.active ? dgl_ctx.height : 0;
    info.display_width = dgl_ctx.active ? dgl_ctx.plan.disp.width : 0;
    info.display_height = dgl_ctx.active ? dgl_ctx.plan.disp.height : 0;
    info.picture_x = dgl_ctx.plan.dx;
    info.picture_y = dgl_ctx.plan.dy;
    info.picture_width = dgl_ctx.active ? dgl_ctx.plan.dw : 0;
    info.picture_height = dgl_ctx.active ? dgl_ctx.plan.dh : 0;
    info.fit = dgl_ctx.active ? mga_fit_name(dgl_ctx.plan.fit) : NULL;
    return &info;
}
