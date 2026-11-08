/* g4exp - silicon experiments E1-E7 (Quake plan Q6; how to read them:
 * docs/silicon-experiments.md). Each experiment draws a small scene, reads
 * it back, saves E<n><tag>.PPM and says whether the card did what DOS-GL
 * assumes: HX-TEST PASS means the assumption holds. The same program runs
 * under the DOS-GL switches the experiments compare (DGL_COMBINER,
 * DGL_TC2_EXTRA, DGL_TLUT); --tag X names that run's pictures and --only
 * lists the experiments to run (default 124567; E3 is a whole run).
 *
 *   E1 map-1 routing and coordinates     (two texture units only)
 *   E2 legacy modulate beside the stage combiner
 *   E3 TEXCTL2 bit 15                    (the whole run under DGL_TC2_EXTRA)
 *   E4 leaving dual texturing            (two texture units only)
 *   E5 paletted textures (TLUT or expanded)
 *   E6 texture cache after in-place texel writes (engine or CPU)
 *   E8 a queued ILOAD then a CPU write to the same texture (ordering)
 *   E7 identity: PCI config space, revision, OPTION registers
 *
 * Uses DOS-GL internals for the counters and config space, as probe does. */
#define GL_GLEXT_PROTOTYPES 1
#include "hx.h"
#include <GL/gl.h>
#include <GL/dosgl.h>
#include "../../src/dgl/dgl.h"
#include "../../src/gl/gl_tex.h"
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define TS 64                                   /* texture size */

static unsigned char frame[W * H * 4];
static const char *tag = "";
static int units;

typedef struct { unsigned char r, g, b, a; } rgba;

static const rgba RED = { 255, 0, 0, 255 }, GREEN = { 0, 255, 0, 255 }, BLUE = { 0, 0, 255, 255 },
                  CLEAR = { 255, 255, 255, 0 }, WHITE = { 255, 255, 255, 255 };

/* ---- Textures ---------------------------------------------------------- */

/* A size x size texture, left half one colour and right half another; with
 * check, a 2x2 checkerboard (l bottom-left and top-right). */
static GLuint tex_pattern(int size, rgba l, rgba r, int check)
{
    static unsigned char buf[TS * TS * 4];
    GLuint t;
    int x, y;
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++)
            memcpy(buf + (y * size + x) * 4, (x < size / 2) ^ (check && y >= size / 2) ? &l : &r, 4);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

static GLuint tex_split(int size, rgba l, rgba r)
{
    return tex_pattern(size, l, r, 0);
}

static GLuint tex_solid(rgba c)
{
    return tex_split(TS, c, c);
}

static void fill(unsigned char *buf, int n, rgba c)
{
    int i;
    for (i = 0; i < n; i++)
        memcpy(buf + i * 4, &c, 4);
}

/* ---- Units and quads --------------------------------------------------- */

static void unit(int u, GLuint t, GLenum env)
{
    if (units < 2 && u)
        return;
    if (units >= 2)
        glActiveTextureARB(GL_TEXTURE0_ARB + u);
    if (t) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)env);
    } else
        glDisable(GL_TEXTURE_2D);
    if (units >= 2)
        glActiveTextureARB(GL_TEXTURE0_ARB);
}

/* A screen-space quad; unit 1 gets unit 0's coordinates, or with swap its
 * s and t exchanged (its left/right split then runs bottom/top). */
static void quad(float x0, float y0, float x1, float y1, float r, int swap)
{
    static const float c[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    static const float p[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int i;
    glBegin(GL_QUADS);
    for (i = 0; i < 4; i++) {
        float s = c[i][0] * r, t = c[i][1] * r;
        if (units >= 2) {
            glMultiTexCoord2fARB(GL_TEXTURE0_ARB, s, t);
            glMultiTexCoord2fARB(GL_TEXTURE1_ARB, swap ? t : s, swap ? s : t);
        } else
            glTexCoord2f(s, t);
        glVertex2f(x0 + p[i][0] * (x1 - x0), y0 + p[i][1] * (y1 - y0));
    }
    glEnd();
}

static void ortho(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* ---- Readback ---------------------------------------------------------- */

static void grab(void)
{
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, frame);
}

static const unsigned char *px(int x, int y)       /* GL window coordinates, y up */
{
    return frame + ((size_t)y * W + x) * 4;
}

/* Save the rectangle (x, y)-(x + w, y + h) as <name><tag>.PPM, top row first. */
static void save(const char *name, int x0, int y0, int w, int h)
{
    static unsigned char rgb[W * H * 3];
    char n[16];
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            memcpy(rgb + ((size_t)y * w + x) * 3, px(x0 + x, y0 + h - 1 - y), 3);
    strcpy(n, name);
    strncat(n, tag, 2);
    hx_save_ppm(n, w, h, rgb);
}

static int near(const unsigned char *p, int r, int g, int b)
{
    return abs(p[0] - r) <= 12 && abs(p[1] - g) <= 12 && abs(p[2] - b) <= 12;
}

static int is(const unsigned char *p, rgba c)
{
    return near(p, c.r, c.g, c.b);
}

static const char *cname(const unsigned char *p)
{
    static const struct { const char *n; int r, g, b; } names[] = {
        { "red", 255, 0, 0 }, { "green", 0, 255, 0 }, { "blue", 0, 0, 255 }, { "white", 255, 255, 255 },
        { "black", 0, 0, 0 }, { "purple", 128, 0, 128 }, { "teal", 0, 128, 128 }, { "cyan", 0, 255, 255 },
        { "yellow", 255, 255, 0 }, { "mix", 192, 64, 64 },
    };
    unsigned i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (near(p, names[i].r, names[i].g, names[i].b))
            return names[i].n;
    return "other";
}

static void begin(void)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    unit(0, 0, GL_MODULATE);
    unit(1, 0, GL_MODULATE);
    glDisable(GL_BLEND);
    glColor4f(1, 1, 1, 1);
    ortho();
}

static void show(void)
{
    grab();
    dglSwapBuffers();
}

/* ---- E1: map-1 routing and coordinates ----------------------------------
 * a) Unit 0 REPLACE red|green, unit 1 DECAL blue|clear with s and t swapped:
 *    bottom half blue, top-left red, top-right green. Anything else means
 *    map-1 writes reached the wrong map or the combiner read the wrong one.
 * b, c) A perspective floor, both units on the same coordinates (4 repeats)
 *    and checkerboards: unit 0 red/green, unit 1 half-transparent blue/clear.
 *    Where both maps agree the floor is purple/green; red or teal pixels are where map 1's
 *    coordinates drifted from map 0's (c: map 1 is 32x32, another prescale).
 *    Half alpha is 8/15 in ARGB4444, so purple is only roughly (128,0,128). */
static void e1_floor(GLuint t0, GLuint t1)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -0.75, 0.75, 1, 40);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    unit(0, t0, GL_REPLACE);
    unit(1, t1, GL_DECAL);
    glBegin(GL_QUADS);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0, 0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0, 0);
    glVertex3f(-3, -1, -1.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 4, 0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 4, 0);
    glVertex3f(3, -1, -1.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 4, 4); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 4, 4);
    glVertex3f(3, -1, -30);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0, 4); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0, 4);
    glVertex3f(-3, -1, -30);
    glEnd();
}

static void e1_count(const char *name, const char *save_as)
{
    long ok = 0, red = 0, teal = 0, other = 0;
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            const unsigned char *p = px(x, y);
            int mid_r = p[0] > 80 && p[0] < 176, mid_g = p[1] > 80 && p[1] < 176, mid_b = p[2] > 80 && p[2] < 176;
            if (near(p, 0, 0, 0))
                continue;
            if ((mid_r && p[1] < 40 && mid_b) || near(p, 0, 255, 0))
                ok++;                           /* purple or green */
            else if (near(p, 255, 0, 0))
                red++;
            else if (p[0] < 40 && mid_g && mid_b)
                teal++;
            else
                other++;
        }
    save(save_as, 0, 0, W, H / 2);
    hx_log("HX-STAT g4exp %s tag=%s agree=%ld red=%ld teal=%ld other=%ld", name, tag, ok, red, teal, other);
    hx_test(name, ok > 1000 && (red + teal + other) * 500 <= ok,
            "map 1 disagrees on %ld of %ld pixels", red + teal + other, ok + red + teal + other);
}

static void e1(void)
{
    static const rgba HALFBLUE = { 0, 0, 255, 128 };
    GLuint a = tex_split(TS, RED, GREEN), b = tex_split(TS, BLUE, CLEAR);
    GLuint a2 = tex_pattern(TS, RED, GREEN, 1);
    GLuint c = tex_pattern(TS, HALFBLUE, CLEAR, 1), c32 = tex_pattern(32, HALFBLUE, CLEAR, 1);
    const unsigned char *q[4];
    int ok;
    begin();
    unit(0, a, GL_REPLACE);
    unit(1, b, GL_DECAL);
    quad(64, 64, 320, 320, 1, 1);
    show();
    q[0] = px(128, 128); q[1] = px(256, 128); q[2] = px(128, 256); q[3] = px(256, 256);
    ok = is(q[0], BLUE) && is(q[1], BLUE) && is(q[2], RED) && is(q[3], GREEN);
    save("E1A", 64, 64, 256, 256);
    hx_test("e1a-routing", ok, "bottom %s %s, top %s %s (expect blue blue, red green)",
            cname(q[0]), cname(q[1]), cname(q[2]), cname(q[3]));
    begin();
    e1_floor(a2, c);
    show();
    e1_count("e1b-coords", "E1B");
    begin();
    e1_floor(a2, c32);
    show();
    e1_count("e1c-coords32", "E1C");
}

/* ---- E2: modulate with a single texture ---------------------------------
 * White texture, alpha 1 on the left and 1/2 on the right; vertex colour
 * (1, 1/2, 0) with alpha 1/2; MODULATE, blended over black. Colour and
 * alpha both modulated give (128,64,0) | (64,32,0). The other outcomes name
 * what the hardware skipped: with DOS-GL's legacy path on the G400 (TEXCTL
 * tmodulate, stage words 0) a skipped modulate means DGL_COMBINER is needed. */
static void e2(void)
{
    static const rgba HALF = { 255, 255, 255, 128 };
    static const struct { const char *what; int l[3], r[3]; } h[4] = {
        { "modulated", { 128, 64, 0 }, { 64, 32, 0 } },
        { "colour-not-modulated", { 128, 128, 128 }, { 64, 64, 64 } },
        { "alpha-not-modulated", { 255, 128, 0 }, { 128, 64, 0 } },
        { "neither-modulated", { 255, 255, 255 }, { 128, 128, 128 } },
    };
    GLuint t = tex_split(TS, WHITE, HALF);
    const unsigned char *l, *r;
    int i, found = -1;
    begin();
    unit(0, t, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 0.5f, 0, 0.5f);
    quad(64, 64, 320, 192, 1, 0);
    show();
    l = px(128, 128);
    r = px(256, 128);
    for (i = 0; i < 4 && found < 0; i++)
        if (near(l, h[i].l[0], h[i].l[1], h[i].l[2]) && near(r, h[i].r[0], h[i].r[1], h[i].r[2]))
            found = i;
    save("E2", 64, 64, 256, 128);
    hx_log("HX-STAT g4exp e2 tag=%s left=%d,%d,%d right=%d,%d,%d", tag, l[0], l[1], l[2], r[0], r[1], r[2]);
    hx_test("e2-modulate", found == 0, "%s", found < 0 ? "unexplained" : h[found].what);
}

/* ---- E4: leaving dual texturing -----------------------------------------
 * A 8x4 grid, dual and single cells interleaved with no sync between them:
 * dual = red REPLACE then grey DECAL at alpha 1/2 (192,64,64), single = cyan.
 * The kernel runs a flush sequence when it leaves the dual pipe; DOS-GL just
 * rewrites TEXCTL2. Any wrong cell means the flush is needed. */
static void e4(void)
{
    static const rgba GREY = { 128, 128, 128, 128 }, CYAN = { 0, 255, 255, 255 };
    GLuint r = tex_solid(RED), g = tex_solid(GREY), c = tex_solid(CYAN);
    static const char pattern[33] = "DSDSDSDSSDSDSDSDDDSSDDSSSSDDSSDD";
    int i, bad = 0, first = -1;
    begin();
    for (i = 0; i < 32; i++) {
        float x = 32 + (i % 8) * 72, y = 32 + (i / 8) * 72;
        if (pattern[i] == 'D') {
            unit(0, r, GL_REPLACE);
            unit(1, g, GL_DECAL);
        } else {
            unit(1, 0, GL_MODULATE);
            unit(0, c, GL_REPLACE);
        }
        quad(x, y, x + 64, y + 64, 1, 0);
    }
    show();
    for (i = 0; i < 32; i++) {
        const unsigned char *p = px(64 + (i % 8) * 72, 64 + (i / 8) * 72);
        if (pattern[i] == 'D' ? !near(p, 192, 64, 64) : !near(p, 0, 255, 255)) {
            bad++;
            if (first < 0)
                first = i;
        }
    }
    save("E4", 32, 32, 576, 288);
    hx_test("e4-leave-dual", bad == 0, "%d of 32 cells wrong (first %d)", bad, first);
}

/* ---- E5: paletted textures ----------------------------------------------
 * Four stripes of indices 0-3 through the shared palette, drawn, then the
 * palette reversed and drawn again below. Entry 0, (0,132,0), is RGB565
 * 0x0420; read as ARGB1555 it would be (8,8,0), telling the LUT's format.
 * Through the TLUT (G200 default; G400 with DGL_TLUT=1) or expanded. */
static void e5(void)
{
    static const unsigned char pal[4][3] = { { 0, 132, 0 }, { 255, 0, 0 }, { 0, 0, 255 }, { 200, 200, 200 } };
    static unsigned char idx[TS * TS], full[256][3];
    unsigned char rev[4][3];
    unsigned long loads = dgl_texc.lut_loads;
    GLuint t;
    int i, x, y, bad = 0;
    for (y = 0; y < TS; y++)
        for (x = 0; x < TS; x++)
            idx[y * TS + x] = (unsigned char)(x / (TS / 4));
    for (i = 0; i < 256; i++)
        memset(full[i], 96, 3);
    for (i = 0; i < 4; i++) {
        memcpy(full[i], pal[i], 3);
        memcpy(rev[i], pal[3 - i], 3);
    }
    begin();
    glEnable(GL_SHARED_TEXTURE_PALETTE_EXT);
    glColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, full);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, TS, TS, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    unit(0, t, GL_REPLACE);
    quad(64, 224, 320, 320, 1, 0);
    glColorSubTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, 0, 4, GL_RGB, GL_UNSIGNED_BYTE, rev);
    quad(64, 96, 320, 192, 1, 0);
    show();
    glDisable(GL_SHARED_TEXTURE_PALETTE_EXT);
    for (i = 0; i < 4; i++) {
        const unsigned char *a = px(96 + i * 64, 272), *b = px(96 + i * 64, 144);
        hx_log("HX-STAT g4exp e5 tag=%s entry=%d first=%d,%d,%d second=%d,%d,%d", tag, i, a[0], a[1], a[2],
               b[0], b[1], b[2]);
        bad += !near(a, pal[i][0], pal[i][1], pal[i][2]) + !near(b, rev[i][0], rev[i][1], rev[i][2]);
    }
    save("E5", 64, 96, 256, 224);
    hx_test("e5-palette", bad == 0, "%d of 8 stripes wrong, path=%s (lut loads %lu)", bad,
            dgl_texc.lut_loads > loads ? "tlut" : "expanded", dgl_texc.lut_loads - loads);
}

/* ---- E6: texture cache after in-place texel writes ----------------------
 * DOS-GL writes glTexSubImage2D rectangles straight into the texture's VRAM
 * when it can (texture.c sub_in_place): through the engine (ILOAD) by
 * default, by the CPU under DGL_ILOAD=0. The engine may cache texels:
 *   a) red texture drawn, glFinish, whole texture rewritten green, drawn
 *      again with no register change: expect green;
 *   b) drawn again and at once an 8x8 corner rewritten blue (the CPU path
 *      waits for the engine first), drawn: expect a blue corner;
 *   c) as b with yellow, but another texture drawn in between (TEXORG is
 *      rewritten): if only c is right, a register write clears the cache.
 * The HX-STAT line shows which write path each step took. */
static int e6_step(float x, int corner, rgba want_corner, rgba want_rest, const char *name)
{
    int stale = 0, i, j;
    for (j = 0; j < 128; j += 4)
        for (i = 0; i < 128; i += 4) {
            const unsigned char *p = px((int)x + i + 2, 64 + j + 2);
            int in_corner = corner && i < 16 && j < 16;
            stale += !is(p, in_corner ? want_corner : want_rest);
        }
    hx_log("HX-STAT g4exp %s tag=%s stale=%d of 1024 iload=%lu cpu=%lu sync=%lu full=%lu", name, tag, stale,
           dgl_texc.sub_iload, dgl_texc.sub_fast, dgl_texc.sub_sync, dgl_texc.sub_full);
    return stale;
}

static void e6(void)
{
    static const rgba YELLOW = { 255, 255, 0, 255 };
    static unsigned char buf[TS * TS * 4];
    GLuint t = tex_solid(RED), other = tex_solid(WHITE);
    unsigned long iload0, fast0, sync0, full0;
    int sa, sb, sc;
    begin();
    unit(0, t, GL_REPLACE);
    quad(16, 64, 144, 192, 1, 0);               /* red, loads the cache */
    glFinish();
    iload0 = dgl_texc.sub_iload; fast0 = dgl_texc.sub_fast; sync0 = dgl_texc.sub_sync;
    full0 = dgl_texc.sub_full;
    fill(buf, TS * TS, GREEN);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TS, TS, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    quad(176, 64, 304, 192, 1, 0);              /* a: green */
    fill(buf, 64, BLUE);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    quad(336, 64, 464, 192, 1, 0);              /* b: blue corner */
    unit(0, other, GL_REPLACE);
    quad(600, 440, 608, 448, 1, 0);
    unit(0, t, GL_REPLACE);
    fill(buf, 64, YELLOW);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    quad(496, 64, 624, 192, 1, 0);              /* c: yellow corner */
    show();
    sa = e6_step(176, 0, GREEN, GREEN, "e6a");
    sb = e6_step(336, 1, BLUE, GREEN, "e6b");
    sc = e6_step(496, 1, YELLOW, GREEN, "e6c");
    save("E6", 16, 64, 608, 128);
    hx_test("e6-texcache", sa + sb + sc == 0, "stale samples a=%d b=%d c=%d; engine %lu, cpu %lu (after sync %lu), "
            "re-uploaded %lu", sa, sb, sc, dgl_texc.sub_iload - iload0, dgl_texc.sub_fast - fast0,
            dgl_texc.sub_sync - sync0, dgl_texc.sub_full - full0);
}

/* ---- E8: a queued ILOAD, then a CPU write to the same texture -----------
 * glTexSubImage2D sends the texels through the engine (ILOAD), behind the
 * draws already queued. Until 2026-10-01 that did not mark the texture
 * busy: a texture not drawn since the last sync could then take a
 * glTexImage2D of the same size in place by the CPU at once, and the late
 * ILOAD overwrote it. Here: one ILOAD into another texture first (the
 * switch to ILOAD mode waits for the engine once); a red texture drawn and
 * synced; quads with a third texture in the top half to keep the engine
 * busy; the red one's texels sent green (ILOAD, queued behind them), then
 * replaced blue (glTexImage2D), then drawn: blue, not green. */
static void e8(void)
{
    static unsigned char buf[TS * TS * 4];
    GLuint t = tex_solid(RED), other = tex_solid(WHITE), busy = tex_pattern(TS, WHITE, BLUE, 1);
    int i, x, y, bad = 0, green = 0;
    unsigned long iload0;
    begin();
    unit(0, other, GL_REPLACE);
    quad(600, 200, 608, 208, 1, 0);
    unit(0, t, GL_REPLACE);
    quad(16, 64, 144, 192, 1, 0);               /* both resident */
    glFinish();
    unit(0, other, GL_REPLACE);
    fill(buf, TS * TS, WHITE);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TS, TS, GL_RGBA, GL_UNSIGNED_BYTE, buf);   /* ILOAD mode */
    glFinish();                                 /* t is not busy */
    unit(0, busy, GL_REPLACE);
    for (i = 0; i < 10; i++)
        quad(0, 240, W, H, 1, 0);               /* the engine busy, above the test quad */
    unit(0, t, GL_REPLACE);
    iload0 = dgl_texc.sub_iload;
    fill(buf, TS * TS, GREEN);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TS, TS, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    fill(buf, TS * TS, BLUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TS, TS, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    quad(176, 64, 304, 192, 1, 0);
    show();
    for (y = 0; y < 128; y += 8)
        for (x = 0; x < 128; x += 8) {
            const unsigned char *p = px(176 + x + 4, 64 + y + 4);
            bad += !is(p, BLUE);
            green += is(p, GREEN);
        }
    save("E8", 176, 64, 128, 128);
    hx_test("e8-iload-order", bad == 0, "%d of 256 samples not blue (%d green, centre %s); ILOADs %lu", bad, green,
            cname(px(240, 128)), dgl_texc.sub_iload - iload0);
}

/* ---- E7: identity -------------------------------------------------------
 * Config space 00h-5Ch (OPTION 40h, OPTION2 50h, OPTION3 54h on the G400),
 * so the bench records which silicon revision each card is. */
static void e7(void)
{
    const DGLDeviceInfo *di = dglGetDeviceInfo();
    int reg;
    hx_log("HX-STAT g4exp e7 chip=%s id=%04x rev=%02x vram=%lu emulated=%d units=%d tlut=%d dual=%d",
           mga.name, di->device_id, di->revision, di->vram_bytes, di->emulated, units, mga.has_tlut,
           mga.has_dual_tex);
    for (reg = 0; reg < 0x60; reg += 16)
        hx_log("HX-STAT g4exp e7 cfg%02x %08lx %08lx %08lx %08lx", reg,
               (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, reg),
               (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, reg + 4),
               (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, reg + 8),
               (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, reg + 12));
    hx_log("HX-STAT g4exp e7 env DGL_COMBINER=%s DGL_TC2_EXTRA=%s DGL_TLUT=%s",
           getenv("DGL_COMBINER") ? getenv("DGL_COMBINER") : "-",
           getenv("DGL_TC2_EXTRA") ? getenv("DGL_TC2_EXTRA") : "-", getenv("DGL_TLUT") ? getenv("DGL_TLUT") : "-");
}

int main(int argc, char **argv)
{
    const char *only = "1245678";
    GLint n = 1;
    int i;
    for (i = 1; i < argc; i++) {
        int take = 0;
        if (!strcmp(argv[i], "--tag") && i + 1 < argc) { tag = argv[i + 1]; take = 2; }
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) { only = argv[i + 1]; take = 2; }
        if (take) {
            memmove(&argv[i], &argv[i + take], (size_t)(argc - i - take + 1) * sizeof *argv);
            argc -= take; i--;
        }
    }
    hx_init(argc, argv, "g4exp");
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    glDisable(GL_DITHER);
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &n);
    units = n;
    hx_log("HX-STAT g4exp run tag=%s only=%s", tag, only);
    if (strchr(only, '7')) e7();
    if (strchr(only, '1')) {
        if (units >= 2) e1(); else hx_log("HX-STAT g4exp e1 skipped: one texture unit");
    }
    if (strchr(only, '2')) e2();
    if (strchr(only, '4')) {
        if (units >= 2) e4(); else hx_log("HX-STAT g4exp e4 skipped: one texture unit");
    }
    if (strchr(only, '5')) e5();
    if (strchr(only, '6')) e6();
    if (strchr(only, '8')) e8();
    hx_test("gl-errors", glGetError() == GL_NO_ERROR, "none expected");
    dglShutdown();
    hx_done(0);
    return 0;
}
