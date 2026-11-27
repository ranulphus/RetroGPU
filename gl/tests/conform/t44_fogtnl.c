/* t44: fog on a T&L engine: t10's three floors (LINEAR, EXP, EXP2), drawn from arrays in depth bands across which
 * the factor changes by 0.08 at most, each band a draw of its own: what a transform engine's per-vertex fog draws as
 * the CPU's path does (gl/docs/dx7.md, X3b; the factor is interpolated across the screen, so one long draw drifts even
 * in linear fog). t44_1: each floor one draw of a fine grid (rows half a unit deep): its whole depth range too much
 * for the draw's box, each triangle's own little enough (the engine's per-triangle rule, Fifth Wheel's chunks). On a
 * card without the engine the CPU's path draws the same. */
#include <math.h>
#include "ct.h"

#define COLS 4
#define MAXV 4096

static GLfloat pos[MAXV][3];
static GLubyte col[MAXV][4];

static float factor(GLenum mode, float density, float d)
{
    if (mode == GL_LINEAR) {             /* (start 2, end 15, clamped: density unused) */
        float f = (15.0f - d) / 13.0f;
        return f < 0 ? 0 : f > 1 ? 1 : f;
    }
    if (mode == GL_EXP)
        return (float)exp(-density * d);
    return (float)exp(-(density * d) * (density * d));
}

/* One band: COLS quads across [x0, x1] between depths d0 and d1, as triangles, in a draw of their own. */
static void band(float x0, float x1, float d0, float d1)
{
    int i, n = 0;
    for (i = 0; i < COLS; i++) {
        float a = x0 + (x1 - x0) * i / COLS, b = x0 + (x1 - x0) * (i + 1) / COLS;
        const float q[6][2] = { { a, d0 }, { b, d0 }, { b, d1 }, { a, d0 }, { b, d1 }, { a, d1 } };
        int k;
        for (k = 0; k < 6; k++, n++) {
            pos[n][0] = q[k][0], pos[n][1] = -1, pos[n][2] = -q[k][1];
            col[n][0] = 230, col[n][1] = 153, col[n][2] = 51, col[n][3] = 255;
        }
    }
    glDrawArrays(GL_TRIANGLES, 0, n);
}

/* A floor from depth d0 to d1 as one draw: a grid of COLS columns and rows h deep. */
static void grid(float x0, float x1, float d0, float d1, float h)
{
    int n = 0;
    float d;
    for (d = d0; d < d1 - 1e-4f; d += h) {
        float e = d + h > d1 ? d1 : d + h;
        int i;
        for (i = 0; i < COLS && n + 6 <= MAXV; i++) {
            float a = x0 + (x1 - x0) * i / COLS, b = x0 + (x1 - x0) * (i + 1) / COLS;
            const float q[6][2] = { { a, d }, { b, d }, { b, e }, { a, d }, { b, e }, { a, e } };
            int k;
            for (k = 0; k < 6; k++, n++) {
                pos[n][0] = q[k][0], pos[n][1] = -1, pos[n][2] = -q[k][1];
                col[n][0] = 230, col[n][1] = 153, col[n][2] = 51, col[n][3] = 255;
            }
        }
    }
    glDrawArrays(GL_TRIANGLES, 0, n);
}

/* A floor from depth d0 to d1 in bands of at most 0.08 in the factor (and 0.5 deep at most). */
static void banded(GLenum mode, float density, float x0, float x1, float d0, float d1)
{
    float d = d0;
    while (d < d1) {
        float e = d, f = factor(mode, density, d);
        while (e < d1 && e - d < 0.5f && f - factor(mode, density, e + 0.01f) <= 0.08f)
            e += 0.01f;
        if (e <= d)
            e = d + 0.01f;
        band(x0, x1, d, e > d1 ? d1 : e);
        d = e;
    }
}

void ct_run(void)
{
    static const GLfloat fogc[4] = { 0.5f, 0.6f, 0.7f, 1 };
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -0.75, 0.75, 1, 30);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.5f, 0.6f, 0.7f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    glEnable(GL_FOG);
    glFogfv(GL_FOG_COLOR, fogc);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 2);
    glFogf(GL_FOG_END, 15);
    banded(GL_LINEAR, 0.0f, -3.0f, -1.1f, 2.0f, 15.0f);
    glFogi(GL_FOG_MODE, GL_EXP);
    glFogf(GL_FOG_DENSITY, 0.15f);
    banded(GL_EXP, 0.15f, -0.9f, 0.9f, 1.5f, 20.0f);
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogf(GL_FOG_DENSITY, 0.12f);
    banded(GL_EXP2, 0.12f, 1.1f, 3.0f, 1.5f, 20.0f);
    ct_frame("t44_0");
    glClear(GL_COLOR_BUFFER_BIT);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    grid(-3.0f, -1.1f, 2.0f, 15.0f, 0.5f);
    glFogi(GL_FOG_MODE, GL_EXP);
    glFogf(GL_FOG_DENSITY, 0.15f);
    grid(-0.9f, 0.9f, 1.5f, 20.0f, 0.5f);
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogf(GL_FOG_DENSITY, 0.12f);
    grid(1.1f, 3.0f, 1.5f, 20.0f, 0.5f);
    glDisable(GL_FOG);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    ct_frame("t44_1");
}
