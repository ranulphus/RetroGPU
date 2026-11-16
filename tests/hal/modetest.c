/* modetest.c - MODETEST: does the card's display really switch to a 640x480x16 VBE mode? (the G450 on bench F
 * stayed in text mode under DOS-GL, 2026-11-16).
 *
 *   MODETEST [hold=SECONDS]
 *
 * Then (check) the CPU reads its bars back, and the drawing engine fills rectangles that the CPU reads back,
 * at the BIOS's pitch and at 1024 pixels (both mode sets): mismatches counted, the first one logged.
 *
 * Reads the VBE controller and the 640x480x16 mode, the display registers before, sets the mode as DOS-GL does
 * (vbe_set_mode, the HAL's pitch), reads them again, fills the screen by the CPU with eight colour bars inside a
 * white border, holds it (default 6 s) and reads them once more, then text mode. Every line goes to the screen
 * (stdout) and COM1: HX-STAT modetest. Writes only the display's index registers to read them, and the mode. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <dpmi.h>
#include <sys/farptr.h>
#include <sys/nearptr.h>
#endif

static uint8_t idx_rd(uint16_t port, uint8_t i)
{
    sys_outb(port, i);
    return sys_inb((uint16_t)(port + 1));
}

static uint8_t atc_rd(uint8_t i)
{
    uint8_t v;
    (void)sys_inb(0x3DA);               /* (the attribute controller's flip-flop to index) */
    sys_outb(0x3C0, (uint8_t)(i | 0x20));   /* (bit 5 kept on: the display stays enabled) */
    v = sys_inb(0x3C1);
    (void)sys_inb(0x3DA);
    return v;
}

static void regs(const char *when)
{
    uint8_t misc = sys_inb(0x3CC);
    uint16_t crtc = (misc & 1) ? 0x3D4 : 0x3B4;
    hx_log("HX-STAT modetest regs %s misc=%02x atc10=%02x gc06=%02x seq01=%02x seq04=%02x crtc13=%02x crtc17=%02x "
           "crtcext0=%02x ext1=%02x ext2=%02x ext3=%02x ext4=%02x status=%08lx",
           when, misc, atc_rd(0x10), idx_rd(0x3CE, 6), idx_rd(0x3C4, 1), idx_rd(0x3C4, 4), idx_rd(crtc, 0x13),
           idx_rd(crtc, 0x17), idx_rd(0x3DE, 0), idx_rd(0x3DE, 1), idx_rd(0x3DE, 2), idx_rd(0x3DE, 3),
           idx_rd(0x3DE, 4), mga_mmio ? (unsigned long)MGA_RD32(MGAREG_STATUS) : 0ul);
    hx_log("HX-STAT modetest regs %s: %s (atc10 bit 0 = graphics), mgamode (ext3 bit 7) %s", when,
           (atc_rd(0x10) & 1) ? "GRAPHICS" : "TEXT", (idx_rd(0x3DE, 3) & 0x80) ? "on" : "off");
}

/* The mapping, and what single writes read back as: through the near pointer,
 * through a far selector on the same linear address, and through a second
 * mapping of the same physical address. */
static void map_probe(int pitch)
{
#ifdef __DJGPP__
    volatile uint16_t *fb = (volatile uint16_t *)mga_fb;
    uint32_t lin = (uint32_t)(uintptr_t)mga_fb + (uint32_t)__djgpp_base_address;
    __dpmi_meminfo mi;
    int sel = __dpmi_allocate_ldt_descriptors(1), k;
    uint16_t r0, r1, f0 = 0, f1 = 0, s0 = 0;
    hx_log("HX-STAT modetest map fb_phys=%08lx fb_size=%lu KB near=%08lx base=%08lx linear=%08lx mmio_near=%08lx",
           (unsigned long)mga.fb_phys, (unsigned long)(mga.fb_size >> 10), (unsigned long)(uintptr_t)mga_fb,
           (unsigned long)__djgpp_base_address, (unsigned long)lin, (unsigned long)(uintptr_t)mga_mmio);
    for (k = 0; k < 4; k++) {
        uint32_t off = k == 0 ? 0 : k == 1 ? 2 : k == 2 ? 4096 : (uint32_t)pitch * 2u * 100u;
        fb[off / 2] = (uint16_t)(0x1234 + k);
        r0 = fb[off / 2];
        hx_log("HX-STAT modetest map near off=%lu wrote %04x read %04x", (unsigned long)off, 0x1234 + k, r0);
    }
    fb[0] = 0xA5A5;
    fb[1] = 0x5A5A;
    r0 = fb[0];
    r1 = fb[1];
    if (sel >= 0 && __dpmi_set_segment_base_address(sel, lin) == 0 && __dpmi_set_segment_limit(sel, 0xFFFFF) == 0) {
        f0 = _farpeekw(sel, 0);
        f1 = _farpeekw(sel, 2);
        _farpokew(sel, 4, 0x7777);
        hx_log("HX-STAT modetest map far: read %04x %04x (near read %04x %04x); far wrote 7777 at 4, near reads %04x",
               f0, f1, r0, r1, fb[2]);
    }
    mi.address = mga.fb_phys;
    mi.size = 1u << 20;
    if (__dpmi_physical_address_mapping(&mi) == 0) {
        volatile uint16_t *fb2 = (volatile uint16_t *)(mi.address - (uint32_t)__djgpp_base_address);
        s0 = fb2[0];
        fb2[3] = 0x3333;
        hx_log("HX-STAT modetest map second linear=%08lx reads %04x at 0 (near: %04x); wrote 3333 at 6, near reads %04x",
               (unsigned long)mi.address, s0, fb[0], fb[3]);
    } else
        hx_log("HX-STAT modetest map second mapping refused");
#else
    (void)pitch;
#endif
}

/* Count pixels that differ from what was drawn; log the first. */
static void check(const char *what, int pitch, uint16_t (*want)(int x, int y))
{
    volatile uint16_t *fb = (volatile uint16_t *)mga_fb;
    int x, y;
    unsigned long bad = 0, first = 0;
    uint16_t fw = 0, fg = 0;
    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++) {
            uint16_t g = fb[(size_t)y * (size_t)pitch + (size_t)x], w = want(x, y);
            if (g != w) {
                if (!bad) {
                    first = (unsigned long)y * 640u + (unsigned long)x;
                    fw = w;
                    fg = g;
                }
                bad++;
            }
        }
    hx_log("HX-STAT modetest check %s pitch=%d bad=%lu of 307200%s", what, pitch, bad, bad ? "" : " (all right)");
    if (bad)
        hx_log("HX-STAT modetest check %s first at x=%lu y=%lu want=%04x got=%04x", what, first % 640u, first / 640u,
               fw, fg);
}

static const uint16_t bar[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000 };
static uint16_t want_bars(int x, int y) { return (x < 8 || x >= 632 || y < 8 || y >= 472) ? 0xFFFF : bar[x / 80]; }
static uint16_t want_fill(int x, int y) { return y < 240 ? (x < 320 ? 0xF800 : 0x07E0) : (x < 320 ? 0x001F : 0xFFE0); }

static void engine_quads(int pitch)
{
    mga_target tg;
    engine_init(pitch, 16);
    memset(&tg, 0, sizeof tg);
    tg.color_off = 0; tg.pitch_px = pitch; tg.bpp = 16;
    engine_set_target(&tg);
    engine_set_clip(0, 0, 640, 480);
    engine_fill(0, 0, 320, 240, 0xF800);
    engine_fill(320, 0, 320, 240, 0x07E0);
    engine_fill(0, 240, 320, 240, 0x001F);
    engine_fill(320, 240, 320, 240, 0xFFE0);
    engine_sync(500000);
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int i, pitch = 0, hold = 6, x, y, decay = 0, resets = 0, noreinit = 0;
    uint32_t t0;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "hold=", 5))
            hold = atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "decay=", 6))
            decay = atoi(argv[i] + 6);      /* seconds of once-a-second readbacks of the bars */
        else if (!strncmp(argv[i], "resets=", 7))
            resets = atoi(argv[i] + 7);     /* engine resets with the bars on screen, each then counted */
        else if (!strcmp(argv[i], "noreinit"))
            noreinit = 1;                   /* those resets without a G450's memory set-up (the bug, on purpose) */
    hx_init(1, argv, "modetest");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        hx_test("init", 0, "no Matrox card found or mapped");
        hx_done(HX_INIT_FAILED);
    }
    hx_log("HX-STAT modetest card %s device=%04x rev=%02x bus=%d:%d.%d vram=%lu KB fb=%08lx mmio=%08lx",
           mga.name ? mga.name : "?", mga.device_id, mga.revision, mga.bus, mga.dev, mga.fn,
           (unsigned long)(mga.vram_bytes >> 10), (unsigned long)mga.fb_phys, (unsigned long)mga.mmio_phys);
    hx_log("HX-STAT modetest vbe version=%x total_memory=%lu KB", vbe_version(),
           (unsigned long)(vbe_total_memory() >> 10));
    if (vbe_find_mode(640, 480, 16, &m) != 0) {
        hx_test("mode", 0, "the BIOS lists no 640x480x16 mode with a linear framebuffer");
        hx_done(1);
    }
    hx_log("HX-STAT modetest mode %03x %dx%dx%d pitch=%d lfb=%08lx rgb=%d@%d %d@%d %d@%d", m.mode, m.width, m.height,
           m.bpp, m.pitch_bytes, (unsigned long)m.lfb_phys, m.red_size, m.red_pos, m.green_size, m.green_pos,
           m.blue_size, m.blue_pos);
    regs("before");
    if (vbe_set_mode(&m, 640, &pitch) != 0) {
        regs("after-failed-set");
        hx_test("set", 0, "vbe_set_mode failed");
        hx_done(1);
    }
    regs("after-set");
    map_probe(pitch);
    {
        volatile uint16_t *fb = (volatile uint16_t *)mga_fb;
        static const uint16_t bar[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000 };
        for (y = 0; y < 480; y++)
            for (x = 0; x < 640; x++)
                fb[(size_t)y * (size_t)pitch + (size_t)x] =
                    (x < 8 || x >= 632 || y < 8 || y >= 472) ? 0xFFFF : bar[x / 80];
    }
    hx_log("HX-STAT modetest drawn: eight vertical colour bars (white, yellow, cyan, green, magenta, red, blue, black) "
           "inside a white border; pitch %d pixels; holding %d s", pitch, hold);
    if (resets > 0) {
        /* engine_reset (with a G450's memory set-up after it) under the bars:
         * how many pixels each one changes. */
        volatile uint16_t *fb = (volatile uint16_t *)mga_fb;
        int k;
        engine_init(pitch, 16);
        if (noreinit)
            mga_memory_reinit = NULL;
        for (k = 1; k <= resets; k++) {
            unsigned long bad = 0;
            engine_reset();
            for (y = 0; y < 480; y++)
                for (x = 0; x < 640; x++)
                    bad += fb[(size_t)y * (size_t)pitch + (size_t)x] != want_bars(x, y);
            hx_log("HX-STAT modetest reset %d: changed=%lu of 307200 (g450 reinits %lu)", k, bad,
                   (unsigned long)mga_g450_reinits);
        }
    }
    if (decay > 0) {
        /* The bars counted every second: pixels no longer as drawn. */
        volatile uint16_t *fb = (volatile uint16_t *)mga_fb;
        int sec;
        for (sec = 0; sec <= decay; sec++) {
            unsigned long bad = 0;
            if (sec) {
                t0 = sys_time_us();
                while ((uint32_t)(sys_time_us() - t0) < 1000000u)
                    ;
            }
            for (y = 0; y < 480; y++)
                for (x = 0; x < 640; x++)
                    bad += fb[(size_t)y * (size_t)pitch + (size_t)x] != want_bars(x, y);
            hx_log("HX-STAT modetest decay t=%ds changed=%lu of 307200", sec, bad);
        }
    }
    t0 = sys_time_us();
    while ((uint32_t)(sys_time_us() - t0) < (uint32_t)hold * 1000000u)
        ;
    regs("after-hold");
    check("cpu-bars", pitch, want_bars);
    engine_quads(pitch);
    check("engine-fill", pitch, want_fill);
    {
        int p2 = 0;
        if (vbe_set_mode(&m, 1024, &p2) == 0) {
            engine_quads(p2);
            check("engine-fill-pitch1024", p2, want_fill);
            t0 = sys_time_us();
            while ((uint32_t)(sys_time_us() - t0) < 2000000u)
                ;
        } else
            hx_log("HX-STAT modetest pitch 1024: vbe_set_mode refused");
    }
    vbe_set_text_mode();
    regs("text-again");
    hx_test("modetest", 1, "done: tell the DOS-GL session what the screen showed during the hold");
    hx_done(0);
}
