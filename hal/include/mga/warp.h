/* warp.h - the WARP setup engine of the G400 (docs/g400-warp.md) and the
 * G200 (docs/g200-warp.md): triangles set up by the chip from
 * floating-point vertices (the pipe in the WARPs' instruction memories;
 * vertices by bus-master DMA, or through the DMA window by the CPU). */
#ifndef MGA_WARP_H
#define MGA_WARP_H
#include "mga/types.h"

/* One vertex as the pipes take it (Mesa's mga vertex). */
typedef struct {
    float    x, y;      /* pixels, with the subpixel offset WARP_SUBPIXEL_X/Y added */
    float    z;         /* 0..1 of the depth buffer's range */
    float    rhw;       /* 1/w */
    uint32_t bgra;      /* colour: blue in the low byte, alpha in the top */
    uint32_t spec;      /* specular colour (BGR) and the fog factor in the top byte (255: no fog) */
    float    u0, v0;    /* map 0, not divided by w (1.0 = one texture width) */
    float    u1, v1;    /* map 1 (WARP_T2 pipes) */
} mga_wvtx;

#define WARP_SUBPIXEL_X (-0.5f)     /* Mesa's mga driver's offsets */
#define WARP_SUBPIXEL_Y (-0.375f)

/* Pipe flags (Linux's mga_drm.h numbers the pipes by them). */
enum { WARP_F = 1, WARP_A = 2, WARP_S = 4, WARP_T2 = 8 };

/* 1 when the chip has a WARP this HAL drives: the G400 family, and the
 * G200 unless warp_g200 is 0 (docs/g200-warp.md). The G200 has pipes 0-7
 * only (no WARP_T2) and no sequencer (warp_reserve_seq returns NULL). */
int  warp_available(void);
extern int warp_g200;
/* The pipe loaded and started (loaded only if another one is in the
 * instruction memories). 0, or -1 when the WARP did not start: then
 * nothing may be sent (a WARP that takes no vertices stalls the bus). */
int  warp_select(int pipe);
/* WR54/WR62 (map 0) and WR52/WR60 (map 1), or the G200's WR24/WR34 (map
 * 0 only): TEXWIDTH/TEXHEIGHT values for the pipe's texture scaling, 0 for
 * none. */
void warp_tex_sizes(uint32_t w0, uint32_t h0, uint32_t w1, uint32_t h1);
/* A map's size for the WARP: TEXWIDTH and TEXHEIGHT as Mesa's mga driver
 * writes them (tw = log2 + 11 on a G400, + 28 on a G200; rfw = 2 - log2:
 * the microcode scales the TMRs it writes by them) and the pipe's WR
 * registers (WR54/WR62 on the G400, WR24/WR34 on the G200). Map 1 (the
 * G400 only) is written through TEXCTL2.map1; texctl2 is TEXCTL2's value
 * without it. */
void warp_tex(int map, int log2w, int log2h, uint32_t texctl2);
/* n vertices, three a triangle: into the DMA buffer (sent when it fills or
 * before the next register write), or through the DMA window in vertex
 * mode. 0, or -1 when the chip stopped taking them (the rest is not sent;
 * the WARP is suspended and the engine reset). */
int  warp_vertices(const mga_wvtx *v, int n);
/* The same without a copy: room for n vertices (whole triangles) in the DMA
 * stream, where the caller writes each one's dwords (*dwords of them: 8, or
 * 10 for the T2 pipes, in mga_wvtx's order) and then calls warp_commit(n).
 * NULL when there is no stream (warp_vertices then). */
uint32_t *warp_reserve(int n, int *dwords);
void warp_commit(int n);
/* A strip (fan 0) or fan (fan 1) of n vertices by the WARPs' sequencer, as
 * GL_TRIANGLE_STRIP and GL_TRIANGLE_FAN make triangles of them (any
 * winding: the WARPs cull nothing): room for them in the DMA stream, 8
 * dwords each (10 for the T2 pipes), then warp_commit_seq(n). NULL as
 * warp_reserve: the caller sends triangles. */
uint32_t *warp_reserve_seq(int fan, int n, int *dwords);
void warp_commit_seq(int n);
/* Diagnostic: a list of DMAPAD packets (up to the list's size) fetched by
 * the primary channel; its bytes, and the ticks of tsc() it took in *ticks. */
uint32_t warp_dma_bandwidth(uint32_t bytes, uint64_t (*tsc)(void), uint64_t *ticks, void (*flush)(void));
/* Diagnostic: ntris copies of triangle t in one batch, caches flushed, then
 * the ticks from the kick to the fetch's end (*fetch) and the engine idle
 * (*done). The number of triangles sent, or -1. */
int warp_dma_vertex_bandwidth(const mga_wvtx *t, int ntris, uint64_t (*tsc)(void), uint64_t *fetch, uint64_t *done,
                              void (*flush)(void));
/* The DMA stream's fetches by AGP cycles (PRIMEND/SECEND bit 1): set when
 * the rings are write-combining and AGP is on. warp_pre_kick, EXPERIMENT
 * (WARPTRI): called before each kick. */
extern int warp_agp_xfer;
extern void (*warp_pre_kick)(void);
/* The DMA window back in general-purpose mode, the WARP suspended (before
 * the engine is used without it, or by another program). */
void warp_stop(void);
/* After a fault: the command stream and the WARP forgotten without waiting
 * for the chip (the caller resets the engine next). */
void warp_abort(void);
/* Triangles and failures since start-up; DMA kicks (warp_dma_kicks) when the
 * vertices go by bus mastering; strips and fans sent whole (warp_seqs, of
 * them warp_seqs_t2 by the T2 pipes). */
extern uint32_t warp_tris, warp_failures, warp_dma_kicks, warp_seqs, warp_seqs_t2;
/* 1 (the default): vertices by bus-master DMA from conventional memory when
 * the port can provide it (primary DMA in vertex mode); 0: through the DMA
 * window by the CPU. Read at the first warp_select. */
extern int warp_use_dma;
/* 1 (default): write-combining rings fetched by AGP cycles when GLOS gives
 * them (GLOS's DRAW CACHE, docs/g400-warp.md); 0: cached rings, PCI cycles. The
 * G200 keeps cached rings and PCI unless warp_agp_rate is set (its AGP drew
 * nothing on the real card: docs/g200-warp.md). */
extern int warp_use_agp;
/* With AGP: 0 (default) the fastest rate the card and the bridge both have;
 * 1 or 2 at most 1x or 2x. */
extern int warp_agp_rate;
extern uint32_t warp_ring_bytes;        /* the rings' size at most (256 KB) */
extern uint32_t warp_kick_bytes;        /* vertex bytes between kicks (6 KB) */
extern int warp_list_div;               /* the list's share of the rings' block: 1/warp_list_div (8) */
/* What dma_setup got: 0 no rings, 1 cached (PCI), 2 write-combining (PCI),
 * 3 write-combining by AGP; and their size. */
extern int warp_ring_mode;
extern uint32_t warp_ring_kb;
extern int warp_wc_status;              /* write-combining rings: 0 not asked, 1 given, -1 no DOS memory, -2 refused */
/* DMAPAD writes just before the start (Linux's mga DRM writes three: "Padding
 * required due to hardware bug"). */
extern int warp_start_pads;
/* 1 (the default): a soft reset (engine_reset) before each load of the
 * instruction memories; 0 only for experiments. */
extern int warp_reset_before_load;
/* The last start: STATUS at warp_select, before the start and after it,
 * then WARP0's and WARP1's instruction addresses (WIADDRNB, WIADDRNB1). */
extern uint32_t warp_diag[5];

#endif
