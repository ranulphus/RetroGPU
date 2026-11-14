/* rgdev.c - RGDEV: one of RetroGPU's devices (hal/include/rg/device.h)
 * driven through the device table alone (docs/virge.md V3): chosen as
 * DOS-GL chooses (DGL_DEVICE, or the first that probes), its modes listed,
 * a context opened and cleared, a script of 2D drawing into the hidden
 * buffer, compared pixel for pixel through the device's own read with the
 * software model of the 2D primitives (tests/hal/sw2d.c), shown by a swap
 * and read again from the front, the cursor shown, and closed. The same
 * program on the Matrox device and the ViRGE/DX's is the check that the
 * table fits both cards.
 *
 *   RGDEV [dev=NAME] [bpp=N] [n=OPS]
 *
 * HX-TEST lines: choose, modes, open, clear, draw, swap, cursor, close. */
#include "hx.h"
#include "rg/device.h"
#include "mga/sys.h"
#include "sw2d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

static const rg_device *d;
static uint16_t *ref;               /* the hidden buffer as it should be (16 bits a pixel) */
static sw_surf rs;
static unsigned seed = 3;
static int nops = 200;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

static int ri(int lo, int hi) { return lo + (int)rnd((unsigned)(hi - lo + 1)); }

static void log_line(const char *l) { hx_log("%s", l); }

/* The device's pixels (RGBA8) of a buffer against ref's 16-bit ones, as the
 * device's read expands them; the first difference into why. */
static int differs(int front, int bpp, char *why)
{
    static uint8_t row[W * 4];
    int x, y;
    for (y = 0; y < H; y++) {
        if (d->read(front, 0, y, W, 1, row, W * 4) != 0) {
            sprintf(why, "the read failed");
            return 1;
        }
        for (x = 0; x < W; x++) {
            unsigned c = ref[y * W + x], r, g, b;
            if (bpp == 15) {
                r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                r = (r << 3) | (r >> 2), g = (g << 3) | (g >> 2), b = (b << 3) | (b >> 2);
            } else {
                r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                r = (r << 3) | (r >> 2), g = (g << 2) | (g >> 4), b = (b << 3) | (b >> 2);
            }
            if (row[x * 4] != r || row[x * 4 + 1] != g || row[x * 4 + 2] != b) {
                sprintf(why, "at %d,%d: %02x%02x%02x, want %02x%02x%02x", x, y, row[x * 4], row[x * 4 + 1],
                        row[x * 4 + 2], r, g, b);
                return 1;
            }
        }
    }
    return 0;
}

static uint32_t rpix(void) { return rnd(0x10000); }

static void draw_script(void)
{
    static uint8_t bits[64 * 64 / 8];
    static uint16_t img[96 * 64];
    int i, k;
    for (i = 0; i < nops; i++) {
        int op = ri(0, 9), x = ri(-20, W - 20), y = ri(-20, H - 20), w = ri(1, 120), h = ri(1, 90);
        if (op < 3) {                               /* fill, sometimes inside a clip */
            uint32_t c = rpix();
            if (op == 2) {
                int cx0 = ri(0, W / 2), cy0 = ri(0, H / 2), cx1 = ri(cx0 + 1, W), cy1 = ri(cy0 + 1, H);
                d->clip(cx0, cy0, cx1, cy1);
                rs.cx0 = cx0, rs.cy0 = cy0, rs.cx1 = cx1, rs.cy1 = cy1;
            }
            d->fill(x, y, w, h, c);
            sw_fill(&rs, x, y, w, h, c);
            d->clip(0, 0, W, H);
            rs.cx0 = rs.cy0 = 0, rs.cx1 = W, rs.cy1 = H;
        } else if (op < 6) {                        /* copy within, overlapping or not */
            int sx = ri(0, W - 1), sy = ri(0, H - 1), dx = sx + ri(-40, 40), dy = sy + ri(-40, 40);
            if (sx + w > W) w = W - sx;
            if (sy + h > H) h = H - sy;
            if (dx < 0 || dy < 0 || dx + w > W || dy + h > H)
                continue;
            d->copy(sx, sy, dx, dy, w, h);
            sw_copy(&rs, dx, dy, &rs, sx, sy, w, h);
        } else if (op < 8) {                        /* one-bit rows, opaque or transparent */
            int mw = ri(1, 64), mh = ri(1, 64), stride = (mw + 7) / 8, fl = op == 7 ? RG_MONO_TRANSPARENT : 0;
            uint32_t fg = rpix(), bg = rpix();
            for (k = 0; k < stride * mh; k++)
                bits[k] = (uint8_t)rnd(256);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            d->mono(x, y, mw, mh, bits, stride, fg, bg, fl);
            sw_mono(&rs, x, y, mw, mh, bits, stride, fg, bg, fl);
        } else {                                    /* pixels from the CPU */
            int iw = ri(1, 96), ih = ri(1, 64);
            for (k = 0; k < iw * ih; k++)
                img[k] = (uint16_t)rpix();
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            d->image(x, y, iw, ih, img, iw * 2);
            sw_image(&rs, x, y, iw, ih, img, iw * 2);
        }
    }
}

int main(int argc, char **argv)
{
    const char *want = getenv("DGL_DEVICE");
    rg_mode modes[32];
    rg_open o;
    rg_opened op;
    rg_surface back;
    char why[128] = "";
    int bpp = 16, n, i, bad;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "dev=", 4)) want = argv[i] + 4;
        else if (!strncmp(argv[i], "bpp=", 4)) bpp = atoi(argv[i] + 4);
        else if (!strncmp(argv[i], "n=", 2)) nops = atoi(argv[i] + 2);
    hx_init(1, argv, "rgdev");                      /* (its own arguments only) */
    rg_set_log(log_line);
    d = rg_choose(want);
    hx_test("choose", d != NULL, "%s", d ? d->name : rg_error());
    if (!d)
        hx_done(1);
    n = d->modes(modes, 32);
    for (i = 0; i < n && i < 32; i++)
        hx_log("HX-STAT mode %dx%dx%d bios=%03x pitch=%d double=%d depth=%d", modes[i].width, modes[i].height,
               modes[i].color_bits, modes[i].bios_mode, modes[i].pitch_px, modes[i].can_double_buffer,
               modes[i].max_depth_bits);
    hx_test("modes", n > 0, "%d modes", n);

    memset(&o, 0, sizeof o);
    o.width = W, o.height = H, o.color_bits = bpp, o.depth_bits = 16, o.double_buffer = 1, o.vsync = 1;
    if (d->open(&o, &op) != 0) {
        hx_test("open", 0, "%s", rg_error());
        d->close();
        hx_done(1);
    }
    hx_test("open", 1, "%dx%d pitch=%d double=%d depth=%d memory %lu..%lu of %lu", op.width, op.height, op.pitch_px,
            op.double_buffer, op.depth_bits, (unsigned long)op.mem_start, (unsigned long)op.mem_end,
            (unsigned long)op.vram_bytes);
    ref = calloc((size_t)W * H, 2);
    if (!ref)
        hx_done(1);
    rs.mem = (uint8_t *)ref, rs.pitch_px = W, rs.bpp = 16, rs.cx0 = rs.cy0 = 0, rs.cx1 = W, rs.cy1 = H;

    /* Open leaves both buffers black. */
    bad = d->sync() != 0 || differs(0, bpp, why) || differs(1, bpp, why);
    hx_test("clear", !bad, "%s", bad ? why : "both buffers black");

    /* The script into the hidden buffer, then read back through the device. */
    d->surface(RG_SURF_BACK, &back);
    d->target(&back);
    draw_script();
    bad = d->sync() != 0 || differs(0, bpp, why);
    hx_test("draw", !bad, "%d operations: %s", nops, bad ? why : "the same as the model");

    /* Shown: what was hidden is the front now. */
    d->swap(1);
    bad = d->sync() != 0 || differs(1, bpp, why);
    hx_test("swap", !bad, "%s", bad ? why : "the drawn buffer shown");
    hx_snap_screen("RGDEV");

    /* The cursor, where the device has one: an arrow at 100,100 (its image
     * in the memory left after the buffers, 1 KB aligned). */
    why[0] = 0;
    if (d->cursor_load) {
        static uint8_t and_mask[512], xor_mask[512];
        uint32_t at = (op.mem_start + 1023u) & ~1023u;
        int y, ok;
        memset(and_mask, 0xFF, sizeof and_mask);
        for (y = 0; y < 16; y++) {
            and_mask[y * 8] = (uint8_t)(0xFF >> (y < 8 ? y + 1 : 8));
            and_mask[y * 8 + 1] = (uint8_t)(y < 8 ? 0xFF : 0xFF >> (y - 7));
            xor_mask[y * 8] = (uint8_t)(0xFF << (7 - (y < 8 ? y : 7)));
        }
        static uint8_t seen[16 * 16 * 4];
        uint16_t cw, ch;
        uint32_t t0;
        ok = d->cursor_load(at, and_mask, xor_mask) == 0;
        d->cursor_colours(0x000000, 0xFFFFFF);
        d->cursor_move(100, 100);
        d->cursor_show(1);
        t0 = sys_time_us();
        while (sys_time_us() - t0 < 100000u)
            ;                                   /* (a few frames shown since) */
        if (hx_ut_capture(&cw, &ch) == 0 && hx_ut_read(100, 100, 16, 16, seen) == 0) {
            /* The arrow's tip and its eighth row are colour 1: white. */
            static const int at_xy[][2] = { { 0, 0 }, { 0, 8 }, { 7, 8 }, { 3, 5 } };
            for (y = 0; y < 4; y++) {
                const uint8_t *q = seen + (at_xy[y][1] * 16 + at_xy[y][0]) * 4;
                if (q[0] != 255 || q[1] != 255 || q[2] != 255) {
                    ok = 0;
                    sprintf(why, "at %d,%d: %02x%02x%02x, want white", 100 + at_xy[y][0], 100 + at_xy[y][1], q[2], q[1],
                            q[0]);
                }
            }
        } else
            hx_log("HX-STAT no screen capture: the cursor's colour unchecked");
        hx_snap_screen("RGDEV-CURSOR");
        d->cursor_show(0);
        hx_test("cursor", ok, "loaded at %lu, white at 100,100%s%s", (unsigned long)at, why[0] ? ": " : "", why);
    } else
        hx_log("HX-STAT no hardware cursor");

    d->close();
    hx_test("close", 1, "text mode");
    hx_done(hx_failures() ? 1 : 0);
}
