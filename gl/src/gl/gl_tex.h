/* gl_tex.h - textures: conversion, the VRAM heap and texture objects. */
#ifndef DGL_GL_TEX_H
#define DGL_GL_TEX_H
#include "gl_state.h"
#include <stdint.h>

/* Hardware texel formats (TEXCTL.texformat codes). */
enum { DGL_TW8 = 1, DGL_TW15 = 2, DGL_TW16 = 3, DGL_TW12 = 4 };   /* TW8: indices into the texture LUT */
enum { DGL_ALPHA_OPAQUE, DGL_ALPHA_BINARY, DGL_ALPHA_GRADIENT };
/* Internal format classes (what the texture keeps of its texels). */
enum { DGL_IF_RGBA, DGL_IF_RGB, DGL_IF_RGB5_A1, DGL_IF_LUMINANCE, DGL_IF_LUMINANCE_ALPHA, DGL_IF_ALPHA,
       DGL_IF_INTENSITY, DGL_IF_INDEX };

/* texconv.c */
int      dgl_alpha_class(const unsigned char *rgba, long n);
int      dgl_hwfmt_for_class(int cls);
uint16_t dgl_pack_texel(int hwfmt, unsigned r, unsigned g, unsigned b, unsigned a);
void     dgl_convert_rgba(int hwfmt, const unsigned char *rgba, long n, uint16_t *out);
int      dgl_to_rgba(GLenum format, const unsigned char *src, long n, unsigned char *dst);
int      dgl_ifmt_class(GLint internalformat);                 /* DGL_IF_*, -1 if not a GL 1.1 format */
void     dgl_apply_ifmt(int ifc, unsigned char *rgba, long n);  /* keep what the class keeps */
uint16_t dgl_pack_grey565(unsigned l);                          /* RGB565 grey, green from red */
/* n RGBA8 texels in hwfmt (TW16, TW15, TW12), as dgl_pack_texel, or from red as
 * dgl_pack_grey565 when grey and TW16. */
void dgl_pack_row(int hwfmt, int grey, const unsigned char *rgba, long n, uint16_t *out);
int      dgl_format_bytes(GLenum format);

/* vram.c */
void     dgl_vram_init(uint32_t start, uint32_t end);
int      dgl_vram_alloc(uint32_t size, uint32_t *off);     /* 0 ok, -1 out of memory */
void     dgl_vram_free(uint32_t off);
uint32_t dgl_vram_used(void);
int      dgl_vram_blocks(void);
uint32_t dgl_vram_largest_free(void);
int      dgl_vram_retire(uint32_t off);   /* free after the next sync; -1 = list full, sync first */
int      dgl_vram_retired(void);           /* blocks waiting for a sync */
void     dgl_vram_sync_done(void);         /* the engine is idle: free the retired blocks */

/* texture.c */
#define DGL_MAX_LEVELS 12

typedef struct {
    int      w, h;                 /* as uploaded */
    int      ifc;                  /* internal format class, DGL_IF_* */
    GLint    ifmt;                 /* the internal format as given */
    unsigned char *rgba;           /* shadow copy, RGBA8, already reduced to the class */
    unsigned char *idx;            /* DGL_IF_INDEX instead: the colour indices */
} dgl_level;
#define DGL_LEVEL_DEFINED(L) ((L)->rgba || (L)->idx)

/* A colour table (palette.c): 256 entries at most, reduced to its internal
 * format; gen changes with every load. */
typedef struct {
    unsigned char rgba[256 * 4];
    int      width;                 /* entries, a power of two; 0 = none */
    int      opaque;                /* every entry has alpha 255 */
    GLenum   ifmt;
    unsigned gen;
} dgl_palette;

typedef struct dgl_texture {
    GLuint   name;
    int      used;
    dgl_level level[DGL_MAX_LEVELS];
    GLenum   min_filter, mag_filter, wrap_s, wrap_t;
    int      max_level;
    /* Hardware copy: every defined level from 0, in one format. */
    int      resident, dirty, hwfmt, hw_levels;
    int      grey;                  /* luminance or intensity: RGB565 greys stay grey */
    uint32_t vram_off, level_off[DGL_MAX_LEVELS], vram_size;
    int      hw_w_log2, hw_h_log2;  /* level 0 as stored (>= 8 texels) */
    uint32_t drawn;                 /* dgl_sync_epoch + 1 when last drawn, 0 = never */
    dgl_palette *own;               /* its GL_TEXTURE_2D colour table, if loaded */
    const dgl_palette *pal_used;    /* the palette and generation the VRAM copy was expanded with */
    unsigned pal_gen;
} dgl_texture;

/* Engine syncs (texture.c): dgl_sync waits for the engine to go idle and
 * counts completed syncs in dgl_sync_epoch. A texture drawn since the last
 * completed sync is busy: queued draws may still read its VRAM copy. */
extern uint32_t dgl_sync_epoch;
int  dgl_sync(void);                            /* 0 = idle, -1 = timed out */
#define dgl_texture_busy(t) ((t)->drawn == dgl_sync_epoch + 1)
#define dgl_texture_drawn(t) ((t)->drawn = dgl_sync_epoch + 1)

/* Texture traffic, printed and reset by DGL_STATS=2 (DGL-TEX). */
typedef struct {
    unsigned long uploads, upload_bytes;    /* whole textures written to VRAM */
    unsigned long sub_fast, sub_full;       /* glTexSubImage2D: rectangle written / whole re-upload */
    unsigned long sub_sync;                 /* rectangles written after waiting for the engine */
    unsigned long sub_iload;                /* rectangles written through the engine (ILOAD) */
    unsigned long sub_move;                 /* of sub_fast, those into a new block (sub_move) */
    unsigned long lut_loads;                /* palettes loaded into the texture LUT (TW8) */
    unsigned long renames, evictions, syncs;
} dgl_tex_counts;
extern dgl_tex_counts dgl_texc;

dgl_texture *dgl_unit_texture(int unit);        /* a unit's bound texture; NULL when none or incomplete */
/* A texture unit's environment (glTexEnv): the mode, its colour, and
 * GL_ARB_texture_env_combine's parameters for mode GL_COMBINE_ARB. */
typedef struct {
    GLenum  mode;
    GLfloat color[4];
    GLenum  combine_rgb, combine_alpha;
    GLenum  src_rgb[3], src_alpha[3], op_rgb[3], op_alpha[3];
    GLfloat rgb_scale, alpha_scale;
} dgl_texenv;
const dgl_texenv *dgl_tex_env(int unit);
int dgl_texture_units(void);                    /* ext.c: 2 with the G400's combiner, else 1 */
GLenum dgl_tex_env_mode(int unit);
const GLfloat *dgl_tex_env_color(int unit);
dgl_palette *dgl_bound_palette(void);           /* the bound texture's own table (created) */
void dgl_texture_lut(const dgl_texture *t);     /* before drawing a TW8 texture: its palette in the LUT */

/* palette.c */
void dgl_palettes_reset(void);
const dgl_palette *dgl_palette_for(const dgl_texture *t);           /* shared or its own */
const unsigned char *dgl_palette_texel(const dgl_palette *p, unsigned i);
int  dgl_texture_ready(dgl_texture *t);         /* upload if needed; 0 = drawable */
void dgl_textures_reset(uint32_t heap_start, uint32_t heap_end);
int  dgl_white_texture(uint32_t *off);          /* resident 8x8 white TW16 */

#endif
