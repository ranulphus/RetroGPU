/* tnt2.h - the Riva TNT2's register layer (docs/tnt2.md, T1): the NV4 family (the TNT and TNT2) found on PCI and
 * mapped, its graphics engine fed by a command ring in video memory, and its 2D operations. Calls mirror the
 * ViRGE/DX layer's (virge/virge.h) and the Matrox HAL's engine_* where they can. */
#ifndef TNT2_TNT2_H
#define TNT2_TNT2_H

#include <stdint.h>

typedef struct {
    int      bus, dev;
    uint16_t device;            /* the PCI device ID: 0020 TNT, 0028 TNT2, 0029 Ultra, 002C Vanta, 002D M64, 00A0 */
    uint8_t  revision;
    uint32_t bar0, bar1;        /* the register window (16 MB) and the frame buffer */
    uint32_t boot0;             /* PMC_BOOT_0 */
    uint32_t fb_size;           /* bytes of video memory (from PFB_BOOT_0) */
    uint32_t fb_usable;         /* below the 128 KB the layer keeps at the top: the command ring (32 KB), then the
                                   images' scratch (64 KB) */
    const char *name;
} tnt2_chip;

extern tnt2_chip tnt2;
extern volatile uint32_t *tnt2_regs;    /* BAR0, once mapped */
extern volatile uint8_t *tnt2_fb;       /* BAR1, once mapped */

int  tnt2_find(void);           /* 0: an NV4-family chip on PCI (buses 0-3); fills tnt2's PCI fields */
int  tnt2_map(void);            /* 0: the registers and the frame buffer mapped, the memory size read */
void tnt2_unmap(void);

/* The graphics engine, its FIFO and instance memory set up, the command ring started; the BIOS's display mode is
 * left as it is. 0, or -1 when the engine doesn't answer. */
int  tnt2_accel_init(void);
int  tnt2_sync(uint32_t timeout_us);    /* every command done: 0, or -1 on time-out */
void tnt2_kick(void);                   /* the ring's written commands to the chip now */

/* 2D. The target: a surface at byte offset off in video memory, pitch_bytes apart, 8, 16 or 32 bits a pixel
 * (offsets and pitches multiples of 64 bytes). The clip is inclusive-exclusive. */
void tnt2_set_target(uint32_t off, int pitch_bytes, int bpp);
void tnt2_set_clip(int x0, int y0, int x1, int y1);
void tnt2_fill(int x, int y, int w, int h, uint32_t value);
void tnt2_copy(int sx, int sy, int dx, int dy, int w, int h);   /* within the target, overlapping allowed */
/* From another surface of the target's depth (offset and pitch as the target's). */
void tnt2_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h);
/* One-bit pixels from the CPU: rows stride bytes apart, the leftmost pixel in bit 7 of a row's first byte; a set
 * bit draws fg, a clear one bg, or leaves the pixel with TNT2_MONO_TRANSPARENT. */
#define TNT2_MONO_TRANSPARENT 1u
void tnt2_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, unsigned flags);
/* Pixels from the CPU in the target's format, rows stride bytes apart. */
void tnt2_image(int x, int y, int w, int h, const void *px, int stride);

/* The hardware cursor: the NV4's is 32 x 32, so of the 64 x 64 AND and XOR masks (rows of 8 bytes, the leftmost
 * pixel in bit 7; AND 0: colour 0 for XOR 0, colour 1 for XOR 1; AND 1: the screen, and colour 1 where the Matrox
 * and ViRGE cursors would invert it) the top left quarter shows. Colours are 0xRRGGBB. tnt2_cursor_move takes the
 * image's top left corner, off the screen's top or left too. tnt2_set_start: the first byte shown (a multiple of
 * 4) from the next frame on, 0 or -1; tnt2_vsync_wait: to the next vertical retrace's start, 0 or -1. */
int  tnt2_cursor_load(const uint8_t and_mask[512], const uint8_t xor_mask[512]);
void tnt2_cursor_colours(uint32_t c0, uint32_t c1);
void tnt2_cursor_show(int on);
void tnt2_cursor_move(int x, int y);
int  tnt2_set_start(uint32_t byte_off);
int  tnt2_vsync_wait(uint32_t timeout_us);

/* 3D (T2, bring-up): the 3D surfaces and the textured triangle (classes 0x53 and 0x54) on subchannels 4 and 7.
 * Vertices are Direct3D's TLVERTEX: window x and y in pixels, z in [0, 1], 1/w, colour and specular as A8R8G8B8
 * (specular's top byte the fog factor), u and v. tnt2_3d_state takes the triangle object's words as the chip has
 * them (docs/tnt2.md): the texture's offset, FORMAT, FILTER, BLEND, CONTROL and the fog colour. tnt2_3d_trap:
 * PGRAPH's interrupt status (nonzero: a method trapped, and the engine waits), with the trapped method and data. */
typedef struct {
    float sx, sy, sz, rhw;
    uint32_t color, specular;
    float tu, tv;
} tnt2_vertex;
int      tnt2_3d_init(void);
void     tnt2_3d_target(uint32_t color_off, uint32_t zeta_off, int pitch_bytes, int bpp, int w, int h);
void     tnt2_3d_state(uint32_t tex_off, uint32_t format, uint32_t filter, uint32_t blend, uint32_t control,
                       uint32_t fogcolor);
void     tnt2_tri(const tnt2_vertex *a, const tnt2_vertex *b, const tnt2_vertex *c);
uint32_t tnt2_tex_dummy_off(void);      /* a 1x1 white A8R8G8B8 texel in VRAM, for untextured triangles */
uint32_t tnt2_3d_trap(uint32_t *addr, uint32_t *data);
/* A texture's levels, each w x h texels of cpp bytes in rows from the CPU, stored swizzled from off (a multiple of
 * 128) one after another, as the chip reads them (the engine idle: tnt2_sync first). Returns the end. */
uint32_t tnt2_tex_load(uint32_t off, int cpp, int log2w, int log2h, int levels, const void *const *level_texels);

#endif
