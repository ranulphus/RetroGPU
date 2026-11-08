/* GLPLAY <trace> [frames] [--glide=PATH]: replay a Glide call trace (see
 * docs/trace.md) through an OVL, capturing the back buffer before the
 * listed swaps (frame numbers counted from 1, comma separated) as
 * GLP_<n>.PPM. Ends after the last listed frame, or at the end of the
 * trace. The same trace through the retail OVL on the emulated Voodoo and
 * through MGA-Glide gives comparable frames of real game content. */
#include "hx.h"
#include "replay.h"
#include "trace/trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t rp_scratch[2u << 20];          /* output buffers, up to a 1024x1024 read-back */

float rp_f(uint32_t v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static uint8_t vcache[TR_VCACHE][60];
static uint8_t *bcache[TR_BCACHE];
static uint32_t bcache_len[TR_BCACHE];
static uint16_t cap_px[640 * 480];
static uint8_t cap_rgb[640 * 480 * 3];

static FILE *tf;
static uint8_t *blobs;
static uint32_t blob_cap;

static int rd(void *p, uint32_t n) { return fread(p, 1, n, tf) == n ? 0 : -1; }

static int snap_list[64], nsnap, last_snap;

static void parse_frames(const char *s)
{
    while (*s && nsnap < 64) {
        snap_list[nsnap] = atoi(s);
        if (snap_list[nsnap] > last_snap) last_snap = snap_list[nsnap];
        nsnap++;
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
    }
}

static void capture(uint32_t frame)
{
    char name[24];
    int i, w = (int)gl.grSstScreenWidth(), h = (int)gl.grSstScreenHeight();
    if (w > 640) w = 640;
    if (h > 480) h = 480;
    gl.grSstIdle();
    if (!gl.grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, (FxU32)w, (FxU32)h, (FxU32)w * 2, cap_px)) {
        hx_test("readregion", 0, "frame %u", frame);
        return;
    }
    for (i = 0; i < w * h; i++) {
        uint16_t p = cap_px[i];
        cap_rgb[i * 3 + 0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
        cap_rgb[i * 3 + 1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
        cap_rgb[i * 3 + 2] = (uint8_t)((p & 31) * 255 / 31);
    }
    snprintf(name, sizeof name, "GLP_%u", frame);
    hx_save_ppm(name, w, h, cap_rgb);
}

/* The lock info of the open LFB lock, for the recorded write spans. */
static GrLfbInfo_t lock_info;
static int lock_open;

static void apply_spans(const uint8_t *p, uint32_t n, int bpp)
{
    uint32_t off = 0;
    if (!lock_open)
        return;
    while (off + 8 <= n) {
        const uint16_t *h = (const uint16_t *)(p + off);
        uint32_t y = h[0], x = h[1], cnt = h[2], bytes = cnt * (uint32_t)bpp;
        memcpy((uint8_t *)lock_info.lfbPtr + y * lock_info.strideInBytes + x * (uint32_t)bpp, p + off + 8, bytes);
        off += 8 + ((bytes + 3) & ~3u);
    }
}

int main(int argc, char **argv)
{
    char err[128] = "";
    const char *pos[2] = { NULL, NULL };
    int npos = 0, missing, i;
    uint32_t hdr[4], frame = 0, calls = 0, unknown = 0;
    le_module *m;
    for (i = 1; i < argc;) {
        if (argv[i][0] != '-' && npos < 2) {
            pos[npos++] = argv[i];
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof argv[0]);
            argc--;
        } else
            i++;
    }
    hx_init(argc, argv, "glplay");
    if (!pos[0]) {
        hx_log("usage: GLPLAY <trace> [frame,frame,...] [--glide=PATH]");
        hx_done(HX_BAD_ARGS);
    }
    if (pos[1])
        parse_frames(pos[1]);
    m = glbind_load(hx_args.glide ? hx_args.glide : "C:\\TEST\\GLIDE2X.OVL", &missing, err, sizeof err);
    if (!m) {
        hx_test("load", 0, "%s", err);
        hx_done(HX_INIT_FAILED);
    }
    tf = fopen(pos[0], "rb");
    if (!tf || rd(hdr, sizeof hdr) || hdr[0] != TR_MAGIC || hdr[1] != TR_VERSION) {
        hx_test("trace", 0, "%s: not an MGTR v%d trace", pos[0], TR_VERSION);
        hx_done(HX_INIT_FAILED);
    }
    for (;;) {
        uint32_t rh, A[32], len, bo[16], used = 0, ret = 0;
        void *B[16];
        int op, na, nb, k;
        if (rd(&rh, 4) || rh == TR_OP_END)
            break;
        op = (int)(rh & 0xFFFF); na = (int)((rh >> 16) & 0xFF); nb = (int)(rh >> 24);
        if (na > 32 || nb > 16 || (na && rd(A, (uint32_t)na * 4))) {
            hx_test("trace", 0, "bad record after %u calls", calls);
            break;
        }
        for (k = 0; k < nb; k++) {
            if (rd(&len, 4))
                goto truncated;
            if (len == TR_NULL) {
                bo[k] = 0xFFFFFFFFu;
            } else if ((len & 0xC0000000u) == TR_BREF) {
                bo[k] = 0x40000000u | (len & 0xFFFF);
            } else if (len & TR_VREF) {
                bo[k] = 0x80000000u | (len & 0xFFFF);
            } else {
                uint32_t padded = (len + 3) & ~3u;
                if (used + padded + 64 > blob_cap) {
                    uint32_t ncap = (used + padded + 64) * 2;
                    uint8_t *nbuf = (uint8_t *)realloc(blobs, ncap);
                    if (!nbuf) {
                        hx_test("memory", 0, "blob of %u bytes", len);
                        hx_done(HX_FAIL);
                    }
                    blobs = nbuf;
                    blob_cap = ncap;
                }
                if (rd(blobs + used, padded))
                    goto truncated;
                if (len == 60) {             /* a vertex blob enters the cache */
                    uint32_t slot = tr_vhash(blobs + used);
                    memcpy(vcache[slot], blobs + used, 60);
                }
                if (len >= TR_BMIN) {        /* and larger blobs the blob cache */
                    uint32_t h1, h2, slot;
                    tr_bhash(blobs + used, len, &h1, &h2);
                    slot = h1 & (TR_BCACHE - 1);
                    if (bcache_len[slot] < len) {
                        free(bcache[slot]);
                        bcache[slot] = (uint8_t *)malloc(len);
                    }
                    if (bcache[slot]) {
                        memcpy(bcache[slot], blobs + used, len);
                        bcache_len[slot] = len;
                    }
                }
                bo[k] = used;
                used += padded;
            }
        }
        for (k = 0; k < nb; k++)
            B[k] = bo[k] == 0xFFFFFFFFu ? NULL
                 : (bo[k] & 0x80000000u) ? (void *)vcache[bo[k] & 0xFFFF]
                 : (bo[k] & 0x40000000u) ? (void *)bcache[bo[k] & 0xFFFF] : (void *)(blobs + bo[k]);
        if (op == TR_OP_LFBSPANS) {
            uint32_t sl = (bo[0] & 0x40000000u) && bo[0] != 0xFFFFFFFFu ? bcache_len[bo[0] & 0xFFFF] : used;
            if (B[0])
                apply_spans((const uint8_t *)B[0], sl, (int)A[2]);
            continue;
        }
        if (op >= TR_OP_PSEUDO)
            continue;
        if (op == MGA_API_grBufferSwap) {
            frame++;
            for (k = 0; k < nsnap; k++)
                if ((int)frame == snap_list[k])
                    capture(frame);
        }
        if (op == MGA_API_grGlideShutdown && last_snap && (int)frame < last_snap)
            hx_log("trace ended at frame %u before frame %d", frame, last_snap);
        if (rp_dispatch(op, A, B, &ret) < 0)
            unknown++;
        calls++;
        if (op == MGA_API_grLfbLock) {
            lock_open = ret != 0;
            memcpy(&lock_info, rp_scratch, sizeof lock_info);
        } else if (op == MGA_API_grLfbUnlock)
            lock_open = 0;
        if (last_snap && (int)frame >= last_snap && op == MGA_API_grBufferSwap)
            break;
    }
    hx_stat("glplay calls=%u frames=%u unknown=%u", calls, frame, unknown);
    fclose(tf);
    gl.grGlideShutdown();
    hx_done(0);
    return 0;
truncated:
    hx_test("trace", 0, "truncated after %u calls", calls);
    gl.grGlideShutdown();
    hx_done(HX_FAIL);
    return 1;
}
