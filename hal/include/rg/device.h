/* device.h - RetroGPU's device level (gl/docs/backend-interface.md, B0 v2;
 * gl/docs/b1.md): one table of functions a graphics device gives, for GL
 * today and for Glide and GLOS's desktop later. A device is the card (or the
 * CPU, for the software one) as GL's layer sees it: found, its modes, its
 * buffers opened and shown, its memory, its engine's order, read back, torn
 * down. What is drawn, and how, is the drawing back end's (GL's: gl/src/
 * backend), which knows its own device's private side.
 *
 * Devices: "mga" (the Matrox HAL behind the table: hal/src/rg/mga.c);
 * later "soft" (B2/B3) and "virge" (docs/virge.md V3). rg_choose picks one by
 * DGL_DEVICE, or the first whose probe succeeds. */
#ifndef RG_DEVICE_H
#define RG_DEVICE_H
#include "mga/types.h"

/* A full-screen mode as a device offers it (DOS-GL's DGLMode, field for field). */
typedef struct {
    int width, height;              /* what is drawn */
    int color_bits;
    int max_depth_bits;             /* 16, or 0 when no depth buffer fits */
    int can_double_buffer;
    int bios_mode;                  /* the mode the monitor shows */
    int pitch_px;
    int scaled, zoomed;             /* drawn smaller and scaled at each swap; doubled by the chip */
    int display_width, display_height;
} rg_mode;

/* What a program asks for. */
typedef struct {
    int width, height;
    int color_bits, depth_bits;
    int double_buffer, vsync;
    int scale_filter;               /* scaled modes: 0 nearest, 1 bilinear */
    uint32_t heap_kb;               /* the memory left after the buffers capped (DGL_TEXHEAP_KB), 0 none */
    /* The user's hooks (each may be NULL): */
    void (*wait)(void);             /* before a retrace wait (the program's wait hook) */
    void (*yield)(void);            /* in long CPU work, such as reading a frame back (at most every few ms) */
    void (*on_sync)(void);          /* each completed sync: queued drawing has read everything it was given */
} rg_open;

/* What the device gave. */
typedef struct {
    int width, height, pitch_px;
    int double_buffer, depth_bits, vsync;
    uint32_t mem_start, mem_end;    /* the video memory left after the buffers (textures) */
    unsigned window;                /* a GLOS window's handle, 0 full screen */
    uint32_t vram_bytes;
    int display_width, display_height;
    const char *fit;                /* "native", "zoom", "integer", "fill", "aspect"; "window" */
} rg_opened;

/* dglGetDeviceInfo's fields, and Glide's and GLOS's later. */
typedef struct {
    const char *chip_name;
    unsigned device_id, revision;
    uint32_t vram_bytes, fb_phys, mmio_phys, iload_phys;
    unsigned fifo_depth;
    int emulated;
    int display_width, display_height;
    int picture_x, picture_y, picture_width, picture_height;
    const char *fit;
} rg_info;

/* The engine's own counters (the drawing back end counts triangles). */
typedef struct {
    uint32_t resets, timeouts, fence_stuck;   /* engine_* */
    uint32_t present_us;                       /* scaled modes: time spent scaling frames */
    uint32_t retrace_us;                       /* time spent waiting for the retrace */
} rg_stats;

/* GLOS's GPU lock (GLOS's API 2.0, its shared page), for any device that
 * draws in a GLOS window with its own engine: before each piece of work with
 * the engine, RG_GPU_ENTER takes the lock if the program hasn't got it, and
 * if someone else had the engine since, the device forgets what it believed
 * of the registers and the user is told (on_lost: GL writes its state again).
 * RG_GPU_QUIET where long work without the engine begins. (DOS-GL's
 * DGL_GPU_ENTER and DGL_GPU_QUIET until B1, inline as they were.) */
typedef struct {
    int on;                               /* drawing in a GLOS window */
    volatile unsigned *held, *lost;       /* GLOS's page: the engine is the program's; how often others had it */
    volatile unsigned *quiet;             /* ... and ours: no engine work now, the lock may go */
    unsigned seen, resyncs;
    volatile void *share;
    void (*slow)(void);                   /* the device's: take the lock, resync */
    void (*on_lost)(void);                /* the user's: its state written again */
} rg_gpu_lock;
extern rg_gpu_lock rg_lock;
#define RG_GPU_ENTER() do { if (rg_lock.on) { *rg_lock.quiet = 0; \
                                if (!*rg_lock.held || *rg_lock.lost != rg_lock.seen) rg_lock.slow(); } } while (0)
#define RG_GPU_QUIET() do { if (rg_lock.on) *rg_lock.quiet = 1; } while (0)

typedef struct {
    uint32_t vram_bytes;            /* all of it */
    int max_color_bits;
    int windows;                    /* can draw in a GLOS window */
} rg_caps;

/* What a swap disturbed, for its user to write again: the drawing target
 * (the engine's buffer and its access register), or the engine's drawing
 * state too (texture, blend and mask registers: a scaled present draws). */
#define RG_SWAP_TARGET 1
#define RG_SWAP_ENGINE 2

typedef struct rg_device {
    const char *name;               /* DGL_DEVICE's: "mga" */

    /* Finding and capabilities */
    int  (*probe)(void);                            /* 0: it can run on this PC (once; later calls repeat the answer) */
    void (*caps)(rg_caps *c);
    int  (*modes)(rg_mode *out, int max);           /* its full-screen modes, with its rules */

    /* Opening, showing, closing */
    int  (*open)(const rg_open *o, rg_opened *out); /* full screen, cleared: 0, or -1 with rg_error set */
    int  (*window_ok)(void);                        /* GLOS can give this device's drawing a window */
    int  (*window_modes)(rg_mode *out, int max);    /* the sizes such a window can have on the desktop */
    int  (*open_glos)(const rg_open *o, rg_opened *out);  /* in a GLOS window: 0; 1 not possible here; -1 */
    void (*close)(void);                            /* every way out, the fault path's too: safe twice */
    int  (*swap)(int vsync);                        /* show the finished back buffer (a flip, a scaled
                                                       present, a GLOS window's), drawing to the other:
                                                       RG_SWAP_ flags for what it disturbed */
    int  (*present_front)(void);                    /* scaled modes: show what was drawn into the front now */
    void (*draw_buffer)(int front);                 /* where drawing goes: the shown buffer (1) or the hidden */

    /* The engine's order, and its owner */
    int  (*idle)(void);                             /* 1: nothing queued or drawing */
    int  (*sync)(void);                             /* everything drawn: 0 (and rg_open's on_sync run), or -1
                                                       timed out */
    /* (GLOS's lock: rg_lock and RG_GPU_ENTER, above) */

    /* Reading and diagnostics */
    int  (*read)(int front, int x, int y, int w, int h, uint8_t *rgba, int stride);   /* RGBA8, rows top down */
    void (*info)(rg_info *i);
    void (*stats)(rg_stats *s);
    void (*crash)(void);                            /* the fault report's device lines (DGL_ERR's) */
} rg_device;

/* device.c: the devices built in, and the choice. */
const rg_device *rg_choose(const char *want);       /* want: DGL_DEVICE (NULL or "": the first that probes) */
const rg_device *rg_choose_from(const rg_device *const *list, int n, const char *want);   /* (tests) */
const rg_device *rg_window_device(void);            /* the device GLOS can give a window to, or NULL */
const char *rg_error(void);                         /* why the last open, or the choice, failed */
void rg_set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The log: lines a device prints (its DGL- lines today) go to the program's
 * logger, set by the GL layer at its start; none until then. */
void rg_set_log(void (*fn)(const char *line));
void rg_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rg_log_line(const char *line);                 /* one line, as it is */
/* Levels as DOS-GL's (dgl.h): 0 errors, 1 warnings (the default), 2 info,
 * 3 debug (DGL_DEBUG); lines above the level compile out. */
#ifndef RG_LOG_LEVEL
#  ifdef DGL_DEBUG
#    define RG_LOG_LEVEL 3
#  else
#    define RG_LOG_LEVEL 1
#  endif
#endif
#define RG_ERR(...)  rg_logf(__VA_ARGS__)
#define RG_WARN(...) do { if (RG_LOG_LEVEL >= 1) rg_logf(__VA_ARGS__); } while (0)
#define RG_INFO(...) do { if (RG_LOG_LEVEL >= 2) rg_logf(__VA_ARGS__); } while (0)
#define RG_DBG(...)  do { if (RG_LOG_LEVEL >= 3) rg_logf(__VA_ARGS__); } while (0)
extern const rg_device rg_mga_device;

#endif
