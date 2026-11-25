/* t43: big draws in one call each (the Radeon 7500's transform engine takes them in pieces of indices: DOS-GL's first
 * pieces, 4,092 long, hung the P3 in Echoes' ECHB fly scene on 2026-11-25; Mesa's driver sends at most 300). Under a
 * perspective projection with depth, smooth-shaded, unlit, each draw small enough for the engine's vertex area:
 * - three overlapping 50 x 50 grids of quads, each one glDrawElements of 15,000 indices over 2,601 vertices (the
 *   engine's area cycles its halves through them, so its fences are waited on);
 * - a triangle strip of 2,002 vertices and a fan of 1,002 (more than one piece: drawn as lists);
 * - a textured 40 x 40 grid through an affine texture matrix (texel coordinates scaled by 1/64, as Fifth Wheel's HIGH
 *   scales its atlas's).
 * Frame t43_0. */
#include "ct_tex.h"
#include <math.h>

#define GN 50
#define TN 40
static float gpos[(GN + 1) * (GN + 1)][3];
static unsigned char gcol[(GN + 1) * (GN + 1)][4];
static unsigned short gidx[GN * GN * 6];
static float spos[2002][3], fpos[1002][3];
static unsigned char scol[2002][4], fcol[1002][4];
static float tpos[(TN + 1) * (TN + 1)][3];
static short ttex[(TN + 1) * (TN + 1)][2];
static unsigned short tidx[TN * TN * 6];

static void grid(int n, float (*pos)[3], unsigned short *idx)
{
    int i, j, k = 0;
    for (j = 0; j <= n; j++)
        for (i = 0; i <= n; i++) {
            float *p = pos[j * (n + 1) + i];
            p[0] = (float)i / n * 2 - 1, p[2] = (float)j / n * 2 - 1;
            p[1] = 0.15f * (float)sin(i * 0.4) * (float)cos(j * 0.3);
        }
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++) {
            unsigned short a = (unsigned short)(j * (n + 1) + i), b = (unsigned short)(a + 1);
            unsigned short c = (unsigned short)(a + n + 1), d = (unsigned short)(c + 1);
            idx[k++] = a, idx[k++] = c, idx[k++] = b, idx[k++] = b, idx[k++] = c, idx[k++] = d;
        }
}

void ct_run(void)
{
    int i, g;
    grid(GN, gpos, gidx);
    for (i = 0; i < (GN + 1) * (GN + 1); i++) {
        gcol[i][0] = (unsigned char)(60 + 150 * (gpos[i][0] * 0.5f + 0.5f));
        gcol[i][1] = (unsigned char)(60 + 150 * (gpos[i][2] * 0.5f + 0.5f));
        gcol[i][2] = (unsigned char)(120 + 600 * gpos[i][1]);
        gcol[i][3] = 255;
    }
    for (i = 0; i < 2002; i++) {            /* (a ribbon spiralling outwards) */
        double a = i / 2 * 0.012, r = 0.25 + i / 2 * 0.0004 + (i & 1) * 0.06;
        spos[i][0] = (float)(r * cos(a)), spos[i][1] = (float)(r * sin(a)), spos[i][2] = 0;
        scol[i][0] = (unsigned char)(i * 7), scol[i][1] = (unsigned char)(255 - (i & 255)), scol[i][2] = 200;
        scol[i][3] = 255;
    }
    fpos[0][0] = fpos[0][1] = fpos[0][2] = 0;
    fcol[0][0] = fcol[0][1] = fcol[0][2] = fcol[0][3] = 255;
    for (i = 1; i < 1002; i++) {            /* (a disc of 1,000 thin slices) */
        double a = (i - 1) * 2 * 3.14159265358979 / 1000;
        fpos[i][0] = (float)(0.3 * cos(a)), fpos[i][1] = (float)(0.3 * sin(a)), fpos[i][2] = 0;
        fcol[i][0] = (unsigned char)(128 + 127 * cos(a * 3)), fcol[i][1] = (unsigned char)(128 + 127 * sin(a * 2));
        fcol[i][2] = (unsigned char)(i & 255), fcol[i][3] = 255;
    }
    grid(TN, tpos, tidx);
    for (i = 0; i < (TN + 1) * (TN + 1); i++) {    /* (texel coordinates: 64 to the texture's width) */
        ttex[i][0] = (short)(i % (TN + 1) * 8), ttex[i][1] = (short)(i / (TN + 1) * 8);
    }

    glClearColor(0.05f, 0.05f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.8, 0.8, -0.6, 0.6, 1, 20);
    glMatrixMode(GL_MODELVIEW);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    /* The three grids, each a little further on and turned. */
    glVertexPointer(3, GL_FLOAT, 0, gpos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, gcol);
    for (g = 0; g < 3; g++) {
        glLoadIdentity();
        glTranslatef(-0.9f + g * 0.9f, -0.8f, -3.5f - g * 0.6f);
        glRotatef(25.0f + g * 10, 1, 0, 0);
        glRotatef(g * 20.0f, 0, 1, 0);
        glScalef(0.7f, 1, 0.7f);
        glDrawElements(GL_TRIANGLES, GN * GN * 6, GL_UNSIGNED_SHORT, gidx);
    }

    /* The strip and the fan, upper left and upper middle. */
    glLoadIdentity();
    glTranslatef(-1.3f, 0.75f, -3);
    glVertexPointer(3, GL_FLOAT, 0, spos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, scol);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 2002);
    glLoadIdentity();
    glTranslatef(0, 0.8f, -3);
    glRotatef(20, 1, 0, 0);
    glVertexPointer(3, GL_FLOAT, 0, fpos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, fcol);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 1002);
    glDisableClientState(GL_COLOR_ARRAY);

    /* The textured grid, upper right: short texel coordinates through a texture matrix of 1/64. */
    ct_texture(64, 64, 8, 0, GL_LINEAR, GL_REPEAT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glScalef(1.0f / 64, 1.0f / 64, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(1.25f, 0.7f, -3.2f);
    glRotatef(-60, 1, 0, 0);
    glScalef(0.45f, 1, 0.45f);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, tpos);
    glTexCoordPointer(2, GL_SHORT, 0, ttex);
    glDrawElements(GL_TRIANGLES, TN * TN * 6, GL_UNSIGNED_SHORT, tidx);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_DEPTH_TEST);
    ct_frame("t43_0");
}
