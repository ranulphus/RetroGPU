/* texexp - silicon experiment E9: texture coordinates through the CPU's
 * triangle setup against the G400's WARP (docs/silicon-experiments.md).
 *
 * On the real G400 the Arena (DOSBench G1ARNA) drew some textures displaced
 * through the CPU setup, while the WARP's frames matched 86Box. This draws
 * sixteen cases, one per 160x120 cell, each varying one property of the
 * texture or its coordinates, and saves the frame as E9<tag>.PPM; the
 * program runs once with DGL_WARP=0 and once with DGL_WARP=1, and
 * tools/silicon/texexp.py compares the cells.
 *
 *   row 0  flat (w = 1), a 64x64 gradient grid magnified 4x, nearest:
 *          coordinates near 0; 8 periods out; 100 periods out; bilinear
 *   row 1  a floor receding to w = 8: 4 repeats; 32 repeats; 32 repeats
 *          mipmapped; 32 repeats bilinear without mipmaps
 *   row 2  lightmap-like, clamped and bilinear: one 16x16 block of a
 *          128x128 atlas, flat; the same in perspective; a lone 16x16; a
 *          lone 8x8
 *   row 3  sizes and extremes: 256x256; 32x8; a quad seen nearly edge-on;
 *          a triangle far larger than its cell
 *
 * Part 2 (draw2) places triangles across the clip.
 *
 *   TEXEXP [--tag X] [--part 2] */
#include "hx.h"
#include <GL/gl.h>
#include <GL/dosgl.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define CW 160
#define CH 120

static unsigned char frame[W * H * 4];
static const char *tag = "";
static int part = 1;

/* A size_w x size_h gradient (red across, green up, blue 128) with white
 * lines every 8 texels, so a displacement shows as a moved line. */
static void fill_grid(unsigned char *buf, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char *p = buf + (y * w + x) * 4;
            int line = (x % 8) == 0 || (y % 8) == 0;
            p[0] = line ? 255 : (unsigned char)(x * 255 / (w > 1 ? w - 1 : 1));
            p[1] = line ? 255 : (unsigned char)(y * 255 / (h > 1 ? h - 1 : 1));
            p[2] = line ? 255 : 128;
            p[3] = 255;
        }
}

static GLuint tex_new(int w, int h, const unsigned char *buf, GLenum filt, GLenum wrap)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    return t;
}

static GLuint tex_grid(int w, int h, GLenum filt, GLenum wrap)
{
    static unsigned char buf[256 * 256 * 4];
    fill_grid(buf, w, h);
    return tex_new(w, h, buf, filt, wrap);
}

/* The grid with its mipmap levels (each level the box filter of the last). */
static GLuint tex_grid_mip(int size)
{
    static unsigned char a[64 * 64 * 4], b[32 * 32 * 4];
    GLuint t;
    int lv = 0, s = size, x, y, c;
    unsigned char *cur = a, *nxt = b;
    fill_grid(a, size, size);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    for (;;) {
        glTexImage2D(GL_TEXTURE_2D, lv, GL_RGBA, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, cur);
        if (s == 1)
            break;
        for (y = 0; y < s / 2; y++)
            for (x = 0; x < s / 2; x++)
                for (c = 0; c < 4; c++)
                    nxt[(y * (s / 2) + x) * 4 + c] =
                        (unsigned char)((cur[((2 * y) * s + 2 * x) * 4 + c] + cur[((2 * y) * s + 2 * x + 1) * 4 + c] +
                                         cur[((2 * y + 1) * s + 2 * x) * 4 + c] + cur[((2 * y + 1) * s + 2 * x + 1) * 4 + c] + 2) / 4);
        { unsigned char *k = cur; cur = nxt; nxt = k; }
        s /= 2;
        lv++;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return t;
}

/* A 128x128 atlas of 8x8 blocks of 16x16: each block its own gradient
 * inside a one-texel black border, as lightmaps are packed. */
static GLuint tex_atlas(void)
{
    static unsigned char buf[128 * 128 * 4];
    int x, y;
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++) {
            unsigned char *p = buf + (y * 128 + x) * 4;
            int bx = x / 16, by = y / 16, lx = x % 16, ly = y % 16;
            int border = lx == 0 || ly == 0 || lx == 15 || ly == 15;
            p[0] = border ? 0 : (unsigned char)(64 + bx * 24 + lx * 4);
            p[1] = border ? 0 : (unsigned char)(64 + by * 24 + ly * 4);
            p[2] = border ? 0 : (unsigned char)(200 - lx * 8);
            p[3] = 255;
        }
    return tex_new(128, 128, buf, GL_LINEAR, GL_CLAMP);
}

/* Cell (col, row) of the 4x4 grid; row 0 at the top. */
static void cell(int col, int row)
{
    const int x = col * CW, y = H - (row + 1) * CH;
    glViewport(x, y, CW, CH);
    glScissor(x, y, CW, CH);
}

static void flat(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CW, 0, CH, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void persp(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -0.75, 0.75, 1, 64);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* A screen-filling quad with s0..s1 across and t0..t1 up (flat cells). */
static void quad(float s0, float t0, float s1, float t1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(s0, t0); glVertex2f(4, 4);
    glTexCoord2f(s1, t0); glVertex2f(CW - 4, 4);
    glTexCoord2f(s1, t1); glVertex2f(CW - 4, CH - 4);
    glTexCoord2f(s0, t1); glVertex2f(4, CH - 4);
    glEnd();
}

/* A floor from z = -1 to z = -8 under the eye, rep repeats across. */
static void floor_quad(float s0, float t0, float rep)
{
    glBegin(GL_QUADS);
    glTexCoord2f(s0, t0); glVertex3f(-1.2f, -0.7f, -1.05f);
    glTexCoord2f(s0 + rep, t0); glVertex3f(1.2f, -0.7f, -1.05f);
    glTexCoord2f(s0 + rep, t0 + rep * 3); glVertex3f(9.0f, -0.7f, -8.0f);
    glTexCoord2f(s0, t0 + rep * 3); glVertex3f(-9.0f, -0.7f, -8.0f);
    glEnd();
}

static void draw(void)
{
    GLuint g64n = tex_grid(64, 64, GL_NEAREST, GL_REPEAT), g64l = tex_grid(64, 64, GL_LINEAR, GL_REPEAT);
    GLuint g64m = tex_grid_mip(64), atlas = tex_atlas();
    GLuint t16 = tex_grid(16, 16, GL_LINEAR, GL_CLAMP), t8 = tex_grid(8, 8, GL_LINEAR, GL_CLAMP);
    GLuint g256 = tex_grid(256, 256, GL_NEAREST, GL_REPEAT), g32x8 = tex_grid(32, 8, GL_NEAREST, GL_REPEAT);
    /* 4 pixels a texel: 38 texels across a 152-pixel quad, 28 up. */
    const float sw = 38.0f / 64.0f, th = 28.0f / 64.0f;
    /* Atlas block (5, 3): texels 80..95 by 48..63, inset half a texel. */
    const float a0 = 80.5f / 128.0f, a1 = 95.5f / 128.0f, b0 = 48.5f / 128.0f, b1 = 63.5f / 128.0f;

    glClearColor(0.2f, 0.0f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_SCISSOR_TEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    flat();
    cell(0, 0); glBindTexture(GL_TEXTURE_2D, g64n); quad(0.1f, 0.1f, 0.1f + sw, 0.1f + th);
    cell(1, 0); quad(8.1f, 8.1f, 8.1f + sw, 8.1f + th);
    cell(2, 0); quad(100.1f, 100.1f, 100.1f + sw, 100.1f + th);
    cell(3, 0); glBindTexture(GL_TEXTURE_2D, g64l); quad(0.1f, 0.1f, 0.1f + sw, 0.1f + th);

    persp();
    cell(0, 1); glBindTexture(GL_TEXTURE_2D, g64n); floor_quad(0.1f, 0.1f, 4);
    cell(1, 1); floor_quad(0.1f, 0.1f, 32);
    cell(2, 1); glBindTexture(GL_TEXTURE_2D, g64m); floor_quad(0.1f, 0.1f, 32);
    cell(3, 1); glBindTexture(GL_TEXTURE_2D, g64l); floor_quad(0.1f, 0.1f, 32);

    flat();
    cell(0, 2); glBindTexture(GL_TEXTURE_2D, atlas); quad(a0, b0, a1, b1);
    persp();
    cell(1, 2);
    glBegin(GL_QUADS);
    glTexCoord2f(a0, b0); glVertex3f(-1.2f, -0.7f, -1.05f);
    glTexCoord2f(a1, b0); glVertex3f(1.2f, -0.7f, -1.05f);
    glTexCoord2f(a1, b1); glVertex3f(9.0f, -0.7f, -8.0f);
    glTexCoord2f(a0, b1); glVertex3f(-9.0f, -0.7f, -8.0f);
    glEnd();
    flat();
    cell(2, 2); glBindTexture(GL_TEXTURE_2D, t16); quad(0.5f / 16, 0.5f / 16, 15.5f / 16, 15.5f / 16);
    cell(3, 2); glBindTexture(GL_TEXTURE_2D, t8); quad(0.5f / 8, 0.5f / 8, 7.5f / 8, 7.5f / 8);

    cell(0, 3); glBindTexture(GL_TEXTURE_2D, g256); quad(0.1f, 0.1f, 0.1f + 38.0f / 256, 0.1f + 28.0f / 256);
    cell(1, 3); glBindTexture(GL_TEXTURE_2D, g32x8); quad(0.1f, 0.1f, 0.1f + 38.0f / 32, 0.1f + 28.0f / 8);
    persp();
    cell(2, 3); glBindTexture(GL_TEXTURE_2D, g64n);
    glBegin(GL_QUADS);                          /* a wall seen nearly edge-on, from z = -1 to -40 */
    glTexCoord2f(0.1f, 0.1f); glVertex3f(-0.9f, -0.7f, -1.02f);
    glTexCoord2f(40.1f, 0.1f); glVertex3f(-0.9f, -0.7f, -40.0f);
    glTexCoord2f(40.1f, 2.1f); glVertex3f(-0.9f, 0.7f, -40.0f);
    glTexCoord2f(0.1f, 2.1f); glVertex3f(-0.9f, 0.7f, -1.02f);
    glEnd();
    flat();
    cell(3, 3);
    glBegin(GL_TRIANGLES);                      /* corners far outside the cell, clipped by the scissor */
    glTexCoord2f(0.1f, 0.1f); glVertex2f(-1500, -1200);
    glTexCoord2f(40.1f, 0.1f); glVertex2f(1700, -1200);
    glTexCoord2f(0.1f, 30.1f); glVertex2f(-1500, 1300);
    glEnd();
    glDisable(GL_SCISSOR_TEST);
}

/* Part 2 (--part 2): where a triangle meets the clip. One floor (z = -1.05
 * to -8) or flat quad, placed fully inside its cell, across the scissor's
 * left, right or both sides, or across the screen's own left edge (column
 * 0 lies against it).
 *   row 0  perspective floor, the atlas block (clamped, bilinear)
 *   row 1  perspective floor, the 64x64 grid (repeat, nearest)
 *   row 2  flat quad, the grid: inside; across the left; across the top
 *          and bottom; across the screen's left edge (and the cell's)
 *   row 3  perspective wall (q varying across): inside; across the left;
 *          across the right; across the screen's left edge */
static void floor_at(float xl_near, float xr_near, float xl_far, float xr_far, float s0, float t0, float s1, float t1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(s0, t0); glVertex3f(xl_near, -0.7f, -1.05f);
    glTexCoord2f(s1, t0); glVertex3f(xr_near, -0.7f, -1.05f);
    glTexCoord2f(s1, t1); glVertex3f(xr_far, -0.7f, -8.0f);
    glTexCoord2f(s0, t1); glVertex3f(xl_far, -0.7f, -8.0f);
    glEnd();
}

static void wall_at(float x0, float x1, float z0, float z1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.1f, 0.1f); glVertex3f(x0, -0.6f, z0);
    glTexCoord2f(4.1f, 0.1f); glVertex3f(x1, -0.6f, z1);
    glTexCoord2f(4.1f, 1.1f); glVertex3f(x1, 0.6f, z1);
    glTexCoord2f(0.1f, 1.1f); glVertex3f(x0, 0.6f, z0);
    glEnd();
}

static void flat_at(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.1f, 0.1f); glVertex2f(x0, y0);
    glTexCoord2f(0.1f + (x1 - x0) / 256.0f, 0.1f); glVertex2f(x1, y0);
    glTexCoord2f(0.1f + (x1 - x0) / 256.0f, 0.1f + (y1 - y0) / 256.0f); glVertex2f(x1, y1);
    glTexCoord2f(0.1f, 0.1f + (y1 - y0) / 256.0f); glVertex2f(x0, y1);
    glEnd();
}

static void draw2(void)
{
    GLuint g64n = tex_grid(64, 64, GL_NEAREST, GL_REPEAT), atlas = tex_atlas();
    const float a0 = 80.5f / 128.0f, a1 = 95.5f / 128.0f, b0 = 48.5f / 128.0f, b1 = 63.5f / 128.0f;
    /* near half-widths: inside 0.6 (the cell is +-1.05 there), across 1.6; far: inside 4, across 12 */
    static const float fl[4][4] = { { -0.6f, 0.6f, -4, 4 }, { -1.6f, 0.6f, -12, 4 }, { -0.6f, 1.6f, -4, 12 },
                                    { -1.6f, 1.6f, -12, 12 } };
    int i;
    glClearColor(0.2f, 0.0f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_SCISSOR_TEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    persp();
    glBindTexture(GL_TEXTURE_2D, atlas);
    for (i = 0; i < 4; i++) {
        cell(i, 0);
        floor_at(fl[i][0], fl[i][1], fl[i][2], fl[i][3], a0, b0, a1, b1);
    }
    glBindTexture(GL_TEXTURE_2D, g64n);
    for (i = 0; i < 4; i++) {
        cell(i, 1);
        floor_at(fl[i][0], fl[i][1], fl[i][2], fl[i][3], 0.1f, 0.1f, 4.1f, 12.1f);
    }
    flat();
    cell(0, 2); flat_at(20, 20, 140, 100);
    cell(1, 2); flat_at(-60, 20, 140, 100);
    cell(2, 2); flat_at(20, -40, 140, 160);
    cell(3, 2); flat_at(-1500, 20, 140, 100);  /* far into the guard band */
    persp();
    cell(0, 3);                                 /* against the screen's left edge: the viewport reaches off it */
    glViewport(-120, 0, CW + 120, CH);
    wall_at(-2.5f, 0.8f, -1.2f, -6.0f);
    cell(1, 3); wall_at(-0.8f, 0.8f, -1.5f, -6.0f);
    cell(2, 3); wall_at(-2.5f, 0.8f, -1.2f, -6.0f);
    cell(3, 3); wall_at(-0.8f, 2.5f, -1.5f, -1.2f);
    glDisable(GL_SCISSOR_TEST);
}

static void save(void)
{
    static unsigned char rgb[W * H * 3];
    char n[16];
    int x, y;
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, frame);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            memcpy(rgb + ((size_t)y * W + x) * 3, frame + ((size_t)(H - 1 - y) * W + x) * 4, 3);
    strcpy(n, part == 2 ? "E9B" : "E9");
    strncat(n, tag, 2);
    hx_save_ppm(n, W, H, rgb);
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        if ((!strcmp(argv[i], "--tag") || !strcmp(argv[i], "--part")) && i + 1 < argc) {
            if (argv[i][2] == 't')
                tag = argv[i + 1];
            else
                part = atoi(argv[i + 1]);
            memmove(&argv[i], &argv[i + 2], (size_t)(argc - i - 1) * sizeof *argv);
            argc -= 2;
            i--;
        }
    hx_init(argc, argv, "texexp");
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    glDisable(GL_DITHER);
    if (part == 2)
        draw2();
    else
        draw();
    save();
    hx_test("gl-errors", glGetError() == GL_NO_ERROR, "none expected");
    dglShutdown();
    hx_done(0);
    return 0;
}
