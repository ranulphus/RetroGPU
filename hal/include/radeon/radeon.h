/* radeon.h - RetroGPU's ATI Radeon (R100 family) register layer (docs/radeon.md, step RD1): finding and mapping the
 * chip, modes through its VBE 2.0 BIOS with the linear window, the display start and pitch, the cursor, and the 2D
 * engine by register writes. The Rage 128's layer (r128/r128.h), from which it is made: the same calls, so the
 * device level (rg_device) puts either behind one interface. Port-neutral: only mga/sys.h's platform calls. */
#ifndef RADEON_RADEON_H
#define RADEON_RADEON_H
#include <stdint.h>

typedef struct {
    int      bus, dev;
    uint16_t device_id;
    uint8_t  revision;
    uint32_t fb_phys;                   /* the frame buffer's window (BAR0) */
    uint32_t mmio_phys;                 /* the registers (BAR2, 64 KB) */
    uint32_t vram_bytes;                /* CONFIG_MEMSIZE (64 MB on the P3's) */
    uint32_t fb_location;               /* where the chip's engines see the frame buffer (MC_FB_LOCATION's start) */
    unsigned flags;                     /* RADEON_HAS_* and RADEON_PLL_* below: what this chip of the family is */
    const char *name;
    volatile uint8_t *fb;               /* the linear framebuffer, vram_bytes */
    volatile uint8_t *mmio;             /* the registers */
} radeon_chip;
extern radeon_chip radeon;              /* the one Radeon this layer drives */
/* 1 once the engine stayed stuck through radeon_engine_recover or a FIFO wait ran out: from then on the layer neither
 * writes nor reads the engine's registers (offsets 1000h up, behind the command FIFO), and its waits fail at once. A
 * write to a full FIFO holds the PCI bus until the engine takes it, which never comes: the PC froze that way twice
 * (the P3, 2026-11-25). The display's registers below 1000h stay usable. */
extern int radeon_dead;
/* 1 once a wait of half a second or more for the engine ran out (radeon_sync, radeon_cp_idle_wait, a fence): the
 * engine is stuck, though its registers still answer. The layer's user deals with it (DOS-GL: the recovery reset at
 * its next clear) and clears it. A program that only waited, its frames crawling, left the card stuck once. */
extern int radeon_stalled;

/* The R100 family's chips differ in these (Mesa's radeon_screen.c, the DRM's radeon_cp.c, X.org's radeon_driver.c):
 *   - the first Radeon (R100, the 7200: 5144-5147) and the RV200 (the 7500: 5157, 5158) have a transform and
 *     lighting engine and a hierarchical Z buffer; the RV100 (the 7000 / VE: 5159, 515A) has neither;
 *   - after a PLL register's data access the RV100 needs 5 ms (or can hang); after a PLL index write the RV200 needs
 *     two dummy reads (or PLL reads return garbage);
 *   - the R100's stencil operations have a fault (Mesa falls back for some).
 * One microcode (R100_cp_microcode) and one register set serve them all. Only the RV100 has been on a bench. */
#define RADEON_HAS_TCL          1u      /* (SE_CNTL_STATUS without TCL_BYPASS; a primitive asks for it in SE_VF_CNTL) */
#define RADEON_HAS_HIERZ        2u
#define RADEON_PLL_DELAY        4u
#define RADEON_PLL_DUMMYREADS   8u
#define RADEON_BROKEN_STENCIL   16u
#define RADEON_TILED_Z          32u     /* the 3D engine's depth buffer is always tiled (the chips with a transform
                                         * engine: Mesa's radeon_screen.c, X.org's RADEONChangeSurfaces; the RV100's is
                                         * linear): the CPU sees it through a surface, partial clears by the 3D engine */

/* chip.c */
int  radeon_find(void);                 /* 0: a Radeon of the R100 family on PCI (bus 0-3); fills radeon's PCI fields */
int  radeon_map(void);                  /* 0: registers and framebuffer mapped, the memory size read */
void radeon_unmap(void);
uint32_t radeon_rd(uint32_t off);
void     radeon_wr(uint32_t off, uint32_t v);
/* After the CPU wrote video memory the chip will read: the CPU's write-combining buffers out (a locked instruction),
 * the last dword written (at offset last) read back, and the chip's destination cache flushed (the Rage 128 kept
 * the host's last written lines in its cache: docs/rage128.md, bench A). */
void     radeon_vram_written(uint32_t last);
/* A PLL register read through the clock index, then the 5 ms the RV100 needs after every PLL access (X.org's
 * CHIP_ERRATA_PLL_DELAY: "the chip could hang on a subsequent access"). No PLL writes in this layer. */
uint32_t radeon_pll_rd(int index);

typedef struct {
    uint16_t mode, width, height, bpp, pitch_bytes;
} radeon_mode;
/* The BIOS's linear graphics modes at 8 (palettized), 15, 16 or 32 bits (15 when a 16-bit mode has 5 green bits):
 * their count, up to max into out; one of exactly w x h x bpp: 0, or -1. */
int  radeon_list_modes(radeon_mode *out, int max);
int  radeon_find_mode(int w, int h, int bpp, radeon_mode *out);
/* The mode set by the BIOS with its linear window, then the 2D engine reset and set up for it: 0, or -1. */
int  radeon_set_mode(const radeon_mode *m);
void radeon_text_mode(void);
extern int radeon_mode_bpp;             /* the mode's bits a pixel: 8, 15, 16 or 32 */

/* The display: the first byte shown (a multiple of 8, from the next frame), the rows' length in bytes (a multiple
 * of 8 pixels), and the wait for the start of the next vertical retrace (0, or -1 on time-out). */
int  radeon_set_start(uint32_t byte_off);
int  radeon_set_pitch(int pitch_bytes);
/* A new start is taken once a frame, at the end of the vertical blank (the BIOS's setting); radeon_flip_at_line(1)
 * has it taken at the next scan line instead (no vertical sync: tearing), (0) puts that back.
 * Flips in the command stream (the command processor running): each queued after the drawing before it; wait
 * holds the stream until the display has taken the start; seq comes back from radeon_flip_seen once the chip has
 * passed the flip. */
void radeon_flip_at_line(int on);
int  radeon_flip_queue(uint32_t byte_off, uint32_t seq, int wait);
uint32_t radeon_flip_seen(void);
int  radeon_vsync_wait(uint32_t timeout_us);
/* Gamma: in 16- and 32-bit modes the DAC looks each component up in its 256-entry palette (as the Rage 128's); one
 * ramp for the three, 8-bit entries (DAC_8BIT_EN). radeon_ramp_off: the DAC as it was before the first ramp. */
void radeon_set_ramp(const uint8_t ramp[256]);
void radeon_ramp_off(void);

/* engine2d.c: the 2D engine. Coordinates are pixels of the target, any value; the clip (inclusive-exclusive,
 * within the target) trims everything drawn, in software. Colours are pixel values in the target's format. */
int  radeon_sync(uint32_t timeout_us);  /* 0 when the engine is idle and its cache flushed, -1 on time-out */
int  radeon_idle(void);                 /* 1: the engine is idle now */
int  radeon_engine_reset(void);         /* (X.org's RADEONEngineReset, without its PLL accesses); -1: still busy */
int  radeon_engine_recover(void);       /* after a hang: 0 when the engine came back, else radeon_dead is set */
extern uint32_t radeon_recover_status;  /* (RBBM_STATUS after the recovery's resets, for the log) */
extern int radeon_recover_waited;       /* (how the engine came back: 0 bus mastering off, 1 on) */
/* The target: its first pixel at off in VRAM, pitch_bytes apart (a multiple of 64 bytes: the engine's unit), bpp 8,
 * 15, 16 or 32. The engine places a surface in 1 KB steps: the rest of off goes into x. */
void radeon_set_target(uint32_t off, int pitch_bytes, int bpp);
void radeon_set_clip(int x0, int y0, int x1, int y1);
void radeon_fill(int x, int y, int w, int h, uint32_t value);
void radeon_copy(int sx, int sy, int dx, int dy, int w, int h);  /* within the target; the two may overlap */
/* From another surface in VRAM of the target's depth (its pitch a multiple of 64 bytes). */
void radeon_copy_from(uint32_t src_off, int src_pitch_bytes, int sx, int sy, int dx, int dy, int w, int h);
/* One-bit pixels from the CPU: rows stride bytes apart, the leftmost pixel in bit 7 of a row's first byte; a set
 * bit draws fg, a clear one bg, or leaves the pixel with RADEON_MONO_TRANSPARENT. */
#define RADEON_MONO_TRANSPARENT 1u
void radeon_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg, unsigned flags);
/* Pixels from the CPU in the target's format, rows stride bytes apart, written through the frame buffer window
 * with the engine idle. */
void radeon_image(int x, int y, int w, int h, const void *px, int stride);

/* tri.c: the 3D engine by plain register writes (RD2): the state's registers, then each triangle as SE_VF_CNTL and
 * its vertices' words to SE_PORT_DATA0. radeon_3d holds the state as the registers take it (regs3d_radeon.h's
 * fields); a caller changes its words and sets radeon_3d_dirty, and the next triangle writes the registers that
 * differ from what the chip has. 2D drawing between leaves the 3D registers alone, but the engines take turns:
 * the layer waits for the other to be idle and clean when the kind of drawing changes. */
typedef struct {
    uint32_t pp_misc, pp_fog_color, re_solid_color, rb3d_blendcntl, rb3d_depthoffset, rb3d_depthpitch,
             rb3d_zstencilcntl, pp_cntl, rb3d_cntl, rb3d_coloroffset, re_width_height, rb3d_colorpitch, se_cntl,
             se_coord_fmt, re_top_left, rb3d_stencilrefmask, rb3d_ropcntl, rb3d_planemask, se_cntl_status;
    struct {
        uint32_t txfilter, txformat, txoffset, txcblend, txablend, tfactor, border_color;
        uint32_t cubic_faces, cubic_off[5];     /* a cube map's faces 1-4 sizes, 0-4 offsets (TXOFFSET: face 5) */
    } tex[3];
} radeon_ctx3d;
extern radeon_ctx3d radeon_3d;
extern int radeon_3d_dirty;             /* (set by a caller that changed radeon_3d's words) */
typedef struct {
    float x, y, z, rhw;                 /* window coordinates (pixel centres at halves), z 0..1, 1/w */
    uint32_t argb, spec;                /* (the specular's top byte: the fog factor) */
    float s, t, s2, t2, s3, t3;         /* (the three units' coordinates) */
    float q, q2, q3;                    /* a cube map unit's third coordinate (r, sent in q's place: Mesa's swtcl) */
} radeon_vertex;
/* The state for a colour buffer at color_off (pitch_bytes a row, 16 or 32 bits) and a depth buffer at z_off (16
 * bits, or 24 with stencil; z_off 0 and zbits 0: none): Mesa's defaults, vertices in window coordinates. */
void radeon_3d_init(uint32_t color_off, int pitch_bytes, int bpp, uint32_t z_off, int z_pitch_bytes, int zbits,
                    int width, int height);
void radeon_3d_emit(void);
void radeon_3d_tex_written(void);       /* texels written under a unit's texture: its registers out again */
/* A rectangle of the depth buffer (and the stencil's byte with Z24S8) set by the 3D engine: a quad with the colour
 * writes off and the depth test always passing, the state put back after (the DRM's R100 depth clears). z: the
 * depth's own value (16 or 24 bits); stencil < 0: the stencil left. Wanted where depth is tiled (RADEON_TILED_Z):
 * the 2D engine writes it as if linear. */
void radeon_3d_depth_clear(int x, int y, int w, int h, uint32_t z, int write_z, int stencil);
/* What a vertex carries after x, y, z: RADEON_VTX_* below, in this order: rhw, the colour, the specular and fog,
 * s and t (and q), s2 and t2 (and q2), s3 and t3 (and q3). */
#define RADEON_VTX_RHW   1
#define RADEON_VTX_ARGB  2
#define RADEON_VTX_SPEC  4
#define RADEON_VTX_ST    8
#define RADEON_VTX_ST2   16
#define RADEON_VTX_ST3   32
#define RADEON_VTX_Q     64                 /* (each unit's q after its s and t: a cube map's r) */
#define RADEON_VTX_Q2    128
#define RADEON_VTX_Q3    256
void radeon_3d_format(int what);
void radeon_tri(const radeon_vertex *a, const radeon_vertex *b, const radeon_vertex *c);
void radeon_poly(const radeon_vertex *v, int n);     /* a convex polygon of 3 to 16 vertices as one triangle fan */
void radeon_2d_begin(void);             /* (engine2d.c calls it: 2D after 3D waits for the 3D engine) */

/* tcl.c: the transform engine (RADEON_HAS_TCL chips; gl/docs/dx7.md X3), through the command processor only.
 * radeon_tcl's words are SE_TCL_OUTPUT_VTX_FMT's and the consecutive registers after it, written at radeon_tcl_emit
 * (the draws call it) where they changed. radeon_tcl_on: radeon_3d for vertices the engine transforms (the viewport
 * transform on, w as clip w) or, off, the window-space ones radeon_3d_init set up. The matrices: GL's column-major
 * into a slot (MODEL_PROJ 0, MODEL 1, MODEL_IT 2, TEXMAT_0..2 3..5: radeon_tcl_defaults' selects); vectors and
 * scalars into the engine's memories at a start and stride. A draw: an array of dwords-sized vertices at addr (in
 * the chip's space: radeon.fb_location + a video memory offset), as vfmt (RADEON_CP_VC_FRMT_*) says, n of them; or
 * n 16-bit indices into it (lists in pieces; a strip or fan at most RADEON_TCL_PIECE). */
typedef struct {
    uint32_t output_vtx_fmt, output_vtx_sel, matrix_select_0, matrix_select_1, ucp_vert_blend_ctl, texture_proc_ctl,
             light_model_ctl, per_light_ctl[4];
} radeon_tcl_regs;
extern radeon_tcl_regs radeon_tcl;
/* (300, as Mesa's radeon_tcl.c's GET_MAX_HW_ELTS: "testing shows a maximum around here". DOS-GL's first 4,092 hung
 * the P3's 7500 in Echoes' ECHB fly scene, 2026-11-25: a fence never reached, the PC down till a power cycle.
 * A multiple of 6, so a list's piece ends at a triangle's end.) */
#define RADEON_TCL_PIECE 300
void radeon_tcl_defaults(void);
void radeon_tcl_on(int yes);
uint32_t radeon_tcl_window_cntl(void);  /* SE_CNTL as the window-space draws have it (the engine's start from it) */
int  radeon_tcl_is_on(void);
extern int radeon_tcl_live;             /* (the same, for a test without a call) */
void radeon_tcl_forget(void);           /* (a new 3D state: everything written again) */
void radeon_tcl_emit(void);
void radeon_tcl_viewport(const float v[6]);     /* x scale, offset, y scale, offset, z scale, offset */
void radeon_tcl_vectors(unsigned start, unsigned stride, const float *v, int n);
void radeon_tcl_scalars(unsigned start, unsigned stride, const float *v, int n);
void radeon_tcl_matrix(int slot, const float m[16]);
void radeon_tcl_matrix_t(int slot, const float m[16]);  /* (as it is: an inverse's, for MODEL_IT) */
void radeon_tcl_guard_band(void);
void radeon_tcl_draw(uint32_t prim, uint32_t vfmt, uint32_t addr, int dwords, int n);
void radeon_tcl_draw_indexed(uint32_t prim, uint32_t vfmt, uint32_t addr, int dwords, const uint16_t *idx, int n);
void radeon_tcl_draw_indexed_s(uint32_t prim, uint32_t vfmt, uint32_t addr, int dwords, int stride, const uint16_t *idx,
                               int n);         /* (vertices stride dwords apart: LOAD_VBPNTR's stride field) */
extern int radeon_tcl_sync;            /* 5 (default): the 3D engine idle after every draw packet; 0 none */
extern int radeon_tcl_piece;           /* the indices a list's packet takes (RADEON_TCL_PIECE; tests only) */
extern int radeon_tcl_old_packets;      /* 1 (default): 3D_RNDR_GEN_INDX_PRIM, Mesa 7's; 0: LOAD_VBPNTR + DRAW_INDX */

/* chip.c: a surface over the depth buffer at off (1 KB aligned), bytes long (a multiple of 1 KB), pitch_bytes a row,
 * 16 or 32 bits (24-bit Z with its stencil): the CPU's reads and writes there translated to the chip's tiling, so
 * the CPU sees the buffer linear. 0, or -1 (not aligned). radeon_depth_surface_off: none (before text mode). */
int  radeon_depth_surface(uint32_t off, uint32_t bytes, int pitch_bytes, int bits);
void radeon_depth_surface_off(void);

/* cp.c: the command processor (RD3). radeon_cp_init loads the microcode (pairs: 256 x low, high words; NULL: the
 * embedded R100 table) and starts the chip fetching a ring of bytes (a power of two, 4 KB to 256 KB) in
 * conventional memory through its PCI GART: 0, or -1 with everything put back (drawing then stays on plain
 * register writes). While it runs, every drawing-engine register goes as a packet (radeon_out), and radeon_sync
 * waits for the ring. */
extern const uint32_t radeon_cp_microcode[256 * 2];
extern uint32_t radeon_cp_gart_start;
int  radeon_cp_init(const uint32_t *pairs, uint32_t bytes);
int  radeon_cp_running(void);
void radeon_cp_out(const uint32_t *d, int n);
void radeon_cp_reg(uint32_t reg, uint32_t v);
uint32_t *radeon_cp_reserve(int n);     /* n contiguous dwords at the write index to fill (NULL: not running) */
void radeon_cp_advance(int n);          /* ... and given to the ring */
void radeon_cp_flush(void);
int  radeon_cp_idle_wait(uint32_t timeout_us);
void radeon_cp_stop(void);
int  radeon_cp_abandon(void);           /* after a hang: the CP off at once, radeon_engine_recover; 0: back */
void radeon_cp_forget(void);
/* A vertex area in conventional memory beside the ring, through the same GART (the transform engine's vertices: the
 * CPU writes cached memory, the chip fetches it): radeon_cp_varea_want bytes (a multiple of 4 KB) asked for before
 * radeon_cp_init; radeon_cp_varea NULL when there is none (not asked, or DOS hadn't the room). Fences say when the
 * chip is past what was queued: radeon_cp_fence after it, radeon_cp_fence_wait (0, or -1 at the time-out). */
extern uint32_t radeon_cp_varea_want;
extern uint8_t *radeon_cp_varea;
extern uint32_t radeon_cp_varea_gart, radeon_cp_varea_bytes;
uint32_t radeon_cp_fence(void);
int  radeon_cp_fence_wait(uint32_t f, uint32_t timeout_us);
void radeon_cp_peek(uint32_t *rp, uint32_t *wp, const volatile uint32_t **base, uint32_t *dwords);
uint32_t radeon_cp_wptr(void);          /* (diagnostics: the CPU's write index, no register read) */
void radeon_out(uint32_t reg, uint32_t v);      /* a drawing-engine register, either way */
/* (engine2d.c) Bytes into video memory through the command stream, ordered with the drawing around them: texture
 * levels without an engine sync. off and bytes multiples of 32. 0, or -1 without the command processor. */
int  radeon_upload(uint32_t off, const uint32_t *data, uint32_t bytes);
extern void (*radeon_cp_trace)(const char *stage, uint32_t a, uint32_t b);     /* (a test's log of radeon_cp_init's steps) */

/* cursor.c: the hardware cursor (64 x 64), as the Rage 128 layer's: the AND and XOR masks, rows of 8 bytes, the
 * leftmost pixel in bit 7; stored at off (a multiple of 16); colours 0xRRGGBB; radeon_cursor_move takes the image's
 * top left corner, as far as 63 pixels off the top or the left. */
#define RADEON_CURSOR_BYTES 1024u
int  radeon_cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512]);
void radeon_cursor_colours(uint32_t c0, uint32_t c1);
void radeon_cursor_show(int on);
void radeon_cursor_move(int x, int y);

#endif
