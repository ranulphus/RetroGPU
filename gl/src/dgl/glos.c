/* glos.c - DOS-GL in a window on a GLOS desktop, by GLOS's API 2.0 (GLOS PRD §9.7, §10.3; GLOS
 * docs/milestones-m8.md step D).
 *
 * When the program runs under GLOS (DPMI 0A00h "GLOS") and GLOS says a GL
 * program can have a window there (GLOS_MACHINE_GLWIN: a Matrox card drawing a
 * 16-bit desktop), dglInit makes the program a GLOS app and opens a window of
 * the context's size instead of setting a video mode. The colour, depth and
 * texture memory are blocks of VRAM that GLOS gives the app; the program
 * draws into them with the engine from its own copy of the HAL, as on DOS,
 * and sees no other memory of the card. A swap tells GLOS which colour
 * buffer the window shows now (DRAW's WINVRAM): nothing is copied by the
 * CPU, and the desktop's engine puts it on the screen.
 *
 * The engine is shared with the desktop and other apps (GLOS's GPU lock).
 * DGL_GPU_ENTER (dgl.h) runs before each piece of work with the engine: it
 * takes the lock if GLOS's shared page says the program hasn't got it, and
 * if someone else had the engine since (the page's `lost` moved) the HAL
 * forgets what it believed of the registers (engine_resync) and GL's state
 * is written again. GLOS only takes the lock back in a GLOS or DOS call,
 * never between that look and the drawing.
 *
 * Without GLOS, without GLOS_MACHINE_GLWIN, or with DGL_FULLSCREEN set, nothing
 * here runs and the program takes the screen as it always has (under GLOS,
 * as a session). */
#include "dgl.h"
#include "../gl/gl_state.h"
#include "mga/mmio.h"
#include <stdlib.h>
#include <string.h>

dgl_glos_state dgl_glos;

#if defined(__DJGPP__) && !defined(DGL_RIG)
#include <glos/api2.h>                 /* (GLOS's API 2.0: its numbers and structures only, from its checkout:
                                           GLOS in config.mk; the calls are made here, not through libglos) */
#include <dpmi.h>
#include <sys/nearptr.h>

extern char **__crt0_argv;

static struct {
    unsigned long off;
    unsigned short sel;
} entry;
static int found;                       /* 1 GLOS, -1 none */
static unsigned blocks[8], nblocks;
static glos_handle window;

/* An API 2.0 call: the function in EAX, its structure (its size first) in EBX; the carry flag says it failed,
   with GLOS's error in EAX. 0, or the GLOS_E_ code. */
static int call(unsigned fn, void *s)
{
    unsigned a = fn, b = (unsigned)s, c = 0, d = 0;
    unsigned char failed;
    __asm__ __volatile__("lcall *%[e]\n\tsetc %[f]"
                         : "+a"(a), "+b"(b), "+c"(c), "+d"(d), [f] "=m"(failed)
                         : [e] "m"(entry)
                         : "esi", "edi", "cc", "memory");
    return failed ? (int)a : 0;
}

/* What the machine can do (GLOS_MACHINE_), 0 without a GLOS that speaks 2.0. */
static unsigned caps(void)
{
    static struct glos_version v;
    __dpmi_paddr p;
    if (!found) {
        found = -1;
        if (__dpmi_get_vendor_specific_api_entry_point("GLOS", &p) == 0) {
            entry.off = p.offset32;
            entry.sel = p.selector;
            found = 1;
        }
    }
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    if (found < 0 || call(GLOS_CORE_VERSION, &v) != 0 || v.major != GLOS_MAJOR)
        return 0;
    return v.machine;
}

static void *ptr(unsigned linear)       /* a linear address GLOS gave, as a near pointer */
{
    if (!__djgpp_nearptr_enable())
        return NULL;
    return (void *)(linear + __djgpp_conventional_base);
}

int dgl_glos_want(void)
{
    const char *e = getenv("DGL_FULLSCREEN");
    if (e && *e && *e != '0')
        return 0;
    return (caps() & GLOS_MACHINE_GLWIN) != 0;
}

static void free_all(void)
{
    struct glos_vram v;
    struct glos_close c;
    while (nblocks) {
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        v.op = 1;
        v.offset = blocks[--nblocks];
        call(GLOS_DRAW_VRAM, &v);
    }
    if (window) {
        c.size = sizeof c;
        c.handle = window;
        call(GLOS_CORE_CLOSE, &c);
    }
    window = 0;
}

static int block(uint32_t min, uint32_t max, uint32_t *off, uint32_t *bytes)
{
    struct glos_vram v;
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    v.least = min, v.most = max, v.align = 4096;
    if (nblocks == sizeof blocks / sizeof blocks[0] || call(GLOS_DRAW_VRAM, &v) != 0)
        return -1;
    blocks[nblocks++] = v.offset;
    *off = v.offset;
    if (bytes)
        *bytes = v.bytes;
    return 0;
}

/* 0 in a window; 1 when GLOS won't have the program as an app (it runs
   full-screen, as a session); -1 on an error. */
int dgl_glos_open(dgl_glos_layout *l)
{
    static struct glos_gpu g;
    static struct glos_init in;
    volatile struct glos_gpu_share *sh;
    struct glos_win_create w;
    const char *name = __crt0_argv && __crt0_argv[0] ? __crt0_argv[0] : "DOS-GL", *e;
    unsigned i;
    uint32_t colour = (uint32_t)l->pitch_px * (uint32_t)l->height * 2u, kb, b;
    int r;
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
    r = call(GLOS_CORE_INIT, &in);
    if (r && !(r == GLOS_E_INIT && in.why == GLOS_WHY_ALREADY))
        return 1;                       /* not to be an app (a session's, a child's): the screen, as on DOS */
    memset(&g, 0, sizeof g);
    g.size = sizeof g;
    if ((r = call(GLOS_DRAW_GPU, &g)) != 0) {
        dgl_set_error("GLOS has no drawing engine for the program (error %d)", r);
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
    mga_mmio = ptr(g.regs);
    mga_fb = ptr(g.base);
    sh = ptr(g.share);
    if (!mga_mmio || !mga_fb || !sh) {
        dgl_set_error("near pointers unavailable");
        return -1;
    }
    dgl_glos.share = sh;
    dgl_glos.held = &sh->held;
    dgl_glos.lost = &sh->lost;
    dgl_glos.quiet = &sh->quiet;
    dgl_glos.seen = *dgl_glos.lost - 1u;        /* (resynchronised before the first use) */
    /* The window: the context's size, where GLOS likes, titled with the program's name. */
    memset(&w, 0, sizeof w);
    w.size = sizeof w;
    w.kind = GLOS_WK_NORMAL;
    w.w = l->width;
    w.h = l->height;
    w.flags = GLOS_WF_FIXED | GLOS_WF_ANYWHERE;
    for (i = 0; i < sizeof w.title - 1 && name[i] && name[i] != '.'; i++)
        w.title[i] = name[i];
    if ((r = call(GLOS_WIN_CREATE, &w)) != 0) {
        dgl_set_error("GLOS made no %dx%d window (error %d)", l->width, l->height, r);
        return -1;
    }
    window = w.handle;
    /* Its buffers, each a block of its own, and the texture heap. */
    if (block(colour, 0, &l->front_off, NULL) != 0
        || (l->double_buffer && block(colour, 0, &l->back_off, NULL) != 0)
        || (l->depth && block(colour, 0, &l->z_off, NULL) != 0)) {
        free_all();
        dgl_set_error("no VRAM for a %dx%d window's buffers", l->width, l->height);
        return -1;
    }
    if (!l->double_buffer)
        l->back_off = l->front_off;
    e = getenv("DGL_TEXHEAP_KB");
    kb = e && *e ? (uint32_t)strtoul(e, NULL, 10) : 4096u;
    if (block(256u * 1024u < kb * 1024u ? 256u * 1024u : kb * 1024u, kb * 1024u, &l->heap_off, &b) != 0) {
        free_all();
        dgl_set_error("no VRAM for a texture heap beside a %dx%d window", l->width, l->height);
        return -1;
    }
    l->heap_end = l->heap_off + b;
    l->vram_bytes = g.vram;
    dgl_glos.on = 1;
    /* No WARP in a window: the CPU sets triangles up here. GLOS takes the
     * engine back in any DOS or GLOS call, or while the program is quiet,
     * and work still in the DMA stream's list then ran after GLOS's own
     * drawing, onto GLOS's target (conformance t27 in a window copied an
     * empty frame). The WARP fed by the CPU instead was slower than the
     * CPU's setup on bench-g400 (WARPTRI), and DOSBench's scenes in a
     * window that way were ended by GLOS's GPU watchdog (2026-10-31). */
    dgl_emit_no_warp();
    return 0;
}

unsigned int dglGetGlosWindow(void) { return dgl_glos.on ? window : 0; }

void dgl_glos_show(uint32_t off)
{
    struct glos_winvram v;
    if (!window)
        return;
    v.size = sizeof v;
    v.handle = window;
    v.offset = off;
    v.pitch = (unsigned)dgl_ctx.pitch_px * 2u;
    call(GLOS_DRAW_WINVRAM, &v);
}

void dgl_glos_visible(void)
{
    struct glos_win_show v;
    if (!window)
        return;
    v.size = sizeof v;
    v.handle = window;
    v.state = GLOS_WS_NORMAL;
    v.flags = 0;
    call(GLOS_WIN_SHOW, &v);
}

void dgl_glos_slow(void)
{
    if (!*dgl_glos.held)
        call(GLOS_DRAW_GPU_LOCK, 0);
    if (*dgl_glos.lost != dgl_glos.seen) {
        dgl_glos.seen = *dgl_glos.lost;
        dgl_glos.resyncs++;
        engine_resync();                /* nothing the HAL knew of the registers holds */
        dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG;
    }
}

void dgl_glos_close(void)
{
    if (!dgl_glos.on)
        return;
    dgl_glos.on = 0;
    free_all();
    call(GLOS_DRAW_GPU_UNLOCK, 0);
}

/* The sizes a window can have: DOS-GL's usual ones that fit in the desktop's
   work area (the screen less the task bar) with a frame and a title bar. */
int dgl_glos_modes(DGLMode *out, int max)
{
    static const int sizes[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 400 },
                                    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 } };
    struct glos_screen scr;
    unsigned i;
    int n = 0;
    memset(&scr, 0, sizeof scr);
    scr.size = sizeof scr;
    if (caps() == 0 || call(GLOS_DRAW_SCREEN, &scr) != 0)
        return 0;
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        DGLMode d;
        if (sizes[i][0] > scr.work_w - 8 || sizes[i][1] > scr.work_h - 28)
            continue;
        memset(&d, 0, sizeof d);
        d.width = d.display_width = sizes[i][0];
        d.height = d.display_height = sizes[i][1];
        d.color_bits = 16;
        d.pitch_px = dgl_pitch_for(sizes[i][0]);
        d.can_double_buffer = 1;
        d.max_depth_bits = 16;
        if (out && n < max)
            out[n] = d;
        n++;
    }
    return n;
}

#else   /* the rig and the host: no GLOS */

int dgl_glos_modes(DGLMode *out, int max) { (void)out; (void)max; return 0; }
unsigned int dglGetGlosWindow(void) { return 0; }

int dgl_glos_want(void) { return 0; }
int dgl_glos_open(dgl_glos_layout *l) { (void)l; return -1; }
void dgl_glos_show(uint32_t off) { (void)off; }
void dgl_glos_visible(void) { }
void dgl_glos_slow(void) { }
void dgl_glos_close(void) { }

#endif
