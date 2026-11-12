/* virge.h - RetroGPU's S3 ViRGE/DX register layer (docs/virge.md, step V1):
 * finding and mapping the chip, modes through its BIOS with the linear
 * window opened by this code (its BIOS may be VBE 1.2, with none), and the
 * S3d engine's 2D drawing. The 2D calls follow the Matrox HAL's (mga/hal.h,
 * engine_*) so that RetroGPU's device level (B1) can put either behind one
 * interface: a target in VRAM, a clip, then fills, copies, colour expansion
 * and images from the CPU, in order. Port-neutral: it uses only mga/sys.h's
 * platform calls (ports, physical mappings, real-mode interrupts). */
#ifndef VIRGE_VIRGE_H
#define VIRGE_VIRGE_H
#include <stdint.h>

#define VIRGE_PCI_VENDOR 0x5333u
#define VIRGE_PCI_DX     0x8A01u   /* ViRGE/DX and /GX (CR2F tells them apart) */

typedef struct {
    int      bus, dev;
    uint16_t device_id;
    uint8_t  revision;
    uint32_t bar0;                  /* the 64 MB window's base */
    uint32_t vram_bytes;            /* from the BIOS (4 MB on bench C) */
    volatile uint8_t *fb;           /* the linear framebuffer, vram_bytes */
    volatile uint8_t *mmio;         /* the new MMIO window, 64 KB */
} virge_chip;

extern virge_chip virge;            /* the one ViRGE this layer drives */

/* chip.c */
int  virge_find(void);              /* 0: a ViRGE/DX or /GX on PCI (bus 0-1); fills virge's PCI fields */
int  virge_map(void);               /* 0: framebuffer and MMIO mapped, new MMIO and the window on */
void virge_unmap(void);
uint8_t virge_cr_read(uint8_t index);
void    virge_cr_write(uint8_t index, uint8_t value);

typedef struct {
    uint16_t mode, width, height, bpp, pitch_bytes;
} virge_mode;
/* A BIOS mode of exactly w x h at bpp bits (15 and 16 are told apart), in
 * the packed (8) or direct-colour (15, 16, 24, 32) memory models: 0, or -1. */
int  virge_find_mode(int w, int h, int bpp, virge_mode *out);
/* The mode set by the BIOS, then the chip's linear window and new MMIO
 * opened (the BIOS's mode set closes them): 0, or -1. */
int  virge_set_mode(const virge_mode *m);
void virge_text_mode(void);

/* engine2d.c: the S3d engine's 2D drawing. Coordinates are pixels of the
 * target, any value; the clip (inclusive-exclusive, within the target)
 * trims everything drawn. Colours are pixel values in the target's format. */
int  virge_sync(uint32_t timeout_us);              /* 0 when the engine is idle, -1 on time-out */
/* The target: its first pixel at off in VRAM (a multiple of 8), pitch_bytes
 * apart (a multiple of 8, at most 4095), bpp 8, 16 or 24 (packed). */
void virge_set_target(uint32_t off, int pitch_bytes, int bpp);
void virge_set_clip(int x0, int y0, int x1, int y1);
void virge_fill(int x, int y, int w, int h, uint32_t value);
/* A rectangle of the target copied elsewhere on it; the two may overlap. */
void virge_copy(int sx, int sy, int dx, int dy, int w, int h);
/* From another surface in VRAM of the target's depth (its first pixel at
 * src_off, a multiple of 8; src_pitch_bytes a multiple of 8). */
void virge_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h);
/* One-bit pixels from the CPU: rows stride bytes apart, the leftmost pixel
 * in bit 7 of a row's first byte; a set bit draws fg, a clear one bg, or
 * leaves the pixel with VIRGE_MONO_TRANSPARENT. */
#define VIRGE_MONO_TRANSPARENT 1u
void virge_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg,
                unsigned flags);
/* Pixels from the CPU in the target's format, rows stride bytes apart. */
void virge_image(int x, int y, int w, int h, const void *px, int stride);

/* cursor.c: the hardware cursor (64 x 64) and the display start, as the
 * Matrox HAL's cursor_* and crtc_set_start: the image's AND and XOR masks,
 * rows of 8 bytes, the leftmost pixel in bit 7 (AND 0: colour 0 for XOR 0,
 * colour 1 for XOR 1; AND 1: the screen, inverted for XOR 1), stored at
 * off, a multiple of 1 KB: 0, or -1. Colours are 0xRRGGBB, converted to
 * the mode's pixels (8 bits: a palette index). virge_cursor_move takes the
 * image's top left corner, as far as 63 pixels off the top or the left.
 * virge_set_start: the first byte shown, a multiple of 4, from the next
 * frame on (call it in the vertical blank: virge_vsync_wait). */
#define VIRGE_CURSOR_BYTES 1024u
extern int virge_mode_bpp;          /* the mode's bits a pixel: 8, 15, 16, 24 or 32 */
int  virge_cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512]);
void virge_cursor_colours(uint32_t c0, uint32_t c1);
void virge_cursor_show(int on);
void virge_cursor_move(int x, int y);
int  virge_set_start(uint32_t byte_off);
int  virge_vsync_wait(uint32_t timeout_us);        /* to the start of the next vertical retrace: 0, or -1 */

#endif
