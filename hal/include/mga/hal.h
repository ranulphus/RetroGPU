/* hal.h - shared Matrox HAL (PRD D3): device discovery, capabilities,
 * mode setting, drawing-engine control and trapezoid setup. */
#ifndef MGA_HAL_H
#define MGA_HAL_H
#include "mga/types.h"

typedef enum {
    MGA_FAMILY_NONE = 0,
    MGA_FAMILY_G100,
    MGA_FAMILY_G200,
    MGA_FAMILY_G400,      /* G400, G450 and G550 */
    MGA_FAMILY_G200E      /* server variants: reference only */
} mga_family;

typedef struct {
    /* Identity. */
    uint16_t   device_id;
    uint8_t    revision;
    uint8_t    bus, dev, fn;
    mga_family family;
    const char *name;
    uint32_t   subsys;          /* subsystem vendor:device */
    /* Apertures (physical) and PCI OPTION register. */
    uint32_t   fb_phys, fb_size;
    uint32_t   mmio_phys;
    uint32_t   iload_phys;
    uint32_t   option;
    uint32_t   vram_bytes;
    int        emulated;        /* 86Box unit tester present */
    /* Capabilities (PRD §5.3; plan §3). */
    uint8_t    fifo_depth, fifo_mask;
    uint8_t    max_mip_levels;  /* 1 (G100), 5 (G200), 11 (G400) */
    uint8_t    ar_bits;         /* AR0/2/4/5/6 width, signed: 18 (G100, G200), 22 (G400); 0 = unlimited */
    uint8_t    min_tex_dim;
    uint16_t   max_tex_size;
    unsigned   has_dwgsync:1, has_dstorg:1, has_ydstorg:1, zorg_ydst_relative:1,
               has_texctl2:1, has_alpha_blend:1, has_alpha_test:1, has_tlut:1,
               has_specular:1, has_decalblend:1, fog_textrap_only:1,
               alphasel_tex_always:1, blk_clear_ok:1, g400_clip_quirk:1,
               has_dual_tex:1,  /* G400: two texture maps, TDUALSTAGE combiner */
               texel_centre:1,  /* bilinear filtering samples at texel centres itself (G100, G200, G400: their
                                   silicon); otherwise from texel corners, and tex_adjust_coords shifts half a texel */
               textrap_empty_stall:1;  /* a TEXTURE_TRAP with a row whose span is empty and left of the clip stalls
                                   the engine (G100): the setup sends only rows that draw (trap_rows) */
} mga_chip;

extern mga_chip mga;                 /* the device in use */
extern volatile uint8_t *mga_fb;     /* mapped framebuffer (BAR0) */

/* pci.c / chip.c */
uint32_t mga_pci_read32(int bus, int dev, int fn, int reg);
void     mga_pci_write32(int bus, int dev, int fn, int reg, uint32_t v);
int      mga_find(mga_chip *c);      /* 0 on success */
int      mga_map(mga_chip *c);       /* maps MMIO + framebuffer */
void     mga_unmap(void);
const char *mga_family_name(mga_family f);
int      mga_detect_emulator(void);
void     mga_chip_caps(mga_chip *c);   /* fill capabilities from c->family */
/* The G400's clocks (clocks.c; gl/docs/g4path.md, C). mga_clocks_fast raises
 * them from the BIOS's first PInS record to the one Matrox's own driver uses
 * (system PLL 252 MHz), only where the PInS and the running clocks are the
 * ones seen on bench-g400; no engine context needed (the engine idle). 0:
 * raised, *saved holds what was there; 1: nothing changed (not this card, the
 * PInS or clocks don't match, already raised; mga_clocks_why says which); -1:
 * the PLL didn't lock, the BIOS's put back. mga_clocks_restore puts *saved
 * back (nothing if it isn't valid). GLOS calls them for its whole run; the
 * Matrox device only without GLOS (DGL_CLOCKS=0: never). */
typedef struct { uint32_t opt, opt3; uint8_t m, n, p, valid; } mga_clocks;
extern const char *mga_clocks_why;
int  mga_clocks_fast(mga_clocks *saved);
void mga_clocks_restore(mga_clocks *saved);
uint32_t mga_probe_vram(void);

/* vbe.c: modes via the card BIOS (VBE 2.0+). */
typedef struct {
    uint16_t mode, width, height, bpp, pitch_bytes;
    uint32_t lfb_phys;
    uint8_t  red_size, red_pos, green_size, green_pos, blue_size, blue_pos;
} mga_vbe_mode;
int  vbe_find_mode(int w, int h, int bpp, mga_vbe_mode *out);
int  vbe_enumerate(void (*cb)(const mga_vbe_mode *m, void *ctx), void *ctx);
int  vbe_set_mode(const mga_vbe_mode *m, int pitch_px, int *actual_pitch_px);
int  vbe_set_display_start(uint32_t byte_offset, int pitch_bytes, int bpp);
void vbe_set_text_mode(void);
int  vbe_version(void);
/* Any card's modes for the software renderer's full screen (B3, rg/vbe_soft.c): direct colour at 15/16/32 bits
   or packed pixels at 8, with a linear framebuffer or only the banked window (A at win_seg, win_kb long, moved in
   gran_kb steps by vbe_set_bank). The Matrox device keeps vbe_find_mode's linear-framebuffer modes. */
typedef struct {
    uint16_t mode, width, height, bpp, pitch_bytes;
    uint32_t lfb_phys;                  /* 0: banked only */
    uint16_t win_seg, win_kb, gran_kb;
    uint8_t  red_size, red_pos, green_size, green_pos, blue_size, blue_pos;
} vbe_any_mode;
int  vbe_enumerate_any(void (*cb)(const vbe_any_mode *m, void *ctx), void *ctx);
int  vbe_set_mode_any(const vbe_any_mode *m);   /* the linear framebuffer when it has one */
int  vbe_set_bank(unsigned bank);               /* window A, in gran_kb units */
uint32_t vbe_total_memory(void);   /* bytes, from the VBE info block (0 if unknown) */

/* The mode planner: how a requested size is shown with the modes this
 * card's BIOS offers (16 bpp means RGB565; only modes on the card's own
 * aperture). In order of preference:
 *   NATIVE   the BIOS has the size: draw straight into the display buffers;
 *   ZOOM     (MGA_PLAN_ZOOM) the BIOS has twice the size: draw into the
 *            display buffers at the BIOS pitch and turn on the chip's line
 *            and pixel doubling (vbe_set_zoom), same monitor signal;
 *   INTEGER  2x, 3x or 4x the size is a BIOS mode: draw into a render
 *            buffer and scale it up at every swap (engine_present);
 *   FILL     a CRT-era size (320x200, 640x200, 640x350, 640x400, 400x256,
 *            512x256): stretched to fill the smallest 4:3 mode, as a CRT
 *            showed it;
 *   ASPECT   anything else: the smallest mode that holds it, scaled
 *            evenly, centred with black bars;
 *   TOPLEFT  (MGA_PLAN_TOPLEFT, instead of scaling) the smallest mode that
 *            holds it, drawn 1:1 in its top-left corner.
 * MGA_PLAN_FORCE takes the scaled path (INTEGER at 1x) even when the BIOS
 * has the size: a test switch. dx, dy, dw, dh is where the picture lands
 * in the display mode. */
enum { MGA_FIT_NATIVE, MGA_FIT_ZOOM, MGA_FIT_INTEGER, MGA_FIT_FILL, MGA_FIT_ASPECT, MGA_FIT_TOPLEFT };
#define MGA_PLAN_ZOOM    1u
#define MGA_PLAN_TOPLEFT 2u
#define MGA_PLAN_FORCE   4u
typedef struct {
    int          w, h;             /* the requested size: what the program draws */
    mga_vbe_mode disp;             /* the BIOS mode shown */
    int          fit;              /* MGA_FIT_* */
    int          zoom;             /* 2 for MGA_FIT_ZOOM, else 1 */
    int          dx, dy, dw, dh;   /* the picture's rectangle in the display mode */
} mga_mode_plan;
int  mga_plan_from_list(const mga_vbe_mode *list, int n, int w, int h, int bpp, unsigned flags, mga_mode_plan *p);
int  vbe_plan_mode(int w, int h, int bpp, unsigned flags, mga_mode_plan *p);   /* 0, or -1: nothing fits */
const char *mga_fit_name(int fit);
int  mga_pow2_pitch(int w);        /* a render surface's pitch: the next power of two, at least 32 */
/* Line and pixel doubling for MGA_FIT_ZOOM: CRTC9's maxscan repeats lines
 * and XZOOMCTRL repeats pixels (factor 1, 2 or 4), after vbe_set_mode.
 * Undone by vbe_set_zoom(1) and before text mode. */
void vbe_set_zoom(int factor);

/* engine.c */
typedef struct {
    uint32_t color_off;    /* byte offset of the colour buffer in VRAM */
    uint32_t z_off;        /* byte offset of the depth buffer (0 = none) */
    int      pitch_px;     /* shared by colour and depth */
    int      bpp;          /* 16 or 32 (8 for 2D drawing only) */
    int      zbits;        /* 16 or 32 */
} mga_target;

void     engine_init(int pitch_px, int bpp);
/* Another program has used the engine since this copy of the HAL last did
 * (GLOS's GPU lock changing hands): forget what is known of the registers
 * and write the current target, clip and MACCESS again. */
void     engine_resync(void);
int      engine_sync(uint32_t timeout_us);     /* 0 ok, -1 timeout (engine reset) */
/* engine_sync when the engine changes hands: another HAL's WARP triangles
 * fenced too (the G400 family; GLOS's hand-overs). */
int      engine_take(uint32_t timeout_us);
int      engine_idle(void);                    /* 1 when the FIFO is empty and the engine idle (no wait) */
/* A soft reset of the drawing engine (and the WARPs): every drawing
 * register goes back to its reset value. The HAL writes back what it owns
 * (the target, the clip and the MACCESS flags); callers write the rest. */
void     engine_reset(void);
/* What OPMODE holds, for the modules that write it (engine.c's ILOAD,
 * warp.c's vertices); MGA_OPMODE_UNKNOWN after a reset or a change of hands. */
#define MGA_OPMODE_UNKNOWN 0xFFFFFFFFu
extern uint32_t mga_opmode;
void     engine_set_target(const mga_target *t);
void     engine_set_maccess_flags(uint32_t flags);     /* MACCESS_NODITHER / MACCESS_FOGEN */
void     engine_set_clip(int x0, int y0, int x1, int y1);   /* inclusive-exclusive */
void     engine_get_clip(int *x0, int *y0, int *x1, int *y1);  /* the clip engine_set_clip (or a restore) set */
void     engine_fill(int x, int y, int w, int h, uint32_t value);
uint32_t engine_colour(uint32_t value);                /* a pixel value repeated to 32 bits, at the target's depth */
void     engine_fill_depth(int x, int y, int w, int h, uint32_t zvalue);
void     engine_tlut_load(uint32_t off, int first, int count);   /* G200+: texture LUT from VRAM (RGB565) */
/* G200+: write a w x h rectangle at (x, y) of the surface at VRAM offset off
 * (pitch_px pixels a line, bpp 8 or 16) through the engine (ILOAD), queued in
 * order with drawing. begin returns the dwords per row (pixels in the
 * surface's format, first pixel in the low bits, each row padded to a
 * dword), or 0 when the surface can't be loaded so (pitch not a multiple of
 * 32 pixels, or 64 at 8 bpp; origin not 64-byte aligned); then exactly h
 * calls of engine_iload_row, then engine_iload_end. */
int      engine_iload_begin(uint32_t off, int pitch_px, int bpp, int x, int y, int w, int h);
void     engine_iload_row(const uint32_t *d);
void     engine_iload_end(void);
/* G200+: copies between surfaces in VRAM (not overlapping), queued in order
 * with drawing. engine_vram_copy: w x h pixels of bpp bits (8, 16 or 32),
 * rows pitch_px apart in both (pitch_px x bpp a multiple of 64 bytes, up
 * to 4096), from the pixel at byte address src to the one at dst (whole
 * pixels, anywhere). engine_vram_move: bytes bytes. Each returns 1, or 0
 * when it can't (then nothing of that call was queued); after one or more,
 * engine_vram_copy_end writes the target back. They leave PLNWT all ones. */
int      engine_vram_copy(uint32_t dst, uint32_t src, int pitch_px, int bpp, int w, int h);
int      engine_vram_move(uint32_t dst, uint32_t src, uint32_t bytes);
void     engine_vram_copy_end(void);

/* 2D drawing (engine.c, blit.c; docs/matrox-2d.md). On the target
 * (engine_set_target: its origin 64-byte aligned, its pitch a multiple of
 * 32 pixels, 8, 16 or 32 bits a pixel) inside the clip, queued in order
 * with other drawing; colours are pixel values in the target's format.
 * engine_fill fills. */
/* A rectangle of the target copied elsewhere on it; the two may overlap. */
void     engine_copy(int sx, int sy, int dx, int dy, int w, int h);
/* From another surface in VRAM of the target's depth (its origin a whole
 * pixel, any pitch) to the target. */
void     engine_copy_from(uint32_t src_off, int src_pitch_px, int sx, int sy, int dx, int dy, int w, int h);
/* Colour expansion of one-bit pixels: a set bit draws fg, a clear one bg,
 * or leaves the pixel as it was. From the CPU: rows stride bytes apart,
 * the leftmost pixel in bit 7 of a row's first byte (as GLOS's glyphs and
 * Windows' monochrome bitmaps are). */
#define MGA_MONO_TRANSPARENT 1u              /* clear bits leave the pixel as it was */
void     engine_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg,
                     unsigned flags);
/* From VRAM: engine_mono_store writes such rows at off (the engine must not
 * be using those bytes; the G100 keeps them in its own bit order), and
 * engine_mono_vram expands them. A G100's rows had best not cross a 2 MB
 * boundary. */
void     engine_mono_store(uint32_t off, const uint8_t *bits, int stride, int w, int h);
void     engine_mono_vram(uint32_t off, int stride, int x, int y, int w, int h, uint32_t fg, uint32_t bg,
                          unsigned flags);
/* Pixels from the CPU in the target's format to (x, y) of the target,
 * clipped: returns the dwords of each row (padded to a dword; 0 when it
 * can't), then exactly h calls of engine_iload_row, then engine_iload_end. */
int      engine_image_begin(int x, int y, int w, int h);

/* cursor.c: the hardware cursor (64 x 64, of the chip's own DAC) and the
 * display start through the CRTC. The image: rows of 8 bytes, the leftmost
 * pixel in bit 7, an AND mask and an XOR mask as DOS and Windows have them
 * (AND 0: colour 0 for XOR 0, colour 1 for XOR 1; AND 1: the screen, or the
 * screen inverted for XOR 1), written to VRAM at off, a multiple of 1 KB in
 * the first 16 MB (8 MB on a G100): 0, or -1. Colours are 0xRRGGBB.
 * cursor_move takes the image's top left corner (as far as 63 pixels off
 * the top or the left), and waits for the vertical retrace to end. */
#define MGA_CURSOR_BYTES 1024u
int      cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512]);
void     cursor_colours(uint32_t c0, uint32_t c1);
void     cursor_show(int on);
void     cursor_move(int x, int y);
/* The first byte shown (a multiple of 8, below 16 MB), from the next line
 * on: call in the vertical blank (engine_vsync_wait) to change it between
 * frames. 0, or -1. */
int      crtc_set_start(uint32_t byte_off);
int      engine_vsync_wait(uint32_t timeout_us);
int      engine_in_vblank(void);
uint32_t engine_vcount(void);
extern const mga_target *engine_target;
extern uint32_t engine_resets, engine_timeouts;
/* Moves whenever the HAL itself writes registers a client's drawing state
 * owns (PLNWT, ALPHACTRL, the samplers, the WARPs' sizes): engine_init (a
 * reset), engine_resync, a VRAM copy, a present, a WARP start. A client that
 * skips writing registers it believes unchanged writes them all again when
 * this has moved. */
extern uint32_t engine_regs_epoch;
extern uint32_t engine_timeout_diag[8];  /* the first engine_sync timeout's registers (engine.c) */
extern uint32_t engine_fence_stuck;      /* WARP fences DWGSYNC never took, let go (engine.c) */
/* The engine state the HAL owns (target, clip, MACCESS flags), saved and
 * restored around work that borrows the engine, such as engine_present. */
typedef struct { mga_target t; int clip[4]; uint32_t maccess_flags; } mga_engine_state;
void     engine_save(mga_engine_state *s);
void     engine_restore(const mga_engine_state *s);

/* present.c: draw a 16-bit render surface onto a display surface, scaled
 * to the rectangle (dx, dy, dw, dh), for the scaled fits of the mode
 * planner. The render surface is sampled as one TW16 texture, so its pitch
 * must be a power of two, a multiple of 32 and at most 2048 texels, its
 * height at most 2048 and its offset 64-byte aligned (mga_present_ok).
 * Nearest filtering gives exact pixel repetition at 2x (texel = floor of
 * (pixel centre x source/destination + 1/64)); bilinear maps the
 * first and last pixel centres to the first and last texel centres. The
 * caller has finished drawing the render surface (engine drained) and runs
 * inside its FPU guard. Overwritten and not restored, so the caller must
 * re-emit them before its next draw: DWGCTL, TEXORG, TEXCTL, TEXCTL2,
 * TEXFILTER, TEXTRANS, TEXWIDTH, TEXHEIGHT, TMR0-8, the DR registers,
 * ALPHACTRL, PLNWT and, on the G400, TDUALSTAGE0/1. Target, clip and MACCESS
 * flags are restored; setup_stats is left as it was. */
typedef struct { uint32_t off; int w, h, pitch_px; } mga_surface;
enum { MGA_PRESENT_NEAREST, MGA_PRESENT_BILINEAR };
int      mga_present_ok(const mga_surface *src);
void     engine_present(const mga_surface *src, const mga_surface *dst, int dx, int dy, int dw, int dh,
                        int filter);

/* dac.c */
void dac_set_ramp(const uint8_t ramp[256]);

/* vram.c: the card's memory shared out among owners (any non-zero tag the
 * caller chooses): blocks in address order covering [base, end), each free
 * or one owner's, neighbouring free blocks merged. Functions returning int
 * give 0, or -1 (no room, the table full, a bad argument). */
#define MGA_VRAM_BLOCKS 256
#define MGA_VRAM_TOP    1u           /* vram_alloc: as high as it fits, not as low */
typedef struct { uint32_t off, size, owner; } mga_vram_block;
typedef struct {
    uint32_t base, end;
    int n;
    mga_vram_block b[MGA_VRAM_BLOCKS];
} mga_vram;
void     vram_init(mga_vram *v, uint32_t base, uint32_t end);
int      vram_alloc(mga_vram *v, uint32_t bytes, uint32_t align, unsigned flags, uint32_t owner, uint32_t *off);
int      vram_reserve(mga_vram *v, uint32_t off, uint32_t bytes, uint32_t owner);   /* a given place */
int      vram_free(mga_vram *v, uint32_t off);                 /* a block, by where it starts */
void     vram_free_owner(mga_vram *v, uint32_t owner);         /* all of an owner's */
uint32_t vram_size_of(const mga_vram *v, uint32_t off);        /* an owned block's size, 0 if none starts there */
uint32_t vram_free_bytes(const mga_vram *v, uint32_t *largest);
uint32_t vram_owned_bytes(const mga_vram *v, uint32_t owner);

/* fifo.c: the single choke point for register writes (PRD §10). */
void fifo_reserve(int n);
void fifo_reset(void);
/* fifo_reserve's fast path, inline: most reservations fit the slots the
 * last FIFOSTATUS read found free, and cost no call. */
extern int mga_fifo_free;
/* Vertices that warp.c has buffered for, or is sending by, bus-master DMA:
 * fifo_reserve (and so every register write's reservation) and engine_sync
 * call mga_dma_flush first, which sends them and waits until the chip has
 * fetched them, so register writes never overtake them. warp.c keeps
 * mga_fifo_free at 0 while they are pending, which sends fifo_need to
 * fifo_reserve. */
extern int mga_dma_pending;
extern void (*mga_dma_flush)(void);
extern void (*mga_dma_kick)(void);      /* sends what is queued, without waiting (warp.c) */
/* A fence after everything queued so far: its tag (0 where the chip has no
 * DWGSYNC, the G100). engine_fence_wait returns once the drawing engine has
 * finished what came before it, without waiting for what came after (0, or
 * engine_sync's result when it timed out into a reset); tag 0 is engine_sync. */
uint32_t engine_fence(void);
int      engine_fence_wait(uint32_t tag, uint32_t timeout_us);
int      engine_fence_passed(uint32_t tag);     /* 1 once passed (one register read, no wait); tag 0: 1 */
/* WARP triangles sent since the engine was last idle (warp.c sets it,
 * engine_sync clears it). On a G400 the clip registers do not wait for
 * them: emit_clip puts the DRM's barrier in front of a new clip. */
extern int mga_warp_pending;
/* The DWGCTL the WARP's triangles are drawn with: setup_dwgctl keeps it, and
 * the barrier draws with it (0: not known). */
extern uint32_t mga_warp_dwgctl;
/* Two DWGSYNC writes while WARP triangles may be in flight, as the DRM's
 * before its clears and blits: the HAL's fills, blits and loads start with
 * them, and so must any other draw a caller starts itself. */
void engine_warp_order(void);
/* Called by engine_reset after the soft reset (warp.c's DMA stream). */
extern void (*mga_reset_hook)(void);
/* A G450: its DDR memory set up again after a soft reset (pci.c sets it at
 * mga_find from the PInS v5 block; engine_reset calls it), and how often. */
extern void (*mga_memory_reinit)(void);
extern uint32_t mga_g450_reinits;
extern uint32_t mga_g450_reinit_steps;  /* (diagnosis: which steps of that set-up run; pci.c) */
/* DWGSYNC's tags: a new one, and the last given (one sequence). */
uint32_t mga_dwgsync_next(void);
uint32_t mga_dwgsync_last(void);
MGA_INLINE void fifo_need(int n)
{
    if (mga_fifo_free >= n)
        mga_fifo_free -= n;
    else
        fifo_reserve(n);
}

#endif
