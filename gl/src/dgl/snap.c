/* snap.c - frame captures for tests: dglSnapshot, and DGL_SNAP, which
 * captures chosen frames by swap number. A timedemo draws every demo frame
 * however fast the machine is, so frame n is the same picture on every card
 * and in every emulator run, unlike a screenshot taken at a wall-clock time.
 *
 *   DGL_SNAP=100,250,400   capture the frames shown by those swaps
 *   DGL_SNAPDIR=C:\OUT     where (default C:\OUT, which Loop A collects)
 *
 * Files are binary PPMs named F<swap>.PPM (F00100.PPM), RGB565 expanded to
 * 8 bits per channel by bit replication. Each capture logs DGL-SNAP. */
#include "dgl.h"
#include "../gl/gl_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SNAPS 64

static unsigned long snap_at[MAX_SNAPS];
static int nsnaps, next_snap;
static char snap_dir[96];

int dglSnapshot(const char *path)
{
    const volatile uint16_t *fb;
    unsigned char *row;
    FILE *f;
    int x, y, ok;
    if (!dgl_ctx.active || !path)
        return -1;
    f = fopen(path, "wb");
    if (!f) {
        DGL_WARN("DGL-SNAP cannot write %s", path);
        return -1;
    }
    row = (unsigned char *)malloc((size_t)dgl_ctx.width * 3);
    if (!row) {
        fclose(f);
        return -1;
    }
    dgl_sync();                                   /* the LFB is not ordered with queued draws */
    DGL_GPU_QUIET();
    fb = (const volatile uint16_t *)(mga_fb + dgl_color_off(dgl_ctx.draw_front));
    fprintf(f, "P6\n%d %d\n255\n", dgl_ctx.width, dgl_ctx.height);
    for (y = 0; y < dgl_ctx.height; y++) {
        for (x = 0; x < dgl_ctx.width; x++) {
            uint16_t c = fb[y * dgl_ctx.pitch_px + x];
            unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            row[x * 3 + 0] = (unsigned char)((r << 3) | (r >> 2));
            row[x * 3 + 1] = (unsigned char)((g << 2) | (g >> 4));
            row[x * 3 + 2] = (unsigned char)((b << 3) | (b >> 2));
        }
        fwrite(row, 3, (size_t)dgl_ctx.width, f);
    }
    free(row);
    ok = !ferror(f);
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int cmp_ul(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
    return x < y ? -1 : x > y;
}

void dgl_snap_init(void)
{
    const char *e = getenv("DGL_SNAP"), *d = getenv("DGL_SNAPDIR");
    nsnaps = next_snap = 0;
    while (e && *e && nsnaps < MAX_SNAPS) {
        char *end;
        unsigned long n = strtoul(e, &end, 10);
        if (end == e)
            break;
        if (n)
            snap_at[nsnaps++] = n;
        e = *end == ',' ? end + 1 : end;
    }
    qsort(snap_at, (size_t)nsnaps, sizeof snap_at[0], cmp_ul);
    snprintf(snap_dir, sizeof snap_dir, "%s", d && *d ? d : "C:\\OUT");
}

/* Called by dglSwapBuffers with the number of the swap about to happen,
 * before the hidden buffer is shown. */
void dgl_snap_frame(unsigned long swap)
{
    char path[128];
    while (next_snap < nsnaps && snap_at[next_snap] < swap)
        next_snap++;
    if (next_snap == nsnaps || snap_at[next_snap] != swap)
        return;
    next_snap++;
    snprintf(path, sizeof path, "%s\\F%05lu.PPM", snap_dir, swap % 100000);
    if (dglSnapshot(path) == 0)
        DGL_ERR("DGL-SNAP frame=%lu %s", swap, path);
}
