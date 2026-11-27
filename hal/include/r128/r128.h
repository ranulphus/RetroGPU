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
/* The CPU writes VRAM the chip will read (textures) only with the engine idle (r128_sync first): written while it
 * was busy, a texture's last 32-byte line was still old when the triangles after it read it, though the CPU read
 * it back right (the P3, DOS-GL conformance t14). After writing: the CPU's write-combining buffers out (a locked
 * instruction) and the last dword written (at offset last) read back. */
void     r128_vram_written(uint32_t last);
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
/* A flip in the command stream (the CCE running): the start written after what is queued, then (wait) the engine
 * stalled until it is shown (WAIT_UNTIL's EVENT_CRTC_OFFSET: the chip takes a start at the vertical blank), then seq
 * written to GUI_SCRATCH_REG0; the CPU doesn't wait. With wait, drawing queued after it reaches the buffer that was
 * shown only once it no longer is; without, at once (it may tear, as with no retrace). r128_flip_seen: the seq of
 * the last flip reached. 0, or -1 (no CCE, or a bad start). */
int  r128_flip_queue(uint32_t byte_off, uint32_t seq, int wait);
uint32_t r128_flip_seen(void);
/* A start taken at the next scan line (on: CRTC_OFFSET_CNTL's FLIP_CNTL, as the Radeon's) instead of at the vertical
 * blank: swaps without the retrace then tear rather than draw into the buffer still shown. Off before a mode set. */
void r128_flip_at_line(int on);
/* Gamma: in 16- and 32-bit modes the DAC looks each component up in its 256-entry palette (X.org's
 * R128LoadPalette: 565's red at index r5 * 8, green g6 * 4); one ramp for the three, 8-bit entries (DAC_8BIT_EN).
 * r128_ramp_off: the DAC as it was before the first ramp (for text mode). */
void r128_set_ramp(const uint8_t ramp[256]);
void r128_ramp_off(void);

/* engine2d.c: the 2D engine. Coordinates are pixels of the target, any value; the clip (inclusive-exclusive,
 * within the target) trims everything drawn, in software. Colours are pixel values in the target's format. */
int  r128_sync(uint32_t timeout_us);    /* 0 when the engine is idle and its pixel cache flushed, -1 on time-out */
int  r128_idle(void);                   /* 1: the engine is idle now */
void r128_engine_reset(void);           /* (X.org's R128EngineReset: the GUI soft reset with the clocks forced) */
/* The target: its first pixel at off in VRAM (a multiple of 32), pitch_bytes apart (a multiple of 8 pixels and of
 * 16 bytes: 8-bit surfaces with pitches of 8 bytes' steps drew wrongly on the P3), bpp 8, 15, 16 or 32. */
void r128_set_target(uint32_t off, int pitch_bytes, int bpp);
/* The same for a tiled surface (a tiled depth buffer, Z_TILE): fills only (DST_TILE in its pitch and offset). */
void r128_set_target_tiled(uint32_t off, int pitch_bytes, int bpp);
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
/* Texels into video memory through the engine, in the command stream (no sync; the CCE running): a rectangle of
 * a surface at off, pitch_px texels a row (a multiple of 8), format dt (R128_TEX_*), its texels packed in data.
 * 0, or -1 (not a shape the engine takes: the caller writes them itself). */
int  r128_upload(uint32_t off, int pitch_px, int dt, int x, int y, int w, int h, const uint32_t *data);

/* cce.c: the Concurrent Command Engine in PIO mode (R2). r128_cce_init loads the microcode (pairs: 512 dwords,
 * DATAH and DATAL in turn, an r128_cce.bin read by the caller; NULL: the embedded one, cce_ucode.c) and starts the
 * engine in 192PIO; from then on the 2D engine's writes go as type-0 packets and r128_sync waits for the CCE. The
 * CCE stops at r128_cce_stop, a mode set, and an engine reset. */
extern const uint32_t r128_cce_microcode[512];
int  r128_cce_init(const uint32_t *pairs);  /* 0, or -1 (the engine not idle to begin with) */
/* The same in 192BM: the packets in a ring of bytes (a power of two, 4 KB to 256 KB) in conventional memory that the
 * CCE fetches itself through its PCI GART, the CPU writing it as ordinary memory: no bus write a dword, and the chip
 * as far behind as the ring holds. 0, or -1 (no memory, or the first fetch didn't come: PIO then). */
int  r128_cce_init_ring(const uint32_t *pairs, uint32_t bytes);
int  r128_cce_ring(void);                   /* 1: running from a ring */
int  r128_cce_drained(void);                /* 1: everything sent fetched, the engine idle */
void r128_cce_ring_peek(uint32_t *rp, uint32_t *wp, const volatile uint32_t **base, uint32_t *dwords);   /* (diag) */
/* PIO through a burst window: page is a mapping of the registers' FIFO page (offset 0x1000 of either register
 * aperture; the second, CONFIG_REG_1_BASE's, can be made write-combining without the first), written a 32-byte line
 * at a time in address order; NULL: back to EVEN and ODD. Until r128_cce_stop. */
void r128_cce_burst_window(volatile void *page);
/* The same through the second register aperture's FIFO page made write-combining by GLOS (2.11): 0, or -1 (not
 * given: sys_cache_wc_err says why) with EVEN and ODD kept. After r128_cce_init. */
int  r128_cce_burst_wc(void);
void r128_cce_stop(void);
int  r128_cce_running(void);
void r128_cce_out(const uint32_t *d, int n);    /* packet dwords, buffered */
void r128_cce_reg(uint32_t reg, uint32_t v);    /* a type-0 register write */
void r128_cce_flush(void);
int  r128_cce_idle_wait(uint32_t timeout_us);
void r128_cce_forget(void);

/* tri.c: the 3D engine through the CCE (R2). r128_3d holds the context's register words (Mesa's r128 driver's
 * starting values, made by r128_3d_init for a target, a linear depth buffer of zbits 16, 24 or 32); a caller
 * changes them, then r128_3d_emit sends them all (the CCE running). Triangles go as 3D_RNDR_GEN_PRIM packets in
 * r128_3d_format's vertex format (R128_VC_FRMT_*: x, y, z always). */
typedef struct {
    uint32_t dst_pitch_offset_c, dp_gui_master_cntl_c, sc_top_left_c, sc_bottom_right_c, z_offset_c, z_pitch_c;
    uint32_t z_sten_cntl_c, tex_cntl_c, misc_3d_state_cntl_reg, texture_clr_cmp_clr_c, texture_clr_cmp_msk_c;
    uint32_t fog_color_c, setup_cntl, pm4_vc_fpu_setup, dp_write_mask, sten_ref_mask_c, plane_3d_mask_c;
    uint32_t window_xy_offset, constant_color_c, scale_3d_cntl;
    /* The first texture unit's block (the DRM's r128_emit_tex0): its control, combine, size and pitch, the eleven
     * levels' offsets in VRAM. */
    uint32_t prim_tex_cntl_c, prim_texture_combine_cntl_c, tex_size_pitch_c, prim_tex_offset_c[11];
    /* The second unit's (r128_emit_tex1): its control, combine and offsets (its size and pitch in
     * tex_size_pitch_c's upper half); used with TEX_CNTL_C's SEC_TEXMAP_ENABLE. */
    uint32_t sec_tex_cntl_c, sec_tex_combine_cntl_c, sec_tex_offset_c[11];
} r128_ctx3d;
extern r128_ctx3d r128_3d;
/* (set by any 2D register write: the 2D engine shares the 3D context's registers, and a triangle after a fill drew
 * nothing on the P3; r128_tri sends the context again first) */
extern int r128_3d_stale;
/* (set by a caller that changed r128_3d's words: the next triangle sends the groups that changed) */
extern int r128_3d_dirty;
void r128_3d_tex_written(void);         /* textures written by the CPU: the texture cache flushed at the next draw */
typedef struct {
    float x, y, z, rhw;
    uint32_t argb, spec;                /* (the specular's top byte: the fog factor) */
    float s, t, s2, t2;
} r128_vertex;
void r128_3d_init(uint32_t color_off, int pitch_bytes, int bpp, uint32_t z_off, int z_pitch_bytes, int zbits);
void r128_3d_emit(void);
void r128_3d_format(int fmt);
void r128_tri(const r128_vertex *a, const r128_vertex *b, const r128_vertex *c);
void r128_poly(const r128_vertex *v, int n);   /* a convex polygon of 3 to 16 vertices as one triangle fan */

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
