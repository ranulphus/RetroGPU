/* trace.c - the runtime's call recorder (MGAGLIDE trace=1).
 *
 * Every export is a generated thunk (build/gen/thunks.c) that hands the
 * outermost calls to tr_begin/tr_blob/tr_vertex/tr_end. Records go to a
 * memory buffer and are appended to trace_path when it fills, at
 * grGlideShutdown, at exit_after and from the exit hook. The window is
 * frames trace_from..trace_to; recording starts at grGlideInit when
 * trace_from is 0. */
#include "glide/mg.h"
#include "trace/trace.h"
#include "tex/texfmt.h"
#include "mga/sys.h"
#include <string.h>

int mg_trace_on, mg_trace_depth;

#define TBUF (1024u * 1024u)
static uint8_t *tbuf;
static uint32_t used;
static uint8_t (*vcache)[60];
static uint8_t *vvalid;
static struct { uint32_t h1, h2, len; } *bslot;
static int started, stopped, write_failed, fh = -1;
static uint32_t bytes_total, records;

static void flush_buf(void)
{
    if (!used)
        return;
    if (!write_failed && mg_file_write(fh, tbuf, (int)used) != (int)used) {
        write_failed = 1;
        mg_log(MG_LOG_ERROR, "trace: cannot write %s", mg_config.trace_path);
    }
    bytes_total += used;
    used = 0;
}

static void put(const void *p, uint32_t n)
{
    if (used + n > TBUF)
        flush_buf();
    if (n > TBUF) {                       /* large blob: straight to the file */
        if (!write_failed && mg_file_write(fh, p, (int)n) != (int)n)
            write_failed = 1;
        bytes_total += n;
        return;
    }
    memcpy(tbuf + used, p, n);
    used += n;
}

static void put32(uint32_t v) { put(&v, 4); }

static void start(void)
{
    uint32_t hdr[4];
    if (started)
        return;
    tbuf = (uint8_t *)sys_alloc(TBUF);
    vcache = (uint8_t (*)[60])sys_alloc(TR_VCACHE * 60);
    vvalid = (uint8_t *)sys_alloc(TR_VCACHE);
    bslot = sys_alloc(TR_BCACHE * sizeof *bslot);
    if (!tbuf || !vcache || !vvalid || !bslot) {
        mg_log(MG_LOG_ERROR, "trace: out of memory");
        return;
    }
    memset(vvalid, 0, TR_VCACHE);
    memset(bslot, 0, TR_BCACHE * sizeof *bslot);
    started = 1;
    hdr[0] = TR_MAGIC; hdr[1] = TR_VERSION; hdr[2] = MGA_API_COUNT; hdr[3] = 0;
    /* One handle for the whole recording: reopening and seeking to the end
     * walks the FAT chain each time, which grows with the file. */
    fh = mg_file_create(mg_config.trace_path);
    if (fh < 0 || mg_file_write(fh, hdr, sizeof hdr) != (int)sizeof hdr) {
        write_failed = 1;
        return;
    }
    mg_trace_on = 1;
    mg_line("MGL-TRACE start frame=%u file=%s", mg.frame, mg_config.trace_path);
}

static void stop(void)
{
    uint32_t end = TR_OP_END;
    if (!mg_trace_on)
        return;
    put(&end, 4);
    flush_buf();
    if (fh >= 0)
        mg_file_close(fh);
    fh = -1;
    mg_trace_on = 0;
    stopped = 1;
    mg_line("MGL-TRACE stop frame=%u records=%u bytes=%u%s", mg.frame, records, bytes_total,
            write_failed ? " WRITE-FAILED" : "");
}

/* grGlideInit (after the configuration is read): a window starting at
 * frame 0 begins here, with the grGlideInit call itself. */
void trace_init_hook(void)
{
    if (!mg_config.trace || started || mg_config.trace_from > 0)
        return;
    start();
    if (mg_trace_on) {
        tr_begin(MGA_API_grGlideInit, 0, 0, 0);
        tr_end();
    }
}

void trace_frame(uint32_t frame)
{
    if (!mg_config.trace || stopped)
        return;
    if (!started && (int)frame >= mg_config.trace_from) {
        mg_log(MG_LOG_WARN, "trace: windows not starting at frame 0 need keyframes (not implemented); "
                            "recording from frame %u without state", frame);
        start();
    }
    if (mg_trace_on && (int)frame > mg_config.trace_to)
        stop();
}

void trace_flush(void)
{
    stop();
}

void tr_begin(int op, const uint32_t *args, int nargs, int nblobs)
{
    uint32_t h = (uint32_t)(op & 0xFFFF) | ((uint32_t)(nargs & 0xFF) << 16) | ((uint32_t)(nblobs & 0xFF) << 24);
    put(&h, 4);
    if (nargs)
        put(args, (uint32_t)nargs * 4u);
    records++;
}

void tr_blob(const void *p, uint32_t n)
{
    static const uint8_t zero[4];
    if (!p) {
        put32(TR_NULL);
        return;
    }
    if (n >= TR_BMIN) {
        uint32_t h1, h2, slot;
        tr_bhash(p, n, &h1, &h2);
        slot = h1 & (TR_BCACHE - 1);
        if (bslot[slot].len == n && bslot[slot].h1 == h1 && bslot[slot].h2 == h2) {
            put32(TR_BREF | slot);
            return;
        }
        bslot[slot].h1 = h1; bslot[slot].h2 = h2; bslot[slot].len = n;
    }
    put32(n);
    put(p, n);
    if (n & 3)
        put(zero, 4 - (n & 3));
}

void tr_vertex(const GrVertex *v)
{
    uint32_t slot;
    if (!v) {
        put32(TR_NULL);
        return;
    }
    slot = tr_vhash(v);
    if (vvalid[slot] && !memcmp(vcache[slot], v, 60)) {
        put32(TR_VREF | slot);
        return;
    }
    memcpy(vcache[slot], v, 60);
    vvalid[slot] = 1;
    put32(60);
    put(v, 60);
}

void tr_end(void)
{
}

/* ---- Size helpers for the generated recorders ---------------------------- */

uint32_t tr_max_index(const int *ilist, int n)
{
    int i, m = -1;
    if (!ilist)
        return 0;
    for (i = 0; i < n; i++)
        if (ilist[i] > m)
            m = ilist[i];
    return (uint32_t)(m + 1);
}

uint32_t tr_region_bytes(FxU32 w, FxU32 h, FxI32 stride, int bpp)
{
    uint32_t s = (uint32_t)(stride < 0 ? -stride : stride);
    if (!w || !h)
        return 0;
    return (h - 1) * s + w * (uint32_t)bpp;
}

int tr_lfb_src_bpp(GrLfbSrcFmt_t fmt)
{
    return (fmt == GR_LFB_SRC_FMT_888 || fmt == GR_LFB_SRC_FMT_8888 || fmt == GR_LFB_SRC_FMT_565_DEPTH ||
            fmt == GR_LFB_SRC_FMT_555_DEPTH || fmt == GR_LFB_SRC_FMT_1555_DEPTH) ? 4 : 2;
}

uint32_t tr_partial_bytes(GrLOD_t lod, GrAspectRatio_t aspect, GrTextureFormat_t fmt, int start, int end)
{
    int w, h;
    if (end < start)
        return 0;
    tex_dims(lod, aspect, &w, &h);
    return (uint32_t)(end - start + 1) * (uint32_t)w * (uint32_t)tex_bpp(fmt);
}

uint32_t tr_table_bytes(GrTexTable_t type)
{
    return type == GR_TEXTABLE_PALETTE ? 1024u : (uint32_t)sizeof(GuNccTable);
}

uint32_t tr_texinfo_bytes(const GrTexInfo *info)
{
    uint32_t n = 0;
    int l;
    for (l = info->largeLod; l <= info->smallLod; l++)
        n += tex_level_data_bytes((GrLOD_t)l, info->aspectRatio, info->format);
    return n;
}
