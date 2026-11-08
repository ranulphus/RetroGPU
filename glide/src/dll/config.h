/* config.h - MGAGLIDE.CFG and environment options (PRD §8, §9). */
#ifndef MG_CONFIG_H
#define MG_CONFIG_H

typedef struct {
    char report_version[16];   /* grGlideGetVersion string */
    int  report_fb_mb;         /* reported Voodoo framebuffer MB */
    int  report_tmu_mb;        /* reported texture memory per TMU, MB */
    int  report_tmus;          /* reported TMU count */
    int  report_voodoo2;       /* report a Voodoo 2 board */
    int  force_z32;            /* -1 auto, 0 16-bit, 1 32-bit depth buffer */
    int  log_level;            /* 0 error .. 4 trace */
    int  exit_after;           /* end the run after N swaps (tests) */
    int  snap[8];              /* screenshot at these swap counts (86Box) */
    int  nsnap;
    int  trace;                /* write a Glide call trace */
    char trace_path[64];
    char retail_path[64];      /* GLTRACE.OVL: the retail runtime to forward to */
    int  trace_from, trace_to; /* frame window */
    int  stats_every;          /* MGL-STAT line every N swaps (0 = off) */
    int  g100_additive;        /* 0 lumstipple, 1 skip, 2 stipple50 */
    int  combine_strict;
    int  forced_bilinear;
    int  gamma_enable;
    int  census;               /* log distinct draw states (MGL-CENSUS) */
    int  hooks;                /* fault / exit hooks that restore the video (default 1) */
    int  bpp;                  /* enhancement: 32 = render in 8888 (default 16, as the Voodoo) */
    int  trilinear;            /* enhancement: G200+ blends mip levels (default 0) */
    int  hwmip;                /* enhancement: G200+ picks mip levels per pixel (default 0) */
    int  res_w, res_h;         /* enhancement: render at this size instead of the game's (res=WxH) */
    int  scale;                /* sizes the BIOS lacks: 1 scale into a BIOS mode (default), 0 top-left, 2 force */
    int  scale_filter;         /* 0 nearest (default), 1 bilinear */
    int  zoom;                 /* 1: half-size modes by the chip's line and pixel doubling */
    int  warp;                 /* 1 (default): the G400 family's WARP sets triangles up (docs/g400-warp.md); 0 the CPU */
    int  fast;                 /* with warp: 1 (default) whole-triangle draws by the fast path (draw.c); 0 the general path */
    int  agp;                  /* 1 (default): under GLOS, write-combining DMA rings fetched by AGP cycles; 0 PCI */
    int  dma_ring_kb;          /* with warp: the DMA rings' size in KB at most (128; 0: the HAL's, 256) */
    int  dma_list_div;         /* with warp: the list's share of the rings, 1/N (2; 0: the HAL's, 8) */
    int  dma_kick_kb;          /* with warp: KB of vertices between kicks (0: the HAL's, 6) */
} mg_config_t;

extern mg_config_t mg_config;
void mg_config_load(void);
const char *mg_getenv(const char *name);
int mg_read_file(const char *path, char *buf, int size);
int mg_write_file(const char *path, const void *data, int size, int append);

#endif
