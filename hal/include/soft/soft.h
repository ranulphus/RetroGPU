/* soft.h - RetroGPU's software rasteriser core (gl/docs/b2.md): window-space
 * triangles with their attributes drawn into a colour buffer, a depth buffer
 * and a stencil buffer in memory, by a pipeline state that knows no API: GL's
 * software back end (gl/src/backend/soft) and, later, Glide's set it.
 *
 * Coordinates: x and y in pixels, y down, pixel (i, j)'s centre at
 * (i + 0.5, j + 0.5); positions snapped to 1/16 of a pixel. A pixel is drawn
 * when its centre is inside, an edge's centres by the top-left rule.
 * Attributes are planes through the vertices, taken at pixel centres;
 * texture coordinates divided by w per pixel (perspective-correct). */
#ifndef SOFT_SOFT_H
#define SOFT_SOFT_H
#include <stdint.h>

/* Colour buffer formats. */
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
 * the texture coordinates' own w, 1 for GL 1.1's 2D ones); fog factor 0..1
 * (1: none of the fog colour). */
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

typedef struct {
    int      scissor[4];            /* x0, y0, x1, y1 (exclusive), within the target */
    unsigned color_mask;            /* bit 0 red, 1 green, 2 blue, 3 alpha */
    int      smooth;                /* 0: flat (the first vertex's colour) */
    int      depth_test, depth_func, depth_write;
    soft_stage stage[2];
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

/* texture.c: a texture sampled at (s, t) as OpenGL samples, its level of
 * detail from the coordinates' derivatives on the screen; RGBA 0..1. And a
 * stage's combine of its texel, the constant, the primary colour and the
 * previous stage's (RGBA 0..1 each). */
void soft_sample(const soft_texture *tx, double s, double t, double dsdx, double dsdy, double dtdx, double dtdy,
                 float out[4]);
void soft_combine_apply(const soft_stage *st, const float tex[4], const float prim[4], const float prev[4],
                        float out[4]);

/* Counters. */
typedef struct {
    uint32_t tris, pixels;
} soft_stats;
extern soft_stats soft_count;

#endif
