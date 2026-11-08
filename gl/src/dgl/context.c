/* context.c - dglInit, dglShutdown, dglSwapBuffers (PRD §5.5, FR-HAL-3/4).
 *
 * VRAM layout: colour buffer A at 0, colour buffer B (double buffering),
 * the 16-bit depth buffer, then the texture heap; each 4 KB aligned. The
 * engine always draws into the hidden buffer; a swap shows it through VBE
 * 4F07h, after the vertical retrace when vsync is on.
 *
 * Scaled modes (a size the BIOS lacks, shown in a larger BIOS mode; the
 * HAL's mode planner decides): two display buffers of the BIOS mode, then
 * one render buffer that GL draws into, its depth buffer and the heap. A
 * swap scales the render buffer into the hidden display buffer
 * (engine_present) and flips that. Zoomed modes use the layout above at
 * the BIOS mode's pitch, and the chip doubles lines and pixels. */
#include "dgl.h"
#include "../gl/gl_state.h"
#include "../gl/gl_tex.h"
#include "mga/mmio.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <sys/nearptr.h>
#endif

dgl_context dgl_ctx;
static DGLStats stats;
/* Test hooks (PRD §5.5, M5): DGL_EXIT_AFTER=n ends the program after n
 * swaps; DGL_STATS=1 logs frame and triangle rates once a second. */
static unsigned long exit_after;
static int exit_pending;           /* DGL-START printed, DGL-EXIT owed */
static int stats_on;                /* DGL_STATS: 1 = DGL-STAT, 2 = also DGL-PRIMS */
static uint32_t stats_t0, stats_tris0;
static unsigned long stats_swaps0;
/* DGL_STATS: time the swaps spent waiting for the engine to finish the frame
 * (the drain) and for the retrace, in the second being reported. */
static uint32_t drain_us, retrace_us;
#ifdef MGA_PROF
#include "mga/fp.h"
#include "mga/regs_mga.h"
#include <math.h>
static void prof_start(int level);
static void prof_report(uint32_t tris);
#endif
static unsigned long present0_us;       /* stats.present_us at the last DGL-STAT */

#define ALIGN4K(x) (((x) + 4095u) & ~4095u)

/* The first GL errors, with the address inside the GL function that raised
 * them (look it up in the program's link map). */
static void log_gl_error(GLenum e, void *at)
{
    static int n;
    if (n < 16 && ++n)
        DGL_WARN("DGL-GLERR 0x%04x at %p%s", (unsigned)e, at, n == 16 ? " (no more logged)" : "");
}

uint32_t dgl_color_off(int front)
{
    if (dgl_ctx.scaled || !dgl_ctx.double_buffer)
        return dgl_ctx.front_off;
    return dgl_ctx.front_is_a == !!front ? dgl_ctx.front_off : dgl_ctx.back_off;
}

static void set_target(void)
{
    mga_target t;
    DGL_GPU_ENTER();
    memset(&t, 0, sizeof t);
    t.color_off = dgl_color_off(dgl_ctx.draw_front);
    t.z_off = dgl_ctx.z_off;
    t.pitch_px = dgl_ctx.pitch_px;
    t.bpp = 16;
    t.zbits = dgl_ctx.z_off ? 16 : 0;
    engine_set_target(&t);
    engine_set_clip(0, 0, dgl_ctx.width, dgl_ctx.height);
}

/* Where the display starts. Loop C (DGL_RIG) shows nothing: the host's
 * driver may own the screen. */
static uint32_t rig_base;
static void show_start(uint32_t off, int pitch_bytes)
{
#ifdef DGL_RIG
    (void)off;
    (void)pitch_bytes;
#else
    vbe_set_display_start(off, pitch_bytes, 16);
#endif
}

/* Scale the render buffer into a display buffer: the hidden one for a
 * swap, the one on screen for GL_FRONT or single buffering. The engine's
 * texture, blend and mask registers are overwritten: GL re-emits them. */
static void present(int to_front)
{
    mga_surface rs, ds;
    uint32_t t0 = sys_time_us();
    rs.off = dgl_ctx.front_off; rs.w = dgl_ctx.width; rs.h = dgl_ctx.height; rs.pitch_px = dgl_ctx.pitch_px;
    ds.off = dgl_ctx.disp_off[to_front ? dgl_ctx.disp_front : !dgl_ctx.disp_front];
    ds.w = dgl_ctx.plan.disp.width; ds.h = dgl_ctx.plan.disp.height; ds.pitch_px = dgl_ctx.disp_pitch_px;
    DGL_FPU_ENTER();
    engine_present(&rs, &ds, dgl_ctx.plan.dx, dgl_ctx.plan.dy, dgl_ctx.plan.dw, dgl_ctx.plan.dh,
                   dgl_ctx.filter ? MGA_PRESENT_BILINEAR : MGA_PRESENT_NEAREST);
    DGL_FPU_LEAVE();
    dgl_sync();
    stats.present_us += sys_time_us() - t0;
    dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG;
}

void dgl_present_front(void)
{
    if (!dgl_ctx.active || !dgl_ctx.scaled)
        return;
    dgl_sync();
    present(1);
}

static void after_init(const DGLConfig *c);

/* Under GLOS with a desktop that can show GL in a window (glos.c): a window of
   the context's size, its buffers and texture heap in VRAM GLOS gives; no mode
   set, no palette, no display start. Any size the engine can draw. */
static int init_glos(const DGLConfig *cfg)
{
    static const DGLConfig zero;
    DGLConfig c;
    dgl_glos_layout l;
    int r;
    if (!cfg || !memcmp(cfg, &zero, sizeof zero)) {
        memset(&c, 0, sizeof c);
        c.width = 640; c.height = 480; c.color_bits = 16; c.depth_bits = 16;
        c.double_buffer = 1; c.vsync = 1;
    } else {
        c = *cfg;
        if (!c.width || !c.height) { c.width = 640; c.height = 480; }
    }
    if (c.color_bits && c.color_bits != 16) {
        dgl_set_error("only 16-bit colour is supported");
        return -1;
    }
    if (c.width < 1 || c.height < 1 || !dgl_pitch_for(c.width) || c.height > 2048) {
        dgl_set_error("no %dx%d window", c.width, c.height);
        return -1;
    }
    memset(&dgl_ctx, 0, sizeof dgl_ctx);
    memset(&l, 0, sizeof l);
    l.width = c.width;
    l.height = c.height;
    l.pitch_px = dgl_pitch_for(c.width);
    l.double_buffer = c.double_buffer != 0;
    l.depth = c.depth_bits != 0;
    if ((r = dgl_glos_open(&l)) != 0)
        return r;
    dgl_ctx.width = c.width;
    dgl_ctx.height = c.height;
    dgl_ctx.pitch_px = dgl_ctx.disp_pitch_px = l.pitch_px;
    dgl_ctx.double_buffer = l.double_buffer;
    dgl_ctx.depth_bits = l.depth ? 16 : 0;
    dgl_ctx.vsync = 0;                          /* (the desktop shows the window when it composes) */
    dgl_ctx.front_off = l.front_off;
    dgl_ctx.back_off = l.back_off;
    dgl_ctx.z_off = l.depth ? l.z_off : 0;
    dgl_ctx.heap_off = l.heap_off;
    dgl_ctx.vram_bytes = l.vram_bytes;
    dgl_ctx.plan.disp.width = (uint16_t)c.width;
    dgl_ctx.plan.disp.height = (uint16_t)c.height;
    dgl_ctx.plan.dw = c.width;
    dgl_ctx.plan.dh = c.height;
    dgl_crash_install();
    DGL_GPU_ENTER();
    engine_init(dgl_ctx.pitch_px, 16);
    dgl_ctx.front_is_a = 1;
    dgl_ctx.active = 1;
    memset(&stats, 0, sizeof stats);
    dgl_ctx.front_is_a = 0; set_target(); engine_fill(0, 0, c.width, c.height, 0);
    dgl_ctx.front_is_a = 1; set_target(); engine_fill(0, 0, c.width, c.height, 0);
    if (dgl_ctx.z_off)
        engine_fill_depth(0, 0, c.width, c.height, 0xFFFF);
    engine_sync(500000);
    dgl_glos_show(dgl_ctx.front_off);
    dgl_glos_visible();
    after_init(&c);
    {
        const char *e = getenv("DGL_TEXHEAP_KB");       /* (glos.c sized the heap's block by it already) */
        (void)e;
        dgl_textures_reset(l.heap_off, l.heap_end);
    }
    dgl_emit_install();
    DGL_GPU_QUIET();
    DGL_INFO("DGL-INIT %dx%d pitch=%d double=%d depth=%d heap=%lu..%lu window (GLOS)", c.width, c.height,
             dgl_ctx.pitch_px, dgl_ctx.double_buffer, dgl_ctx.depth_bits, (unsigned long)l.heap_off,
             (unsigned long)l.heap_end);
    return 0;
}

int dglInit(const DGLConfig *cfg)
{
    static const DGLConfig zero;
    DGLConfig c;
    const DGLMode *md;
    const mga_mode_plan *pl;
    int pitch = 0;
    uint32_t fb, render;
    if (dgl_ctx.active) {
        dgl_set_error("dglInit called twice");
        return -1;
    }
    if (dgl_glos_want()) {
        int r = init_glos(cfg);
        if (r <= 0)
            return r;                   /* (1: GLOS won't have it as an app: the screen, as on DOS) */
    }
    if (!cfg || !memcmp(cfg, &zero, sizeof zero)) {
        memset(&c, 0, sizeof c);        /* PRD §8.1 defaults */
        c.width = 640; c.height = 480; c.color_bits = 16; c.depth_bits = 16;
        c.double_buffer = 1; c.vsync = 1;
    } else {
        c = *cfg;
        if (!c.width || !c.height) { c.width = 640; c.height = 480; }
    }
    if (c.color_bits && c.color_bits != 16) {
        dgl_set_error("only 16-bit colour is supported");
        return -1;
    }
    if (dgl_discover() != 0)
        return -1;
    md = dgl_mode_for(c.width, c.height, &pl);
    if (!md) {
        dgl_set_error("no usable %dx%dx16 mode on this card", c.width, c.height);
        return -1;
    }
    memset(&dgl_ctx, 0, sizeof dgl_ctx);
    dgl_ctx.width = c.width;
    dgl_ctx.height = c.height;
    dgl_ctx.plan = *pl;
    dgl_ctx.scaled = md->scaled;
    dgl_ctx.zoomed = md->zoomed;
    dgl_ctx.pitch_px = md->pitch_px;            /* the render buffer's (scaled) or the display's */
    dgl_ctx.disp_pitch_px = dgl_pitch_for(md->display_width);
    {
        const char *e = getenv("DGL_SCALE_FILTER");
        dgl_ctx.filter = c.scale_filter ? c.scale_filter == 2 : (e && !strcmp(e, "bilinear"));
    }
    dgl_ctx.double_buffer = c.double_buffer != 0;
    dgl_ctx.depth_bits = c.depth_bits ? 16 : 0;
    dgl_ctx.vsync = c.vsync != 0;
    {
        const char *e = getenv("DGL_VSYNC");     /* a test or user override */
        if (e && *e)
            dgl_ctx.vsync = *e != '0';
    }
    dgl_ctx.vram_bytes = mga.vram_bytes;
    /* Map at least 16 MB (the most a G200 carries; every supported card's
     * aperture is that big or bigger), so the probe below can find VRAM the
     * BIOS did not report. */
    mga.fb_size = mga.vram_bytes > (16u << 20) ? mga.vram_bytes : (16u << 20);
    if (mga_map(&mga) != 0) {
        dgl_set_error("cannot map the card's apertures (DPMI 0800h)");
        return -1;
    }
    dgl_crash_install();
#ifdef DGL_RIG
    /* Loop C: nothing is shown. The buffers start at DGL_RIG_VRAM_BASE (KB;
     * above the host's console when its driver is still bound), and neither
     * the mode, the DAC nor the display start is touched. */
    (void)pitch;
    {
        const char *e = getenv("DGL_RIG_VRAM_BASE");
        rig_base = e ? (uint32_t)strtoul(e, NULL, 10) * 1024u : 0;
    }
#else
    if (vbe_set_mode(&pl->disp, dgl_ctx.disp_pitch_px, &pitch) != 0 || pitch != dgl_ctx.disp_pitch_px) {
        dgl_teardown();
        dgl_set_error("VBE mode %03x with pitch %d failed (BIOS gave %d)", pl->disp.mode, dgl_ctx.disp_pitch_px,
                      pitch);
        return -1;
    }
    vbe_set_zoom(pl->zoom);
#endif
#ifndef DGL_RIG
    {
        /* VBE's total memory can be short: Matrox's G200 BIOS reports 2 MB of
         * 8 in 86Box. In graphics mode VRAM can be written freely, so probe it
         * and keep the larger size for the texture heap. */
        uint32_t probed = mga_probe_vram();
        if (probed > dgl_ctx.vram_bytes) {
            DGL_WARN("DGL-VRAM the BIOS reports %lu KB, the card has %lu KB", (unsigned long)(dgl_ctx.vram_bytes >> 10),
                     (unsigned long)(probed >> 10));
            dgl_ctx.vram_bytes = probed;
            dgl_note_vram(probed);
        }
    }
#endif
    /* The layout, checked against the VRAM found (the BIOS can under-report). */
    fb = (uint32_t)dgl_ctx.pitch_px * (uint32_t)c.height * 2u;
    render = rig_base;
    if (dgl_ctx.scaled) {
        uint32_t disp = (uint32_t)dgl_ctx.disp_pitch_px * (uint32_t)pl->disp.height * 2u;
        dgl_ctx.disp_off[0] = rig_base;
        dgl_ctx.disp_off[1] = ALIGN4K(rig_base + disp);
        render = ALIGN4K(dgl_ctx.disp_off[1] + disp);
    }
    dgl_ctx.front_off = render;
    dgl_ctx.back_off = dgl_ctx.double_buffer && !dgl_ctx.scaled ? ALIGN4K(render + fb) : render;
    dgl_ctx.z_off = dgl_ctx.depth_bits ? ALIGN4K(dgl_ctx.back_off + fb) : 0;
    dgl_ctx.heap_off = ALIGN4K((dgl_ctx.z_off ? dgl_ctx.z_off : dgl_ctx.back_off) + fb);
    if (dgl_ctx.heap_off > dgl_ctx.vram_bytes) {
        dgl_teardown();
        dgl_set_error("%dx%d with these buffers needs %lu bytes of VRAM; the card has %lu", c.width, c.height,
                      (unsigned long)dgl_ctx.heap_off, (unsigned long)dgl_ctx.vram_bytes);
        return -1;
    }
    engine_init(dgl_ctx.pitch_px, 16);
#ifndef DGL_RIG
    {
        /* 16-bit pixels index the DAC palette on these chips, and the BIOS
         * leaves it non-linear: load an identity ramp. */
        uint8_t ramp[256];
        int i;
        for (i = 0; i < 256; i++)
            ramp[i] = (uint8_t)i;
        dac_set_ramp(ramp);
    }
#endif
    dgl_ctx.front_is_a = 1;
    dgl_ctx.active = 1;
    /* Clear everything the context owns, then draw into the hidden buffer. */
    memset(&stats, 0, sizeof stats);
    if (dgl_ctx.scaled) {
        mga_target t;
        int i;
        memset(&t, 0, sizeof t);
        t.pitch_px = dgl_ctx.disp_pitch_px; t.bpp = 16;
        for (i = 0; i < 2; i++) {
            t.color_off = dgl_ctx.disp_off[i];
            engine_set_target(&t);
            engine_set_clip(0, 0, pl->disp.width, pl->disp.height);
            engine_fill(0, 0, pl->disp.width, pl->disp.height, 0);
        }
    }
    dgl_ctx.front_is_a = 0; set_target(); engine_fill(0, 0, c.width, c.height, 0);
    dgl_ctx.front_is_a = 1; set_target(); engine_fill(0, 0, c.width, c.height, 0);
    if (dgl_ctx.z_off)
        engine_fill_depth(0, 0, c.width, c.height, 0xFFFF);
    engine_sync(500000);
    show_start(dgl_ctx.scaled ? dgl_ctx.disp_off[0] : dgl_ctx.front_off,
               (dgl_ctx.scaled ? dgl_ctx.disp_pitch_px : dgl_ctx.pitch_px) * 2);
    after_init(&c);
    {
        /* DGL_TEXHEAP_KB caps the texture heap (tests of eviction, or a
         * smaller card's memory on a bigger one). */
        const char *e = getenv("DGL_TEXHEAP_KB");
        uint32_t end = dgl_ctx.vram_bytes, kb = e ? (uint32_t)strtoul(e, NULL, 10) : 0;
        if (kb && dgl_ctx.heap_off + kb * 1024u < end)
            end = dgl_ctx.heap_off + kb * 1024u;
        dgl_textures_reset(dgl_ctx.heap_off, end);
    }
    dgl_emit_install();
    DGL_INFO("DGL-INIT %dx%d pitch=%d double=%d depth=%d heap=%lu..%lu display=%dx%d fit=%s", c.width, c.height,
             dgl_ctx.pitch_px, dgl_ctx.double_buffer, dgl_ctx.depth_bits, (unsigned long)dgl_ctx.heap_off,
             (unsigned long)dgl_ctx.vram_bytes, pl->disp.width, pl->disp.height, mga_fit_name(pl->fit));
    return 0;
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
        stats_tris0 = setup_stats.tris;
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
    dgl_ftrace_write();
    dgl_teardown();
}

/* Every way out (dglShutdown, DGL_EXIT_AFTER, exit, a fault) ends here once. */
void dgl_note_exit(void)
{
    if (!exit_pending)
        return;
    exit_pending = 0;
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

static void prof_start(int level)
{
    int i;
    uint64_t t0;
    if (level >= 3)
        prof_micro();
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

static void (*wait_hook)(void *);
static void *wait_hook_arg;

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
    if (wait_hook && mga_mmio) {
        uint32_t start = sys_time_us(), last = start, now;
        while (!engine_idle() && (now = sys_time_us()) - start < 200000u)
            if (now - last >= 1000u) {
                run_wait_hook();
                last = sys_time_us();
            }
    }
    dgl_sync();
}

/* The retrace wait: the hook runs once before it, never during it (a hook
 * that ran past the start of the blank would cost a whole frame). */
static void retrace_wait(void)
{
    uint32_t t0 = stats_on ? sys_time_us() : 0;
    PROF_SCOPE(PROF_D_VSYNC);
    if (wait_hook)
        run_wait_hook();
    engine_vsync_wait(50000);
    if (stats_on)
        retrace_us += sys_time_us() - t0;
}

static void swap_buffers(void)
{
    uint32_t show;
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
    stats.triangles = setup_stats.tris;
    stats.stub_calls = dgl_stub_calls;
    if (stats_on) {
        uint32_t now = sys_time_us(), dt = now - stats_t0;
        if (dt >= 1000000u) {
            DGL_ERR("DGL-STAT fps=%.1f tris/s=%.0f swaps=%lu tex_kb=%lu stubs=%lu drain_ms=%lu retrace_ms=%lu "
                    "present_ms=%lu hooks=%lu", (stats.swaps - stats_swaps0) * 1e6 / dt,
                    (setup_stats.tris - stats_tris0) * 1e6 / dt, stats.swaps, (unsigned long)(dgl_vram_used() >> 10),
                    dgl_stub_calls, (unsigned long)(drain_us / 1000), (unsigned long)(retrace_us / 1000),
                    (unsigned long)((stats.present_us - present0_us) / 1000), stats.wait_hooks);
            drain_us = retrace_us = 0;
            present0_us = stats.present_us;
#ifdef MGA_PROF
            if (stats_on >= 2)
                prof_report(setup_stats.tris - stats_tris0);
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
            stats_tris0 = setup_stats.tris;
            stats_swaps0 = stats.swaps;
        }
    }
    if (exit_after && stats.swaps >= exit_after) {
        dglShutdown();
        exit(0);
    }
    if (dgl_glos.on) {
        /* In a GLOS window: it shows the finished buffer from now on (the
         * desktop puts it on the screen), and GL draws into the other. */
        if (!dgl_ctx.double_buffer) {
            dgl_glos_show(dgl_ctx.front_off);
            return;
        }
        show = dgl_ctx.front_is_a ? dgl_ctx.back_off : dgl_ctx.front_off;
        dgl_glos_show(show);
        dgl_ctx.front_is_a = !dgl_ctx.front_is_a;
        set_target();
        dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER;
        DGL_GPU_QUIET();                /* (the program's own work until the next frame's drawing) */
        return;
    }
    if (dgl_ctx.scaled) {
        /* Scale the finished frame into the hidden display buffer, then
         * flip the display (single buffering: into the one on screen). */
        present(!dgl_ctx.double_buffer);
        if (!dgl_ctx.double_buffer)
            return;
        if (dgl_ctx.vsync)
            retrace_wait();
        dgl_ctx.disp_front = !dgl_ctx.disp_front;
        show_start(dgl_ctx.disp_off[dgl_ctx.disp_front], dgl_ctx.disp_pitch_px * 2);
        set_target();
        return;
    }
    if (!dgl_ctx.double_buffer)
        return;
    show = dgl_ctx.front_is_a ? dgl_ctx.back_off : dgl_ctx.front_off;
    if (dgl_ctx.vsync)
        retrace_wait();
    show_start(show, dgl_ctx.pitch_px * 2);
    dgl_ctx.front_is_a = !dgl_ctx.front_is_a;
    set_target();
    dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER;    /* the target write reset MACCESS */
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
    if (dgl_ftrace_on)
        dgl_ftrace_frame(t0);
}

void dgl_retarget(void)
{
    if (dgl_ctx.active)
        set_target();                   /* through the FIFO, so queued draws keep their buffer */
}

const DGLStats *dglGetStats(void)
{
    stats.stub_calls = dgl_stub_calls;
    return &stats;
}
