/* context.c - dglInit, dglShutdown, dglSwapBuffers (PRD §5.5, FR-HAL-3/4):
 * GL's side of a context. The buffers, the display, swaps and the engine's
 * order are the device's (rg/device.h; the Matrox device is hal/src/rg/mga.c,
 * gl/docs/b1.md): this file chooses one (DGL_DEVICE), opens it with the
 * program's configuration, and keeps the test hooks, the statistics and GL's
 * own state. */
#include "dgl.h"
#include "../gl/gl_state.h"
#include "../gl/gl_tex.h"
#include "mga/sys.h"
#include "../backend/backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <sys/nearptr.h>
#endif

dgl_context dgl_ctx;
const rg_device *dgl_dev;                 /* chosen at the first dglInit, kept */
static DGLStats stats;
/* Test hooks (PRD §5.5, M5): DGL_EXIT_AFTER=n ends the program after n
 * swaps; DGL_STATS=1 logs frame and triangle rates once a second. */
static unsigned long exit_after;
static int exit_pending;           /* DGL-START printed, DGL-EXIT owed */
static int stats_on;                /* DGL_STATS: 1 = DGL-STAT, 2 = also DGL-PRIMS */
static uint32_t stats_t0, stats_tris0;
static unsigned long stats_swaps0;
/* DGL_STATS: time the swaps spent waiting for the engine to finish the frame
 * (the drain), in the second being reported; the retrace's and the scaled
 * presents' are the device's (rg_stats), taken as differences. */
static uint32_t drain_us;
static uint32_t retrace0_us, present_base_us;
#ifdef MGA_PROF
static void prof_start(int level);
static void prof_report(uint32_t tris);
#endif
static unsigned long present0_us;       /* stats.present_us at the last DGL-STAT */

/* The first GL errors, with the address inside the GL function that raised
 * them (look it up in the program's link map). */
static void log_gl_error(GLenum e, void *at)
{
    static int n;
    if (n < 16 && ++n)
        DGL_WARN("DGL-GLERR 0x%04x at %p%s", (unsigned)e, at, n == 16 ? " (no more logged)" : "");
}

static void (*wait_hook)(void *);
static void *wait_hook_arg;
static void run_wait_hook(void);

/* The device's hooks: the program's wait hook before a retrace wait, its
 * yield in long CPU work, the texture memory's turn at each completed sync,
 * and GL's state written again when GLOS lent the engine to someone else. */
static void dev_wait(void) { if (wait_hook) run_wait_hook(); }
static void dev_yield(void) { dgl_yield_maybe(0); }
static void dev_synced(void) { dgl_be->synced(); }

/* The back end's texture memory in use. */
static uint32_t be_mem_used(void)
{
    dgl_be_stats bs;
    dgl_be->stats(&bs);
    return bs.mem_used;
}

/* Triangles set up by the CPU so far (the back end's count). */
static uint32_t be_tris(void)
{
    dgl_be_stats bs;
    dgl_be->stats(&bs);
    return bs.tris;
}
static void dev_lost(void) { dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG; }
static void dev_log(const char *line) { DGL_ERR("%s", line); }

/* What a swap or a present disturbed, marked for GL to write again. */
static void disturbed(int d)
{
    if (d & RG_SWAP_ENGINE)
        dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG;
    else if (d & RG_SWAP_TARGET)
        dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER;    /* the target write reset MACCESS */
}

static void stats_present(void)
{
    rg_stats rs;
    dgl_dev->stats(&rs);
    stats.present_us = rs.present_us - present_base_us;
}

int dgl_present_front(void)
{
    int d;
    if (!dgl_ctx.active)
        return 0;
    d = dgl_dev->present_front();
    disturbed(d);
    stats_present();
    return d != 0;
}

static void after_init(const DGLConfig *c);

/* The device's open, from the program's configuration (the defaults filled
 * in) and the test and user overrides. */
static void open_args(rg_open *o, const DGLConfig *c)
{
    const char *e;
    memset(o, 0, sizeof *o);
    o->width = c->width;
    o->height = c->height;
    o->color_bits = c->color_bits;
    o->depth_bits = c->depth_bits;
    o->double_buffer = c->double_buffer;
    o->vsync = c->vsync != 0;
    if ((e = getenv("DGL_VSYNC")) != NULL && *e)        /* a test or user override */
        o->vsync = *e != '0';
    e = getenv("DGL_SCALE_FILTER");
    o->scale_filter = c->scale_filter ? c->scale_filter == 2 : (e && !strcmp(e, "bilinear"));
    /* DGL_TEXHEAP_KB caps the texture heap (tests of eviction, or a smaller
     * card's memory on a bigger one). */
    e = getenv("DGL_TEXHEAP_KB");
    o->heap_kb = e && *e ? (uint32_t)strtoul(e, NULL, 10) : 0;
    o->wait = dev_wait;
    o->yield = dev_yield;
    o->on_sync = dev_synced;
}

/* The context is open: GL's side from what the device gave. */
static int opened(const DGLConfig *c, const rg_opened *op)
{
    rg_stats rs;
    dgl_ctx.width = op->width;
    dgl_ctx.height = op->height;
    dgl_ctx.double_buffer = op->double_buffer;
    dgl_ctx.depth_bits = op->depth_bits;
    dgl_ctx.vsync = op->vsync;
    dgl_ctx.window = op->window;
    dgl_ctx.active = 1;
    dgl_dev->stats(&rs);
    present_base_us = rs.present_us;
    retrace0_us = rs.retrace_us;
    after_init(c);
    dgl_textures_reset(op->mem_start, op->mem_end);
    dgl_emit_install();
    if (op->window) {
        DGL_GPU_QUIET();
        DGL_INFO("DGL-INIT %dx%d pitch=%d double=%d depth=%d heap=%lu..%lu window (GLOS)", c->width, c->height,
                 op->pitch_px, dgl_ctx.double_buffer, dgl_ctx.depth_bits, (unsigned long)op->mem_start,
                 (unsigned long)op->mem_end);
    } else
        DGL_INFO("DGL-INIT %dx%d pitch=%d double=%d depth=%d heap=%lu..%lu display=%dx%d fit=%s", c->width,
                 c->height, op->pitch_px, dgl_ctx.double_buffer, dgl_ctx.depth_bits, (unsigned long)op->mem_start,
                 (unsigned long)op->vram_bytes, op->display_width, op->display_height, op->fit);
    return 0;
}

int dglInit(const DGLConfig *cfg)
{
    static const DGLConfig zero;
    DGLConfig c;
    rg_open o;
    rg_opened op;
    if (dgl_ctx.active) {
        dgl_set_error("dglInit called twice");
        return -1;
    }
    rg_set_log(dev_log);
    rg_lock.on_lost = dev_lost;
    if (!cfg || !memcmp(cfg, &zero, sizeof zero)) {
        memset(&c, 0, sizeof c);        /* PRD §8.1 defaults */
        c.width = 640; c.height = 480; c.color_bits = 16; c.depth_bits = 16;
        c.double_buffer = 1; c.vsync = 1;
    } else {
        c = *cfg;
        if (!c.width || !c.height) { c.width = 640; c.height = 480; }
    }
    /* (16-bit colour only until a device has more: B2's software one.) */
    if (c.color_bits && c.color_bits != 16) {
        dgl_set_error("only 16-bit colour is supported");
        return -1;
    }
    if (dgl_glos_want()) {
        /* Under GLOS with a desktop that can show GL in a window: a window of
         * the context's size, its buffers and texture heap in memory GLOS
         * gives; no mode set, no palette, no display start. */
        const rg_device *wd = rg_window_device();
        int r;
        memset(&dgl_ctx, 0, sizeof dgl_ctx);
        open_args(&o, &c);
        r = wd->open_glos(&o, &op);
        if (r < 0) {
            dgl_set_error("%s", rg_error());
            return -1;
        }
        if (r == 0) {
            dgl_dev = wd;
            dgl_crash_install();
            memset(&stats, 0, sizeof stats);
            return opened(&c, &op);
        }
        /* (1: GLOS won't have it as an app: the screen, as on DOS) */
    }
    if (!dgl_dev && !(dgl_dev = rg_choose(getenv("DGL_DEVICE")))) {
        dgl_set_error("%s", rg_error());
        return -1;
    }
    if (dgl_dev != dgl_be->dev) {               /* (GL draws on the Matrox device only until V4 and B2) */
        dgl_set_error("DOS-GL has no drawing back end for the %s device yet", dgl_dev->name);
        return -1;
    }
    memset(&dgl_ctx, 0, sizeof dgl_ctx);
    memset(&stats, 0, sizeof stats);
    open_args(&o, &c);
    dgl_crash_install();
    if (dgl_dev->open(&o, &op) != 0) {
        char why[160];
        snprintf(why, sizeof why, "%s", rg_error());
        dgl_teardown();
        dgl_set_error("%s", why);
        return -1;
    }
    return opened(&c, &op);
}

/* What every context does once its buffers are clear: the test hooks, the
   statistics and GL's own state. */
static void after_init(const DGLConfig *cp)
{
    const DGLConfig c = *cp;
    {
        const char *e = getenv("DGL_EXIT_AFTER");
        exit_after = e ? strtoul(e, NULL, 10) : 0;
        if (e && *e) {                  /* the harness pairs each DGL-START with a DGL-EXIT; 0 = no limit */
            exit_pending = 1;
            DGL_ERR("DGL-START %dx%d exit_after=%lu", c.width, c.height, exit_after);
        }
        dgl_snap_init();
        dgl_ftrace_init();
        e = getenv("DGL_STATS");
        stats_on = e ? atoi(e) : 0;
        stats_t0 = sys_time_us();
        stats_tris0 = be_tris();
#ifdef MGA_PROF
        prof_start(stats_on);
#endif
    }
    dgl_gl_reset();
    dgl_buffers_reset();                /* buffer objects (buffer.c) go with the old context */
    dgl_gl_error_hook = log_gl_error;
    dgl_gl_set_window(c.width, c.height);
}

void dglShutdown(void)
{
    if (dgl_ctx.active)
        dgl_regcrc("shutdown");
    dgl_ftrace_write();
    dgl_teardown();
}

#ifdef MGA_REGCRC
/* REGCRC=1 builds: the running CRC of every register write and WARP vertex
 * so far (the HAL's register trace), at each swap and at the way out: B1's
 * check that moving code changed no write (docs/b1.md). Declared here so the
 * GL layer includes no Matrox header for it. */
uint32_t mga_trace_crc(void);
uint32_t mga_trace_crc_count(void);
void dgl_regcrc(const char *at)
{
    DGL_ERR("DGL-REGCRC %s frame=%lu crc=%08lx n=%lu", at, stats.swaps, (unsigned long)mga_trace_crc(),
            (unsigned long)mga_trace_crc_count());
}
#endif

/* Every way out (dglShutdown, DGL_EXIT_AFTER, exit, a fault) ends here once. */
void dgl_note_exit(void)
{
    if (!exit_pending)
        return;
    exit_pending = 0;
    dgl_regcrc("exit");
    DGL_ERR("DGL-EXIT frames=%lu", stats.swaps);
}

void dglSetVSync(int enabled)
{
    dgl_ctx.vsync = enabled != 0;
}

/* v1.3: the program's function to run while a swap waits (SDL's bridge:
 * the DOS scheduler's yield, so the audio thread refills its ring). */
#ifdef MGA_PROF
/* Stage timers (make PROF=1). DGL-PROF, once a second with DGL_STATS=2:
 * each stage's cycles in units of 1024 (app is the program's own time),
 * triangles set up, register writes, FIFOSTATUS reads, stage switches and
 * the cycles one switch costs. DGL-MICRO, at start with DGL_STATS=3: what
 * single operations cost (in 86Box the emulated cycles, which are not the
 * hardware's: an MMIO access there costs almost nothing). */
static uint32_t prof_ovh;
static void prof_start(int level)
{
    int i;
    uint64_t t0;
    if (level >= 3 && dgl_be->prof_micro)
        dgl_be->prof_micro();
    t0 = prof_rdtsc();
    for (i = 0; i < 1024; i++) {
        prof_switch(PROF_D_SWAP);
        prof_back(PROF_APP);
    }
    prof_ovh = (uint32_t)((prof_rdtsc() - t0) >> 11);
    prof_reset(PROF_APP);
}

static void prof_report(uint32_t tris)
{
    uint32_t sw = 0;
    int i;
    prof_back(prof_cur);
    for (i = 0; i < PROF_N; i++)
        sw += prof_n[i];
#define K(s) ((unsigned long)(prof_cyc[s] >> 10))
    DGL_ERR("DGL-PROF app=%lu fifo=%lu splane=%lu sinc=%lu strap=%lu xform=%lu valid=%lu clip=%lu proj=%lu setup=%lu "
            "tex=%lu clear=%lu drain=%lu vsync=%lu swap=%lu tris=%lu wr=%lu fiford=%lu sw=%lu ovh=%lu",
            K(PROF_APP), K(PROF_FIFO), K(PROF_SPLANE), K(PROF_SINC), K(PROF_STRAP), K(PROF_D_XFORM),
            K(PROF_D_VALID), K(PROF_D_CLIP), K(PROF_D_PROJ), K(PROF_D_SETUP), K(PROF_D_TEX), K(PROF_D_CLEAR),
            K(PROF_D_DRAIN), K(PROF_D_VSYNC), K(PROF_D_SWAP), (unsigned long)tris, (unsigned long)prof_wr,
            (unsigned long)prof_fifo_rd, (unsigned long)sw, (unsigned long)prof_ovh);
#undef K
    prof_reset(prof_cur);
}
#endif

void dglSetWaitHook(void (*fn)(void *arg), void *arg)
{
    wait_hook = fn;
    wait_hook_arg = arg;
}

static void run_wait_hook(void)
{
    stats.wait_hooks++;
    wait_hook(wait_hook_arg);
}

/* In long work outside the swap (a texture's texels written or converted):
 * the hook (SDL's yield: its audio thread refills its ring), at most every
 * 4 ms. The engine may have changed hands in it under GLOS (the hook runs
 * another thread's code, which may call GLOS): taken back before the caller
 * goes on, with the HAL's target and clip and GL's state written again. */
void dgl_yield_maybe(int engine)
{
    static uint32_t last;
    uint32_t now;
    if (!wait_hook)
        return;
    now = sys_time_us();
    if (now - last < 4000u)
        return;
    run_wait_hook();
    last = sys_time_us();
    if (engine)                               /* (in work with the engine: it is the program's again) */
        DGL_GPU_ENTER();
}

/* dgl_sync, with the hook run about once a millisecond while the engine
 * finishes the frame. The engine's own timeout stays dgl_sync's: this only
 * waits up to 200 ms before handing over to it. */
static void drain_with_hook(void)
{
    PROF_SCOPE(PROF_D_DRAIN);
    DGL_GPU_ENTER();
    if (wait_hook) {
        uint32_t start = sys_time_us(), last = start, now;
        while (!dgl_dev->idle() && (now = sys_time_us()) - start < 200000u)
            if (now - last >= 1000u) {
                run_wait_hook();
                last = sys_time_us();
            }
    }
    dgl_sync();
}

static void swap_buffers(void)
{
    PROF_SCOPE(PROF_D_SWAP);
#ifdef __DJGPP__
    __djgpp_nearptr_enable();           /* the consumer may have turned near pointers off */
#endif
    if (stats_on || dgl_ftrace_on) {
        uint32_t t0 = sys_time_us(), dt;
        drain_with_hook();              /* retired texture blocks go back to the heap */
        dt = sys_time_us() - t0;
        drain_us += dt;
        dgl_ftrace_drain_us += dt;
    } else
        drain_with_hook();
    stats.swaps++;
    dgl_snap_frame(stats.swaps);        /* DGL_SNAP: before the frame is shown */
    stats.frames++;
    stats.triangles = be_tris();
    stats.stub_calls = dgl_stub_calls;
    if (stats_on) {
        uint32_t now = sys_time_us(), dt = now - stats_t0;
        if (dt >= 1000000u) {
            rg_stats rs;
            uint32_t retrace_us;
            dgl_dev->stats(&rs);
            retrace_us = rs.retrace_us - retrace0_us;
            retrace0_us = rs.retrace_us;
            stats.present_us = rs.present_us - present_base_us;
            DGL_ERR("DGL-STAT fps=%.1f tris/s=%.0f swaps=%lu tex_kb=%lu stubs=%lu drain_ms=%lu retrace_ms=%lu "
                    "present_ms=%lu hooks=%lu", (stats.swaps - stats_swaps0) * 1e6 / dt,
                    (be_tris() - stats_tris0) * 1e6 / dt, stats.swaps, (unsigned long)(be_mem_used() >> 10),
                    dgl_stub_calls, (unsigned long)(drain_us / 1000), (unsigned long)(retrace_us / 1000),
                    (unsigned long)((stats.present_us - present0_us) / 1000), stats.wait_hooks);
            drain_us = 0;
            present0_us = stats.present_us;
#ifdef MGA_PROF
            if (stats_on >= 2)
                prof_report(be_tris() - stats_tris0);
#endif
            if (stats_on >= 2) {
                DGL_ERR("DGL-PRIMS begins=%lu skipped=%lu tris=%lu clipped=%lu zero=%lu culled=%lu seqs=%lu",
                        dgl_prims.begins, dgl_prims.skipped, dgl_prims.tris_in, dgl_prims.clipped,
                        dgl_prims.zero_area, dgl_prims.culled, dgl_prims.seqs);
                memset(&dgl_prims, 0, sizeof dgl_prims);
                DGL_ERR("DGL-TEX uploads=%lu kb=%lu sub_fast=%lu sub_sync=%lu sub_move=%lu sub_iload=%lu sub_full=%lu "
                        "renames=%lu evictions=%lu syncs=%lu lut_loads=%lu", dgl_texc.uploads,
                        dgl_texc.upload_bytes >> 10, dgl_texc.sub_fast, dgl_texc.sub_sync, dgl_texc.sub_move,
                        dgl_texc.sub_iload, dgl_texc.sub_full, dgl_texc.renames,
                        dgl_texc.evictions, dgl_texc.syncs, dgl_texc.lut_loads);
                memset(&dgl_texc, 0, sizeof dgl_texc);
            }
            stats_t0 = now;
            stats_tris0 = be_tris();
            stats_swaps0 = stats.swaps;
        }
    }
    if (exit_after && stats.swaps >= exit_after) {
        dglShutdown();
        exit(0);
    }
    disturbed(dgl_dev->swap(dgl_ctx.vsync));
    stats_present();
}

void dglSwapBuffers(void)
{
    uint32_t t0 = 0;
    if (!dgl_ctx.active)
        return;
    if (dgl_ftrace_on) {
        t0 = sys_time_us();
        dgl_ftrace_counts();
    }
    swap_buffers();
    dgl_regcrc("swap");
    if (dgl_ftrace_on)
        dgl_ftrace_frame(t0);
}

void dgl_retarget(void)
{
    if (dgl_ctx.active)
        dgl_dev->draw_buffer(dgl_ctx.draw_front);   /* through the FIFO, so queued draws keep their buffer */
}

const DGLStats *dglGetStats(void)
{
    stats.stub_calls = dgl_stub_calls;
    if (dgl_dev)
        stats_present();
    return &stats;
}
