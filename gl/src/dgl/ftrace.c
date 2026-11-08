/* ftrace.c - DGL_FRAMES: a line per frame, for finding long frames.
 *
 *   DGL_FRAMES=C:\OUT\FRAMES.TXT   where (unset: no trace)
 *   DGL_FRAMES_MAX=65536           frames kept, at most (the first ones)
 *
 * Each swap records the frame's time since the last swap returned (the
 * first: since the context was made), the part of it spent in the swap and
 * its wait for the engine, the time in texture uploads and sub-images, and
 * what happened in the frame: texture uploads, sub-images (moved, after a
 * wait, or as whole re-uploads), evictions, waits forced by texture memory,
 * triangles, engine resets (the WARP's pipe switches among them), engine
 * timeouts and stuck fences. The file is written when the program leaves
 * (dglShutdown, or exit), not on a fault; the counts go to COM1 as
 * DGL-FRAMES. Recording costs a few clock reads a frame, and two for each
 * texture upload or sub-image. */
#include "dgl.h"
#include "../gl/gl_tex.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include "mga/warp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dgl_ftrace_on;
uint32_t dgl_ftrace_tex_us, dgl_ftrace_drain_us;

typedef struct {
    uint32_t us, swap_us, drain_us, tex_us;
    uint32_t uploads, upload_kb, subs, moves, sub_syncs, fulls, evictions, syncs;
    uint32_t tris, resets, timeouts, stuck;
} frame_rec;

static frame_rec *rec;
static unsigned long nrec, caprec, maxrec, frames;
static char path[128];
static uint32_t last_us;
static dgl_tex_counts prev;             /* dgl_texc at the last swap (DGL_STATS=2 zeroes it once a second) */
static frame_rec pending;               /* counts taken at the swap's start */
static uint32_t prev_tris, prev_resets, prev_timeouts, prev_stuck;

void dgl_ftrace_init(void)
{
    const char *e = getenv("DGL_FRAMES");
    dgl_ftrace_on = 0;
    if (!e || !*e)
        return;
    snprintf(path, sizeof path, "%s", e);
    e = getenv("DGL_FRAMES_MAX");
    maxrec = e && *e ? strtoul(e, NULL, 10) : 65536ul;
    free(rec);
    rec = NULL;
    nrec = caprec = frames = 0;
    prev = dgl_texc;
    prev_tris = warp_tris + setup_stats.tris;
    prev_resets = engine_resets;
    prev_timeouts = engine_timeouts;
    prev_stuck = engine_fence_stuck;
    dgl_ftrace_tex_us = dgl_ftrace_drain_us = 0;
    last_us = sys_time_us();
    dgl_ftrace_on = 1;
}

#define DELTA(f) (uint32_t)(dgl_texc.f - prev.f)

/* At the swap's start: what the frame did. */
void dgl_ftrace_counts(void)
{
    uint32_t tris = warp_tris + setup_stats.tris;
    pending.uploads = DELTA(uploads);
    pending.upload_kb = DELTA(upload_bytes) >> 10;
    pending.subs = DELTA(sub_fast) + DELTA(sub_iload) + DELTA(sub_full);
    pending.moves = DELTA(sub_move);
    pending.sub_syncs = DELTA(sub_sync);
    pending.fulls = DELTA(sub_full);
    pending.evictions = DELTA(evictions);
    pending.syncs = DELTA(syncs);
    pending.tris = tris - prev_tris;
    pending.resets = engine_resets - prev_resets;
    pending.timeouts = engine_timeouts - prev_timeouts;
    pending.stuck = engine_fence_stuck - prev_stuck;
    pending.tex_us = dgl_ftrace_tex_us;
    prev_tris = tris;
    prev_resets = engine_resets;
    prev_timeouts = engine_timeouts;
    prev_stuck = engine_fence_stuck;
    dgl_ftrace_tex_us = 0;
}

/* At the swap's end (it started at t0). */
void dgl_ftrace_frame(uint32_t t0)
{
    uint32_t now = sys_time_us();
    frames++;
    pending.us = now - last_us;
    pending.swap_us = now - t0;
    pending.drain_us = dgl_ftrace_drain_us;
    dgl_ftrace_drain_us = 0;
    last_us = now;
    prev = dgl_texc;                    /* (after the swap: DGL_STATS=2 may have zeroed it) */
    if (nrec == caprec && nrec < maxrec) {
        unsigned long n = caprec ? caprec * 2 : 4096;
        frame_rec *r;
        if (n > maxrec)
            n = maxrec;
        r = (frame_rec *)realloc(rec, n * sizeof *rec);
        if (r) {
            rec = r;
            caprec = n;
        }
    }
    if (nrec < caprec)
        rec[nrec++] = pending;
}

void dgl_ftrace_write(void)
{
    FILE *f;
    unsigned long i, worst = 0;
    if (!dgl_ftrace_on)
        return;
    dgl_ftrace_on = 0;
    for (i = 1; i < nrec; i++)
        if (rec[i].us > rec[worst].us)
            worst = i;
    f = fopen(path, "w");
    if (!f) {
        DGL_WARN("DGL-FRAMES cannot write %s", path);
        return;
    }
    fprintf(f, "# DGL_FRAMES: times in microseconds; frame 1 counts from the context's start\n"
               "frame us swap_us drain_us tex_us uploads upload_kb subs moves sub_syncs fulls evictions syncs tris "
               "resets timeouts stuck\n");
    for (i = 0; i < nrec; i++) {
        const frame_rec *r = &rec[i];
        fprintf(f, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", i + 1,
                (unsigned long)r->us, (unsigned long)r->swap_us, (unsigned long)r->drain_us,
                (unsigned long)r->tex_us, (unsigned long)r->uploads, (unsigned long)r->upload_kb,
                (unsigned long)r->subs, (unsigned long)r->moves, (unsigned long)r->sub_syncs,
                (unsigned long)r->fulls, (unsigned long)r->evictions, (unsigned long)r->syncs,
                (unsigned long)r->tris, (unsigned long)r->resets, (unsigned long)r->timeouts,
                (unsigned long)r->stuck);
    }
    fclose(f);
    DGL_ERR("DGL-FRAMES frames=%lu kept=%lu worst=%lu us at frame %lu file=%s", frames, nrec,
            nrec ? (unsigned long)rec[worst].us : 0ul, nrec ? worst + 1 : 0ul, path);
}
