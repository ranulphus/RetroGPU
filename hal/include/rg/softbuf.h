/* softbuf.h - buffers in system memory for the software renderer
 * (gl/docs/b2.md): what the memory device (hal/src/rg/mem.c) gives, and B3's
 * software device after it, for a software drawing back end to draw into. */
#ifndef RG_SOFTBUF_H
#define RG_SOFTBUF_H
#include "rg/device.h"

typedef struct {
    int      active;
    int      width, height;
    int      bpp;                   /* 8 (RGB332), 16 (RGB565) or 32 (XRGB8888) */
    int      pitch;                 /* bytes a colour row */
    uint8_t *color[2];              /* A and B (B NULL single-buffered) */
    int      front_is_a;            /* A is shown */
    int      draw_front;            /* drawing goes into the shown buffer */
    uint8_t *depth;                 /* NULL: none */
    int      depth_bits, depth_pitch;
    uint8_t *stencil;               /* NULL: none */
    int      stencil_pitch;
    /* The drawing back end's, set after each open: draw what it has kept
       (B2.7's tiles), at a sync, a swap or a read; or forget it, when the
       buffers go. NULL: nothing is kept. */
    void   (*flush)(void);
    void   (*drop)(void);
} rg_softbuf;
extern rg_softbuf rg_sb;

/* The buffer drawing goes into now: the shown one (1) or the hidden. */
uint8_t *rg_sb_color(int front);

#endif
