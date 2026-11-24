/* backend.h - Glide's drawing back end: the card under the Glide 2 API.
 *
 * The API (glide/src/glide, tex, lfb) keeps the Glide state mirror (mg.st), the texture records, the LFB's shadow
 * and pixel pipeline and the shared maths; a back end turns that state into drawing on one family of cards. One is
 * chosen when the runtime finds its card (mg_device_open) and kept in mg_be. The Matrox back end is the runtime's
 * own code, moved behind this table without changing a register write (glide/tools/conform/regcrc.py checks it);
 * other vendors' cards (the Riva TNT2, the ViRGE/DX) are back ends of their own beside it (the plan of 2026-11-19:
 * docs/tnt2.md T5, docs/virge.md V5). */
#ifndef MG_BACKEND_H
#define MG_BACKEND_H
#include "glide/entry.h"
#include <stdint.h>

typedef struct mg_backend {
    const char *name;                   /* "mga", "tnt2" */

    /* The card: found and mapped (0), or -1 with the reason logged. Called by grGlideInit and the board queries,
     * so more than once. */
    int  (*probe)(void);

    /* grSstWinOpen once the resolution is known (w x h, the game's size): the mode, the buffers' layout, the
     * texture heap, the engine, the first clears, the display start, gamma and the fault hooks
     * (mg_hooks_install); sets mg.open and the mg fields the API reads (width, height, pitch_px, bpp, buf_off,
     * aux_off, zbits, has_aux, nbuffers, rw, rh, front, back). FXTRUE when open. */
    FxBool (*win_open)(int w, int h, GrColorFormat_t color_format, GrOriginLocation_t origin, int ncolor, int naux);
    void (*win_close)(void);            /* grSstWinClose after its checks: engine idle, text mode, mg.open = 0 */
    void (*fault)(void);                /* a fault with the window open: no waiting, the engine reset, text mode */
    void (*exit_restore)(void);         /* the game left without grGlideShutdown: drain, text mode */

    /* The engine and the display. */
    void (*sync)(void);                 /* everything queued drawn */
    int  (*busy)(void);                 /* the engine drawing (grSstIsBusy) */
    int  (*in_vblank)(void);
    unsigned (*vcount)(void);           /* the scan line being shown */
    void (*set_ramp)(const uint8_t ramp[256]);  /* the DAC's gamma ramp */

    /* Drawing: the API's validated state in mg.st, positions in the game's window. */
    void (*validate)(void);             /* the target, clip and raster state to the card (mg.dirty) */
    void (*clear)(int x0, int y0, int x1, int y1, GrColor_t color, FxU16 depth);   /* (card pixels, validated) */
    void (*swap)(int swap_interval);    /* grBufferSwap: drain, wait, show mg.back (the API turns the buffers) */
    void (*present_front)(void);        /* scaled fits: show what was drawn into the front buffer */
    void (*tri)(const GrVertex *a, const GrVertex *b, const GrVertex *c);

    /* A texture level the texture manager converted (16-bit texels: 565, 1555 or 4444 as tex_convert_level's
     * HW_TW16/15/12, hw_w x hw_h, rows hw_w apart) into video memory at org, in the card's layout: the Matrox's
     * rows as they are, the TNT2's swizzled. The engine is done with the old texels (the caller synced). */
    void (*tex_write)(uint32_t org, const uint16_t *texels, int hw_w, int hw_h);
    uint32_t tex_align;                 /* where a texture's block starts: a multiple of this (0: the heap's 64) */
    int tex_square;                     /* 1: each level's slot square (the larger side), padded as the texture repeats */
    int tex_no565;                      /* 1: no 565 texels (opaque levels as 1555 with the alpha bit set) */
    int tex_ordered;                    /* 1: tex_write is ordered with the queued drawing (it syncs itself when it
                                         * can't be): no engine sync before it */
    int tex_min_w;                      /* a level's shortest row in video memory, in texels (0: 8); the Radeon's
                                         * rows are 32 bytes at least: 16 */
} mg_backend;

extern const mg_backend *mg_be;         /* the back end of the card found (sst.c) */
extern volatile uint8_t *mg_vram;       /* its video memory mapped (texture records, the LFB): set after probe */

extern const mg_backend mg_mga_backend; /* the Matrox cards (backend/mga) */
extern const mg_backend mg_tnt2_backend; /* the Riva TNT and TNT2 (backend/tnt2) */
extern const mg_backend mg_virge_backend; /* the S3 ViRGE/DX and /GX (backend/virge) */
extern const mg_backend mg_r128_backend; /* the ATI Rage 128 (backend/r128) */
extern const mg_backend mg_radeon_backend; /* the ATI Radeon, R100 family (backend/radeon) */

#endif
