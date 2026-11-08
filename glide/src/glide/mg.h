/* mg.h - MGA-Glide runtime context: the Glide state mirror, the display
 * surfaces and the translation state shared by the src/glide modules. */
#ifndef MG_H
#define MG_H
#include "glide/entry.h"
#include "mga/hal.h"
#include "rt/rt.h"
#include "dll/config.h"

#ifndef MGA_GLIDE_VERSION
#define MGA_GLIDE_VERSION "dev"
#endif

#define MG_MAX_BUFFERS 3

typedef struct {
    /* Colour / alpha combine (grColorCombine / grAlphaCombine). */
    GrCombineFunction_t cc_func;  GrCombineFactor_t cc_factor;
    GrCombineLocal_t    cc_local; GrCombineOther_t  cc_other; FxBool cc_invert;
    GrCombineFunction_t ac_func;  GrCombineFactor_t ac_factor;
    GrCombineLocal_t    ac_local; GrCombineOther_t  ac_other; FxBool ac_invert;
    GrColor_t           constant_color;
    /* Blending and alpha test. */
    GrAlphaBlendFnc_t   blend_src, blend_dst, blend_asrc, blend_adst;
    GrCmpFnc_t          alpha_test_func;
    GrAlpha_t           alpha_test_ref;
    /* Depth. */
    GrDepthBufferMode_t depth_mode;
    GrCmpFnc_t          depth_func;
    FxBool              depth_mask;
    FxI32               depth_bias;
    /* Rasterisation. */
    GrCullMode_t        cull;
    GrDitherMode_t      dither;
    FxBool              color_mask_rgb, color_mask_a;
    int                 clip_x0, clip_y0, clip_x1, clip_y1;
    GrOriginLocation_t  origin;
    /* Fog. */
    GrFogMode_t         fog_mode;
    GrColor_t           fog_color;
    GrFog_t             fog_table[GR_FOG_TABLE_SIZE];
    /* Chroma key. */
    GrChromakeyMode_t   chroma_mode;
    FxBool              alpha_itrgb;    /* grAlphaControlsITRGBLighting */
    FxBool              delta0;         /* ITRGB_DELTA0 presets: flat iterated colour */
    uint32_t            delta0_argb;    /* grConstantColorValue4 */
    GrColor_t           chroma_value;
    /* Hints. */
    FxU32               stw_hint;
} mg_state;

typedef struct {
    int         initialised;
    int         open;
    int         sst;                 /* selected board */
    mga_vbe_mode mode;
    int         width, height, pitch_px;
    int         bpp;                 /* colour depth: 16, or 32 with MGAGLIDE bpp=32 */
    int         scaled;              /* MGAGLIDE res=WxH: rendering larger than the game's size */
    double      sx, sy;              /* hardware pixels per game pixel */
    int         nbuffers, has_aux;
    uint32_t    buf_off[MG_MAX_BUFFERS];
    uint32_t    aux_off;
    int         zbits;               /* 16 or 32 */
    int         front, back;         /* indices into buf_off */
    /* How the mode planner shows the game (hal.h, vbe_plan_mode). For a
     * scaled fit, buf_off[] are render buffers at pitch_px and each swap
     * scales the finished one into the hidden display buffer, then flips
     * the display. rw x rh is the drawable area of the buffers. */
    mga_mode_plan plan;
    int         present_scaled;
    uint32_t    disp_off[2];
    int         disp_front, disp_pitch_px;
    int         rw, rh;
    GrBuffer_t  render_buffer;
    uint32_t    heap_off, heap_end;  /* texture heap in VRAM */
    GrColorFormat_t color_format;
    uint32_t    frame;
    mg_state    st;
    unsigned    dirty;
    GrErrorCallbackFnc_t error_cb;
    float       gamma;
    int         fog_on;         /* MACCESS.FOGEN wanted by the current draw */
    int         fogcol_valid;   /* fogcol_hw holds what FOGCOL contains */
    uint32_t    fogcol_hw;
    char        version[32];
} mg_ctx;

extern mg_ctx mg;
void mg_present_front(void);          /* buffer.c: show the front render buffer (scaled fits) */

enum { MG_DIRTY_TARGET = 1u << 0, MG_DIRTY_CLIP = 1u << 1, MG_DIRTY_RASTER = 1u << 2 };

/* sst.c */
int  mg_device_open(void);
void mg_fatal(const char *msg);
/* state.c */
void mg_state_defaults(void);
void mg_validate(void);
void mg_state_forget(void);        /* draw.c: the texture state and ALPHACTRL to be written again */
void mg_warp_stop(void);           /* draw.c: the WARP suspended, its DMA stream sent */
uint32_t mg_color_to_argb(GrColor_t c);
GrColor_t mg_argb_to_color(uint32_t argb);
uint32_t mg_argb_to_565(uint32_t argb);
/* buffer.c */
void mg_target_for(GrBuffer_t b);
uint32_t mg_buffer_offset(GrBuffer_t b);
/* draw.c */
void mg_draw_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c);

/* depth.c */
double   mg_wdepth_from_oow(float oow, int zbits);
double   mg_oow_from_wcode(unsigned code);
unsigned mg_wcode_from_oow(double oow);

/* fog.c */
int    mg_fog_source(void);                 /* GR_FOG_* source bits of the fog mode */
double mg_fog_table_at(double oow);
double mg_fog_vertex(const GrVertex *v);    /* 0 = no fog .. 255 = fog colour */
void   mg_fog_emit_color(void);
uint32_t mg_wdepth_from_code(FxU16 code, int zbits);
uint32_t mg_depth_clear_value(FxU16 depth);
/* combine.c */
void mg_note_approx(void);
void mg_census(void);
/* frame.c */
typedef struct {
    uint32_t tris;
    uint32_t tri_processed, tri_drawn;
    uint32_t bands, banded;
    uint32_t fast, fast_bands;  /* by the fast path; sent the general way for bands of levels */
} mg_stats_t;
extern mg_stats_t mg_stats;
void mg_frame_end(void);

/* Glide's profiling stages (MGA_PROF builds, hal/include/mga/prof.h): the
 * MGL-PROF line at each MGL-STAT. Time in LFB and texture API calls stays
 * in app. */
#include "mga/prof.h"
enum {
    PROF_G_VTX = PROF_CLIENT,   /* draw calls: vertices, culling, plans, bands (setup excluded) */
    PROF_G_TEXBIND,             /* binding the level(s) and emitting texture state */
    PROF_G_TEXDL,               /* converting and writing a level into VRAM */
    PROF_G_DRAIN,               /* grBufferSwap waiting for the engine */
    PROF_G_VSYNC,               /* grBufferSwap waiting for retrace */
    PROF_G_SWAP,                /* the rest of grBufferSwap (present, flip, statistics) */
    PROF_G_FAST,                /* within a triangle: the fast path (draw.c), all of it */
    PROF_G_VERT,                /* within a triangle: setup vertices, alpha, fog and validation */
    PROF_G_LOD,                 /* within a triangle: the level planes and the bands */
    PROF_G_WARP                 /* within a triangle: the WARP's vertices */
};

/* FPU control for entry points that do float maths (see fp.h). */
#include "mga/fp.h"

void mg_gamma_apply(void);
void mg_hooks_install(void);
/* Game coordinates to hardware pixels (resolution override). */
int  mg_hx(int x);
int  mg_hy(int y);

#endif
