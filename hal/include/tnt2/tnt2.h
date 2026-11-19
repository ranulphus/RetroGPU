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
int  tnt2_idle(void);                   /* 1: every command written so far is done (no wait) */
void tnt2_kick(void);                   /* the ring's written commands to the chip now */
/* A place in the command stream, and whether the chip has done everything before it (a fence without notifiers). */
uint32_t tnt2_mark(void);
int  tnt2_mark_done(uint32_t mark);
int  tnt2_mark_wait(uint32_t mark, uint32_t timeout_us);    /* 0, or -1 on time-out */

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
void tnt2_set_pitch(int pitch_bytes);   /* the display's row length (CR13/CR19), when the BIOS's 4F06 won't */

/* 3D (T2, bring-up): the 3D surfaces and the textured triangle (classes 0x53 and 0x54) on subchannels 4 and 7.
 * Vertices are Direct3D's TLVERTEX: window x and y in pixels, z in [0, 1], 1/w, colour and specular as A8R8G8B8
 * (specular's top byte the fog factor), u and v. tnt2_3d_state takes the triangle object's words as the chip has
 * them (docs/tnt2.md): the texture's offset, FORMAT, FILTER, BLEND, CONTROL and the fog colour. tnt2_3d_trap:
 * PGRAPH's interrupt status (nonzero: a method trapped, and the engine waits), with the trapped method and data. */
typedef struct {
    float sx, sy, sz, rhw;
    uint32_t color, specular;
    float tu, tv;
    float tu1, tv1;             /* the second texture's (the multitexture object's only) */
} tnt2_vertex;
int      tnt2_3d_init(void);
void     tnt2_3d_target(uint32_t color_off, uint32_t zeta_off, int pitch_bytes, int bpp, int w, int h);
void     tnt2_3d_clip(int x0, int y0, int x1, int y1);  /* inclusive-exclusive; a change waits for the engine */
void     tnt2_3d_state(uint32_t tex_off, uint32_t format, uint32_t filter, uint32_t blend, uint32_t control,
                       uint32_t fogcolor);
void     tnt2_tri(const tnt2_vertex *a, const tnt2_vertex *b, const tnt2_vertex *c);
void     tnt2_poly(const tnt2_vertex *v, int n);     /* a fan of n (3-16) vertices, each sent once */
/* The multitexture triangle object (class 0x55: two textures, combiners, colour masks, stencil) in subchannel 7's
 * place: tnt2_3d_mt_state takes its 16 words (OFFSET 0-1, FORMAT 0-1, FILTER 0-1, COMBINE_ALPHA(0), COMBINE_COLOR(0),
 * COMBINE_ALPHA(1), COMBINE_COLOR(1), COMBINE_FACTOR, BLEND, CONTROL0-2, FOGCOLOR) and makes 0x55 the object drawn
 * with; tnt2_3d_state makes 0x54 it again. tnt2_tri_mt and tnt2_poly_mt (3-8 vertices) draw with it. */
void     tnt2_3d_mt_state(const uint32_t w[16]);
void     tnt2_3d_tex_written(void);     /* texels written in place: the state sent again (the texture cache) */
void     tnt2_tri_mt(const tnt2_vertex *a, const tnt2_vertex *b, const tnt2_vertex *c);
void     tnt2_poly_mt(const tnt2_vertex *v, int n);
uint32_t tnt2_tex_dummy_off(void);      /* a 1x1 white A8R8G8B8 texel in VRAM, for untextured triangles */
uint32_t tnt2_3d_trap(uint32_t *addr, uint32_t *data);
void     tnt2_3d_raw(int sub3d, uint32_t method, uint32_t value);  /* (bring-up: a method on the 3D surfaces' (1)
                                                                    or the triangle's (0) subchannel) */
uint32_t tnt2_3d_handle(int sub3d);
/* A texture's levels, each w x h texels of cpp bytes in rows from the CPU, stored swizzled from off (a multiple of
 * 128) one after another, as the chip reads them (the engine idle: tnt2_sync first). Returns the end. */
uint32_t tnt2_tex_load(uint32_t off, int cpp, int log2w, int log2h, int levels, const void *const *level_texels);
/* The same from RGBA8 levels (r, g, b, a bytes, rows of w), converted to the chip's format in the same pass. */
enum { TNT2_TEX_A8R8G8B8, TNT2_TEX_R5G6B5, TNT2_TEX_A1R5G5B5, TNT2_TEX_A4R4G4B4 };
uint32_t tnt2_tex_load_rgba8(uint32_t off, int fmt, int log2w, int log2h, int levels, const uint8_t *const *rgba);
/* A rectangle of the base level written in place (the engine done with the texture first: tnt2_mark_wait). */
void     tnt2_tex_sub_rgba8(uint32_t off, int fmt, int log2w, int log2h, int x, int y, int w, int h,
                            const uint8_t *rgba, int stride);

#endif
