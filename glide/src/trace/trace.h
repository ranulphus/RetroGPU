/* trace.h - binary Glide call traces (docs/trace.md).
 *
 * File: "MGTR", version, API count, reserved (4 x u32), then records:
 *     u16 op   u8 nargs   u8 nblobs   u32 args[nargs]
 *     nblobs x { u32 len; len bytes, padded to 4 }
 * len 0xFFFFFFFF marks a NULL pointer; len with bit 31 set (and not all
 * ones) is a reference to vertex-cache entry (len & 0xFFFF); len with
 * bit 30 set is a reference to blob-cache entry (len & 0xFFFF). Blobs of
 * TR_BMIN bytes or more enter the blob cache at slot tr_bhash() & mask,
 * vertex blobs (60 bytes, from tr_vertex) the vertex cache. op values at
 * and above TR_OP_PSEUDO are not API calls (LFB write spans, end). */
#ifndef MG_TRACE_H
#define MG_TRACE_H
#include "glide/glide2.h"
#include "mga/types.h"

#define TR_MAGIC        0x5254474Du      /* "MGTR" */
#define TR_VERSION      1
#define TR_OP_PSEUDO    0xF000
#define TR_OP_LFBSPANS  0xF001           /* args: buffer, mode, bpp; blob: spans */
#define TR_OP_END       0xFFFF
#define TR_NULL         0xFFFFFFFFu
#define TR_VREF         0x80000000u
#define TR_VCACHE       4096
#define TR_BREF         0x40000000u
#define TR_BCACHE       512
#define TR_BMIN         64

extern int mg_trace_on;                  /* recording now */
extern int mg_trace_depth;               /* nesting: only outermost calls are recorded */

void tr_begin(int op, const uint32_t *args, int nargs, int nblobs);
void tr_blob(const void *p, uint32_t n);
void tr_vertex(const GrVertex *v);
void tr_end(void);

void trace_init_hook(void);             /* grGlideInit, after the configuration */
void trace_frame(uint32_t frame);        /* at each swap: window start / end */
void trace_flush(void);                  /* write buffered records (shutdown, exit) */
void tr_lfb_unlock_spans(GrLock_t type, GrBuffer_t buffer);

/* Size helpers used by the generated recorders. */
uint32_t tr_max_index(const int *ilist, int n);
uint32_t tr_region_bytes(FxU32 w, FxU32 h, FxI32 stride, int bpp);
int      tr_lfb_src_bpp(GrLfbSrcFmt_t fmt);
uint32_t tr_partial_bytes(GrLOD_t lod, GrAspectRatio_t aspect, GrTextureFormat_t fmt, int start, int end);
uint32_t tr_table_bytes(GrTexTable_t type);
uint32_t tr_texinfo_bytes(const GrTexInfo *info);
uint32_t tr_gu_src_bytes(GrMipMapId_t mmid);
uint32_t tr_gu_level_bytes(GrMipMapId_t mmid, GrLOD_t lod);

/* Shared by writer and reader (trfmt.c): the cache slot of a vertex. */
uint32_t tr_vhash(const void *v);
/* Blob content hash (two independent 32-bit FNV variants). */
void     tr_bhash(const void *p, uint32_t n, uint32_t *h1, uint32_t *h2);

#endif
