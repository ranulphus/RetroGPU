/* probe - M1 hardware diagnostic (PRD §13 M1): identity, apertures, VRAM,
 * the filtered mode list, dglInit at 640x480x16, a pattern drawn by direct
 * framebuffer writes into the hidden buffer and shown by a swap, then back
 * to text mode. --crash faults while in graphics mode; the crash path must
 * restore text mode and log the exception. Uses DOS-GL internals for the
 * framebuffer pointer, as a diagnostic may. */
#include "hx.h"
#include "../../src/dgl/dgl.h"
#include <stdio.h>

#if !defined(__WATCOMC__)
static void load_bad_selector(void)
{
    __asm__ volatile("movw $0x1235, %%ax\n\tmovw %%ax, %%es" ::: "eax");
}
#endif

static uint16_t pattern(int x, int y)
{
    static const uint16_t bars[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000 };
    return (x % 64 == 0 || y % 64 == 0) ? 0x8410 : bars[x / 80];
}

int main(int argc, char **argv)
{
    DGLMode modes[32];
    const DGLDeviceInfo *di;
    int n, i, x, y, bad = 0;
    volatile uint16_t *fb;
    hx_init(argc, argv, "probe");
    hx_log("HX-STAT library %s", dglVersion());
    n = dglEnumModes(modes, 32);
    di = dglGetDeviceInfo();
    hx_test("device", di != NULL, "%s", di ? di->chip_name : dglGetErrorString());
    if (!di)
        hx_done(HX_INIT_FAILED);
    hx_log("HX-STAT device id=%04x rev=%02x vram=%lu fb=%08lx mmio=%08lx iload=%08lx fifo=%u emulated=%d",
           di->device_id, di->revision, di->vram_bytes, di->fb_phys, di->mmio_phys, di->iload_phys,
           di->fifo_depth, di->emulated);
    for (i = 0; i < n; i++)
        hx_log("HX-STAT mode %dx%d vbe=%03x pitch=%d double=%d depth=%d", modes[i].width, modes[i].height,
               modes[i].vbe_mode, modes[i].pitch_px, modes[i].can_double_buffer, modes[i].max_depth_bits);
    hx_test("modes", n > 0, "%d usable 16-bit modes", n);
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    hx_test("init", 1, "640x480 pitch=%d heap=%lu", dgl_ctx.pitch_px, (unsigned long)dgl_ctx.heap_off);
    /* Colour bars and a grid, straight into the hidden buffer. */
    fb = (volatile uint16_t *)(mga_fb + (dgl_ctx.front_is_a ? dgl_ctx.back_off : dgl_ctx.front_off));
    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++)
            fb[y * dgl_ctx.pitch_px + x] = pattern(x, y);
    for (y = 1; y < 480; y += 67)
        for (x = 1; x < 640; x += 71)
            bad += fb[y * dgl_ctx.pitch_px + x] != pattern(x, y);
    hx_test("pattern", bad == 0, "readback mismatches=%d", bad);
    dglSwapBuffers();
    hx_snap_screen("probe");
    if (hx_args.crash)
        load_bad_selector();
    dglShutdown();
    hx_done(0);
    return 0;
}
