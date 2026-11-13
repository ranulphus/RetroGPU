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
    uint32_t heap_kb;               /* the drawing memory's cap (DGL_TEXHEAP_KB), 0 none */
    void (*wait_hook)(void);        /* run in long waits for the engine and before a retrace wait (or NULL) */
} rg_open;

/* What the device gave. */
typedef struct {
    int width, height, pitch_px;
    int double_buffer, depth_bits, vsync;
    uint32_t mem_start, mem_end;    /* the memory left for drawing (textures): alloc's */
    uint32_t vram_bytes;
    int display_width, display_height;
    const char *fit;                /* "native", "zoom", "integer", "fill", "aspect"; "window" */
} rg_opened;

/* Memory the device keeps for the drawing back end (VRAM on a card). */
typedef struct { uint32_t off, bytes; } rg_mem;

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

typedef struct {
    uint32_t vram_bytes;            /* all of it */
    int max_color_bits;
    int windows;                    /* can draw in a GLOS window */
} rg_caps;

typedef struct rg_device {
    const char *name;               /* DGL_DEVICE's: "mga" */

    /* Finding and capabilities */
    int  (*probe)(void);                            /* 0: it can run on this PC (once; later calls repeat the answer) */
    void (*caps)(rg_caps *c);
    int  (*modes)(rg_mode *out, int max);           /* its full-screen modes, with its rules */

    /* Opening, showing, closing */
    int  (*open)(const rg_open *o, rg_opened *out); /* full screen, cleared: 0, or -1 with rg_error set */
    int  (*open_glos)(const rg_open *o, rg_opened *out);  /* in a GLOS window: 0; 1 not possible here; -1 */
    void (*close)(void);                            /* every way out, the fault path's too: safe twice */
    void (*swap)(void);                             /* show the finished back buffer (a flip, a scaled
                                                       present, a GLOS window's), drawing to the other */
    void (*present_front)(void);                    /* scaled modes: show what was drawn into the front now */
    void (*draw_buffer)(int front);                 /* where drawing goes: the shown buffer (1) or the hidden */

    /* Memory */
    void (*mem_reset)(uint32_t start, uint32_t end);
    int  (*alloc)(uint32_t bytes, rg_mem *m);       /* 0, or -1 when there is no room */
    void (*free)(const rg_mem *m, int busy);        /* busy: queued drawing may still read it: kept to the next sync */
    uint32_t (*mem_used)(void);

    /* The engine's order, and its owner */
    int  (*idle)(void);                             /* 1: nothing queued or drawing */
    int  (*sync)(void);                             /* everything drawn: 0, or -1 timed out */
    void (*enter)(void);                            /* GLOS's GPU lock taken, before work with the device */
    void (*quiet)(void);                            /* no device work for a while: the lock may go */

    /* Reading and diagnostics */
    int  (*read)(int front, int x, int y, int w, int h, uint8_t *rgba, int stride);   /* RGBA8, rows top down */
    void (*info)(rg_info *i);
    void (*crash)(void);                            /* the fault report's device lines (DGL_ERR's) */
} rg_device;

/* device.c: the devices built in, and the choice. */
const rg_device *rg_choose(const char *want);       /* want: DGL_DEVICE (NULL or "": the first that probes) */
const rg_device *rg_choose_from(const rg_device *const *list, int n, const char *want);   /* (tests) */
const char *rg_error(void);                         /* why the last open, or the choice, failed */
void rg_set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The log: lines a device prints (its DGL- lines today) go to the program's
 * logger, set by the GL layer at its start; none until then. */
void rg_set_log(void (*fn)(const char *line));
void rg_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
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
