/* dgl.h - DOS-GL internals shared by src/dgl and src/gl. */
#ifndef DGL_INTERNAL_H
#define DGL_INTERNAL_H
#include <GL/dosgl.h>
#include "rg/device.h"

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

/* The device the context is on (context.c; rg/device.h), chosen at the
 * first dglInit and kept. */
extern const rg_device *dgl_dev;
void dgl_mode_out(DGLMode *d, const rg_mode *m);   /* device.c: a device's mode as DOS-GL's */

/* crash.c: fault/exit teardown (FR-DBG-7..9). */
void dgl_crash_install(void);
void dgl_teardown(void);                  /* the device closed (engine reset, text mode); safe to call twice */
void dgl_note_exit(void);                 /* DGL-EXIT if DGL-START was printed */
#ifdef MGA_REGCRC
void dgl_regcrc(const char *at);          /* REGCRC=1: a DGL-REGCRC line (context.c; docs/b1.md) */
#else
#define dgl_regcrc(at) ((void)0)
#endif
void dgl_snap_init(void);                 /* snap.c: read DGL_SNAP, DGL_SNAPDIR */
void dgl_snap_frame(unsigned long swap);  /* capture if swap is listed */
/* ftrace.c: DGL_FRAMES, a line per frame written at exit. */
extern int dgl_ftrace_on;
extern uint32_t dgl_ftrace_tex_us, dgl_ftrace_drain_us;   /* this frame's, so far */
void dgl_ftrace_init(void);
void dgl_ftrace_counts(void);             /* at a swap's start */
void dgl_ftrace_frame(uint32_t t0);       /* at its end (t0: its start) */
void dgl_ftrace_write(void);              /* at exit (not on a fault) */
/* DGL_FRAMES: the time spent in texture work, a frame at a time. */
#define FT_START() uint32_t ft0 = dgl_ftrace_on ? sys_time_us() : 0
#define FT_END() do { if (dgl_ftrace_on) dgl_ftrace_tex_us += sys_time_us() - ft0; } while (0)

/* emit.c: install the hardware sinks for the vertex stream. */
void dgl_emit_install(void);

/* Context state (context.c): GL's side; the buffers are the device's. */
typedef struct {
    int      active;
    int      width, height;
    int      double_buffer, depth_bits, vsync;
    int      draw_front, read_front;      /* glDrawBuffer/glReadBuffer chose GL_FRONT */
    unsigned window;                      /* a GLOS window's handle, 0 full screen */
} dgl_context;
int  dgl_present_front(void);             /* scaled, single-buffered or GL_FRONT: show the render buffer now (1 if
                                             the device did; 0 in modes that are not scaled) */
extern dgl_context dgl_ctx;

void dgl_retarget(void);                  /* point the engine at dgl_ctx.draw_front's buffer */
void dgl_yield_maybe(int engine);         /* long work: the program's wait hook, at most every 4 ms (engine:
                                             in work that uses the engine, taken back after it) */

/* glos.c: in a window on a GLOS desktop (GLOS's PRD §9.7, §10.3); the window
 * itself is the device's (rg_device's open_glos). */
int  dgl_glos_want(void);                 /* GLOS can show this program's GL in a window (and DGL_FULLSCREEN isn't set) */
int  dgl_glos_modes(DGLMode *out, int max);       /* the sizes a window can have on the desktop */
/* Before each piece of work with the engine: GLOS's lock, and the device told
 * if someone else had the engine (rg/device.h's rg_lock). */
#define DGL_GPU_ENTER() RG_GPU_ENTER()
/* Where long work without the engine begins (reading pixels back, converting textures, between frames): GLOS may
   give the engine to the desktop meanwhile; the next DGL_GPU_ENTER takes it back. No engine work until then. */
#define DGL_GPU_QUIET() RG_GPU_QUIET()

#endif
