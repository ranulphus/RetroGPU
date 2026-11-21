/* r128.h - RetroGPU's ATI Rage 128 register layer (docs/rage128.md, step R1): finding and mapping the chip, modes
 * through its VBE 2.0 BIOS with the linear window, the display start and pitch, the cursor, and the 2D engine by
 * MMIO (the CCE off: NONPM4). The 2D calls follow the Matrox HAL's and the ViRGE layer's (virge/virge.h), so the
 * device level (rg_device) puts any of them behind one interface: a target in VRAM, a clip, then fills, copies,
 * colour expansion and images from the CPU, in order. Port-neutral: only mga/sys.h's platform calls. */
#ifndef R128_R128_H
#define R128_R128_H
#include <stdint.h>

typedef struct {
    int      bus, dev;
    uint16_t device_id;
    uint8_t  revision;
    uint32_t fb_phys;                   /* aperture 0 (BAR0) */
    uint32_t mmio_phys;                 /* the registers (BAR2, 16 KB) */
    uint32_t vram_bytes;                /* CONFIG_MEMSIZE (16 MB on the P3's) */
    volatile uint8_t *fb;               /* the linear framebuffer, vram_bytes */
    volatile uint8_t *mmio;             /* the registers */
} r128_chip;
extern r128_chip r128;                  /* the one Rage 128 this layer drives */

/* chip.c */
int  r128_find(void);                   /* 0: an ATI display device on PCI (bus 0-3); fills r128's PCI fields */
int  r128_map(void);                    /* 0: registers and framebuffer mapped, the memory size read */
void r128_unmap(void);
uint32_t r128_rd(uint32_t off);
void     r128_wr(uint32_t off, uint32_t v);
uint32_t r128_pll_rd(int index);
void     r128_pll_wr(int index, uint32_t v);

typedef struct {
    uint16_t mode, width, height, bpp, pitch_bytes;
} r128_mode;
/* The BIOS's linear graphics modes at 8 (palettized), 15, 16 or 32 bits (15 when a 16-bit mode has 5 green bits):
 * their count, up to max into out; one of exactly w x h x bpp: 0, or -1. */
int  r128_list_modes(r128_mode *out, int max);
int  r128_find_mode(int w, int h, int bpp, r128_mode *out);
/* The mode set by the BIOS with its linear window, then the 2D engine reset and set up for it: 0, or -1. */
int  r128_set_mode(const r128_mode *m);
void r128_text_mode(void);
extern int r128_mode_bpp;               /* the mode's bits a pixel: 8, 15, 16 or 32 */

/* The display: the first byte shown (a multiple of 8, from the next frame), the rows' length in bytes (a multiple
 * of 8 pixels), and the wait for the start of the next vertical retrace (0, or -1 on time-out). */
int  r128_set_start(uint32_t byte_off);
int  r128_set_pitch(int pitch_bytes);
int  r128_vsync_wait(uint32_t timeout_us);

/* engine2d.c: the 2D engine. Coordinates are pixels of the target, any value; the clip (inclusive-exclusive,
 * within the target) trims everything drawn, in software. Colours are pixel values in the target's format. */
int  r128_sync(uint32_t timeout_us);    /* 0 when the engine is idle and its pixel cache flushed, -1 on time-out */
int  r128_idle(void);                   /* 1: the engine is idle now */
void r128_engine_reset(void);           /* (X.org's R128EngineReset: the GUI soft reset with the clocks forced) */
/* The target: its first pixel at off in VRAM (a multiple of 32), pitch_bytes apart (a multiple of 8 pixels and of
 * 16 bytes: 8-bit surfaces with pitches of 8 bytes' steps drew wrongly on the P3), bpp 8, 15, 16 or 32. */
void r128_set_target(uint32_t off, int pitch_bytes, int bpp);
void r128_set_clip(int x0, int y0, int x1, int y1);
void r128_fill(int x, int y, int w, int h, uint32_t value);
void r128_copy(int sx, int sy, int dx, int dy, int w, int h);  /* within the target; the two may overlap */
/* From another surface in VRAM of the target's depth (its first pixel at src_off, a multiple of 32). */
void r128_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h);
/* One-bit pixels from the CPU: rows stride bytes apart, the leftmost pixel in bit 7 of a row's first byte; a set
 * bit draws fg, a clear one bg, or leaves the pixel with R128_MONO_TRANSPARENT. */
#define R128_MONO_TRANSPARENT 1u
void r128_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, unsigned flags);
/* Pixels from the CPU in the target's format, rows stride bytes apart. */
void r128_image(int x, int y, int w, int h, const void *px, int stride);

/* cursor.c: the hardware cursor (64 x 64), as the Matrox HAL's cursor_*: the AND and XOR masks, rows of 8 bytes,
 * the leftmost pixel in bit 7 (AND 0: colour 0 for XOR 0, colour 1 for XOR 1; AND 1: the screen, inverted for
 * XOR 1), stored at off (a multiple of 16); colours 0xRRGGBB; r128_cursor_move takes the image's top left corner,
 * as far as 63 pixels off the top or the left. */
#define R128_CURSOR_BYTES 1024u
int  r128_cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512]);
void r128_cursor_colours(uint32_t c0, uint32_t c1);
void r128_cursor_show(int on);
void r128_cursor_move(int x, int y);

#endif
