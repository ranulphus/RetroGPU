/* dwc.c - DWC: what the CPU's writes to the Radeon's frame buffer cost, for GLOS's write-combining setting
 * (docs/radeon.md): run once as GLOS has the range (uncached) and once after `MTRR LFB ON`.
 *
 *   DWC [base=MB]
 *
 * In megabytes 16 and 17 of the card (base=): 2 MB filled with dwords, then with 16-bit words (a texture's or the
 * LFB's stores), a 640 x 480 16-bit frame copied in from memory row by row (an LFB blit), and 2 MB read back.
 * Then what must still hold with combining on: the pattern read back whole after radeon_vram_written, and a
 * CPU-written block copied by the engine right after it (the engine must see the CPU's last bytes).
 * Lines: DWC-TIME (megabytes a second for each; the frame's milliseconds); HX-TEST readback, engine-sees. */
#include "hx.h"
#include "radeon/radeon.h"
#include "mga/sys.h"
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

static uint32_t pat(uint32_t i) { return (i * 0x9E3779B1u) ^ (i >> 7) ^ 0xC3A55A3Cu; }

int main(int argc, char **argv)
{
    radeon_mode m;
    uint32_t base = 16u << 20, n = 2u << 20, i, t0, us_d, us_w, us_r, us_f, bad = 0, sum = 0;
    volatile uint32_t *d;
    volatile uint16_t *w16;
    uint16_t *frame;
    int k, y;
    for (k = 1; k < argc; k++)
        if (!strncmp(argv[k], "base=", 5)) base = (uint32_t)atoi(argv[k] + 5) << 20;
    hx_init(1, argv, "dwc");
    if (radeon_find() != 0 || radeon_map() != 0 || radeon_find_mode(W, H, 16, &m) != 0 || radeon_set_mode(&m) != 0) {
        hx_test("init", 0, "no Radeon or no 640x480x16 mode");
        hx_done(1);
    }
    frame = malloc(W * H * 2);
    if (!frame)
        hx_done(1);
    for (i = 0; i < W * H; i++)
        frame[i] = (uint16_t)pat(i);
    radeon_set_start(base);
    d = (volatile uint32_t *)(radeon.fb + base);
    w16 = (volatile uint16_t *)(radeon.fb + base);
    radeon_sync(1000000);
    /* (each three times, the best: a timer interrupt or a job of GLOS's in one of them) */
    us_d = us_w = us_r = us_f = 0xFFFFFFFFu;
    for (k = 0; k < 3; k++) {
        t0 = sys_time_us();
        for (i = 0; i < n / 4; i++)
            d[i] = pat(i);
        t0 = sys_time_us() - t0;
        if (t0 < us_d) us_d = t0;
        t0 = sys_time_us();
        for (i = 0; i < n / 2; i++)
            w16[i] = (uint16_t)i;
        t0 = sys_time_us() - t0;
        if (t0 < us_w) us_w = t0;
        t0 = sys_time_us();
        for (y = 0; y < H; y++)
            memcpy((void *)(radeon.fb + base + (uint32_t)y * m.pitch_bytes), frame + y * W, W * 2);
        t0 = sys_time_us() - t0;
        if (t0 < us_f) us_f = t0;
        t0 = sys_time_us();
        for (i = 0; i < n / 4; i++)
            sum += d[i];
        t0 = sys_time_us() - t0;
        if (t0 < us_r) us_r = t0;
    }
    hx_log("DWC-TIME dword fill %lu MB/s, 16-bit stores %lu MB/s, read %lu MB/s (2 MB each); a 640x480x16 frame copied in "
           "%lu.%lu ms (sum %08lx)", (unsigned long)(n / (us_d ? us_d : 1)), (unsigned long)(n / (us_w ? us_w : 1)),
           (unsigned long)(n / (us_r ? us_r : 1)), (unsigned long)(us_f / 1000u), (unsigned long)(us_f / 100u % 10u),
           (unsigned long)sum);
    /* The pattern whole after the flush. */
    for (i = 0; i < n / 4; i++)
        d[i] = pat(i);
    radeon_vram_written(base + n - 4);
    for (i = 0; i < n / 4; i++)
        bad += d[i] != pat(i);
    hx_test("readback", bad <= 8, "%lu of %lu dwords differ after the flush (the P3's card: a few lost bytes at most)",
            (unsigned long)bad, (unsigned long)(n / 4));
    /* A block written by the CPU, then copied by the engine at once: the copy must hold the CPU's bytes. */
    {
        uint32_t off_s = base, off_d = base + (1u << 20), wrong = 0;
        int trial;
        for (trial = 0; trial < 20; trial++) {
            volatile uint16_t *s = (volatile uint16_t *)(radeon.fb + off_s), *q = (volatile uint16_t *)(radeon.fb + off_d);
            for (i = 0; i < 64u * 64u; i++)
                s[(i / 64u) * (uint32_t)(m.pitch_bytes / 2) + (i % 64u)] = (uint16_t)(pat(i + (uint32_t)trial * 7919u));
            radeon_vram_written(off_s + 63u * (uint32_t)m.pitch_bytes + 126u);
            radeon_set_target(off_d, m.pitch_bytes, 16);
            radeon_set_clip(0, 0, W, H);
            radeon_copy_from(off_s, m.pitch_bytes, 0, 0, 0, 0, 64, 64);
            radeon_sync(1000000);
            for (i = 0; i < 64u * 64u; i++)
                wrong += q[(i / 64u) * (uint32_t)(m.pitch_bytes / 2) + (i % 64u)] != (uint16_t)(pat(i + (uint32_t)trial * 7919u));
        }
        hx_test("engine-sees", wrong <= 8, "%lu of %lu pixels of 20 engine copies differ from what the CPU had just written",
                (unsigned long)wrong, 20ul * 64 * 64);
    }
    radeon_text_mode();
    radeon_unmap();
    hx_done(hx_failures() ? 1 : 0);
}
