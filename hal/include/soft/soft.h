/* soft.h - RetroGPU's software rasteriser core (gl/docs/b2.md): window-space
 * triangles with their attributes drawn into a colour buffer, a depth buffer
 * and a stencil buffer in memory, by a pipeline state that knows no API: GL's
 * software back end (gl/src/backend/soft) and, later, Glide's set it.
 *
 * Coordinates: x and y in pixels, y down, pixel (i, j)'s centre at
 * (i + 0.5, j + 0.5); positions snapped to 1/16 of a pixel. A pixel is drawn
 * when its centre is inside; a centre on an edge by the state's fill rule:
 * on a left edge always, on a top edge (SOFT_FILL_TOP_LEFT) or a bottom one
 * (SOFT_FILL_BOTTOM_LEFT, OpenGL's as llvmpipe draws it with y down).
 * Attributes are planes through the vertices, taken at pixel centres;
 * texture coordinates divided by w per pixel (perspective-correct).
 *
 * Lines and points (B2.5) follow OpenGL's rules as Mesa's llvmpipe applies
 * them, the references the conformance tests compare with: a line is a
 * parallelogram, its ends moved by the diamond-exit test, its integer width
 * across the minor axis, its attributes varying along the major axis only;
 * a point is a square of its integer size (odd sizes about the pixel the
 * point is in, even ones about the nearest pixel corner), its attributes
 * the vertex's. */
#ifndef SOFT_SOFT_H
#define SOFT_SOFT_H
#include <stdint.h>

/* Colour buffer formats. RGB332 is a fixed palette (red and green 3 bits,
 * blue 2), ordered-dithered (4x4) when the state's dither is set; the others
 * are written undithered. XRGB8888 keeps no destination alpha (it reads as 1). */
enum { SOFT_RGB565, SOFT_XRGB8888, SOFT_RGB332 };

typedef struct {
    uint8_t *color;
    int      color_pitch;           /* bytes */
    int      format;                /* SOFT_* */
    uint8_t *depth;                 /* NULL: none */
    int      depth_pitch, depth_bits;   /* 16 or 32 */
    uint8_t *stencil;               /* NULL: none (8 bits a pixel) */
    int      stencil_pitch;
    int      width, height;
} soft_target;

/* A vertex in window space. z 0..1; oow = 1/w (the clip w), for
 * perspective; colour 0..1; texture coordinates per unit (s, t, and q:
 * the texture coordinates' own w, 1 for GL 1.1's 2D ones); fog: its fog
 * coordinate (GL's eye distance), from which the fog factor is worked out
 * at each pixel. */
typedef struct {
    float x, y, z, oow;
    float r, g, b, a;
    float tex[2][3];
    float fog;
} soft_vertex;

/* Comparison functions, as GL numbers them less 0x200 (GL_NEVER .. GL_ALWAYS). */
enum { SOFT_NEVER, SOFT_LESS, SOFT_EQUAL, SOFT_LEQUAL, SOFT_GREATER, SOFT_NOTEQUAL, SOFT_GEQUAL, SOFT_ALWAYS };

/* Textures: levels of RGBA8 texels (rows of w; level 0 the largest, each
 * next half the size), sampled as OpenGL samples. */
#define SOFT_MAX_LEVELS 12
enum { SOFT_REPEAT, SOFT_CLAMP, SOFT_CLAMP_TO_EDGE };
enum { SOFT_NEAREST, SOFT_LINEAR, SOFT_NEAREST_MIPMAP_NEAREST, SOFT_LINEAR_MIPMAP_NEAREST,
       SOFT_NEAREST_MIPMAP_LINEAR, SOFT_LINEAR_MIPMAP_LINEAR };
typedef struct {
    const uint8_t *rgba;
    int w, h;
} soft_level;
typedef struct {
    soft_level level[SOFT_MAX_LEVELS];
    int levels;                     /* 1 without mipmaps */
    int wrap_s, wrap_t;
    int min_filter, mag_filter;
    float border[4];
} soft_texture;

/* A texture stage's colour and alpha: GL_ARB_texture_env_combine's
 * functions of up to three arguments, each a source through an operand,
 * times a scale (1, 2 or 4), clamped. GL's other environments are said in
 * these terms by GL's back end. */
enum { SOFT_C_REPLACE, SOFT_C_MODULATE, SOFT_C_ADD, SOFT_C_ADD_SIGNED, SOFT_C_INTERPOLATE, SOFT_C_SUBTRACT };
enum { SOFT_S_TEXTURE, SOFT_S_CONSTANT, SOFT_S_PRIMARY, SOFT_S_PREVIOUS };
enum { SOFT_O_COLOR, SOFT_O_ONE_MINUS_COLOR, SOFT_O_ALPHA, SOFT_O_ONE_MINUS_ALPHA };
typedef struct {
    int func, src[3], op[3];
    float scale;
} soft_combine;
typedef struct {
    const soft_texture *tex;        /* NULL: the stage is off */
    int coord;                      /* which of the vertex's coordinates (0 or 1) */
    soft_combine rgb, alpha;
    float constant[4];
} soft_stage;

/* Blend factors (GL 1.1's), fog modes and stencil operations. */
enum { SOFT_B_ZERO, SOFT_B_ONE, SOFT_B_SRC_COLOR, SOFT_B_ONE_MINUS_SRC_COLOR, SOFT_B_DST_COLOR,
       SOFT_B_ONE_MINUS_DST_COLOR, SOFT_B_SRC_ALPHA, SOFT_B_ONE_MINUS_SRC_ALPHA, SOFT_B_DST_ALPHA,
       SOFT_B_ONE_MINUS_DST_ALPHA, SOFT_B_SRC_ALPHA_SATURATE };
enum { SOFT_FOG_LINEAR, SOFT_FOG_EXP, SOFT_FOG_EXP2 };
enum { SOFT_SO_KEEP, SOFT_SO_ZERO, SOFT_SO_REPLACE, SOFT_SO_INCR, SOFT_SO_DECR, SOFT_SO_INVERT };
enum { SOFT_FILL_TOP_LEFT, SOFT_FILL_BOTTOM_LEFT };

typedef struct {
    int      scissor[4];            /* x0, y0, x1, y1 (exclusive), within the target */
    int      fill_rule;             /* SOFT_FILL_*: a centre on a horizontal edge */
    unsigned color_mask;            /* bit 0 red, 1 green, 2 blue, 3 alpha */
    int      dither;                /* RGB332: ordered dither (GL_DITHER) */
    int      smooth;                /* 0: flat (the first vertex's colour) */
    int      depth_test, depth_func, depth_write;
    soft_stage stage[2];
    int      fog, fog_mode;
    int      fog_per_vertex;        /* the factor at the vertices, interpolated (else per pixel from the coordinate) */
    float    fog_start, fog_end, fog_density, fog_color[3];
    int      alpha_test, alpha_func; /* SOFT_NEVER .. SOFT_ALWAYS */
    float    alpha_ref;
    int      stencil_test, stencil_func, stencil_fail, stencil_zfail, stencil_zpass;
    unsigned stencil_ref, stencil_mask, stencil_writemask;
    int      blend, blend_src, blend_dst;
} soft_state;

void soft_target_set(const soft_target *t);
void soft_state_set(const soft_state *s);

/* Clears inside the scissor, through the colour mask: colour 0..1,
 * depth 0..1, the stencil's 8 bits; each when its flag is set. */
#define SOFT_CLEAR_COLOR   1u
#define SOFT_CLEAR_DEPTH   2u
#define SOFT_CLEAR_STENCIL 4u
void soft_clear(unsigned what, const float rgba[4], double depth, unsigned stencil);

void soft_triangle(const soft_vertex *a, const soft_vertex *b, const soft_vertex *c);

/* A line from a to b, width pixels wide (rounded to a whole number, at
 * least 1); flat shading takes b's colour (OpenGL's provoking vertex). A
 * point of size pixels (rounded, at least 1). */
void soft_line(const soft_vertex *a, const soft_vertex *b, float width);
void soft_point(const soft_vertex *v, float size);

/* texture.c: a texture sampled at (s, t) as OpenGL samples, its level of
 * detail from the coordinates' derivatives on the screen; RGBA 0..1. And a
 * stage's combine of its texel, the constant, the primary colour and the
 * previous stage's (RGBA 0..1 each). */
void soft_sample(const soft_texture *tx, double s, double t, double dsdx, double dsdy, double dtdx, double dtdy,
                 float out[4]);
void soft_combine_apply(const soft_stage *st, const float tex[4], const float prim[4], const float prev[4],
                        float out[4]);

/* Tiles (gl/docs/b2.md B2.7). With a tile size set, triangles, lines,
 * points and clears are kept, each with the state in force (the textures'
 * descriptions copied, their texels not), binned by the tiles they touch,
 * and drawn tile by tile in the order given at soft_flush: the same pixels,
 * bit for bit, as drawing each at once. Until then the target's buffers and
 * the texels the kept work samples must stay as they are; soft_target_set
 * flushes when the target changes, and the core flushes when its store is
 * full. soft_epoch() changes at each flush: a texture used since the last
 * flush was used in the current epoch. */
void soft_tiles(int size);          /* 0: draw at once (the default); else the tiles' side in pixels */
void soft_flush(void);              /* draw everything kept */
void soft_drop(void);               /* forget everything kept (its buffers are going) */
int  soft_pending(void);            /* 1: something is kept */
unsigned soft_epoch(void);

/* The spans (B2.8): integer routines where the state allows (the default),
 * or the reference span (double precision) for everything with on = 1. */
void soft_exact(int on);

/* Counters. */
typedef struct {
    uint32_t tris, lines, points, pixels;
    uint32_t flushes, overflows;    /* tiles: flushes, and of them those for a full store */
} soft_stats;
extern soft_stats soft_count;

#endif
