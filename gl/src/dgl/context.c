/* context.c - dglInit, dglShutdown, dglSwapBuffers (PRD §5.5, FR-HAL-3/4):
 * GL's side of a context. The buffers, the display, swaps and the engine's
 * order are the device's (rg/device.h; the Matrox device is hal/src/rg/mga.c,
 * gl/docs/b1.md): this file chooses one (DGL_DEVICE), opens it with the
 * program's configuration, and keeps the test hooks, the statistics and GL's
 * own state. */
#include "dgl.h"
#include "../gl/gl_state.h"
#include "../gl/gl_tex.h"
#include "../gl/gl_draw.h"
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
unsigned long dgl_frame_no;
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
static uint32_t lag0_us, lag0_n;        /* the device's present_lag figures at the last DGL-STAT */
static int present_lag;                 /* dglSetPresentLag */
static unsigned glos_hints;             /* dglGlosHints */

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

/* Triangles the card transformed so far (DGL-STAT's tnl/s). */
static uint32_t be_tris_tnl(void)
{
    dgl_be_stats bs;
    dgl_be->stats(&bs);
    return bs.tris_tnl;
}
static uint32_t stats_tnl0;

/* Triangles set up by the CPU so far (the back end's count). */
static uint32_t be_tris(void)
{
    dgl_be_stats bs;
    dgl_be->stats(&bs);
    return bs.tris;
}

#ifdef MGA_PROF
/* ... and by the card's own setup too (the G400's WARP): the profile's
 * triangles. */
static uint32_t stats_all0;
static uint32_t be_tris_all(void)
{
    dgl_be_stats bs;
    dgl_be->stats(&bs);
    return bs.tris + bs.tris_offloaded;
}
#endif
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

/* GL's drawing back end for a device: the Matrox one on the Matrox device,
 * the software one on buffers in memory (docs/b2.md). */
static const dgl_backend *backend_for(const rg_device *d);

/* The full-screen device: DGL_DEVICE's, or the first card found that DOS-GL
   draws on; with none (or one without a back end of its own yet: the
   ViRGE/DX's waits for V4), the software renderer through the VBE BIOS (B3).
   NULL with rg_error() set. dglInit's, and dglEnumModes' before it. */
const rg_device *dgl_pick_device(void)
{
    const char *want = getenv("DGL_DEVICE");
    const rg_device *d = rg_choose(want), *v;
    if (want && *want)
        return d;
    if (!d || !backend_for(d))
        if ((v = rg_choose("vbe")) != NULL)
            d = v;
    return d;
}

static const dgl_backend *backend_for(const rg_device *d)
{
    if (d == dgl_mga_backend.dev)
        return &dgl_mga_backend;
    if (d == dgl_soft_backend.dev || d == &rg_vbe_device)  /* (the software renderer: in memory, a GLOS window, VBE) */
        return &dgl_soft_backend;
    if (d == dgl_tnt2_backend.dev)              /* (the Riva TNT2's: RetroGPU's docs/tnt2.md, T4; an NV1x's own: Celsius) */
        return dgl_nv10_wanted() ? &dgl_nv10_backend : &dgl_tnt2_backend;
    if (d == dgl_r128_backend.dev)              /* (the ATI Rage 128's: RetroGPU's docs/rage128.md, R4) */
        return &dgl_r128_backend;
    if (d == dgl_radeon_backend.dev)            /* (the ATI Radeon's, R100 family: RetroGPU's docs/radeon.md, RD5) */
        return &dgl_radeon_backend;
    return NULL;
}

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
    o->stencil_bits = c->stencil_bits;
    o->double_buffer = c->double_buffer;
    o->vsync = c->vsync != 0;
    if ((e = getenv("DGL_VSYNC")) != NULL && *e)        /* a test or user override */
        o->vsync = *e != '0';
    if ((e = getenv("DGL_COLOR_BITS")) != NULL && *e)   /* (likewise: 8, 16 or 32 where the device draws them) */
        o->color_bits = atoi(e);
    e = getenv("DGL_SCALE_FILTER");
    o->scale_filter = c->scale_filter ? c->scale_filter == 2 : (e && !strcmp(e, "bilinear"));
    e = getenv("DGL_PRESENT_LAG");                      /* (fe.md; or dglSetPresentLag before dglInit) */
    o->present_lag = present_lag || (e && *e == '1');
    /* DGL_TEXHEAP_KB caps the texture heap (tests of eviction, or a smaller
     * card's memory on a bigger one). */
    e = getenv("DGL_TEXHEAP_KB");
    o->heap_kb = e && *e ? (uint32_t)strtoul(e, NULL, 10) : 0;
    e = getenv("DGL_GLOS_HINTS");                       /* (or dglGlosHints before dglInit) */
    o->glos_flags = (glos_hints | (e && *e ? (unsigned)strtoul(e, NULL, 0) : 0u)) &
                    (RG_GLOS_HIDDEN | RG_GLOS_BARE | RG_GLOS_FULL);
    e = getenv("DGL_GLOS_FULL");
    if (e && *e && *e != '0')
        o->glos_flags |= RG_GLOS_FULL;
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
    dgl_ctx.stencil_bits = op->stencil_bits;
    dgl_ctx.color_bits = op->color_bits ? op->color_bits : 16;
    dgl_ctx.vsync = op->vsync;
    dgl_ctx.window = op->window;
    dgl_ctx.active = 1;
    dgl_dev->stats(&rs);
    present_base_us = rs.present_us;
    retrace0_us = rs.retrace_us;
    after_init(c);
    dgl_textures_reset(op->mem_start, op->mem_end);
    dgl_be->install();
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
    /* (The colour depth is the device's to refuse: the Matrox cards draw 16 bits, the memory device 8, 16
       or 32.) */
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
            dgl_be = backend_for(wd);
            dgl_crash_install();
            memset(&stats, 0, sizeof stats);
            return opened(&c, &op);
        }
        /* (1: GLOS won't have it as an app: the screen, as on DOS) */
    }
    if (!dgl_dev)
        dgl_dev = dgl_pick_device();
    if (!dgl_dev) {
        dgl_set_error("%s", rg_error());
        return -1;
    }
    if (!backend_for(dgl_dev)) {                /* (the ViRGE/DX's waits for V4) */
        dgl_set_error("DOS-GL has no drawing back end for the %s device yet", dgl_dev->name);
        return -1;
    }
    dgl_be = backend_for(dgl_dev);
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
        const char *a = getenv("DGL_ABLATE");
        const char *e = getenv("DGL_EXIT_AFTER");
        dgl_ablate = a ? atoi(a) : 0;
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
        stats_tnl0 = be_tris_tnl();
#ifdef MGA_PROF
        stats_all0 = be_tris_all();
#endif
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
            "tex=%lu clear=%lu drain=%lu vsync=%lu swap=%lu flip=%lu pres=%lu tris=%lu wr=%lu fiford=%lu sw=%lu ovh=%lu",
            K(PROF_APP), K(PROF_FIFO), K(PROF_SPLANE), K(PROF_SINC), K(PROF_STRAP), K(PROF_D_XFORM),
            K(PROF_D_VALID), K(PROF_D_CLIP), K(PROF_D_PROJ), K(PROF_D_SETUP), K(PROF_D_TEX), K(PROF_D_CLEAR),
            K(PROF_D_DRAIN), K(PROF_D_VSYNC), K(PROF_D_SWAP), K(PROF_FLIP), K(PROF_PRES), (unsigned long)tris,
            (unsigned long)prof_wr,
            (unsigned long)prof_fifo_rd, (unsigned long)sw, (unsigned long)prof_ovh);
#ifdef DGL_PROF_FINE
    /* (log lines stop at 255 characters: the rest on lines of their own, for the DGL-PROF line before them) */
    DGL_ERR("DGL-PFINE fxf=%lu fprj=%lu ftri=%lu ring=%lu vidle=%lu batch=%lu", K(PROF_D_FXF), K(PROF_D_FPRJ),
            K(PROF_D_FTRI), K(PROF_D_RING), K(PROF_D_VIDLE), K(PROF_D_BATCH));
#endif
#undef K
    /* Entries into each stage (each costs about one switch: profsum.py
     * takes that out), and calls counted. */
#define N(s) ((unsigned long)prof_n[s])
    DGL_ERR("DGL-PCNT app=%lu fifo=%lu splane=%lu sinc=%lu strap=%lu xform=%lu valid=%lu clip=%lu proj=%lu "
            "setup=%lu tex=%lu clear=%lu drain=%lu vsync=%lu swap=%lu flip=%lu pres=%lu", N(PROF_APP), N(PROF_FIFO),
            N(PROF_SPLANE), N(PROF_SINC), N(PROF_STRAP), N(PROF_D_XFORM), N(PROF_D_VALID), N(PROF_D_CLIP),
            N(PROF_D_PROJ), N(PROF_D_SETUP), N(PROF_D_TEX), N(PROF_D_CLEAR), N(PROF_D_DRAIN), N(PROF_D_VSYNC),
            N(PROF_D_SWAP), N(PROF_FLIP), N(PROF_PRES));
    DGL_ERR("DGL-PCNT fxf=%lu fprj=%lu ftri=%lu ring=%lu vidle=%lu batch=%lu c_verts=%lu c_binds=%lu c_xverts=%lu "
            "c_fverts=%lu", N(PROF_D_FXF), N(PROF_D_FPRJ), N(PROF_D_FTRI), N(PROF_D_RING), N(PROF_D_VIDLE),
            N(PROF_D_BATCH), dgl_pcount.verts, dgl_pcount.binds, dgl_pcount.xverts, dgl_pcount.fverts);
#undef N
    memset(&dgl_pcount, 0, sizeof dgl_pcount);
    /* The waits for the chip by cause (K cycles/count), the flushes by cause,
     * and the registers outside the DMA list that forced one (offset:count). */
    DGL_ERR("DGL-PWAIT fifo=%lu/%lu flush=%lu/%lu kq=%lu/%lu vroom=%lu/%lu fl_noncmd=%lu fl_list=%lu fl_ext=%lu "
            "nc=%lx:%lu,%lx:%lu,%lx:%lu,%lx:%lu", (unsigned long)(prof_wcyc[PROF_W_FIFO] >> 10),
            (unsigned long)prof_wn[PROF_W_FIFO], (unsigned long)(prof_wcyc[PROF_W_FLUSH] >> 10),
            (unsigned long)prof_wn[PROF_W_FLUSH], (unsigned long)(prof_wcyc[PROF_W_KQ] >> 10),
            (unsigned long)prof_wn[PROF_W_KQ], (unsigned long)(prof_wcyc[PROF_W_VROOM] >> 10),
            (unsigned long)prof_wn[PROF_W_VROOM], (unsigned long)prof_fl[PROF_FL_NONCMD],
            (unsigned long)prof_fl[PROF_FL_LIST], (unsigned long)prof_fl[PROF_FL_EXT], (unsigned long)prof_nc_off[0],
            (unsigned long)prof_nc_n[0], (unsigned long)prof_nc_off[1], (unsigned long)prof_nc_n[1],
            (unsigned long)prof_nc_off[2], (unsigned long)prof_nc_n[2], (unsigned long)prof_nc_off[3],
            (unsigned long)prof_nc_n[3]);
    {
        /* The list's shape: runs, their dwords, and the ten registers written
         * most (DMA index:count; 0x... = MGAREG offset 0x1c00 + 4*index for
         * indices below 0x80, else 0x2c00 + 4*(index-0x80)). */
        char b[200] = "";
        int k, m, used = 0, idx[10];
        for (k = 0; k < 10; k++) {
            int best = -1;
            for (m = 0; m < 256; m++) {
                int seen = 0, q;
                for (q = 0; q < k; q++)
                    seen |= idx[q] == m;
                if (!seen && prof_lreg[m] && (best < 0 || prof_lreg[m] > prof_lreg[best]))
                    best = m;
            }
            idx[k] = best;
            if (best >= 0)
                used += snprintf(b + used, sizeof b - (size_t)used, " %02x:%lu", best, (unsigned long)prof_lreg[best]);
        }
        DGL_ERR("DGL-PRUN runs=%lu run_dw=%lu list=%s", (unsigned long)prof_runs, (unsigned long)prof_run_dw, b);
        prof_runs = prof_run_dw = 0;
        memset(prof_lreg, 0, sizeof prof_lreg);
    }
    memset(prof_wcyc, 0, sizeof prof_wcyc);
    memset(prof_wn, 0, sizeof prof_wn);
    memset(prof_fl, 0, sizeof prof_fl);
    memset(prof_nc_n, 0, sizeof prof_nc_n);
    memset(prof_nc_off, 0, sizeof prof_nc_off);
    prof_reset(prof_cur);
}
#endif

/* v1.5: the scaled modes' swap without waiting for the card (rg_open's
 * present_lag; gl/docs/fe.md), for the next dglInit. */
void dglSetPresentLag(int on) { present_lag = on != 0; }
void dglGlosHints(unsigned int flags) { glos_hints = flags & (DGL_GLOS_HIDDEN | DGL_GLOS_BARE | DGL_GLOS_FULL); }

static void vbe_yield(void) { dgl_yield_maybe(0); }

/* (May be set before dglInit: its start-up's BIOS calls then yield too, vbe.c.) */
void dglSetWaitHook(void (*fn)(void *arg), void *arg)
{
    wait_hook = fn;
    wait_hook_arg = arg;
    rg_set_wait_hook(fn ? vbe_yield : NULL);
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
    dgl_frame_no = stats.swaps;
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
            DGL_ERR("DGL-STAT fps=%.1f tris/s=%.0f tnl/s=%.0f swaps=%lu tex_kb=%lu stubs=%lu drain_ms=%lu "
                    "retrace_ms=%lu present_ms=%lu hooks=%lu lag_us=%lu/%lu", (stats.swaps - stats_swaps0) * 1e6 / dt,
                    (be_tris() - stats_tris0) * 1e6 / dt, (be_tris_tnl() - stats_tnl0) * 1e6 / dt, stats.swaps, (unsigned long)(be_mem_used() >> 10),
                    dgl_stub_calls, (unsigned long)(drain_us / 1000), (unsigned long)(retrace_us / 1000),
                    (unsigned long)((stats.present_us - present0_us) / 1000), stats.wait_hooks,
                    (unsigned long)(rs.lag_n != lag0_n ? (rs.lag_us - lag0_us) / (rs.lag_n - lag0_n) : 0),
                    (unsigned long)rs.lag_max_us);    /* (present_lag: a swap's time until its flip, mean/longest) */
            lag0_us = rs.lag_us;
            lag0_n = rs.lag_n;
            {
                dgl_be_stats bs;
                dgl_be->stats(&bs);
                if (bs.tnl_draws | bs.tnl_fog_cpu | bs.tnl_small_cpu)
                    DGL_ERR("DGL-TNLSTAT draws=%lu fogged=%lu fog_to_cpu=%lu small_to_cpu=%lu vbo_hits=%lu "
                            "vbo_made=%lu", (unsigned long)bs.tnl_draws, (unsigned long)bs.tnl_fogged,
                            (unsigned long)bs.tnl_fog_cpu, (unsigned long)bs.tnl_small_cpu,
                            (unsigned long)bs.tnl_vbo_hits, (unsigned long)bs.tnl_vbo_made);   /* (totals so far) */
            }
            drain_us = 0;
            present0_us = stats.present_us;
#ifdef MGA_PROF
            if (stats_on >= 2)
                prof_report(be_tris_all() - stats_all0);
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
            stats_tnl0 = be_tris_tnl();
#ifdef MGA_PROF
            stats_all0 = be_tris_all();
#endif
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
