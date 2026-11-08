/* hreplay - replay a Glide call trace on the build host (PRD M3.6).
 *
 * The runtime is linked in statically and runs on the HAL's host port:
 * register writes go to the reference rasteriser (tests/unit/refrast.c),
 * PCI configuration space and a VBE BIOS are faked here, and COM1 goes to
 * stderr. Frames are read back with grLfbReadRegion before the listed
 * swaps and written as PPM files, for bit-exact comparison with GLPLAY
 * in Loop A. The rasteriser models trapezoids, not texturing, so traces
 * with textured, fogged or blended triangles only match where they draw
 * untextured geometry (refrast counts what it could not draw).
 *
 *   hreplay TRACE OUTDIR frame,frame,... */
#include "glapi.h"
#include "mga/host.h"
#include "mga/sys.h"
#include "refrast.h"
#include "replay.h"
#include "trace/trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

glapi_t gl;
uint8_t rp_scratch[2u << 20];          /* output buffers, up to a 1024x1024 read-back */
void glapi_bind_static(glapi_t *t);

float rp_f(uint32_t v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

#define FB_PHYS   0xD4000000u
#define MMIO_PHYS 0xD5000000u
#define IL_PHYS   0xD6000000u
static uint8_t mmio_dummy[0x4000];

/* ---- Fake PCI configuration space (a G100, AGP, at 1:0.0) and COM1 ---- */
static uint32_t cf8;

static uint32_t cfg_read(uint32_t addr)
{
    int bus = (int)((addr >> 16) & 0xFF), dev = (int)((addr >> 11) & 31), fn = (int)((addr >> 8) & 7);
    int reg = (int)(addr & 0xFC);
    if (bus != 1 || dev != 0 || fn != 0)
        return 0xFFFFFFFFu;
    switch (reg) {
    case 0x00: return 0x1001102Bu;
    case 0x04: return 0x02900007u;
    case 0x08: return 0x03000000u;
    case 0x10: return FB_PHYS | 8u;
    case 0x14: return MMIO_PHYS;
    case 0x18: return IL_PHYS;
    case 0x2C: return 0xFF01102Bu;
    default: return 0;
    }
}

static uint32_t io_in(void *ctx, uint16_t port, int size)
{
    (void)ctx; (void)size;
    if (port == 0xCFC)
        return cfg_read(cf8);
    if (port == 0x3FD)
        return 0x60;                        /* UART: transmitter empty */
    return 0xFFFFFFFFu;
}

static void io_out(void *ctx, uint16_t port, uint32_t v, int size)
{
    (void)ctx; (void)size;
    if (port == 0xCF8)
        cf8 = v;
    else if (port == 0x3F8)
        fputc((int)(v & 0xFF), stderr);
}

/* ---- Fake VBE BIOS: one mode, 0x111 (640x480, 565, linear) ------------- */
static int rm_int(void *ctx, uint8_t intno, void *regs)
{
    sys_rmregs *r = (sys_rmregs *)regs;
    uint8_t *b;
    (void)ctx;
    if (intno != 0x10)
        return -1;
    switch (r->eax & 0xFFFF) {
    case 0x4F00: {
        uint16_t *list = (uint16_t *)sys_real_ptr(0x9000, 0);
        b = (uint8_t *)sys_real_ptr(r->es, (uint16_t)r->edi);
        memcpy(b, "VESA", 4);
        b[4] = 0x00; b[5] = 0x02;
        b[0x0E] = 0x00; b[0x0F] = 0x00; b[0x10] = 0x00; b[0x11] = 0x90;   /* list at 9000:0000 */
        list[0] = 0x111;
        list[1] = 0xFFFF;
        break; }
    case 0x4F01:
        b = (uint8_t *)sys_real_ptr(r->es, (uint16_t)r->edi);
        memset(b, 0, 256);
        if ((r->ecx & 0x1FF) != 0x111)
            return 0;
        b[0] = 0x9B;
        b[0x10] = 0x00; b[0x11] = 0x05;                                   /* 1280 bytes per line */
        b[0x12] = 0x80; b[0x13] = 0x02; b[0x14] = 0xE0; b[0x15] = 0x01;   /* 640 x 480 */
        b[0x19] = 16; b[0x1B] = 6;
        b[0x1F] = 5; b[0x20] = 11; b[0x21] = 6; b[0x22] = 5; b[0x23] = 5; b[0x24] = 0;
        b[0x28] = 0x00; b[0x29] = 0x00; b[0x2A] = 0x00; b[0x2B] = 0xD4;
        break;
    case 0x4F06:
        r->ebx = r->ecx * 2;                                              /* bytes per line */
        break;
    case 0x4F02: case 0x4F07:
        break;
    default:
        return 0;                                                         /* text mode etc. */
    }
    r->eax = (r->eax & ~0xFFFFu) | 0x004F;
    return 0;
}

/* ---- Trace reading (as GLPLAY) --------------------------------------------- */
static uint8_t vcache[TR_VCACHE][60];
static uint8_t *bcache[TR_BCACHE];
static uint32_t bcache_len[TR_BCACHE];
static uint16_t cap_px[640 * 480];

static void capture(const char *dir, uint32_t frame)
{
    char path[512];
    FILE *f;
    int i;
    if (!gl.grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, 640, 480, 1280, cap_px))
        return;
    snprintf(path, sizeof path, "%s/hrp_%u.ppm", dir, frame);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n640 480\n255\n");
    for (i = 0; i < 640 * 480; i++) {
        uint16_t p = cap_px[i];
        fputc(((p >> 11) & 31) * 255 / 31, f);
        fputc(((p >> 5) & 63) * 255 / 63, f);
        fputc((p & 31) * 255 / 31, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    static host_io_hooks hooks;
    FILE *tf;
    uint32_t hdr[4], frame = 0, calls = 0;
    uint8_t *blobs = NULL;
    uint32_t blob_cap = 0;
    int snaps[64], nsnap = 0, last = 0, k, nread = 0;
    if (argc < 4) {
        fprintf(stderr, "usage: hreplay TRACE OUTDIR frame,frame,...\n");
        return 2;
    }
    for (const char *p = argv[3]; *p && nsnap < 64;) {
        snaps[nsnap] = atoi(p);
        if (snaps[nsnap] > last) last = snaps[nsnap];
        nsnap++;
        while (*p && *p != ',') p++;
        if (*p) p++;
    }
    refrast_init();
    refrast_ar_bits = 18;               /* the G100 presented above, as 86Box models it (patch 0012) */
    hooks.in = io_in;
    hooks.out = io_out;
    host_set_io_hooks(&hooks);
    host_set_rm_hook(rm_int, NULL);
    host_register_phys(FB_PHYS, RR_VRAM, rr->vram);
    host_register_phys(MMIO_PHYS, sizeof mmio_dummy, mmio_dummy);
    glapi_bind_static(&gl);
    tf = fopen(argv[1], "rb");
    if (!tf || fread(hdr, 4, 4, tf) != 4 || hdr[0] != TR_MAGIC) {
        fprintf(stderr, "hreplay: %s is not a trace\n", argv[1]);
        return 2;
    }
    for (;;) {
        uint32_t rh, A[32], len, bo[16], used = 0, ret = 0;
        void *B[16];
        int op, na, nb;
        if (fread(&rh, 4, 1, tf) != 1 || rh == TR_OP_END)
            break;
        op = (int)(rh & 0xFFFF); na = (int)((rh >> 16) & 0xFF); nb = (int)(rh >> 24);
        if (na > 32 || nb > 16 || (na && fread(A, 4, (size_t)na, tf) != (size_t)na))
            break;
        for (k = 0; k < nb; k++) {
            if (fread(&len, 4, 1, tf) != 1)
                goto done;
            if (len == TR_NULL) {
                bo[k] = 0xFFFFFFFFu;
            } else if ((len & 0xC0000000u) == TR_BREF) {
                bo[k] = 0x40000000u | (len & 0xFFFF);
            } else if (len & TR_VREF) {
                bo[k] = 0x80000000u | (len & 0xFFFF);
            } else {
                uint32_t padded = (len + 3) & ~3u;
                if (used + padded + 64 > blob_cap) {
                    blob_cap = (used + padded + 64) * 2;
                    blobs = (uint8_t *)realloc(blobs, blob_cap);
                }
                if (fread(blobs + used, 1, padded, tf) != padded)
                    goto done;
                if (len == 60)
                    memcpy(vcache[tr_vhash(blobs + used)], blobs + used, 60);
                if (len >= TR_BMIN) {
                    uint32_t h1, h2, slot;
                    tr_bhash(blobs + used, len, &h1, &h2);
                    slot = h1 & (TR_BCACHE - 1);
                    bcache[slot] = (uint8_t *)realloc(bcache[slot], len);
                    memcpy(bcache[slot], blobs + used, len);
                    bcache_len[slot] = len;
                }
                bo[k] = used;
                used += padded;
            }
        }
        for (k = 0; k < nb; k++)
            B[k] = bo[k] == 0xFFFFFFFFu ? NULL
                 : (bo[k] & 0x80000000u) ? (void *)vcache[bo[k] & 0xFFFF]
                 : (bo[k] & 0x40000000u) ? (void *)bcache[bo[k] & 0xFFFF] : (void *)(blobs + bo[k]);
        if (op >= TR_OP_PSEUDO)
            continue;                        /* LFB spans: not replayed on the host */
        if (op == MGA_API_grBufferSwap) {
            frame++;
            for (k = 0; k < nsnap; k++)
                if ((int)frame == snaps[k])
                    capture(argv[2], frame);
        }
        rp_dispatch(op, A, B, &ret);
        calls++;
        if (op == MGA_API_grLfbReadRegion && A[3] == 640 && A[4] == 480) {
            /* A conformance capture: keep what the program read back. */
            char path[512];
            FILE *f;
            int i;
            snprintf(path, sizeof path, "%s/hrp_read%d.ppm", argv[2], nread++);
            if ((f = fopen(path, "wb")) != NULL) {
                const uint16_t *px = (const uint16_t *)rp_scratch;
                fprintf(f, "P6\n640 480\n255\n");
                for (i = 0; i < 640 * 480; i++) {
                    uint16_t p = px[i];
                    if (A[0] == GR_BUFFER_AUXBUFFER) {       /* as the conformance driver shows depth */
                        uint16_t d = (uint16_t)(p >> 11);
                        p = (uint16_t)((d << 11) | (d << 6) | d);
                    }
                    fputc(((p >> 11) & 31) * 255 / 31, f);
                    fputc(((p >> 5) & 63) * 255 / 63, f);
                    fputc((p & 31) * 255 / 31, f);
                }
                fclose(f);
            }
        }
        if (last && (int)frame >= last && op == MGA_API_grBufferSwap)
            break;
    }
done:
    printf("hreplay calls=%u frames=%u refrast draws=%u pixels=%u unsupported=%u\n", calls, frame, rr->draws,
           rr->pixels, rr->unsupported);
    return 0;
}
