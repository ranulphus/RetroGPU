/* dmem.c - DMEM: does the Radeon's video memory hold what is written? (RD1: DTWOD's surfaces lost bits at the same
 * few places, whatever was drawn; the G450 and the G550 lost theirs after an engine reset.)
 *
 *   DMEM [mb=N] [at=MB] [secs=N] [reset] [rounds=N]
 *   DMEM xcopy                    the pattern by the CPU into a bad megabyte (4) and a clean one (20), each copied
 *                                 to the other kind by the 2D engine, all four read back: whether the memory holds
 *                                 wrong data or the CPU's reads there are wrong
 *   DMEM map [const=HEX]          every megabyte of the card written and read back once: the dwords wrong in each
 *                                 (the pattern, or one value everywhere), and which bits were ever wrong
 *
 * Sets 640x480x16 by the BIOS alone (no engine set-up, no reset), writes N megabytes (8) from megabyte at (2) with
 * a pattern made from each dword's index, and reads them back at once and after 1, 2, 4 ... seconds up to secs (8):
 * the dwords that differ, how many of those lost bits only, the first few. With reset: then radeon_engine_reset,
 * the pattern again, the same readings; rounds (1) times. Text mode at the end. Writes the display's memory only. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include "radeon/radeon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int use_const;
static uint32_t const_v;
static uint32_t pat(uint32_t i) { return use_const ? const_v : (i * 0x9E3779B1u) ^ (i >> 5) ^ 0x5A5AA5A5u; }

static volatile uint32_t *base;
static uint32_t dwords;

static void fill(void)
{
    uint32_t k;
    for (k = 0; k < dwords; k++)
        base[k] = pat(k);
}

static void scan(const char *what)
{
    uint32_t i, bad = 0, lost = 0, gained = 0;
    char line[300];
    int pos = 0, shown = 0;
    line[0] = 0;
    for (i = 0; i < dwords; i++) {
        uint32_t got = base[i], want = pat(i);
        if (got == want)
            continue;
        bad++;
        if (!(got & ~want)) lost++;
        else if (!(want & ~got)) gained++;
        if (shown < 6) {
            pos += sprintf(line + pos, " %lx:%08lx/%08lx", (unsigned long)i * 4ul, (unsigned long)got, (unsigned long)want);
            shown++;
        }
    }
    hx_log("HX-STAT dmem %s: changed=%lu of %lu dwords (bits lost only %lu, gained only %lu)%s%s", what,
           (unsigned long)bad, (unsigned long)dwords, (unsigned long)lost, (unsigned long)gained, bad ? " first" : "",
           line);
}

static void watch(const char *what, int secs)
{
    char n[40];
    int t = 0, step = 1;
    sprintf(n, "%s t=0s", what);
    scan(n);
    while (t < secs) {
        uint32_t t0 = sys_time_us();
        while ((uint32_t)(sys_time_us() - t0) < (uint32_t)step * 1000000u)
            ;
        t += step;
        sprintf(n, "%s t=%ds", what, t);
        scan(n);
        step = t;
    }
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int i, pitch = 0, mb = 8, at = 2, secs = 8, reset = 0, rounds = 1, map = 0, xcopy = 0;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "mb=", 3)) mb = atoi(argv[i] + 3);
        else if (!strncmp(argv[i], "at=", 3)) at = atoi(argv[i] + 3);
        else if (!strncmp(argv[i], "secs=", 5)) secs = atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "rounds=", 7)) rounds = atoi(argv[i] + 7);
        else if (!strcmp(argv[i], "reset")) reset = 1;
        else if (!strcmp(argv[i], "map")) map = 1;
        else if (!strcmp(argv[i], "xcopy")) xcopy = 1;
        else if (!strncmp(argv[i], "const=", 6)) use_const = 1, const_v = (uint32_t)strtoul(argv[i] + 6, NULL, 16);
    hx_init(1, argv, "dmem");
    if (radeon_find() < 0 || radeon_map() < 0) {
        hx_test("map", 0, "no Radeon mapped");
        hx_done(HX_INIT_FAILED);
    }
    if ((uint32_t)(at + mb) << 20 > radeon.vram_bytes) {
        hx_test("args", 0, "past the card's %lu MB", (unsigned long)(radeon.vram_bytes >> 20));
        hx_done(1);
    }
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 640, &pitch) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    hx_log("HX-STAT dmem card %04x, %lu MB; %d MB from %d MB, %d s%s", radeon.device_id,
           (unsigned long)(radeon.vram_bytes >> 20), mb, at, secs, reset ? ", then with an engine reset" : "");
    if (xcopy) {
        /* Surfaces of 512 x 512 at 16 bits (512 KB, pitch 1024 bytes): A at 4 MB and A2 at 6 MB (bad megabytes), B
         * at 20 MB and C at 24 MB (clean ones). */
        static const uint32_t A = 4u << 20, A2 = 6u << 20, B = 20u << 20, C = 24u << 20;
        radeon_mode rm;
        if (radeon_find_mode(640, 480, 16, &rm) < 0 || radeon_set_mode(&rm) < 0) {
            hx_test("mode", 0, "no mode through the layer");
            hx_done(1);
        }
        dwords = 512u * 512u * 2u / 4u;
        base = (volatile uint32_t *)(radeon.fb + A);
        fill();
        scan("A: the CPU wrote a bad megabyte, the CPU reads it");
        {
            /* (the first wrong dwords with their indices, for the reader to find where their bits came from) */
            uint32_t k, shown = 0;
            char line[900];
            int pos = 0;
            for (k = 0; k < dwords && shown < 60; k++)
                if (base[k] != pat(k)) {
                    pos += sprintf(line + pos, " %lx=%08lx", (unsigned long)k, (unsigned long)base[k]);
                    if (++shown % 12 == 0) {
                        hx_log("HX-STAT dmem dump%s", line);
                        pos = 0;
                    }
                }
        }
        radeon_set_target(B, 1024, 16);
        radeon_set_clip(0, 0, 512, 512);
        radeon_copy_from(A, 1024, 0, 0, 0, 0, 512, 512);
        radeon_sync(2000000);
        base = (volatile uint32_t *)(radeon.fb + B);
        scan("B: the engine copied A to a clean megabyte, the CPU reads that");
        fill();
        scan("B: the CPU wrote the clean megabyte, the CPU reads it");
        radeon_set_target(A2, 1024, 16);
        radeon_copy_from(B, 1024, 0, 0, 0, 0, 512, 512);
        radeon_sync(2000000);
        base = (volatile uint32_t *)(radeon.fb + A2);
        scan("A2: the engine copied B to a bad megabyte, the CPU reads that");
        radeon_set_target(C, 1024, 16);
        radeon_copy_from(A2, 1024, 0, 0, 0, 0, 512, 512);
        radeon_sync(2000000);
        base = (volatile uint32_t *)(radeon.fb + C);
        scan("C: the engine copied A2 to a clean megabyte, the CPU reads that");
        radeon_text_mode();
        radeon_unmap();
        hx_test("dmem", 1, "xcopy done");
        hx_done(0);
    }
    if (map) {
        /* Each megabyte: the pattern in, the dwords that come back different; the bits ever wrong, by the dword's
         * place in its 8 bytes (even: the first four, odd: the last four). */
        uint32_t m2, k, even = 0, odd = 0, total = 0, stray = 0;
        char line[700];
        int pos = 0;
        for (m2 = 0; m2 < radeon.vram_bytes >> 20; m2++) {
            volatile uint32_t *b = (volatile uint32_t *)(radeon.fb + (m2 << 20));
            uint32_t bad = 0;
            for (k = 0; k < (1u << 18); k++)
                b[k] = pat((m2 << 18) + k);
            for (k = 0; k < (1u << 18); k++) {
                uint32_t got = b[k], want = pat((m2 << 18) + k);
                if (got != want) {
                    bad++;
                    if (k & 1) odd |= got ^ want; else even |= got ^ want;
                }
            }
            total += bad;
            if (bad * 100u >> 18 == 0)
                stray += bad;           /* (in megabytes that show as 0%) */
            pos += sprintf(line + pos, " %lu", (unsigned long)(bad * 100u >> 18));
        }
        hx_log("HX-STAT dmem map%s: percent of dwords wrong in each MB:%s", use_const ? " (one value)" : "", line);
        hx_log("HX-STAT dmem map: %lu dwords wrong (%lu of them in the megabytes shown as 0); bits ever wrong: even "
               "dwords %08lx, odd dwords %08lx", (unsigned long)total, (unsigned long)stray, (unsigned long)even,
               (unsigned long)odd);
        vbe_set_text_mode();
        radeon_unmap();
        hx_test("dmem", 1, "mapped");
        hx_done(0);
    }
    base = (volatile uint32_t *)(radeon.fb + ((uint32_t)at << 20));
    dwords = (uint32_t)mb << 18;
    fill();
    watch("as-found", secs);
    for (i = 1; reset && i <= rounds; i++) {
        char n[24];
        radeon_engine_reset();
        fill();
        sprintf(n, "after-reset%d", i);
        watch(n, secs);
    }
    vbe_set_text_mode();
    radeon_unmap();
    hx_test("dmem", 1, "done");
    hx_done(0);
    return 0;
}
