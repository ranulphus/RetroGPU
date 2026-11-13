/* device.c - the mode list and the device's description (PRD FR-HAL-1/3/8,
 * §5.5): GL's side. Finding the card and its modes, with their rules, are the
 * device's (rg_device's probe and modes; the Matrox one is hal/src/rg/mga.c,
 * gl/docs/b1.md). Nothing is mapped and video memory is not touched, so
 * dglEnumModes can run in text mode before dglInit. */
#include "dgl.h"
#include <stdlib.h>
#include <string.h>

static DGLDeviceInfo info;

void dgl_mode_out(DGLMode *d, const rg_mode *m)
{
    memset(d, 0, sizeof *d);
    d->width = m->width;
    d->height = m->height;
    d->color_bits = m->color_bits;
    d->max_depth_bits = m->max_depth_bits;
    d->can_double_buffer = m->can_double_buffer;
    d->vbe_mode = m->bios_mode;
    d->pitch_px = m->pitch_px;
    d->scaled = m->scaled;
    d->zoomed = m->zoomed;
    d->display_width = m->display_width;
    d->display_height = m->display_height;
}

/* The device: the one dglInit chose, or the first that would be. */
static const rg_device *device(void)
{
    if (!dgl_dev && !(dgl_dev = rg_choose(getenv("DGL_DEVICE"))))
        dgl_set_error("%s", rg_error());
    return dgl_dev;
}

int dglEnumModes(DGLMode *out, int max_modes)
{
    rg_mode m[32];
    int n, i;
    if (dgl_glos_want())                        /* under GLOS: the sizes a window can have */
        return dgl_glos_modes(out, max_modes);
    if (!device() || dgl_dev->probe() != 0)
        return 0;
    n = dgl_dev->modes(m, 32);
    for (i = 0; i < n && i < 32 && i < max_modes && out; i++)
        dgl_mode_out(&out[i], &m[i]);
    return n;
}

const DGLDeviceInfo *dglGetDeviceInfo(void)
{
    rg_info i;
    if (!device() || dgl_dev->probe() != 0)
        return NULL;
    dgl_dev->info(&i);
    info.chip_name = i.chip_name;
    info.device_id = i.device_id;
    info.revision = i.revision;
    info.vram_bytes = i.vram_bytes;
    info.fb_phys = i.fb_phys;
    info.mmio_phys = i.mmio_phys;
    info.iload_phys = i.iload_phys;
    info.fifo_depth = i.fifo_depth;
    info.emulated = i.emulated;
    info.width = dgl_ctx.active ? dgl_ctx.width : 0;
    info.height = dgl_ctx.active ? dgl_ctx.height : 0;
    info.display_width = i.display_width;
    info.display_height = i.display_height;
    info.picture_x = i.picture_x;
    info.picture_y = i.picture_y;
    info.picture_width = i.picture_width;
    info.picture_height = i.picture_height;
    info.fit = i.fit;
    return &info;
}
