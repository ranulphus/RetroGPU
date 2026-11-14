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

typedef struct {
    int      scissor[4];            /* x0, y0, x1, y1 (exclusive), within the target */
    unsigned color_mask;            /* bit 0 red, 1 green, 2 blue, 3 alpha */
    int      smooth;                /* 0: flat (the first vertex's colour) */
    int      depth_test, depth_func, depth_write;
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

/* Counters. */
typedef struct {
    uint32_t tris, pixels;
} soft_stats;
extern soft_stats soft_count;

#endif
