/* t42: GL_ARB_texture_cube_map. A cube map whose six faces each have their own colour and an arrow-like gradient
 * (so a face upside down or mirrored shows), unmipmapped (the Radeons can't mipmap a cube map), clamped to the edge.
 * Frame t42_0: the six faces unfolded as a cross, each square drawn with explicit (s, t, r) directions through its
 * face; a sphere with reflection-map coordinates (an environment map) and one with normal-map coordinates, nearest
 * and linear. Then the binding, a face's level and image read back. Skipped where the extension isn't advertised. */
#define GL_GLEXT_PROTOTYPES 1
#include "ct_tex.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FS 32                           /* a face's size */
static const unsigned char face_rgb[6][3] = {
    { 220, 60, 60 }, { 60, 200, 200 }, { 60, 200, 60 }, { 200, 60, 200 }, { 60, 60, 220 }, { 220, 200, 60 },
};

static void make_cube(void)
{
    static unsigned char buf[FS * FS * 4];
    int f, x, y;
    for (f = 0; f < 6; f++) {
        for (y = 0; y < FS; y++)
            for (x = 0; x < FS; x++) {
                unsigned char *p = buf + (y * FS + x) * 4;
                int arrow = x > FS / 4 && x < 3 * FS / 4 && y < 3 * FS / 4 && abs(x - FS / 2) < (3 * FS / 4 - y) / 2;
                double k = arrow ? 1.0 : 0.35 + 0.5 * x / FS;      /* (the arrow points to t = 0; brighter to s = 1) */
                p[0] = (unsigned char)(face_rgb[f][0] * k > 255 ? 255 : face_rgb[f][0] * k);
                p[1] = (unsigned char)(face_rgb[f][1] * k > 255 ? 255 : face_rgb[f][1] * k);
                p[2] = (unsigned char)(face_rgb[f][2] * k > 255 ? 255 : face_rgb[f][2] * k);
                p[3] = 255;
                if (x == 0 || y == 0)
                    p[0] = p[1] = p[2] = 255;      /* (the face's s = 0 and t = 0 edges white) */
            }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + f, 0, GL_RGBA, FS, FS, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
}

/* The direction for (s, t) in [0, 1] on face f (the inverse of the cube map's table). */
static void dir_of(int f, float s, float t, float d[3])
{
    float sc = 2 * s - 1, tc = 2 * t - 1;
    switch (f) {
    case 0: d[0] = 1, d[1] = -tc, d[2] = -sc; break;
    case 1: d[0] = -1, d[1] = -tc, d[2] = sc; break;
    case 2: d[0] = sc, d[1] = 1, d[2] = tc; break;
    case 3: d[0] = sc, d[1] = -1, d[2] = -tc; break;
    case 4: d[0] = sc, d[1] = -tc, d[2] = 1; break;
    default: d[0] = -sc, d[1] = -tc, d[2] = -1; break;
    }
}

/* Face f as a 64x64 square at (x, y), drawn as a 4x4 grid of quads with directions at the corners (s across, t
 * down the screen). */
static void face_square(int f, float x, float y)
{
    int i, j, c;
    glBegin(GL_QUADS);
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
            for (c = 0; c < 4; c++) {
                int ci = i + (c == 1 || c == 2), cj = j + (c >= 2);
                float d[3];
                dir_of(f, ci / 4.0f, cj / 4.0f, d);
                glTexCoord3fv(d);
                glVertex2f(x + ci * 16.0f, y + 64 - cj * 16.0f);
            }
    glEnd();
}

#define SL 24
#define ST 16
static float pos[(ST + 1) * (SL + 1)][3];
static unsigned short idx[ST * SL * 6];

static void sphere(float x, float y, float r)
{
    static int made;
    int i, j, n = 0;
    if (!made) {
        for (i = 0; i <= ST; i++)
            for (j = 0; j <= SL; j++) {
                double th = 3.14159265358979 * i / ST, ph = 2 * 3.14159265358979 * j / SL;
                float *p = pos[i * (SL + 1) + j];
                p[0] = (float)(sin(th) * cos(ph)), p[1] = (float)cos(th), p[2] = (float)(sin(th) * sin(ph));
            }
        for (i = 0; i < ST; i++)
            for (j = 0; j < SL; j++) {
                unsigned short a = (unsigned short)(i * (SL + 1) + j), b = (unsigned short)(a + SL + 1);
                idx[n++] = a, idx[n++] = b, idx[n++] = (unsigned short)(a + 1);
                idx[n++] = (unsigned short)(a + 1), idx[n++] = b, idx[n++] = (unsigned short)(b + 1);
            }
        made = 1;
    }
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(r, r, r);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glNormalPointer(GL_FLOAT, 0, pos);
    glDrawElements(GL_TRIANGLES, ST * SL * 6, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
}

static void gen(GLenum mode)
{
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, (GLint)mode);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, (GLint)mode);
    glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, (GLint)mode);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glEnable(GL_TEXTURE_GEN_R);
}

void ct_run(void)
{
    static unsigned char back[FS * FS * 4], want[FS * FS * 4];
    GLuint t;
    GLint bind = -1, w = 0;
    int f, ok;
    if (!ct_has("GL_ARB_texture_cube_map")) {
        ct_log("CT-SKIP no GL_ARB_texture_cube_map");
        return;
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CT_W, 0, CT_H, -200, 200);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARB, t);
    make_cube();
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEnable(GL_TEXTURE_CUBE_MAP_ARB);
    glColor3f(1, 1, 1);

    /* The cross: +Y on top; -X, +Z, +X, -Z across; -Y below (as seen from inside, looking along -Z in the middle). */
    face_square(2, 112, 392);
    face_square(1, 40, 320);
    face_square(5, 112, 320);
    face_square(0, 184, 320);
    face_square(4, 256, 320);
    face_square(3, 112, 248);

    /* Spheres in perspective views of their own (eye vectors that spread over the sphere): reflection map, linear,
     * the sphere turned so other faces show; then normal map, nearest. */
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glFrustum(-0.6, 0.6, -0.45, 0.45, 1, 10);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glViewport(340, 250, 280, 210);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gen(GL_REFLECTION_MAP_ARB);
    glLoadIdentity();
    glTranslatef(0, 0, -3);
    glRotatef(30, 1, 1, 0);
    sphere(0, 0, 1.1f);
    glViewport(340, 20, 280, 210);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gen(GL_NORMAL_MAP_ARB);
    glLoadIdentity();
    glTranslatef(0, 0, -3);
    glRotatef(-35, 0, 1, 0);
    sphere(0, 0, 1.1f);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glViewport(0, 0, CT_W, CT_H);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_GEN_R);
    glDisable(GL_DEPTH_TEST);

    /* A quad with GL_TEXTURE_2D also on: the cube map wins. */
    glEnable(GL_TEXTURE_2D);
    ct_texture(16, 16, 4, 0, GL_NEAREST, GL_REPEAT);
    glBegin(GL_QUADS);
    glTexCoord3f(1, 0.2f, 0.3f); glVertex2f(40, 40);
    glTexCoord3f(1, 0.2f, -0.3f); glVertex2f(200, 40);
    glTexCoord3f(1, -0.2f, -0.3f); glVertex2f(200, 160);
    glTexCoord3f(1, -0.2f, 0.3f); glVertex2f(40, 160);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_CUBE_MAP_ARB);
    ct_frame("t42_0");

    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP_ARB, &bind);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_ARB, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexImage(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_ARB, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
    for (f = 0; f < FS * FS; f++) {
        int x = f % FS, y = f / FS;
        int arrow = x > FS / 4 && x < 3 * FS / 4 && y < 3 * FS / 4 && abs(x - FS / 2) < (3 * FS / 4 - y) / 2;
        double k = arrow ? 1.0 : 0.35 + 0.5 * x / FS;
        int c;
        for (c = 0; c < 3; c++)
            want[f * 4 + c] = (unsigned char)(face_rgb[3][c] * k > 255 ? 255 : face_rgb[3][c] * k);
        want[f * 4 + 3] = 255;
        if (x == 0 || y == 0)
            want[f * 4] = want[f * 4 + 1] = want[f * 4 + 2] = 255;
    }
    ok = bind == (GLint)t && w == FS && !memcmp(back, want, sizeof want);
    ct_check("cube-state", ok, "binding %d (want %u), -Y's width %d, its image read back %s", bind, t, w,
             memcmp(back, want, sizeof want) ? "DIFFERENT" : "as given");
}
