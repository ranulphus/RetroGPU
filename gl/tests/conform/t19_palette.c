/* t19: paletted textures (GL_EXT_paletted_texture, GL_EXT_shared_texture_palette).
 * Texture A holds 8-bit indices drawn through the shared palette; part of
 * that palette changes between two draws of A (the first quad keeps the old
 * colours), then some of A's indices change. Texture B has its own RGBA
 * palette with transparent entries, drawn alpha-tested with the shared
 * palette off, then through the shared palette. Mesa has no paletted
 * textures, so the host build keeps the same indices and palettes and
 * uploads them expanded to RGBA before each draw: the reference. */
#define GL_GLEXT_PROTOTYPES 1          /* glColorTableEXT and friends (DOS-GL) */
#include "ct_tex.h"
#include <string.h>

static unsigned char idx_a[64 * 64], idx_b[32 * 32];
static unsigned char shared_pal[256 * 3], own_b[256 * 4];
static int shared_on;
static GLuint tex_a, tex_b;

#ifndef __DJGPP__
static void expand(GLuint t, const unsigned char *idx, int w, int own)
{
    static unsigned char rgba[64 * 64 * 4];
    int i;
    for (i = 0; i < w * w; i++) {
        const unsigned char *p = own && !shared_on ? own_b + idx[i] * 4 : shared_pal + idx[i] * 3;
        rgba[i * 4 + 0] = p[0]; rgba[i * 4 + 1] = p[1]; rgba[i * 4 + 2] = p[2];
        rgba[i * 4 + 3] = own && !shared_on ? p[3] : 255;
    }
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
#endif

static GLuint make(const unsigned char *idx, int w)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
#ifdef __DJGPP__
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, w, w, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx);
#else
    (void)idx; (void)w;
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

static void draw(int b, float x, float y)
{
#ifdef __DJGPP__
    glBindTexture(GL_TEXTURE_2D, b ? tex_b : tex_a);
#else
    expand(b ? tex_b : tex_a, b ? idx_b : idx_a, b ? 32 : 64, b);
#endif
    ct_quad2d(x, y, x + 140.0f, y + 140.0f, 0, 0, 1, 1);
}

static void set_shared(int on)
{
    shared_on = on;
#ifdef __DJGPP__
    if (on) glEnable(GL_SHARED_TEXTURE_PALETTE_EXT);
    else glDisable(GL_SHARED_TEXTURE_PALETTE_EXT);
#endif
}

void ct_run(void)
{
    static unsigned char repl[40 * 3], patch[16 * 8];
    int i, x, y;
    for (i = 0; i < 256; i++) {
        shared_pal[i * 3 + 0] = (unsigned char)i;
        shared_pal[i * 3 + 1] = (unsigned char)(255 - i);
        shared_pal[i * 3 + 2] = (unsigned char)((i * 7) & 255);
        own_b[i * 4 + 0] = (unsigned char)((i & 15) * 16);
        own_b[i * 4 + 1] = 200;
        own_b[i * 4 + 2] = (unsigned char)(i & 0xF0);
        own_b[i * 4 + 3] = i < 96 ? 0 : 255;                /* transparent low indices */
    }
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            idx_a[y * 64 + x] = (unsigned char)((x * 4) ^ (y * 2));
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            idx_b[y * 32 + x] = (unsigned char)(x * 8 + y);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -1, 1);
    glClearColor(0.15f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
#ifdef __DJGPP__
    glColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, shared_pal);
#endif
    set_shared(1);
    tex_a = make(idx_a, 64);
    tex_b = make(idx_b, 32);
#ifdef __DJGPP__
    glBindTexture(GL_TEXTURE_2D, tex_b);
    glColorTableEXT(GL_TEXTURE_2D, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_BYTE, own_b);
#endif
    draw(0, 10, 330);                                       /* A, shared palette */
    for (i = 0; i < 40; i++) {                              /* entries 100..139 turn orange */
        repl[i * 3] = 255; repl[i * 3 + 1] = (unsigned char)(120 + i); repl[i * 3 + 2] = 0;
    }
    memcpy(shared_pal + 100 * 3, repl, sizeof repl);
#ifdef __DJGPP__
    glColorSubTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, 100, 40, GL_RGB, GL_UNSIGNED_BYTE, repl);
#endif
    draw(0, 170, 330);                                      /* A again: the new entries */
    for (i = 0; i < 16 * 8; i++)
        patch[i] = (unsigned char)(100 + i % 40);
    for (y = 0; y < 8; y++)
        memcpy(idx_a + (20 + y) * 64 + 24, patch + y * 16, 16);
#ifdef __DJGPP__
    glBindTexture(GL_TEXTURE_2D, tex_a);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 24, 20, 16, 8, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, patch);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#endif
    draw(0, 330, 330);                                      /* A with an orange band */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    set_shared(0);
    draw(1, 10, 170);                                       /* B, its own palette, cut out */
    set_shared(1);
    draw(1, 170, 170);                                      /* B through the shared palette */
    glDisable(GL_ALPHA_TEST);
    ct_frame("t19_0");
}
