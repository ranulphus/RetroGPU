/* mga_glos.c - the Matrox device in a window on a GLOS desktop, by GLOS's
 * API 2.0 (GLOS PRD §9.7, §10.3; GLOS docs/milestones-m8.md step D). Moved
 * from DOS-GL's src/dgl/glos.c and context.c's init_glos in B1 (gl/docs/b1.md).
 *
 * When the program runs under GLOS (DPMI 0A00h "GLOS") and GLOS says a GL
 * program can have a window there (GLOS_MACHINE_GLWIN: a Matrox card drawing a
 * 16-bit desktop), the program becomes a GLOS app and opens a window of the
 * context's size instead of setting a video mode. The colour, depth and
 * texture memory are blocks of VRAM that GLOS gives the app; the program
 * draws into them with the engine from its own copy of the HAL, as on DOS,
 * and sees no other memory of the card. A swap tells GLOS which colour
 * buffer the window shows now (DRAW's WINVRAM): nothing is copied by the
 * CPU, and the desktop's engine puts it on the screen.
 *
 * The engine is shared with the desktop and other apps (GLOS's GPU lock,
 * rg_lock): RG_GPU_ENTER (rg/device.h) runs before each piece of work with
 * the engine: it takes the lock if GLOS's shared page says the program
 * hasn't got it, and if someone else had the engine since (the page's `lost`
 * moved) the HAL forgets what it believed of the registers (engine_resync)
 * and the user's state is written again (rg_lock.on_lost). GLOS only takes
 * the lock back in a GLOS or DOS call, never between that look and the
 * drawing. */
#include "rg/mga_dev.h"
#include "rg/glos.h"
#include "mga/mmio.h"
#include <stdlib.h>
#include <string.h>

void rg_mga_clear_all(void);            /* mga.c */
void rg_mga_graphics(void);

#if defined(__DJGPP__) && !defined(DGL_RIG)

extern char **__crt0_argv;

static unsigned blocks[8], nblocks;
static glos_handle window;

int rg_mga_window_ok(void) { return (rg_glos_caps() & GLOS_MACHINE_GLWIN) != 0; }

static void free_all(void)
{
    struct glos_vram v;
    struct glos_close c;
    while (nblocks) {
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        v.op = 1;
        v.offset = blocks[--nblocks];
        rg_glos_call(GLOS_DRAW_VRAM, &v);
    }
    if (window) {
        c.size = sizeof c;
        c.handle = window;
        rg_glos_call(GLOS_CORE_CLOSE, &c);
    }
    window = 0;
}

static int block(uint32_t min, uint32_t max, uint32_t *off, uint32_t *bytes)
{
    struct glos_vram v;
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    v.least = min, v.most = max, v.align = 4096;
    if (nblocks == sizeof blocks / sizeof blocks[0] || rg_glos_call(GLOS_DRAW_VRAM, &v) != 0)
        return -1;
    blocks[nblocks++] = v.offset;
    *off = v.offset;
    if (bytes)
        *bytes = v.bytes;
    return 0;
}

void rg_mga_glos_show(uint32_t off)
{
    struct glos_winvram v;
    if (!window)
        return;
    v.size = sizeof v;
    v.handle = window;
    v.offset = off;
    v.pitch = (unsigned)rg_mga.pitch_px * 2u;
    rg_glos_call(GLOS_DRAW_WINVRAM, &v);
}

static void visible(void)
{
    struct glos_win_show v;
    if (!window)
        return;
    v.size = sizeof v;
    v.handle = window;
    v.state = GLOS_WS_NORMAL;
    v.flags = 0;
    rg_glos_call(GLOS_WIN_SHOW, &v);
}

void rg_mga_glos_slow(void)
{
    if (!*rg_lock.held)
        rg_glos_call(GLOS_DRAW_GPU_LOCK, 0);
    if (*rg_lock.lost != rg_lock.seen) {
        rg_lock.seen = *rg_lock.lost;
        rg_lock.resyncs++;
        engine_resync();                /* nothing the HAL knew of the registers holds */
        if (rg_lock.on_lost)
            rg_lock.on_lost();
    }
}

void rg_mga_glos_close(void)
{
    if (!rg_lock.on)
        return;
    rg_lock.on = 0;
    free_all();
    rg_glos_call(GLOS_DRAW_GPU_UNLOCK, 0);
}

/* 0 in a window; 1 when GLOS won't have the program as an app (it runs
   full-screen, as a session); -1 on an error. */
int rg_mga_glos_open(const rg_open *o, rg_opened *out)
{
    static struct glos_gpu g;
    static struct glos_init in;
    volatile struct glos_gpu_share *sh;
    struct glos_win_create w;
    const char *name = __crt0_argv && __crt0_argv[0] ? __crt0_argv[0] : "DOS-GL", *e;
    unsigned i;
    int pitch_px = rg_mga_pitch_for(o->width);
    uint32_t colour = (uint32_t)pitch_px * (uint32_t)o->height * 2u, kb, b, front_off = 0, back_off = 0, z_off = 0,
             heap_off = 0;
    int r;
    if (o->color_bits && o->color_bits != 16) {
        rg_set_error("only 16-bit colour is supported");
        return -1;
    }
    if (o->width < 1 || o->height < 1 || !pitch_px || o->height > 2048) {
        rg_set_error("no %dx%d window", o->width, o->height);
        return -1;
    }
    memset(&rg_mga, 0, sizeof rg_mga);
    if ((e = getenv("DGL_TITLE")) != NULL && *e)        /* the program's name: its window's title, and the app's */
        name = e;
    else {
        for (e = name; *e; e++)
            if (*e == '\\' || *e == '/' || *e == ':')
                name = e + 1;
    }
    memset(&in, 0, sizeof in);
    in.size = sizeof in;
    in.major = GLOS_MAJOR, in.minor = GLOS_MINOR;
    for (i = 0; i < sizeof in.name - 1 && name[i] && name[i] != '.'; i++)
        in.name[i] = name[i];
    r = rg_glos_call(GLOS_CORE_INIT, &in);
    if (r && !(r == GLOS_E_INIT && in.why == GLOS_WHY_ALREADY))
        return 1;                       /* not to be an app (a session's, a child's): the screen, as on DOS */
    memset(&g, 0, sizeof g);
    g.size = sizeof g;
    if ((r = rg_glos_call(GLOS_DRAW_GPU, &g)) != 0) {
        rg_set_error("GLOS has no drawing engine for the program (error %d)", r);
        return -1;
    }
    /* The chip as GLOS describes it, in the HAL's terms; the HAL's pointers at
       what GLOS mapped (the registers while the lock is held, the program's
       own blocks of VRAM at their offsets). */
    memset(&mga, 0, sizeof mga);
    mga.device_id = (uint16_t)g.device;
    mga.revision = (uint8_t)g.revision;
    mga.family = g.device == 0x0525 ? MGA_FAMILY_G400 : g.device == 0x0520 || g.device == 0x0521 ? MGA_FAMILY_G200
               : MGA_FAMILY_G100;
    mga.name = mga.family == MGA_FAMILY_G400 ? (g.revision >= 0x80 ? "MGA-G450" : "MGA-G400/G450")
             : mga.family == MGA_FAMILY_G200 ? "MGA-G200" : "MGA-G100";
    mga.fb_phys = g.fb_phys;
    mga.mmio_phys = g.regs_phys;
    mga.option = g.option;
    mga.subsys = g.subsys;
    mga.vram_bytes = g.vram;
    mga_chip_caps(&mga);
    mga_mmio = rg_glos_ptr(g.regs);
    mga_fb = rg_glos_ptr(g.base);
    sh = rg_glos_ptr(g.share);
    if (!mga_mmio || !mga_fb || !sh) {
        rg_set_error("near pointers unavailable");
        return -1;
    }
    rg_lock.share = sh;
    rg_lock.held = &sh->held;
    rg_lock.lost = &sh->lost;
    rg_lock.quiet = &sh->quiet;
    rg_lock.seen = *rg_lock.lost - 1u;          /* (resynchronised before the first use) */
    rg_lock.slow = rg_mga_glos_slow;
    /* The window: the context's size, where GLOS likes, titled with the program's name. */
    memset(&w, 0, sizeof w);
    w.size = sizeof w;
    w.kind = GLOS_WK_NORMAL;
    w.w = o->width;
    w.h = o->height;
    w.flags = GLOS_WF_FIXED | GLOS_WF_ANYWHERE;
    for (i = 0; i < sizeof w.title - 1 && name[i] && name[i] != '.'; i++)
        w.title[i] = name[i];
    if ((r = rg_glos_call(GLOS_WIN_CREATE, &w)) != 0) {
        rg_set_error("GLOS made no %dx%d window (error %d)", o->width, o->height, r);
        return -1;
    }
    window = w.handle;
    /* Its buffers, each a block of its own, and the texture heap. */
    if (block(colour, 0, &front_off, NULL) != 0
        || (o->double_buffer && block(colour, 0, &back_off, NULL) != 0)
        || (o->depth_bits && block(colour, 0, &z_off, NULL) != 0)) {
        free_all();
        rg_set_error("no VRAM for a %dx%d window's buffers", o->width, o->height);
        return -1;
    }
    if (!o->double_buffer)
        back_off = front_off;
    kb = o->heap_kb ? o->heap_kb : 4096u;
    if (block(256u * 1024u < kb * 1024u ? 256u * 1024u : kb * 1024u, kb * 1024u, &heap_off, &b) != 0) {
        free_all();
        rg_set_error("no VRAM for a texture heap beside a %dx%d window", o->width, o->height);
        return -1;
    }
    rg_lock.on = 1;
    /* (No WARP in a window: the Matrox drawing back end sets triangles up with
     * the CPU while rg_lock.on. GLOS takes the engine back in any DOS or GLOS
     * call, or while the program is quiet, and work still in the DMA stream's
     * list then ran after GLOS's own drawing, onto GLOS's target (conformance
     * t27 in a window copied an empty frame). The WARP fed by the CPU instead
     * was slower than the CPU's setup on bench-g400 (WARPTRI), and DOSBench's
     * scenes in a window that way were ended by GLOS's GPU watchdog,
     * 2026-10-31.) */
    rg_mga.hooks = *o;
    rg_mga.width = o->width;
    rg_mga.height = o->height;
    rg_mga.pitch_px = rg_mga.disp_pitch_px = pitch_px;
    rg_mga.double_buffer = o->double_buffer != 0;
    rg_mga.depth_bits = o->depth_bits ? 16 : 0;
    rg_mga.vsync = 0;                           /* (the desktop shows the window when it composes) */
    rg_mga.front_off = front_off;
    rg_mga.back_off = back_off;
    rg_mga.z_off = o->depth_bits ? z_off : 0;
    rg_mga.heap_off = heap_off;
    rg_mga.heap_end = heap_off + b;
    rg_mga.vram_bytes = g.vram;
    rg_mga.plan.disp.width = (uint16_t)o->width;
    rg_mga.plan.disp.height = (uint16_t)o->height;
    rg_mga.plan.dw = o->width;
    rg_mga.plan.dh = o->height;
    rg_mga_graphics();
    RG_GPU_ENTER();
    engine_init(rg_mga.pitch_px, 16);
    rg_mga.front_is_a = 1;
    rg_mga.active = 1;
    rg_mga_clear_all();
    rg_mga_glos_show(rg_mga.front_off);
    visible();
    memset(out, 0, sizeof *out);
    out->width = rg_mga.width;
    out->height = rg_mga.height;
    out->pitch_px = rg_mga.pitch_px;
    out->double_buffer = rg_mga.double_buffer;
    out->depth_bits = rg_mga.depth_bits;
    out->vsync = 0;
    out->mem_start = rg_mga.heap_off;
    out->mem_end = rg_mga.heap_end;
    out->vram_bytes = rg_mga.vram_bytes;
    out->display_width = o->width;
    out->display_height = o->height;
    out->fit = "window";
    out->window = window;
    return 0;
}

/* The sizes a window can have: the usual ones that fit in the desktop's work
   area (the screen less the task bar) with a frame and a title bar. */
int rg_mga_window_modes(rg_mode *out, int max)
{
    static const int sizes[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 400 },
                                    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 } };
    struct glos_screen scr;
    unsigned i;
    int n = 0;
    memset(&scr, 0, sizeof scr);
    scr.size = sizeof scr;
    if (rg_glos_caps() == 0 || rg_glos_call(GLOS_DRAW_SCREEN, &scr) != 0)
        return 0;
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        rg_mode d;
        if (sizes[i][0] > scr.work_w - 8 || sizes[i][1] > scr.work_h - 28)
            continue;
        memset(&d, 0, sizeof d);
        d.width = d.display_width = sizes[i][0];
        d.height = d.display_height = sizes[i][1];
        d.color_bits = 16;
        d.pitch_px = rg_mga_pitch_for(sizes[i][0]);
        d.can_double_buffer = 1;
        d.max_depth_bits = 16;
        if (out && n < max)
            out[n] = d;
        n++;
    }
    return n;
}

#else   /* the rig and the host: no GLOS */

int rg_mga_window_ok(void) { return 0; }
int rg_mga_window_modes(rg_mode *out, int max) { (void)out; (void)max; return 0; }
int rg_mga_glos_open(const rg_open *o, rg_opened *out) { (void)o; (void)out; return -1; }
void rg_mga_glos_show(uint32_t off) { (void)off; }
void rg_mga_glos_slow(void) { }
void rg_mga_glos_close(void) { }

#endif
