/* glos.c - DOS-GL in a window on a GLOS desktop, by GLOS's API 2.0 (GLOS PRD §9.7, §10.3; GLOS
 * docs/milestones-m8.md step D): GL's side. The window, its memory and GLOS's GPU lock are the
 * device's (rg_device's window_ok, window_modes and open_glos: the Matrox one is
 * hal/src/rg/mga_glos.c; gl/docs/b1.md).
 *
 * When the program runs under GLOS and GLOS says a device's GL can have a window there, dglInit
 * opens one of the context's size instead of setting a video mode. Without GLOS, without such a
 * device, or with DGL_FULLSCREEN set, the program takes the screen as it always has (under
 * GLOS, as a session). */
#include "dgl.h"
#include <stdlib.h>
#include <string.h>

int dgl_glos_want(void)
{
    const char *e = getenv("DGL_FULLSCREEN");
    if (e && *e && *e != '0')
        return 0;
    return rg_window_device() != NULL;
}

/* The sizes a window can have: the device's (those that fit in the desktop's work area). */
int dgl_glos_modes(DGLMode *out, int max)
{
    const rg_device *d = rg_window_device();
    rg_mode m[16];
    int n, i;
    if (!d)
        return 0;
    n = d->window_modes(m, 16);
    for (i = 0; i < n && i < 16 && out && i < max; i++)
        dgl_mode_out(&out[i], &m[i]);
    return n;
}

unsigned int dglGetGlosWindow(void) { return rg_lock.on ? dgl_ctx.window : 0; }
