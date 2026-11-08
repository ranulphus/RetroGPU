/* glos.c - DOS-GL in a window on a GLOS desktop (GLOS PRD §9.7, §10.3; GLOS
 * docs/milestones-m8.md step D).
 *
 * When the program runs under GLOS (DPMI 0A00h "GLOS") and GLOS says a GL
 * program can have a window there (GLOS_CAP_GLWIN: a Matrox card drawing a
 * 16-bit desktop), dglInit makes the program a GLOS app and opens a window of
 * the context's size instead of setting a video mode. The colour, depth and
 * texture memory are blocks of VRAM that GLOS gives the app; the program
 * draws into them with the engine from its own copy of the HAL, as on DOS,
 * and sees no other memory of the card. A swap tells GLOS which colour
 * buffer the window shows now (GLOS_FN_WINVRAM): nothing is copied by the
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
 * Without GLOS, without GLOS_CAP_GLWIN, or with DGL_FULLSCREEN set, nothing
 * here runs and the program takes the screen as it always has (under GLOS,
 * as a session). */
#include "dgl.h"
#include "../gl/gl_state.h"
#include "mga/mmio.h"
#include <stdlib.h>
#include <string.h>

dgl_glos_state dgl_glos;

#if defined(__DJGPP__) && !defined(DGL_RIG)
#define GLOS_KERNEL                     /* (the API's numbers and structures only: not libglos) */
#include <glos/api.h>                  /* (GLOS's, from its checkout: GLOS in config.mk) */
#include <dpmi.h>
#include <sys/nearptr.h>

extern char **__crt0_argv;

static struct {
    unsigned long off;
    unsigned short sel;
} entry;
static int found;                       /* 1 GLOS, -1 none */
static unsigned blocks[8], nblocks;
static unsigned window;

/* Function fn (EAX) with EBX, ECX, EDX and ESI: 0, or the GLOS_E_ code. EAX
   and EBX come back in *a and *b. */
static int call(unsigned fn, unsigned *a, unsigned *b, unsigned c, unsigned d, unsigned si)
{
    unsigned char failed;
    *a = fn;
    __asm__ __volatile__("lcall *%[e]\n\tsetc %[f]"
                         : "+a"(*a), "+b"(*b), "+c"(c), "+d"(d), "+S"(si), [f] "=m"(failed)
                         : [e] "m"(entry)
                         : "edi", "cc", "memory");
    return failed ? (int)*a : 0;
}

static int fn(unsigned f, unsigned b, unsigned c, unsigned d)
{
    unsigned a;
    return call(f, &a, &b, c, d, 0);
}

static unsigned caps(void)
{
    __dpmi_paddr p;
    unsigned a, b = 0;
    if (!found) {
        found = -1;
        if (__dpmi_get_vendor_specific_api_entry_point("GLOS", &p) == 0) {
            entry.off = p.offset32;
            entry.sel = p.selector;
            found = 1;
        }
    }
    if (found < 0 || call(GLOS_FN_VERSION, &a, &b, 0, 0, 0) != 0 || a < 0x10001u)
        return 0;
    return b;
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
    return (caps() & GLOS_CAP_GLWIN) != 0;
}

static void free_all(void)
{
    unsigned a, b;
    while (nblocks) {
        b = 1;
        call(GLOS_FN_VRAM, &a, &b, blocks[--nblocks], 0, 0);
    }
    if (window)
        fn(GLOS_FN_CLOSE, window, 0, 0);
    window = 0;
}

static int block(uint32_t min, uint32_t max, uint32_t *off, uint32_t *bytes)
{
    unsigned a, b = 0;
    if (nblocks == sizeof blocks / sizeof blocks[0] || call(GLOS_FN_VRAM, &a, &b, min, max, 4096) != 0)
        return -1;
    blocks[nblocks++] = a;
    *off = a;
    if (bytes)
        *bytes = b;
    return 0;
}

/* 0 in a window; 1 when GLOS won't have the program as an app (it runs
   full-screen, as a session); -1 on an error. */
int dgl_glos_open(dgl_glos_layout *l)
{
    static struct glos_gpu g;
    volatile struct glos_gpu_share *sh;
    struct glos_window w;
    const char *name = __crt0_argv && __crt0_argv[0] ? __crt0_argv[0] : "DOS-GL", *e;
    unsigned a, i;
    uint32_t colour = (uint32_t)l->pitch_px * (uint32_t)l->height * 2u, kb, b;
    unsigned why = GLOS_API_VERSION;
    int r = call(GLOS_FN_INIT, &a, &why, 0, 0, 0);
    if (r && !(r == GLOS_E_INIT && why == GLOS_INIT_ALREADY))
        return 1;                       /* not to be an app (a session's, a child's): the screen, as on DOS */
    memset(&g, 0, sizeof g);
    g.size = sizeof g;
    if ((r = fn(GLOS_FN_GPU, (unsigned)&g, 0, 0)) != 0) {
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
    w.w = l->width;
    w.h = l->height;
    w.flags = GLOS_WIN_FIXED | GLOS_WIN_ANYWHERE;
    if ((e = getenv("DGL_TITLE")) != NULL && *e)
        name = e;
    else {
        for (e = name; *e; e++)
            if (*e == '\\' || *e == '/' || *e == ':')
                name = e + 1;
    }
    for (i = 0; i < sizeof w.title - 1 && name[i] && name[i] != '.'; i++)
        w.title[i] = name[i];
    if ((r = fn(GLOS_FN_WINDOW, (unsigned)&w, 0, 0)) != 0) {
        dgl_set_error("GLOS made no %dx%d window (error %d)", l->width, l->height, r);
        return -1;
    }
    window = w.id;
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
    (void)a;
    return 0;
}

unsigned int dglGetGlosWindow(void) { return dgl_glos.on ? window : 0; }

void dgl_glos_show(uint32_t off)
{
    if (window)
        fn(GLOS_FN_WINVRAM, window, off, (unsigned)dgl_ctx.pitch_px * 2u);
}

void dgl_glos_visible(void)
{
    if (window)
        fn(GLOS_FN_SHOW, window, 1, 0);
}

void dgl_glos_slow(void)
{
    unsigned a, b = 0;
    if (!*dgl_glos.held)
        call(GLOS_FN_GPU_LOCK, &a, &b, 0, 0, 0);
    if (*dgl_glos.lost != dgl_glos.seen) {
        dgl_glos.seen = *dgl_glos.lost;
        dgl_glos.resyncs++;
        engine_resync();                /* nothing the HAL knew of the registers holds */
        dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG;
    }
}

void dgl_glos_close(void)
{
    unsigned a, b = 0;
    if (!dgl_glos.on)
        return;
    dgl_glos.on = 0;
    free_all();
    call(GLOS_FN_GPU_UNLOCK, &a, &b, 0, 0, 0);
}

/* The sizes a window can have: DOS-GL's usual ones that fit on the desktop
   with a frame, a title bar and the task bar. */
int dgl_glos_modes(DGLMode *out, int max)
{
    static const int sizes[][2] = { { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 400 },
                                    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 } };
    struct glos_window scr;
    unsigned a, b, i;
    int n = 0;
    memset(&scr, 0, sizeof scr);
    scr.size = sizeof scr;
    b = (unsigned)&scr;
    if (call(GLOS_FN_SCREEN, &a, &b, 0, 0, 0) != 0)
        return 0;
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        DGLMode d;
        if (sizes[i][0] > scr.w - 8 || sizes[i][1] > scr.h - 48)
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
