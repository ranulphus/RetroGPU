/* g4cost.c - G4COST: what the G400's engine costs for DOS-GL's kind of
 * stream (gl/docs/g4path.md, M0). Streams of textured, depth-tested
 * triangles through the WARP pipe DOS-GL uses (S|A|F), built with the kicks
 * held back, then timed from the kick to the engine idle: the card's own
 * time, the CPU no part of it.
 *
 *   G4COST [pci] [agp=N] [blend] [reps=N] [quick]
 *
 * The sweep: triangles a vertex run (T), what ends each run (kind), and the
 * triangles' size (S pixels):
 *   none     one run for the whole stream (the floor)
 *   pad      a DMAPAD write: a run ended by nothing the engine uses
 *   alpha    ALPHACTRL written again with its value
 *   tex      the texture switched between two (tex_emit_change: TEXORG and
 *            what differs), as GLQuake's surfaces switch
 *   texfull  the whole texture state written (tex_emit)
 *   fan      each run one fan of T triangles by the WARPs' sequencer
 *            (warp_reserve_seq), as DOS-GL sends a GL_POLYGON: WACCEPTSEQ
 *            written for each
 *   fanlist  the same fans' triangles as lists, in one run
 * (fan and fanlist at S 8 and 64 and T up to 8: GLQuake's polygons.)
 * pci keeps the fetches on PCI (DGL_AGP=0), agp=N caps the AGP rate, blend
 * draws with blending on (GLQuake's lightmap pass). Each case is played reps
 * times (default 5) and the median kept.
 *
 * HX-STAT g4cost lines: kind T S tris runs card_us us_tri us_run, then a
 * summary per kind: the extra time a run costs over one long run, from the
 * smallest triangles. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include "mga/tex.h"
#include "mga/warp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 480
#define PITCH 1024
#define ZOFF (PITCH * SH * 2)
#define TEXA 0x400000u
#define TEXB 0x410000u
#define MAXREPS 9

enum { K_NONE, K_PAD, K_ALPHA, K_TEX, K_TEXFULL, K_FAN, K_FANLIST, K_N };
static const char *const kname[K_N] = { "none", "pad", "alpha", "tex", "texfull", "fan", "fanlist" };

static uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static double tsc_mhz;
static mga_texstate ta, tb;
static uint32_t alphactrl;
static int aborted;

static void fill_texture(uint32_t off, int seed)
{
    volatile uint16_t *t = (volatile uint16_t *)(mga_fb + off);
    int i;
    for (i = 0; i < 64 * 64; i++)
        t[i] = (uint16_t)((i * 2654435761u + (unsigned)seed * 40503u) >> 16);
}

static void texstate(mga_texstate *t, uint32_t org)
{
    memset(t, 0, sizeof *t);
    t->org = t->mip_org[0] = org;
    t->mip_n = 1;
    t->w_log2 = t->h_log2 = 6;
    t->pitch = 64;
    t->hwfmt = TEXCTL_TW16;
    t->modulate = 1;
    t->bilinear = 1;
}

/* One triangle of about s pixels (a right triangle, legs sqrt(2s)), the
 * k-th of the stream: placed along rows over the screen. */
static void tri(mga_wvtx *w, int k, int s, int dw)
{
    float l = sqrtf(2.0f * (float)s);
    int per_row = (int)((SW - 4) / (l + 2.0f)), rows = (int)((SH - 4) / (l + 2.0f));
    float x, y;
    int i;
    if (per_row < 1) per_row = 1;
    if (rows < 1) rows = 1;
    x = 2.0f + (float)(k % per_row) * (l + 2.0f);
    y = 2.0f + (float)((k / per_row) % rows) * (l + 2.0f);
    for (i = 0; i < 3; i++) {
        mga_wvtx v;
        memset(&v, 0, sizeof v);
        v.x = x + (i == 1 ? l : 0.0f) + WARP_SUBPIXEL_X;
        v.y = y + (i == 2 ? l : 0.0f) + WARP_SUBPIXEL_Y;
        v.z = 0.5f;
        v.rhw = 1.0f;
        v.bgra = 0xFFC0C0C0u;
        v.spec = 0xFF000000u;
        v.u0 = i == 1 ? 1.0f : 0.0f;
        v.v0 = i == 2 ? 1.0f : 0.0f;
        memcpy(w + (size_t)i * (size_t)dw, &v, (size_t)dw * 4u);   /* (dw dwords: 8 for these pipes) */
    }
}

/* The k-th fan of t triangles of about s pixels each: a centre and t + 1
 * points down a vertical edge (each triangle's legs sqrt(2s)). */
static void fan_vtx(mga_wvtx *w, int k, int t, int s, int dw, int i)
{
    float l = sqrtf(2.0f * (float)s), cw = l + 2.0f, ch = l * (float)t + 2.0f, x, y;
    int per_row = (int)((SW - 4) / cw), rows = (int)((SH - 4) / ch);
    mga_wvtx v;
    if (per_row < 1) per_row = 1;
    if (rows < 1) rows = 1;
    x = 2.0f + (float)(k % per_row) * cw;
    y = 2.0f + (float)((k / per_row) % rows) * ch;
    memset(&v, 0, sizeof v);
    v.x = (i == 0 ? x : x + l) + WARP_SUBPIXEL_X;
    v.y = (i == 0 ? y + l * (float)t * 0.5f : y + l * (float)(i - 1)) + WARP_SUBPIXEL_Y;
    v.z = 0.5f;
    v.rhw = 1.0f;
    v.bgra = 0xFFC0C0C0u;
    v.spec = 0xFF000000u;
    v.u0 = i == 0 ? 0.0f : 1.0f;
    v.v0 = i == 0 ? 0.5f : (float)(i - 1) / (float)t;
    memcpy(w, &v, (size_t)dw * 4u);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* The card's microseconds for n triangles in runs of t, each run after
 * kind's register writes. -1 if the WARP stopped. */
static double play(int kind, int t, int s, int n, int reps, double *cpu_us)
{
    uint64_t card[MAXREPS], cpu[MAXREPS];
    int r, k, j, cur = 0;
    for (r = 0; r < reps; r++) {
        uint64_t t0, t1, t2;
        engine_sync(500000);
        t0 = rdtsc();
        for (k = 0; k < n; k += t) {
            int m = n - k < t ? n - k : t, dw;
            uint32_t *d;
            switch (kind) {
            case K_PAD: MGA_WR32(MGAREG_DMAPAD, 0); break;
            case K_ALPHA: MGA_WR32(MGAREG_ALPHACTRL, alphactrl); break;
            case K_TEX: tex_emit_change(cur ? &tb : &ta, cur ? &ta : &tb); cur = !cur; break;
            case K_TEXFULL: tex_emit(cur ? &tb : &ta); cur = !cur; break;
            default: break;
            }
            if (kind == K_FAN) {
                d = warp_reserve_seq(1, m + 2, &dw);
                if (!d) {
                    aborted = 1;
                    return -1;
                }
                for (j = 0; j < m + 2; j++)
                    fan_vtx((mga_wvtx *)(void *)(d + (size_t)j * (size_t)dw), k / t, t, s, dw, j);
                warp_commit_seq(m + 2);
                continue;
            }
            if (kind == K_FANLIST) {
                d = warp_reserve(m * 3, &dw);
                if (!d) {
                    aborted = 1;
                    return -1;
                }
                for (j = 0; j < m; j++) {
                    mga_wvtx *w = (mga_wvtx *)(void *)(d + (size_t)j * 3u * (size_t)dw);
                    fan_vtx(w, k / t, t, s, dw, 0);
                    fan_vtx((mga_wvtx *)(void *)((uint32_t *)(void *)w + dw), k / t, t, s, dw, j + 1);
                    fan_vtx((mga_wvtx *)(void *)((uint32_t *)(void *)w + 2 * dw), k / t, t, s, dw, j + 2);
                }
                warp_commit(m * 3);
                continue;
            }
            d = warp_reserve(m * 3, &dw);
            if (!d) {
                aborted = 1;
                return -1;
            }
            for (j = 0; j < m; j++)
                tri((mga_wvtx *)(void *)(d + (size_t)j * 3u * (size_t)dw), k + j, s, dw);
            warp_commit(m * 3);
        }
        t1 = rdtsc();
        if (engine_sync(2000000) != 0) {
            aborted = 1;
            return -1;
        }
        t2 = rdtsc();
        card[r] = t2 - t1;
        cpu[r] = t1 - t0;
    }
    qsort(card, (size_t)reps, sizeof card[0], cmp_u64);
    qsort(cpu, (size_t)reps, sizeof cpu[0], cmp_u64);
    *cpu_us = (double)cpu[reps / 2] / tsc_mhz;
    return (double)card[reps / 2] / tsc_mhz;
}

int main(int argc, char **argv)
{
    static const int ts[] = { 1, 2, 4, 8, 32 }, ss[] = { 8, 64, 512 };
    int i, ok, pitch, reps = 5, blend = 0, quick = 0, kind, a, b, rows = 0;
    double floor_us[3][5], run_us[K_N];
    mga_vbe_mode mode;
    mga_target tg;
    uint32_t dw;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "pci")) warp_use_agp = 0;
        else if (!strncmp(argv[i], "agp=", 4)) warp_agp_rate = atoi(argv[i] + 4);
        else if (!strcmp(argv[i], "blend")) blend = 1;
        else if (!strcmp(argv[i], "quick")) quick = 1;
        else if (!strncmp(argv[i], "reps=", 5)) reps = atoi(argv[i] + 5);
    }
    if (reps < 1) reps = 1;
    if (reps > MAXREPS) reps = MAXREPS;
    hx_init(1, argv, "g4cost");
    ok = mga_find(&mga) == 0 && mga_map(&mga) == 0;
    if (!ok || !warp_available() || !mga.has_dwgsync) {
        hx_test("init", 0, "a G400 or G450 is needed");
        hx_done(HX_INIT_FAILED);
    }
    if (vbe_find_mode(SW, SH, 16, &mode) != 0 || vbe_set_mode(&mode, PITCH, &pitch) != 0 || pitch != PITCH) {
        hx_test("init", 0, "640x480x16 with pitch %d", PITCH);
        hx_done(HX_INIT_FAILED);
    }
    engine_init(pitch, 16);
    memset(&tg, 0, sizeof tg);
    tg.color_off = 0; tg.z_off = ZOFF; tg.pitch_px = PITCH; tg.bpp = 16; tg.zbits = 16;
    engine_set_target(&tg);
    engine_set_clip(0, 0, SW, SH);
    engine_fill(0, 0, SW, SH, 0);
    engine_fill_depth(0, 0, SW, SH, 0xFFFF);
    engine_sync(200000);
    fill_texture(TEXA, 1);
    fill_texture(TEXB, 2);
    {
        uint32_t u0 = sys_time_us();
        uint64_t c0 = rdtsc();
        while (sys_time_us() - u0 < 200000u)
            ;
        tsc_mhz = (double)(rdtsc() - c0) / (double)(sys_time_us() - u0);
    }
    warp_kick_bytes = 1u << 30;                 /* (the kicks held back: the stream goes at engine_sync) */
    texstate(&ta, TEXA);
    texstate(&tb, TEXB);
    alphactrl = blend ? ALPHACTRL_SRC(BLEND_DST_COLOR) | ALPHACTRL_DST(BLEND_ZERO) | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE)
                      : ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE);
    if (warp_select(WARP_S | WARP_A | WARP_F) != 0) {
        hx_test("select", 0, "the WARP did not start");
        hx_done(HX_INIT_FAILED);
    }
    dw = DWG_OPCOD_TEXTURE_TRAP | DWG_ATYPE_ZI | DWG_ZMODE_ZLTE | DWG_BOP_COPY;
    MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
    MGA_WR32(MGAREG_ALPHACTRL, alphactrl);
    MGA_WR32(MGAREG_FOGCOL, 0);
    MGA_WR32(MGAREG_FCOL, 0);
    MGA_WR32(MGAREG_WFLAG, 0);
    tex_emit_combiner(0, 0);
    tex_emit(&ta);
    warp_tex(0, 6, 6, 0);
    setup_dwgctl(dw);
    hx_log("HX-STAT g4cost tsc_mhz=%.1f ring_mode=%d agp_xfer=%d ring_kb=%lu blend=%d reps=%d", tsc_mhz,
           warp_ring_mode, warp_agp_xfer, (unsigned long)warp_ring_kb, blend, reps);

    for (kind = 0; kind < K_N && !aborted; kind++) {
        run_us[kind] = 0;
        for (a = 0; a < 3 && !aborted; a++) {
            if ((quick || kind >= K_FAN) && a == 2)
                continue;
            for (b = 0; b < (kind >= K_FAN ? 4 : 5) && !aborted; b++) {
                int t = ts[b], s = ss[a], n = t * 200 < 960 ? t * 200 : 960, runs = (n + t - 1) / t;
                double cpu_us, us;
                if (kind == K_NONE)
                    n = 960, runs = 1;
                us = play(kind, kind == K_NONE ? n : t, s, n, reps, &cpu_us);
                if (us < 0)
                    break;
                hx_log("HX-STAT g4cost kind=%s T=%d S=%d tris=%d runs=%d card_us=%.1f us_tri=%.3f us_run=%.3f cpu_us=%.1f",
                       kname[kind], kind == K_NONE ? n : t, s, n, runs, us, us / n, us / runs, cpu_us);
                rows++;
                if (kind == K_NONE) {
                    floor_us[a][0] = us / n;
                    break;                      /* (one long run: T doesn't apply) */
                }
                if (a == 0 && b == 0)
                    run_us[kind] = (us - floor_us[0][0] * n) / runs;   /* T=1, S=8: a run's extra */
            }
        }
        if (kind != K_NONE && !aborted)
            hx_log("HX-STAT g4cost summary kind=%s extra_us_per_run=%.3f (floor %.3f us a triangle at S=8)",
                   kname[kind], run_us[kind], floor_us[0][0]);
    }
    warp_stop();
    engine_sync(500000);
    hx_test("g4cost", !aborted && rows > 0, "%d cases%s", rows, aborted ? ", the WARP stopped" : "");
    hx_done(aborted ? 1 : 0);
}
