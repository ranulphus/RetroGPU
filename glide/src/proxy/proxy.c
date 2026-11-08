/* proxy.c - GLTRACE.OVL internals: loading the retail runtime, frame
 * counting, gu allocation sizes and LFB write capture for the recorder.
 *
 * LFB write locks while recording: the game gets a shadow filled with
 * sentinel rows instead of the retail pointer; at unlock the written
 * pixels are recorded as spans and copied into the retail lock, so the
 * game sees the same result as without the proxy. */
#include "glide/mg.h"
#include "proxy/proxy.h"
#include "trace/trace.h"
#include "tex/texfmt.h"
#include "mga/sys.h"
#include "leload.h"
#include <string.h>

glapi_t px;
int px_loaded;
mg_ctx mg;                       /* the recorder reads mg.frame */
mg_stats_t mg_stats;

extern const char *const glapi_decorated[GLAPI_COUNT];

/* ---- Loading ------------------------------------------------------------ */

static void *io_alloc(size_t n)
{
    void *p = sys_alloc((uint32_t)n);
    if (p)
        memset(p, 0, n);
    return p;
}

static void io_free(void *p) { sys_free(p); }

static void *io_read(const char *path, size_t *size)
{
    int h = mg_file_open(path), n;
    uint32_t cap = 512u * 1024u, used = 0;
    uint8_t *buf;
    if (h < 0)
        return NULL;
    buf = (uint8_t *)sys_alloc(cap);
    while (buf) {
        n = mg_file_read(h, buf + used, (int)(cap - used));
        if (n <= 0)
            break;
        used += (uint32_t)n;
        if (used == cap) {
            uint8_t *nb = (uint8_t *)sys_alloc(cap * 2);
            if (nb)
                memcpy(nb, buf, used);
            sys_free(buf);
            buf = nb;
            cap *= 2;
        }
    }
    mg_file_close(h);
    *size = used;
    return buf;
}

static void io_log(const char *msg) { mg_log(MG_LOG_DEBUG, "leload: %s", msg); }

static const le_io px_io = { io_alloc, io_free, io_read, io_log };

void px_load(void)
{
    char err[96] = "";
    le_module *m;
    void **slot = (void **)&px;
    int i, missing = 0;
    if (px_loaded)
        return;
    px_loaded = 1;
    mg_log_init();
    if (!mg_config.retail_path[0])
        mg_config_load();
    m = le_load(&px_io, mg_config.retail_path, err, sizeof err);
    if (!m) {
        mg_line("MGL-PROXY cannot load %s: %s", mg_config.retail_path, err);
        return;
    }
    for (i = 0; i < GLAPI_COUNT; i++) {
        slot[i] = le_proc(m, glapi_decorated[i]);
        missing += !slot[i];
    }
    mg_line("MGL-PROXY retail=%s module=%s missing=%d", mg_config.retail_path, le_module_name(m), missing);
}

void px_init(void)
{
    mg_config_load();
    trace_init_hook();
}

void px_missing(int id)
{
    static uint8_t seen[MGA_API_COUNT];
    if (id >= 0 && id < MGA_API_COUNT && !seen[id]) {
        seen[id] = 1;
        mg_line("MGL-PROXY %s not in the retail runtime", glapi_decorated[id]);
    }
}

/* ---- Frames and exit --------------------------------------------------------- */

static void ut_exit(int code)
{
    static const char magic[] = "86Box";
    int i;
    for (i = 0; i < 5; i++)
        sys_outb(0x80, (uint8_t)magic[i]);
    sys_outb(0x80, 0x80);
    sys_outb(0x80, 0x0E);
    if (sys_inb(0x0E80) == 0xFF)
        return;
    sys_outb(0x0E80, 0x04);
    for (i = 0; i < 100000 && !(sys_inb(0x0E80) & 0x02); i++)
        ;
    sys_outb(0x0E81, (uint8_t)(code & 0x7F));
}

void px_frame(void)
{
    mg.frame++;
    if (mg_config.stats_every && mg.frame % (unsigned)mg_config.stats_every == 0)
        mg_line("MGL-STAT frame=%u", mg.frame);
    trace_frame(mg.frame);
    if (mg_config.exit_after && (int)mg.frame >= mg_config.exit_after) {
        mg_line("MGL-EXIT frames=%u", mg.frame);
        trace_flush();
        if (px.grGlideShutdown)
            px.grGlideShutdown();
        ut_exit(0);
    }
}

void px_shutdown(void)
{
    trace_flush();
}

/* ---- gu allocations (sizes of guTexDownloadMipMap sources) ------------------ */

#define PX_GU_MAX 1024
static struct { GrLOD_t small, large; GrAspectRatio_t aspect; GrTextureFormat_t fmt; } gu[PX_GU_MAX];

void px_gu_alloc(GrMipMapId_t id, GrChipID_t tmu, FxU8 odd_even_mask, int width, int height,
                 GrTextureFormat_t fmt, GrMipMapMode_t mm_mode, GrLOD_t smallest_lod, GrLOD_t largest_lod,
                 GrAspectRatio_t aspect, GrTextureClampMode_t s_clamp_mode, GrTextureClampMode_t t_clamp_mode,
                 GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode, float lod_bias,
                 FxBool trilinear)
{
    MGA_UNUSED(tmu); MGA_UNUSED(odd_even_mask); MGA_UNUSED(width); MGA_UNUSED(height); MGA_UNUSED(mm_mode);
    MGA_UNUSED(s_clamp_mode); MGA_UNUSED(t_clamp_mode); MGA_UNUSED(minfilter_mode); MGA_UNUSED(magfilter_mode);
    MGA_UNUSED(lod_bias); MGA_UNUSED(trilinear);
    if ((int)id < 0 || (int)id >= PX_GU_MAX)
        return;
    gu[id].small = smallest_lod;
    gu[id].large = largest_lod;
    gu[id].aspect = aspect;
    gu[id].fmt = fmt;
}

uint32_t tr_gu_src_bytes(GrMipMapId_t mmid)
{
    uint32_t n = 0;
    int l;
    if ((int)mmid < 0 || (int)mmid >= PX_GU_MAX)
        return 0;
    for (l = gu[mmid].large; l <= gu[mmid].small; l++)
        n += tex_level_data_bytes((GrLOD_t)l, gu[mmid].aspect, gu[mmid].fmt);
    return n;
}

uint32_t tr_gu_level_bytes(GrMipMapId_t mmid, GrLOD_t lod)
{
    if ((int)mmid < 0 || (int)mmid >= PX_GU_MAX)
        return 0;
    return tex_level_data_bytes(lod, gu[mmid].aspect, gu[mmid].fmt);
}

/* ---- LFB write capture ------------------------------------------------------ */

#define PAT_N 3072
static uint16_t pat16[PAT_N];
static uint32_t pat32[PAT_N];
static int pat_ready;
static struct {
    int active, bpp, w, h;
    GrLfbInfo_t real;
    GrLfbWriteMode_t mode;
    GrBuffer_t buffer;
    uint32_t seed;
} lk;
static uint8_t *shadow;
static uint32_t shadow_size;

static int mode_bpp(GrLfbWriteMode_t m)
{
    return (m == GR_LFBWRITEMODE_888 || m == GR_LFBWRITEMODE_8888 || m == GR_LFBWRITEMODE_565_DEPTH ||
            m == GR_LFBWRITEMODE_555_DEPTH || m == GR_LFBWRITEMODE_1555_DEPTH) ? 4 : 2;
}

static const void *srow(int y)
{
    uint32_t off = ((uint32_t)y * 37u + lk.seed) & 1023u;
    return lk.bpp == 4 ? (const void *)(pat32 + off) : (const void *)(pat16 + off);
}

FxBool px_lfb_lock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin,
                   FxBool pixelPipeline, GrLfbInfo_t *info)
{
    FxBool r = px.grLfbLock(type, buffer, writeMode, origin, pixelPipeline, info);
    uint32_t stride, need;
    int y;
    lk.active = 0;
    if (!r || !mg_trace_on || (type & 1) != GR_LFB_WRITE_ONLY || !info)
        return r;
    if (!pat_ready) {
        uint32_t v = 0x2545F491u;
        int i;
        for (i = 0; i < PAT_N; i++) {
            v = v * 1664525u + 1013904223u;
            pat32[i] = v ^ (v >> 13);
            pat16[i] = (uint16_t)(pat32[i] >> 7);
        }
        pat_ready = 1;
    }
    lk.real = *info;
    lk.mode = info->writeMode;
    lk.buffer = buffer;
    lk.bpp = mode_bpp(info->writeMode);
    lk.w = px.grSstScreenWidth ? (int)px.grSstScreenWidth() : 640;
    lk.h = px.grSstScreenHeight ? (int)px.grSstScreenHeight() : 480;
    lk.seed = lk.seed * 1664525u + 1013904223u + mg.frame;
    stride = (lk.w <= 1024 ? 2048u : (uint32_t)mga_pow2_pitch(lk.w) * 2u) * (lk.bpp == 4 ? 2u : 1u);
    need = stride * (uint32_t)lk.h;
    if (need > shadow_size) {
        sys_free(shadow);
        shadow = (uint8_t *)sys_alloc(need);
        shadow_size = shadow ? need : 0;
    }
    if (!shadow)
        return r;
    for (y = 0; y < lk.h; y++)
        memcpy(shadow + (uint32_t)y * stride, srow(y), (uint32_t)lk.w * (uint32_t)lk.bpp);
    info->lfbPtr = shadow;
    info->strideInBytes = stride;
    lk.active = 1;
    return r;
}

void px_lfb_unlock(GrLock_t type, GrBuffer_t buffer)
{
    static uint8_t *sp;
    static uint32_t sp_cap;
    uint32_t n = 0, args[3], stride = (lk.w <= 1024 ? 2048u : (uint32_t)mga_pow2_pitch(lk.w) * 2u) * (lk.bpp == 4 ? 2u : 1u);
    int x, y;
    MGA_UNUSED(buffer);
    if (!lk.active || (type & 1) != GR_LFB_WRITE_ONLY)
        return;
    for (y = 0; y < lk.h; y++) {
        const uint8_t *row = shadow + (uint32_t)y * stride, *pr = (const uint8_t *)srow(y);
        if (!memcmp(row, pr, (uint32_t)lk.w * (uint32_t)lk.bpp))
            continue;
        for (x = 0; x < lk.w;) {
            int x0 = x;
            uint32_t cnt, need;
            while (x < lk.w && memcmp(row + x * lk.bpp, pr + x * lk.bpp, (size_t)lk.bpp))
                x++;
            cnt = (uint32_t)(x - x0);
            if (!cnt) {
                x++;
                continue;
            }
            /* into the retail lock */
            memcpy((uint8_t *)lk.real.lfbPtr + (uint32_t)y * lk.real.strideInBytes + (uint32_t)x0 * (uint32_t)lk.bpp,
                   row + x0 * lk.bpp, cnt * (uint32_t)lk.bpp);
            need = n + 8 + ((cnt * (uint32_t)lk.bpp + 3) & ~3u);
            if (need > sp_cap) {
                uint8_t *nb = (uint8_t *)sys_alloc(need * 2);
                if (!nb)
                    return;
                if (sp) {
                    memcpy(nb, sp, n);
                    sys_free(sp);
                }
                sp = nb;
                sp_cap = need * 2;
            }
            ((uint16_t *)(sp + n))[0] = (uint16_t)y;
            ((uint16_t *)(sp + n))[1] = (uint16_t)x0;
            ((uint16_t *)(sp + n))[2] = (uint16_t)cnt;
            ((uint16_t *)(sp + n))[3] = 0;
            memcpy(sp + n + 8, row + x0 * lk.bpp, cnt * (uint32_t)lk.bpp);
            n = need;
        }
    }
    if (mg_trace_on) {
        args[0] = (uint32_t)lk.buffer; args[1] = (uint32_t)lk.mode; args[2] = (uint32_t)lk.bpp;
        tr_begin(TR_OP_LFBSPANS, args, 3, 1);
        tr_blob(sp ? sp : (const void *)args, n);
        tr_end();
    }
    lk.active = 2;                 /* copied: the forward below must not copy again */
}

FxBool px_lfb_unlock_forward(GrLock_t type, GrBuffer_t buffer)
{
    if (lk.active == 1)
        px_lfb_unlock(type, buffer);       /* recording stopped in between: still copy */
    lk.active = 0;
    return px.grLfbUnlock(type, buffer);
}
