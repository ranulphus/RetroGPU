/* rstmem.c - RSTMEM: what a drawing-engine soft reset does to what video memory already holds (docs/g550.md: on
 * bench G's G550 a reset with the G450's memory set-up after it sometimes changed 12% of a screen that the same
 * reset left whole on bench F's G450, 20 times of 20).
 *
 *   RSTMEM [mb=N] [resets=N] [noreinit] [steps=HEX] [wait=MS] [norewrite]
 *
 * Sets 640x480x16, writes the first N megabytes (16; as far as the BIOS says the card has) with a pattern made
 * from each dword's own index, reads it back (the base line), then N times (3): engine_reset (the HAL's: with a
 * G450's or G550's memory set-up after it unless noreinit; steps=: those of its steps, pci.c's
 * mga_g450_reinit_steps, in place of the HAL's own choice for the card), wait MS milliseconds (0), read everything back and
 * say what changed, write the pattern again (unless norewrite: then each count holds the resets before it too).
 * Lines (HX-STAT rstmem): per reset the dwords changed, how many read 0, the OR and AND of the bits that
 * differed, the count in each megabyte, the first runs (byte offset, dwords, what was read and what was written
 * at its start) and the spacing of the runs' starts. Then text mode. Writes the display's memory only. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t pat(uint32_t i) { return (i * 0x9E3779B1u) ^ (i >> 5) ^ 0x5A5AA5A5u; }

static void scan(const char *what, uint32_t dwords)
{
    const volatile uint32_t *fb = (const volatile uint32_t *)mga_fb;
    uint32_t i, bad = 0, zero = 0, orx = 0, andx = ~0u, permb[64], nruns = 0, run_at = 0, run_len = 0, prev_start = 0;
    char line[400];
    int pos = 0, gaps = 0;
    char gline[200];
    int gpos = 0;
    memset(permb, 0, sizeof permb);
    line[0] = gline[0] = 0;
    for (i = 0; i <= dwords; i++) {
        uint32_t got = i < dwords ? fb[i] : 0, want = i < dwords ? pat(i) : 0;
        if (i < dwords && got != want) {
            bad++;
            zero += got == 0;
            orx |= got ^ want;
            andx &= got ^ want;
            permb[(i >> 18) & 63]++;
            if (!run_len) {
                run_at = i;
                if (nruns < 8)
                    pos += sprintf(line + pos, " %lx:", (unsigned long)i * 4ul);
                if (nruns && gaps < 12) {
                    gpos += sprintf(gline + gpos, " %lx", (unsigned long)(i - prev_start) * 4ul);
                    gaps++;
                }
                if (nruns < 8)
                    pos += sprintf(line + pos, "%08lx/%08lx", (unsigned long)got, (unsigned long)want);
                prev_start = i;
            }
            run_len++;
        } else if (run_len) {
            if (nruns < 8)
                pos += sprintf(line + pos, "x%lu", (unsigned long)run_len);
            nruns++;
            run_len = 0;
        }
    }
    (void)run_at;
    hx_log("HX-STAT rstmem %s: changed=%lu of %lu dwords, read0=%lu, runs=%lu, bits or=%08lx and=%08lx", what,
           (unsigned long)bad, (unsigned long)dwords, (unsigned long)zero, (unsigned long)nruns,
           (unsigned long)orx, (unsigned long)(bad ? andx : 0));
    if (bad) {
        /* (what is read at some indices: the tool's reader finds which written dword each is) */
        static const uint32_t at[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 31, 32, 33, 63, 64, 127,
                                       128, 255, 256, 511, 512, 1023, 1024, 2047, 2048, 4095, 4096, 16383, 16384, 21052,
                                       65535, 65536, 262143, 262144, 1048575, 1048576, 2097152, 3000000, 4194303 };
        char sline[700];
        int spos = 0;
        unsigned k2;
        for (k2 = 0; k2 < sizeof at / sizeof at[0]; k2++)
            if (at[k2] < dwords)
                spos += sprintf(sline + spos, " %lu=%08lx", (unsigned long)at[k2], (unsigned long)fb[at[k2]]);
        hx_log("HX-STAT rstmem %s samples:%s", what, sline);
    }
    if (bad) {
        char mline[300];
        int mpos = 0;
        for (i = 0; i < (dwords + 0x3FFFFu) >> 18 && i < 32; i++)
            mpos += sprintf(mline + mpos, " %lu", (unsigned long)permb[i]);
        hx_log("HX-STAT rstmem %s per MB:%s", what, mline);
        hx_log("HX-STAT rstmem %s runs (offset:read/written x dwords):%s", what, line);
        hx_log("HX-STAT rstmem %s run spacing (bytes):%s", what, gline);
    }
}

static void (*g_full)(void);
static uint32_t hal_steps;

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int i, pitch = 0, mb = 16, resets = 3, noreinit = 0, wait_ms = 0, rewrite = 1;
    uint32_t dwords, k, t0, steps = 0, steps_set = 0;
    volatile uint32_t *fb;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "mb=", 3)) mb = atoi(argv[i] + 3);
        else if (!strncmp(argv[i], "resets=", 7)) resets = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "wait=", 5)) wait_ms = atoi(argv[i] + 5);
        else if (!strcmp(argv[i], "noreinit")) noreinit = 1;
        else if (!strncmp(argv[i], "steps=", 6)) steps = (uint32_t)strtoul(argv[i] + 6, NULL, 16), steps_set = 1;
        else if (!strcmp(argv[i], "norewrite")) rewrite = 0;
    hx_init(1, argv, "rstmem");
    if (mga_find(&mga) != 0) {
        hx_test("init", 0, "no Matrox card");
        hx_done(HX_INIT_FAILED);
    }
    if (mb < 1) mb = 1;
    if ((uint32_t)mb > vbe_total_memory() >> 20 && vbe_total_memory()) mb = (int)(vbe_total_memory() >> 20);
    if (mb > 32) mb = 32;
    mga.fb_size = (uint32_t)(mb > 16 ? mb : 16) << 20;
    if (mga_map(&mga) != 0) {
        hx_test("init", 0, "the card didn't map");
        hx_done(HX_INIT_FAILED);
    }
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 640, &pitch) != 0) {
        hx_test("mode", 0, "no 640x480x16 mode");
        hx_done(1);
    }
    engine_init(pitch, 16);
    g_full = mga_memory_reinit;
    hal_steps = mga_g450_reinit_steps;  /* (the HAL's choice for this card) */
    if (g_full)
        g_full();                       /* (its set-up first: whatever an earlier run left) */
    if (!steps_set)
        steps = hal_steps;
    mga_g450_reinit_steps = steps;
    if (noreinit)
        mga_memory_reinit = NULL;
    hx_log("HX-STAT rstmem card %s id=%04x rev=%02x, %d MB, resets=%d reinit=%s steps=%lx wait=%d ms", mga.name,
           mga.device_id, mga.revision, mb, resets, mga_memory_reinit ? "yes" : "no", (unsigned long)steps, wait_ms);
    fb = (volatile uint32_t *)mga_fb;
    dwords = (uint32_t)mb << 18;
    for (k = 0; k < dwords; k++)
        fb[k] = pat(k);
    scan("base", dwords);
    for (i = 1; i <= resets; i++) {
        char what[24];
        engine_reset();
        t0 = sys_time_us();
        while ((uint32_t)(sys_time_us() - t0) < (uint32_t)wait_ms * 1000u)
            ;
        sprintf(what, "reset%d", i);
        scan(what, dwords);
        if (rewrite) {
            for (k = 0; k < dwords; k++)
                fb[k] = pat(k);
            sprintf(what, "rewritten%d", i);
            scan(what, dwords);
        }
    }
    mga_g450_reinit_steps = hal_steps;  /* (the HAL's set-up again before leaving) */
    if (g_full)
        g_full();
    vbe_set_text_mode();
    hx_test("rstmem", 1, "%d resets, reinits %lu", resets, (unsigned long)mga_g450_reinits);
    hx_done(0);
    return 0;
}
