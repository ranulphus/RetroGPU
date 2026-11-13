/* backend.c - GL's Matrox drawing back end: its table (backend.h; docs/b1.md),
 * on the Matrox device (hal/src/rg/mga.c). */
#include "mga_be.h"
#include "mga/setup.h"
#include "mga/warp.h"
#include <string.h>

const dgl_backend *dgl_be = &dgl_mga_backend;

/* What glGet and the extensions report: the chip's record (zeros before a
 * card is found, as GL reports before dglInit). */
static void caps(dgl_caps *c)
{
    memset(c, 0, sizeof *c);
    c->renderer = mga.name ? mga.name : "Matrox";
    c->texture_units = mga.has_dual_tex ? 2 : 1;
    c->max_texture_size = mga.max_tex_size ? mga.max_tex_size : 1024;
}

static void stats(dgl_be_stats *s)
{
    s->tris = setup_stats.tris;
    s->tris_offloaded = warp_tris;
    s->mem_used = dgl_vram_used();
}

#ifdef MGA_PROF
/* DGL-MICRO, at start with DGL_STATS=3 in PROF=1 builds: what single
 * operations cost (in 86Box the emulated cycles, which are not the
 * hardware's: an MMIO access there costs almost nothing). */
#include "mga/fp.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include <math.h>
static volatile double micro_in = 123.456;
static volatile int32_t micro_out;
static __attribute__((noinline)) int32_t micro_cast(double v) { return (int32_t)v; }
static __attribute__((noinline)) int32_t micro_floor(double v) { return mga_ifloor(v); }
static __attribute__((noinline)) int32_t micro_lrint(double v) { return (int32_t)lrint(v); }
static __attribute__((noinline)) double micro_div(double v) { return 1.0 / v; }

static void prof_micro(void)
{
    uint64_t t0, wr = 0, rd, cast, flo, lr, dv;
    int i, r;
    for (r = 0; r < 50; r++) {
        engine_sync(500000);
        t0 = prof_rdtsc();
        for (i = 0; i < 16; i++)
            MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
        wr += prof_rdtsc() - t0;
    }
    engine_sync(500000);
#define LOOP(var, expr) t0 = prof_rdtsc(); for (i = 0; i < 1000; i++) expr; var = (prof_rdtsc() - t0) / 1000
    LOOP(rd, micro_out = MGA_RD8(MGAREG_FIFOSTATUS));
    LOOP(cast, micro_out = micro_cast(micro_in));
    LOOP(flo, micro_out = micro_floor(micro_in));
    LOOP(lr, micro_out = micro_lrint(micro_in));
    LOOP(dv, micro_out = (int32_t)micro_div(micro_in));
#undef LOOP
    DGL_ERR("DGL-MICRO write=%lu read=%lu cast=%lu ifloor=%lu lrint=%lu div=%lu (cycles each, calls included)",
            (unsigned long)(wr / 800), (unsigned long)rd, (unsigned long)cast, (unsigned long)flo,
            (unsigned long)lr, (unsigned long)dv);
}

#endif

const dgl_backend dgl_mga_backend = {
    .name = "mga",
    .dev = &rg_mga_device,
    .caps = caps,
    .install = dgl_emit_install,
    .clear = dgl_mga_clear,
    .synced = dgl_mga_synced,
    .tex_sub = dgl_mga_tex_sub,
    .tex_free = dgl_mga_tex_free,
    .tex_info = dgl_mga_tex_info,
    .tex_reset = dgl_mga_tex_reset,
    .stats = stats,
#ifdef MGA_PROF
    .prof_micro = prof_micro,
#endif
};
