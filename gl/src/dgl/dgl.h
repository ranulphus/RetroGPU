/* dgl.h - DOS-GL internals shared by src/dgl and src/gl. */
#ifndef DGL_INTERNAL_H
#define DGL_INTERNAL_H
#include <GL/dosgl.h>
#include "mga/hal.h"

/* Logging over COM1 through the HAL's unbuffered serial driver (FR-DBG-1/2).
 * Lines start "DGL-". Levels above DGL_LOG_LEVEL compile out (FR-DBG-3):
 * 0 errors (always), 1 warnings, 2 info, 3 debug. */
#ifndef DGL_LOG_LEVEL
#  ifdef DGL_DEBUG
#    define DGL_LOG_LEVEL 3
#  else
#    define DGL_LOG_LEVEL 1
#  endif
#endif
void dgl_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern unsigned long dgl_stub_calls;      /* log.c */
#define DGL_ERR(...)  dgl_logf(__VA_ARGS__)
#define DGL_WARN(...) do { if (DGL_LOG_LEVEL >= 1) dgl_logf(__VA_ARGS__); } while (0)
#define DGL_INFO(...) do { if (DGL_LOG_LEVEL >= 2) dgl_logf(__VA_ARGS__); } while (0)
#define DGL_DBG(...)  do { if (DGL_LOG_LEVEL >= 3) dgl_logf(__VA_ARGS__); } while (0)

/* Errors reported by dglGetErrorString. */
void dgl_set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* device.c: discovery (once) and the filtered mode list. */
int dgl_discover(void);                   /* 0 = a supported card was found */
void dgl_note_vram(uint32_t bytes);       /* the probed VRAM size, when larger than VBE's */
const DGLMode *dgl_mode_for(int width, int height, const mga_mode_plan **plan);
uint32_t dgl_known_vram(void);             /* the most VRAM known (probe, DGL_VRAM_KB, BIOS) */
int dgl_pitch_for(int width);             /* pixels, engine rules */

/* crash.c: fault/exit teardown (FR-DBG-7..9). */
void dgl_crash_install(void);
void dgl_teardown(void);                  /* reset engine, text mode; safe to call twice */
void dgl_note_exit(void);                 /* DGL-EXIT if DGL-START was printed */
void dgl_snap_init(void);                 /* snap.c: read DGL_SNAP, DGL_SNAPDIR */
void dgl_snap_frame(unsigned long swap);  /* capture if swap is listed */
/* ftrace.c: DGL_FRAMES, a line per frame written at exit. */
extern int dgl_ftrace_on;
extern uint32_t dgl_ftrace_tex_us, dgl_ftrace_drain_us;   /* this frame's, so far */
void dgl_ftrace_init(void);
void dgl_ftrace_counts(void);             /* at a swap's start */
void dgl_ftrace_frame(uint32_t t0);       /* at its end (t0: its start) */
void dgl_ftrace_write(void);              /* at exit (not on a fault) */

/* emit.c: install the hardware sinks for the vertex stream. */
void dgl_emit_install(void);
void dgl_emit_no_warp(void);           /* (emit.c) the CPU's setup from now on: GLOS windows */

/* Context state (context.c). */
typedef struct {
    int      active;
    int      width, height, pitch_px;
    int      double_buffer, depth_bits, vsync;
    uint32_t front_off, back_off, z_off, heap_off, vram_bytes;
    int      front_is_a;                  /* which of the two colour buffers is shown */
    int      draw_front, read_front;      /* glDrawBuffer/glReadBuffer chose GL_FRONT */
    /* Scaled modes (v1.2): GL draws into one render buffer (front_off, at
     * pitch_px) and each swap scales it into the hidden display buffer,
     * then flips the display. Zoomed modes draw into the display buffers
     * and the chip doubles lines and pixels. */
    int      scaled, zoomed, filter;
    uint32_t disp_off[2];
    int      disp_front, disp_pitch_px;
    mga_mode_plan plan;
} dgl_context;
void dgl_present_front(void);             /* scaled, single-buffered or GL_FRONT: show the render buffer now */
extern dgl_context dgl_ctx;

uint32_t dgl_color_off(int front);        /* VRAM offset of the shown (1) or hidden (0) buffer */
void dgl_retarget(void);                  /* point the engine at dgl_ctx.draw_front's buffer */
void dgl_yield_maybe(int engine);         /* long work: the program's wait hook, at most every 4 ms (engine:
                                             in work that uses the engine, taken back after it) */

/* glos.c: in a window on a GLOS desktop (GLOS's PRD §9.7, §10.3). */
typedef struct {
    int width, height, pitch_px, double_buffer, depth;          /* in */
    uint32_t front_off, back_off, z_off, heap_off, heap_end;    /* out: blocks of VRAM GLOS gave */
    uint32_t vram_bytes;
} dgl_glos_layout;
typedef struct {
    int on;                               /* the context is in a GLOS window */
    volatile unsigned *held, *lost;       /* GLOS's page: the engine is the program's; how often others had it */
    volatile unsigned *quiet;             /* ... and ours: no engine work now, the lock may go (glos.c) */
    unsigned seen, resyncs;
    volatile void *share;
} dgl_glos_state;
extern dgl_glos_state dgl_glos;
int  dgl_glos_want(void);                 /* GLOS can show this program's GL in a window (and DGL_FULLSCREEN isn't set) */
int  dgl_glos_open(dgl_glos_layout *l);   /* the program an app, the chip from GLOS, a window, its VRAM: 0, or -1 */
void dgl_glos_show(uint32_t off);         /* the window shows this colour buffer (a swap) */
void dgl_glos_visible(void);
void dgl_glos_slow(void);
void dgl_glos_close(void);
int  dgl_glos_modes(DGLMode *out, int max);       /* the sizes a window can have on the desktop */
/* Before each piece of work with the engine: the lock, and the HAL told if someone else had the engine. */
#define DGL_GPU_ENTER() do { if (dgl_glos.on) { *dgl_glos.quiet = 0; \
                                 if (!*dgl_glos.held || *dgl_glos.lost != dgl_glos.seen) dgl_glos_slow(); } } while (0)
/* Where long work without the engine begins (reading pixels back, converting textures, between frames): GLOS may
   give the engine to the desktop meanwhile; the next DGL_GPU_ENTER takes it back. No engine work until then. */
#define DGL_GPU_QUIET() do { if (dgl_glos.on) *dgl_glos.quiet = 1; } while (0)

#endif
