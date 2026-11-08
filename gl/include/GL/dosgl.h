/* dosgl.h - DOS-GL's context and display API (PRD §5.5, D7).
 *
 * Draft until M1, where it is frozen. Everything here is callable from C
 * (DJGPP) and warning-clean under -Wall -Wextra -Werror, as ClassiCube's
 * build requires. */
#ifndef DOSGL_H
#define DOSGL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width, height;      /* any enumerated mode; 0 = 640x480 */
    int color_bits;         /* 16 only in v1.0 (0 = 16) */
    int depth_bits;         /* 0 or 16 */
    int refresh_hz;         /* 0 = BIOS default; VBE 3.0 only */
    int double_buffer;      /* nonzero for double buffering */
    int vsync;              /* nonzero to sync swaps to retrace */
    int scale_filter;       /* scaled modes: 0 = default (nearest), 1 nearest, 2 bilinear (v1.2) */
} DGLConfig;

typedef struct {
    int width, height;
    int color_bits;
    int max_depth_bits;     /* 16, or 0 if VRAM cannot fit a Z buffer */
    int can_double_buffer;
    int vbe_mode;           /* the BIOS mode number */
    int pitch_px;           /* the pitch DOS-GL will program */
    /* v1.2: sizes the BIOS lacks are drawn at their own size and shown in a
       larger BIOS mode, scaled by the drawing engine at every swap (scaled)
       or through the chip's line and pixel doubling (zoomed, DGL_ZOOM=1). */
    int scaled, zoomed;
    int display_width, display_height;   /* the BIOS mode on the monitor */
} DGLMode;

typedef struct {
    const char   *chip_name;        /* e.g. "MGA-G450" */
    unsigned int  device_id, revision;
    unsigned long vram_bytes;
    unsigned long fb_phys, mmio_phys, iload_phys;
    unsigned int  fifo_depth;
    int           width, height;    /* current mode after dglInit */
    int           emulated;         /* nonzero under 86Box */
    /* v1.2, after dglInit: the BIOS mode shown and where the picture lands
       in it; fit is "native", "zoom", "integer", "fill" or "aspect". */
    int           display_width, display_height;
    int           picture_x, picture_y, picture_width, picture_height;
    const char   *fit;
} DGLDeviceInfo;

typedef struct {
    unsigned long frames, triangles, swaps, texture_bytes, fifo_stalls;
    unsigned long stub_calls;       /* calls to GL functions DOS-GL only stubs (v1.1) */
    unsigned long present_us;       /* scaled modes: time spent scaling frames (v1.2) */
    unsigned long wait_hooks;       /* times the wait hook ran (v1.3) */
} DGLStats;

/* The API's version: 0x0103 is 1.3 (DOS-GL 0.3), which added dglSetWaitHook;
   0x0104 the window on a GLOS desktop and dglGetGlosWindow. */
#define DGL_API_VERSION 0x0104

/* The library's version string, e.g. "DOS-GL 0.3 (<build>)". */
const char *dglVersion(void);

/* Enumeration: callable before dglInit, after device discovery. Returns the
   number of usable modes and fills up to max_modes entries, smallest first:
   the BIOS's 16-bit modes and (v1.2) 320x200, 320x240, 400x300, 512x384 and
   640x512 shown scaled or zoomed in a larger one. */
int  dglEnumModes(DGLMode *modes, int max_modes);

int  dglInit(const DGLConfig *cfg);     /* 0 on success */
void dglShutdown(void);
void dglSwapBuffers(void);
void dglSetVSync(int enabled);
/* v1.3: fn(arg) runs while dglSwapBuffers waits for the drawing engine to
 * finish the frame (about once a millisecond), and once before it waits for
 * the retrace. A program with cooperative threads (SDL's on DOS) yields
 * there, so its audio thread keeps up while the chip works. The hook must
 * not call GL. NULL removes it. */
void dglSetWaitHook(void (*fn)(void *arg), void *arg);
const char *dglGetErrorString(void);
const DGLDeviceInfo *dglGetDeviceInfo(void);
const DGLStats *dglGetStats(void);

/* The address of a GL function by name: every GL 1.1 function and the
   extension functions DOS-GL implements; NULL for anything else. Whether an
   extension may be used is still decided by glGetString(GL_EXTENSIONS). (v1.1) */
void *dglGetProcAddress(const char *name);

/* v1.4: under GLOS, on a desktop that can show GL in a window (a Matrox card
   at 16 bits a pixel; DGL_FULLSCREEN=1 asks for the screen instead), dglInit
   opens a window of the context's size rather than setting a mode, and the
   program becomes a GLOS app. The window's GLOS id (for GLOS's own calls: its
   title, its events), or 0 when the context is full-screen or there is none. */
unsigned int dglGetGlosWindow(void);

/* Write the buffer being drawn (the next frame, when double-buffered) to
   path as a binary PPM; 0 on success. For tests and bug reports. (v1.1) */
int dglSnapshot(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* DOSGL_H */
