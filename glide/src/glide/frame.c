/* frame.c - per-frame bookkeeping: statistics lines, snapshot markers for
 * the harness, and the test exit (MGAGLIDE exit_after=N). */
#include "glide/mg.h"
#include "trace/trace.h"
#include "mga/sys.h"
#include "mga/setup.h"

mg_stats_t mg_stats;

/* Returns only when there is no 86Box unit tester to end the run. */
static void ut_exit(int code)
{
    static const char magic[] = "86Box";
    int i;
    for (i = 0; i < 5; i++)
        sys_outb(0x80, (uint8_t)magic[i]);
    sys_outb(0x80, 0x80);
    sys_outb(0x80, 0x0E);
    if (sys_inb(0x0E80) == 0xFF)
        return;
    sys_outb(0x0E80, 0x04);
    for (i = 0; i < 100000 && !(sys_inb(0x0E80) & 0x02); i++)
        ;
    sys_outb(0x0E81, (uint8_t)(code & 0x7F));
}

#ifdef MGA_PROF
/* MGL-PROF: each stage's cycles since the last line in units of 1024 (the
 * game's own time, LFB and texture API calls are app; the first line also
 * holds start-up), then register writes, FIFOSTATUS reads, stage switches
 * and the cycles one switch costs, which the stages above include. */
static void prof_line(void)
{
    static uint32_t ovh;
    uint32_t sw = 0;
    int i;
    if (!ovh) {
        uint64_t t0 = prof_rdtsc();
        for (i = 0; i < 1024; i++) {
            prof_switch(PROF_G_SWAP);
            prof_back(PROF_G_SWAP);
        }
        ovh = (uint32_t)((prof_rdtsc() - t0) >> 11);
    }
    prof_back(prof_cur);
    for (i = 0; i < PROF_N; i++)
        sw += prof_n[i];
#define K(s) ((uint32_t)(prof_cyc[s] >> 10))
    mg_line("MGL-PROF app=%u fifo=%u splane=%u sinc=%u strap=%u vtx=%u texbind=%u texdl=%u drain=%u vsync=%u "
            "swap=%u fast=%u vert=%u lod=%u warp=%u wr=%u fiford=%u sw=%u ovh=%u", K(PROF_APP), K(PROF_FIFO),
            K(PROF_SPLANE), K(PROF_SINC), K(PROF_STRAP), K(PROF_G_VTX), K(PROF_G_TEXBIND), K(PROF_G_TEXDL),
            K(PROF_G_DRAIN), K(PROF_G_VSYNC), K(PROF_G_SWAP), K(PROF_G_FAST), K(PROF_G_VERT), K(PROF_G_LOD),
            K(PROF_G_WARP), prof_wr, prof_fifo_rd, sw, ovh);
#undef K
    prof_reset(prof_cur);
}
#endif

void mg_frame_end(void)
{
    int i;
    for (i = 0; i < mg_config.nsnap; i++)
        if ((int)mg.frame == mg_config.snap[i])
            mg_line("MGL-SNAP %u", mg.frame);
    if (mg_config.stats_every && mg.frame % (unsigned)mg_config.stats_every == 0) {
        mg_line("MGL-STAT frame=%u tris=%u bands=%u banded=%u fast=%u fastbands=%u vfallback=%u splits=%u", mg.frame,
                mg_stats.tris, mg_stats.bands, mg_stats.banded, mg_stats.fast, mg_stats.fast_bands,
                setup_stats.vfallback, setup_stats.splits);
        mg_stats.tris = mg_stats.bands = mg_stats.banded = mg_stats.fast = mg_stats.fast_bands = 0;   /* (vfallback, splits: totals: edges too flat, triangles too big) */
#ifdef MGA_PROF
        prof_line();
#endif
    }
    trace_frame(mg.frame);
    if (mg_config.exit_after && (int)mg.frame >= mg_config.exit_after) {
        mg_line("MGL-EXIT frames=%u", mg.frame);
        trace_flush();
        vbe_set_text_mode();
        ut_exit(0);
        /* A real PC (Loop B): end the game here. The exit hook restores the
         * interrupt vectors and the display on the way out. */
        sys_terminate(0);
    }
}
