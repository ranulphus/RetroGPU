/* gl_tex.h - textures: conversion and texture objects (GL's side; the
 * drawing back end keeps its own copy: src/backend/mga/mga_be.h). */
#ifndef DGL_GL_TEX_H
#define DGL_GL_TEX_H
#include "gl_state.h"
#include <stdint.h>

enum { DGL_ALPHA_OPAQUE, DGL_ALPHA_BINARY, DGL_ALPHA_GRADIENT };
/* Internal format classes (what the texture keeps of its texels). */
enum { DGL_IF_RGBA, DGL_IF_RGB, DGL_IF_RGB5_A1, DGL_IF_LUMINANCE, DGL_IF_LUMINANCE_ALPHA, DGL_IF_ALPHA,
       DGL_IF_INTENSITY, DGL_IF_INDEX };

/* texconv.c */
int      dgl_alpha_class(const unsigned char *rgba, long n);
int      dgl_to_rgba(GLenum format, const unsigned char *src, long n, unsigned char *dst);
int      dgl_ifmt_class(GLint internalformat);                 /* DGL_IF_*, -1 if not a GL 1.1 format */
void     dgl_apply_ifmt(int ifc, unsigned char *rgba, long n);  /* keep what the class keeps */
int      dgl_format_bytes(GLenum format);

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
    int      dirty;                 /* its levels changed since the back end's copy was made */
    dgl_palette *own;               /* its GL_TEXTURE_2D colour table, if loaded */
    void    *be;                    /* the drawing back end's part (its copy), NULL until it makes one */
} dgl_texture;
/* Every texture object, in the table's order (texture.c). */
int dgl_texture_count(void);
dgl_texture *dgl_texture_nth(int i);    /* NULL for an empty place */

/* Engine syncs (texture.c): everything drawn (the device's sync). */
int  dgl_sync(void);                            /* 0 = idle, -1 = timed out */

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

/* palette.c */
void dgl_palettes_reset(void);
const dgl_palette *dgl_palette_for(const dgl_texture *t);           /* shared or its own */
const unsigned char *dgl_palette_texel(const dgl_palette *p, unsigned i);
void dgl_textures_reset(uint32_t heap_start, uint32_t heap_end);   /* every texture gone; the back end's memory */

#endif
