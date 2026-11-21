/* rpio.c - RPIO: what the Rage 128's CCE takes in PIO mode (docs/rage128.md, R4's speed): cycles a dword (the
 * time-stamp counter) for 200,000 dwords of type-2 no-ops, of type-0 writes to CONSTANT_COLOR_C, and of 1-pixel
 * triangles (the 3D context sent once), each to the idle wait; the FIFO-room reads that found too little room
 * (r128_cce_waits). Then the same writes straight to PM4_FIFO_DATA_EVEN/ODD in pairs with no room check at all,
 * 192 at a time after the engine is idle: the bus's own cost.
 *
 *   RPIO
 *
 * Lines: RPIO-CASE name dwords cycles cycles/dword waits. */
#include "hx.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/sys.h"
#include <string.h>

extern uint32_t r128_cce_waits;

static uint64_t tsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__(".byte 0x0f, 0x31" : "=a"(lo), "=d"(hi));
    return (uint64_t)hi << 32 | lo;
}

static void report(const char *name, uint32_t dwords, uint64_t cyc, uint32_t waits)
{
    hx_log("RPIO-CASE %-10s dwords=%lu cycles=%lu per_dword=%lu.%lu waits=%lu", name, (unsigned long)dwords,
           (unsigned long)cyc, (unsigned long)(cyc / dwords), (unsigned long)(cyc * 10 / dwords % 10),
           (unsigned long)waits);
}

int main(int argc, char **argv)
{
    r128_mode m;
    uint32_t p[64];
    uint64_t t0;
    int i, n;
    (void)argc;
    hx_init(1, argv, "rpio");
    if (r128_find() < 0 || r128_map() < 0 || r128_find_mode(640, 480, 16, &m) < 0 || r128_set_mode(&m) < 0 ||
        r128_cce_init(NULL) != 0) {
        hx_test("start", 0, "no Rage 128, mode or CCE");
        hx_done(1);
    }
    for (i = 0; i < 64; i++)
        p[i] = R128_CCE_PACKET2;
    r128_cce_idle_wait(1000000);
    r128_cce_waits = 0;
    t0 = tsc();
    for (n = 0; n < 200000; n += 64)
        r128_cce_out(p, 64);
    r128_cce_idle_wait(1000000);
    report("nop", 200000, tsc() - t0, r128_cce_waits);

    for (i = 0; i < 64; i += 2)
        p[i] = R128_CCE_PACKET0 | (R128_CONSTANT_COLOR_C >> 2), p[i + 1] = 0x00808080u;
    r128_cce_waits = 0;
    t0 = tsc();
    for (n = 0; n < 200000; n += 64)
        r128_cce_out(p, 64);
    r128_cce_idle_wait(1000000);
    report("reg", 200000, tsc() - t0, r128_cce_waits);

    {
        r128_vertex v[3];
        memset(v, 0, sizeof v);
        r128_3d_init(0, m.pitch_bytes, 16, 0x100000u, 1280, 16);
        r128_3d_format(R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB | R128_VC_FRMT_SPEC_FRGB | R128_VC_FRMT_S_T);
        for (i = 0; i < 3; i++)
            v[i].z = 0.5f, v[i].rhw = 1.0f, v[i].argb = 0xFFFF0000u, v[i].spec = 0xFF000000u;
        v[0].x = 10, v[0].y = 10, v[1].x = 11.5f, v[1].y = 10, v[2].x = 10, v[2].y = 11.5f;
        r128_3d_emit();
        r128_cce_idle_wait(1000000);
        r128_cce_waits = 0;
        t0 = tsc();
        for (n = 0; n < 200000; n += 3 + 3 * 8)          /* (27 dwords a triangle in this format) */
            r128_tri(&v[0], &v[1], &v[2]);
        r128_cce_idle_wait(1000000);
        report("tri", (uint32_t)n, tsc() - t0, r128_cce_waits);
    }
    {
        /* The bus alone: 192 dwords at a time into an empty FIFO, no reads between. */
        uint64_t sum = 0;
        int k;
        for (k = 0; k < 500; k++) {
            r128_cce_idle_wait(1000000);
            t0 = tsc();
            for (i = 0; i < 96; i++) {
                r128_wr(R128_PM4_FIFO_DATA_EVEN, R128_CCE_PACKET2);
                r128_wr(R128_PM4_FIFO_DATA_ODD, R128_CCE_PACKET2);
            }
            sum += tsc() - t0;
        }
        r128_cce_idle_wait(1000000);
        report("bus", 500 * 192, sum, 0);
    }
    {
        /* The burst path through the uncached FIFO page: the same writes in whole lines, with a drain a line. */
        r128_cce_idle_wait(1000000);
        r128_cce_burst_window(r128.mmio + 0x1000);
        for (i = 0; i < 64; i++)
            p[i] = R128_CCE_PACKET2;
        r128_cce_waits = 0;
        t0 = tsc();
        for (n = 0; n < 200000; n += 64)
            r128_cce_out(p, 64);
        r128_cce_idle_wait(1000000);
        report("burst-uc", 200000, tsc() - t0, r128_cce_waits);
        r128_cce_burst_window(NULL);
    }
    {
        /* The same through the second aperture's FIFO page made write-combining by GLOS (2.11). */
        r128_cce_idle_wait(1000000);
        if (r128_cce_burst_wc() != 0)
            hx_log("RPIO-CASE burst-wc not given: GLOS's answer %d", sys_cache_wc_err);
        else {
            r128_cce_waits = 0;
            t0 = tsc();
            for (n = 0; n < 200000; n += 64)
                r128_cce_out(p, 64);
            r128_cce_idle_wait(1000000);
            report("burst-wc", 200000, tsc() - t0, r128_cce_waits);
            {
                r128_vertex v[3];
                memset(v, 0, sizeof v);
                for (i = 0; i < 3; i++)
                    v[i].z = 0.5f, v[i].rhw = 1.0f, v[i].argb = 0xFFFF0000u, v[i].spec = 0xFF000000u;
                v[0].x = 10, v[0].y = 10, v[1].x = 11.5f, v[1].y = 10, v[2].x = 10, v[2].y = 11.5f;
                r128_3d_init(0, m.pitch_bytes, 16, 0x100000u, 1280, 16);
                r128_3d_format(R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB | R128_VC_FRMT_SPEC_FRGB | R128_VC_FRMT_S_T);
                r128_3d_emit();
                r128_cce_idle_wait(1000000);
                r128_cce_waits = 0;
                t0 = tsc();
                for (n = 0; n < 200000; n += 3 + 3 * 8)
                    r128_tri(&v[0], &v[1], &v[2]);
                r128_cce_idle_wait(1000000);
                report("tri-wc", (uint32_t)n, tsc() - t0, r128_cce_waits);
            }
            r128_cce_burst_window(NULL);
        }
    }
    {
        /* Aliases: does the FIFO take dwords at the addresses after EVEN and ODD (a burst window)? A register write
         * packet at each pair (0x1000 + 8k, 0x1004 + 8k) with its own marker, then the register read back. */
        int k;
        for (k = 0; k < 8; k++) {
            uint32_t marker = 0x6B000000u | (uint32_t)k << 8 | 0x55u, got;
            r128_cce_idle_wait(1000000);
            r128_wr(0x1000u + 8u * (uint32_t)k, R128_CCE_PACKET0 | (R128_DP_BRUSH_FRGD_CLR >> 2));
            r128_wr(0x1004u + 8u * (uint32_t)k, marker);
            r128_cce_idle_wait(1000000);
            got = r128_rd(R128_DP_BRUSH_FRGD_CLR);
            hx_log("RPIO-ALIAS even=%04lx odd=%04lx marker=%08lx got=%08lx %s", (unsigned long)(0x1000u + 8u * k),
                   (unsigned long)(0x1004u + 8u * k), (unsigned long)marker, (unsigned long)got,
                   got == marker ? "TAKEN" : "no");
        }
        hx_log("RPIO-ALIAS pm4_stat=%08lx", (unsigned long)r128_rd(R128_PM4_STAT));
    }
    r128_text_mode();
    hx_test("ran", 1, "all cases");
    hx_done(0);
}
