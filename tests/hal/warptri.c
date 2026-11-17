/* warptri.c - WARPTRI: the WARP setup engine of the G400 (speed plan W0;
 * docs/g400-warp.md) or the G200 (docs/g200-warp.md) through the HAL's
 * warp.c. On a G200 the G400-only tests (switch=:
 * two-texture pipes; seq: the ACCEPT sequencer) report that they don't apply.
 *
 *   WARPTRI [pipe=N] [n=COUNT] [pio] [pci] [pads=K] [keep]
 *   WARPTRI starts=N [reload] [pads=K] [keep] [pio] [pci]
 *   WARPTRI switch=N [pipe=P]: the cycles of a T0/T2 pipe switch (N triangles on one pipe, then N switching)
 *   WARPTRI hazards: each hazard 86Box's silicon checks report (local patch 0124), beside its safe form
 *
 * The vertices go by bus-master DMA (warp.c's default) or, with pio,
 * through the DMA window by the CPU. By DMA, the HAL turns AGP on where the
 * card and the bridge have it (warp_use_agp); pci keeps the fetches on PCI
 * (cached rings), as DOS-GL's DGL_AGP=0 does. pads sets warp_start_pads; keep leaves
 * the engine as it is at exit (as DOS-GL does: the WARP suspended), where
 * WARPTRI otherwise resets it.
 *
 *   WARPTRI agp=N [agpdraw] [noflush] [cached]
 *
 * agp=N: AGP at N x at most (warp_agp_rate), the bandwidth (dmabw) or with
 * agpdraw the drawing tests; the agp check fails if the fetches stayed on
 * PCI (the HAL turns AGP on only with write-combining rings, which need
 * GLOS). cached: AGP on by WARPTRI itself with the HAL's cached rings, the
 * unsafe form 86Box's silicon checks report as agp-cached (the caches
 * flushed before each kick, unless noflush, so it still draws); not on the
 * G200, whose AGP_PLL is the HAL's.
 *
 * starts=N instead starts the WARP N times, a small triangle and a stop
 * each time, and logs each start (warp_diag); reload alternates pipes 0 and
 * 7, so every start loads the microcode again.
 *
 * a) the pipe (default 0, TGZ) loaded and started (warp_select), and WARP0's
 *    instruction memory read back against the microcode;
 * b) a Gouraud triangle through the WARP (warp_vertices; Mesa's mga vertex
 *    with its subpixel offset) on the left, the same through setup_triangle
 *    320 pixels to the right, compared pixel by pixel;
 * b2) two triangles in one batch, no depth (the G400's two WARPs take
 *    alternate triangles; the G200 has one): both drawn?
 * c) two triangles crossing in depth (ZI, less-than), both ways;
 * d) COUNT small triangles each way (default 4000), timed with RDTSC
 *    including the wait for the engine: cycles a triangle.
 *
 * HX-TEST lines: select, warp-draws, gouraud, two-warps, depth; HX-STAT
 * lines give the counts and timings. The picture is W0.PPM. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include "mga/warp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 480
#define PITCH 1024
#define ZOFF (PITCH * SH * 2)

static int aborted;
static uint32_t opmode_saved;

static uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

typedef struct { float x, y, z; uint8_t r, g, b; } vtx;

static mga_wvtx wv(const vtx *v)
{
    mga_wvtx w;
    memset(&w, 0, sizeof w);
    w.x = v->x + WARP_SUBPIXEL_X;
    w.y = v->y + WARP_SUBPIXEL_Y;
    w.z = v->z * (1.0f / 65535.0f);
    w.rhw = 1.0f;
    w.bgra = (uint32_t)v->b | ((uint32_t)v->g << 8) | ((uint32_t)v->r << 16) | 0xFF000000u;
    w.spec = 0xFF000000u;
    return w;
}

/* A triangle through the WARP. */
static void warp_tri(const vtx *t)
{
    mga_wvtx w[3];
    w[0] = wv(&t[0]); w[1] = wv(&t[1]); w[2] = wv(&t[2]);
    if (!aborted && warp_vertices(w, 3) != 0) {
        aborted = 1;
        hx_log("HX-STAT warptri abort: the FIFO stopped draining");
    }
}

static void cpu_tri(const vtx *t, int dx, uint32_t dwgctl, uint32_t flags)
{
    mga_svtx s[3];
    mga_tri_ctx ctx;
    int i;
    memset(s, 0, sizeof s);
    memset(&ctx, 0, sizeof ctx);
    for (i = 0; i < 3; i++) {
        s[i].X16 = (int32_t)((t[i].x + dx) * 16.0f + 0.5f);
        s[i].Y16 = (int32_t)(t[i].y * 16.0f + 0.5f);
        s[i].z = t[i].z;
        s[i].r = t[i].r; s[i].g = t[i].g; s[i].b = t[i].b; s[i].a = 255;
        s[i].fog = 255;
    }
    ctx.dwgctl = dwgctl;
    ctx.flags = flags;
    ctx.clip_y0 = 0;
    ctx.clip_y1 = SH;
    setup_triangle(&s[0], &s[1], &s[2], &ctx);
}

static void context(uint32_t dwgctl)
{
    fifo_reserve(8);
    MGA_WR32(MGAREG_DWGCTL, dwgctl);
    MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
    MGA_WR32(MGAREG_ALPHACTRL, ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) |
                                ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
    MGA_WR32(MGAREG_FOGCOL, 0);
    MGA_WR32(MGAREG_FCOL, 0);
    MGA_WR32(MGAREG_TDUALSTAGE0, 0);
    MGA_WR32(MGAREG_TDUALSTAGE1, 0);
    MGA_WR32(MGAREG_WFLAG, 0);
    setup_invalidate();                         /* (setup_triangle's shadows: the WARP wrote the registers) */
}

static uint16_t px(int x, int y) { return ((volatile uint16_t *)mga_fb)[y * PITCH + x]; }

/* Pixels of the box (x0, y0)-(x1, y1) against the same box dx to the right:
 * coverage differences and colour differences beyond tol (per channel, 8-bit). */
static void compare(int x0, int y0, int x1, int y1, int dx, int tol, int *cover, int *colour, int *drawn)
{
    int x, y;
    *cover = *colour = *drawn = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            uint16_t a = px(x, y), b = px(x + dx, y);
            if (a)
                (*drawn)++;
            if (!a != !b)
                (*cover)++;
            else if (a && b) {
                int dr = abs((a >> 11) - (b >> 11)) << 3, dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) << 2,
                    db = abs((a & 31) - (b & 31)) << 3;
                if (dr > tol || dg > tol || db > tol)
                    (*colour)++;
            }
        }
}

static void save(void)
{
    static uint8_t rgb[SW * SH * 3];
    int x, y;
    for (y = 0; y < SH; y++)
        for (x = 0; x < SW; x++) {
            uint16_t p = px(x, y);
            uint8_t *o = rgb + 3 * (y * SW + x);
            o[0] = (uint8_t)((p >> 11) << 3);
            o[1] = (uint8_t)(((p >> 5) & 63) << 2);
            o[2] = (uint8_t)((p & 31) << 3);
        }
    hx_save_ppm("W0", SW, SH, rgb);
}

/* Every line of the CPU's caches written back: 2 MB read (4 x a Pentium
 * II's L2), so a bus master that does not snoop sees what the CPU wrote. */
static void flush_caches(void)
{
    static volatile uint32_t *big;
    uint32_t i, sum = 0;
    if (!big)
        big = (volatile uint32_t *)malloc(2u << 20);
    if (!big)
        return;
    for (i = 0; i < (2u << 20) / 4u; i += 8)
        sum += big[i];
    (void)sum;
}

static int agp_hcap;                            /* agp=N: the bridge's AGP capability */
static int agp_forced;                          /* cached: AGP turned on by WARPTRI, not the HAL */
static uint32_t agp_hcmd0, agp_ccmd0;           /* (the commands as they were) */

/* With agp=N, after the first start: what the HAL turned on (logged at
 * each start; checked at the first). */
static void agp_log(void)
{
    static int checked;
    if (!agp_hcap)
        return;
    if (!checked++)
        hx_test("agp", warp_agp_xfer, "%s", warp_agp_xfer ? (agp_forced ? "fetches by AGP from cached rings (forced)"
                                                             : "fetches by AGP from write-combining rings")
                : "the fetches stayed on PCI (write-combining rings need GLOS; cached forces AGP)");
    hx_log("HX-STAT agp ring=%d xfer=%d host cmd=%08lx card cmd=%08lx agp_pll=%08lx", warp_ring_mode, warp_agp_xfer,
           (unsigned long)mga_pci_read32(0, 0, 0, agp_hcap + 8),
           (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF8),
           mga.family == MGA_FAMILY_G200 ? (unsigned long)MGA_RD32(MGAREG_AGP_PLL) : 0ul);
}

int main(int argc, char **argv)
{
    static const vtx g[3] = { { 40, 30, 0, 255, 0, 0 }, { 290, 60, 0, 0, 255, 0 }, { 120, 200, 0, 0, 0, 255 } };
    static const vtx z1[3] = { { 30, 260, 1000, 255, 255, 0 }, { 280, 270, 60000, 255, 255, 0 }, { 150, 460, 30000, 255, 255, 0 } };
    static const vtx z2[3] = { { 40, 450, 60000, 0, 255, 255 }, { 290, 300, 1000, 0, 255, 255 }, { 200, 470, 30000, 0, 255, 255 } };
    int pipe = 0, count = 4000, i, ok, pitch, cover, colour, drawn, starts = 0, reload = 0, keep = 0, dmabw = 0;
    int agpinfo = 0, agp_rate = 0, agp_draw = 0, agp_noflush = 0, agp_cached = 0, seqtest = 0, switches = 0, hazards = 0;
    uint32_t seq_fan = 0, seq_strip = 0;
    mga_vbe_mode mode;
    mga_target tg;
    uint64_t t0, t1;
    uint32_t dw_i = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_SHFTZERO | DWG_BOP_COPY;
    uint32_t dw_z = DWG_OPCOD_TRAP | DWG_ATYPE_ZI | DWG_ZMODE_ZLT | DWG_SHFTZERO | DWG_BOP_COPY;

    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "pipe=", 5)) pipe = atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "n=", 2)) count = atoi(argv[i] + 2);
        else if (!strcmp(argv[i], "pio")) warp_use_dma = 0;
        else if (!strcmp(argv[i], "pci")) warp_use_agp = 0;
        else if (!strncmp(argv[i], "pads=", 5)) warp_start_pads = atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "starts=", 7)) starts = atoi(argv[i] + 7);
        else if (!strcmp(argv[i], "reload")) reload = 1;
        else if (!strcmp(argv[i], "keep")) keep = 1;
        else if (!strcmp(argv[i], "noreset")) warp_reset_before_load = 0;
        else if (!strcmp(argv[i], "ucache")) warp_ucode_cache = 1;        /* the pipes from memory (microcode caching) */
        else if (!strcmp(argv[i], "dmabw")) dmabw = 1;
        else if (!strcmp(argv[i], "agpinfo")) agpinfo = 1;
        else if (!strncmp(argv[i], "agp=", 4)) agp_rate = atoi(argv[i] + 4);
        else if (!strcmp(argv[i], "agpdraw")) agp_draw = 1;     /* with agp=N: the drawing tests, not dmabw */
        else if (!strcmp(argv[i], "noflush")) agp_noflush = 1;  /* EXPERIMENT: does the bridge snoop AGP reads? */
        else if (!strcmp(argv[i], "cached")) agp_cached = 1;    /* with agp=N: AGP on with cached rings (the agp-cached hazard) */
        else if (!strcmp(argv[i], "seq")) seqtest = 1;          /* EXPERIMENT: strips and fans by the ACCEPT sequencer */
        else if (!strncmp(argv[i], "switch=", 7)) switches = atoi(argv[i] + 7);   /* the cost of a pipe switch */
        else if (!strncmp(argv[i], "seqf=", 5)) seq_fan = (uint32_t)strtoul(argv[i] + 5, NULL, 16);
        else if (!strncmp(argv[i], "seqs=", 5)) seq_strip = (uint32_t)strtoul(argv[i] + 5, NULL, 16);
        else if (!strcmp(argv[i], "hazards")) hazards = 1;      /* 86Box's silicon checks (local patch 0124) */
    }
    hx_init(1, argv, "warptri");
    ok = mga_find(&mga) == 0 && mga_map(&mga) == 0;
    if (!ok || !warp_available()) {
        hx_test("init", 0, "a G200, G400 or G450 is needed");
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
    opmode_saved = MGA_RD32(MGAREG_OPMODE);
    hx_log("HX-STAT warptri pipe=%d status=%08lx opmode=%08lx", pipe, (unsigned long)MGA_RD32(MGAREG_STATUS),
           (unsigned long)opmode_saved);

    if (agpinfo) {
        int r;
        for (r = 0; r < 0x100; r += 16)
            hx_log("HX-STAT agp host %02x: %08lx %08lx %08lx %08lx", r, (unsigned long)mga_pci_read32(0, 0, 0, r),
                   (unsigned long)mga_pci_read32(0, 0, 0, r + 4), (unsigned long)mga_pci_read32(0, 0, 0, r + 8),
                   (unsigned long)mga_pci_read32(0, 0, 0, r + 12));
        for (r = 0x30; r < 0x100; r += 16)
            hx_log("HX-STAT agp card %d:%d.%d %02x: %08lx %08lx %08lx %08lx", mga.bus, mga.dev, mga.fn, r,
                   (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, r),
                   (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, r + 4),
                   (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, r + 8),
                   (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, r + 12));
        hx_test("agpinfo", 1, "dumped");
        goto out;
    }
    if (agp_rate) {
        /* AGP at agp_rate x at most: the HAL turns it on at the first start
         * (warp.c's agp_enable: the bridge, then the card, then the G200's
         * AGP_PLL) and off at warp_stop; with cached, WARPTRI does. */
        int hcap = (int)(mga_pci_read32(0, 0, 0, 0x34) & 0xFC), k;
        uint32_t hsts, csts, rate = agp_rate == 2 ? 2u : 1u;
        for (k = 0; k < 8 && hcap && (mga_pci_read32(0, 0, 0, hcap) & 0xFF) != 2; k++)
            hcap = (int)((mga_pci_read32(0, 0, 0, hcap) >> 8) & 0xFC);
        if (!hcap || (mga_pci_read32(0, 0, 0, hcap) & 0xFF) != 2) {
            hx_test("agp", 0, "no AGP capability on the host bridge");
            goto out;
        }
        hsts = mga_pci_read32(0, 0, 0, hcap + 4);
        csts = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF4);
        hx_log("HX-STAT agp host cap=%02x sts=%08lx cmd=%08lx; card sts=%08lx cmd=%08lx", hcap, (unsigned long)hsts,
               (unsigned long)mga_pci_read32(0, 0, 0, hcap + 8), (unsigned long)csts,
               (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF8));
        if (!(hsts & csts & rate) || !(hsts & csts & (1u << 9))) {
            hx_test("agp", 0, "rate %lux or sideband not supported by both", (unsigned long)rate);
            goto out;
        }
        agp_hcap = hcap;
        warp_use_agp = 1;
        warp_agp_rate = (int)rate;
        warp_pre_kick = agp_noflush ? NULL : flush_caches;   /* (the CPU's writes out of its caches before each fetch) */
        dmabw = !agp_draw;
        if (agp_cached) {
            /* The bridge, then the card, at rate x with sideband addressing,
             * the card's queue the bridge's depth (as agp_enable); the HAL
             * keeps its cached rings, and the channels fetch them by AGP. */
            if (mga.family == MGA_FAMILY_G200) {
                hx_test("agp", 0, "cached: not on the G200 (its AGP_PLL is the HAL's)");
                goto out;
            }
            agp_hcmd0 = mga_pci_read32(0, 0, 0, hcap + 8);
            agp_ccmd0 = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF8);
            warp_use_agp = 0;
            mga_pci_write32(0, 0, 0, hcap + 8, rate | (1u << 8) | (1u << 9));
            mga_pci_write32(mga.bus, mga.dev, mga.fn, 0xF8, rate | (1u << 8) | (1u << 9) |
                            ((((hsts >> 24) & 0xFF) > 31 ? 31u : ((hsts >> 24) & 0xFF)) << 24));
            hx_log("HX-STAT agp on: host cmd=%08lx card cmd=%08lx", (unsigned long)mga_pci_read32(0, 0, 0, hcap + 8),
                   (unsigned long)mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF8));
            agp_forced = 1;
            warp_agp_xfer = 1;
        }
    }
    if (dmabw) {
        uint64_t ticks = 0, best = ~(uint64_t)0;
        uint32_t got = 0;
        int k;
        if (warp_select(pipe) != 0) {
            hx_test("dmabw", 0, "the WARP did not start");
            goto out;
        }
        agp_log();
        for (k = 0; k < 20; k++) {
            got = warp_dma_bandwidth(8192, rdtsc, &ticks, flush_caches);
            if (got && ticks < best)
                best = ticks;
        }
        hx_log("HX-STAT warptri dmabw bytes=%lu ticks=%lu mb_s=%lu", (unsigned long)got, (unsigned long)best,
               got && best ? (unsigned long)((double)got * 398.0 / (double)best) : 0ul);
        {   /* WARP triangles by the batch: small ones (fill negligible) */
            static const vtx tv[3] = { { 100, 100, 0, 255, 0, 0 }, { 108, 101, 0, 0, 255, 0 }, { 102, 107, 0, 0, 0, 255 } };
            mga_wvtx w[3];
            uint64_t fe = 0, dn = 0, bf = ~(uint64_t)0, bd = ~(uint64_t)0;
            int n = 0;
            w[0] = wv(&tv[0]); w[1] = wv(&tv[1]); w[2] = wv(&tv[2]);
            context(dw_i);
            for (k = 0; k < 10; k++) {
                n = warp_dma_vertex_bandwidth(w, 1000, rdtsc, &fe, &dn, flush_caches);
                if (n > 0 && fe < bf) bf = fe;
                if (n > 0 && dn < bd) bd = dn;
            }
            hx_log("HX-STAT warptri warpbw tris=%d fetch_ticks=%lu done_ticks=%lu ktris_s_fetch=%lu ktris_s_done=%lu", n,
                   (unsigned long)bf, (unsigned long)bd, n > 0 ? (unsigned long)((double)n * 398000.0 / (double)bf) : 0ul,
                   n > 0 ? (unsigned long)((double)n * 398000.0 / (double)bd) : 0ul);
        }
        hx_test("dmabw", got != 0, "%lu bytes", (unsigned long)got);
        goto out;
    }
    if ((switches > 0 || seqtest) && mga.family == MGA_FAMILY_G200) {
        hx_log("HX-STAT warptri skipped: the G200 has %s", seqtest ? "no ACCEPT sequencer" : "no two-texture pipes");
        goto out;
    }
    if (switches > 0) {
        /* The cost of a pipe switch as a game with two textures makes them
         * (GLQuake -mtex: the world by T2, models by T0): N triangles on one
         * pipe, then N alternating T0 and T2 pipes, timed. */
        static const vtx s1[3] = { { 20, 20, 0, 255, 255, 255 }, { 60, 22, 0, 255, 255, 255 }, { 30, 50, 0, 255, 255, 255 } };
        uint64_t a, b, c;
        uint32_t r0 = engine_resets;
        int k, fails = 0;
        context(dw_i);
        if (warp_select(pipe) != 0)
            fails++;
        a = rdtsc();
        for (k = 0; k < switches && !fails; k++)
            warp_tri(s1);
        engine_sync(2000000);
        b = rdtsc();
        for (k = 0; k < switches && !fails; k++) {
            if (warp_select((k & 1) ? (pipe | WARP_T2) : (pipe & ~WARP_T2)) != 0) {
                fails++;
                break;
            }
            context(dw_i);
            warp_tri(s1);
        }
        engine_sync(2000000);
        c = rdtsc();
        hx_log("HX-STAT warptri switch n=%d same=%lu switching=%lu cycles_per_switch=%lu resets=%lu", switches,
               (unsigned long)((b - a) / (uint64_t)switches), (unsigned long)((c - b) / (uint64_t)switches),
               (unsigned long)(((c - b) - (b - a)) / (uint64_t)switches), (unsigned long)(engine_resets - r0));
        hx_test("switch", fails == 0 && !aborted, "%d switches, %d failed", switches, fails);
        goto out;
    }
    if (hazards) {
        /* Each hazard 86Box's silicon checks report (local patch 0124,
         * MGA-HAZARD lines in Loop A's result.json), beside its safe form,
         * with the vertices by PIO so that every write reaches the engine in
         * program order. Loop A should report warp-inflight twice (steps 1
         * and 2), warp-opmode, rfw and tlut once each, and nothing for the
         * rest. On a real G400 steps 1 and 3 may cut or spoil a triangle. */
        static const vtx s1[3] = { { 20, 20, 0, 255, 255, 255 }, { 60, 22, 0, 255, 255, 255 }, { 30, 50, 0, 255, 255, 255 } };
        warp_use_dma = 0;
        if (warp_select(pipe) != 0) {
            hx_test("hazards", 0, "the WARP did not start");
            goto out;
        }
        context(dw_i);
        /* 1: a new clip straight after WARP triangles (a hazard); after the fence, back (none) */
        warp_tri(s1);
        fifo_reserve(1);
        MGA_WR32(MGAREG_CXBNDRY, (uint32_t)(SW - 2) << 16);
        engine_sync(200000);
        fifo_reserve(1);
        MGA_WR32(MGAREG_CXBNDRY, (uint32_t)(SW - 1) << 16);
        /* 2: state for the next triangles between WARP runs (none, as the
         * DRM writes it); a draw started with the triangles' DWGCTL (none:
         * the DRM's clip barrier); then, after more, one with another (a
         * hazard) */
        warp_tri(s1);
        fifo_reserve(4);
        MGA_WR32(MGAREG_DWGCTL, dw_z);
        MGA_WR32(MGAREG_DWGCTL, dw_i);
        MGA_WR32(MGAREG_DWGCTL, dw_i);
        MGA_WR32(MGAREG_LEN + MGAREG_EXEC, 0x80000000u);
        warp_tri(s1);
        fifo_reserve(2);
        MGA_WR32(MGAREG_DWGCTL, dw_z);
        MGA_WR32(MGAREG_LEN + MGAREG_EXEC, 0x80000000u);
        engine_sync(200000);
        context(dw_i);
        /* 3: OPMODE into blit mode while the WARPs work (a hazard), and back */
        warp_tri(s1);
        MGA_WR32(MGAREG_OPMODE, (opmode_saved & ~0xCu) | 0x4u);
        MGA_WR32(MGAREG_OPMODE, opmode_saved);
        engine_sync(200000);
        /* 4: a clip through the HAL after triangles whose DWGCTL it was given
         * (setup_dwgctl): the DRM's barrier first, with that DWGCTL (none);
         * then a fill through the HAL, after two DWGSYNC writes (none) */
        context(dw_z);
        setup_dwgctl(dw_z);
        warp_tri(s1);
        engine_set_clip(0, 0, SW - 2, SH);
        engine_set_clip(0, 0, SW, SH);
        warp_tri(s1);
        engine_fill(0, 0, 4, 4, 0);
        engine_sync(200000);
        context(dw_i);
        /* 5: TEXWIDTH for a 64-texel map with the specification's rfw for a
         * prescale of 3 (a hazard), then the CPU setup's and Mesa's (none) */
        fifo_reserve(3);
        MGA_WR32(MGAREG_TEXWIDTH, TEXWH(6, 8 - 6 - 3, 63));
        MGA_WR32(MGAREG_TEXWIDTH, TEXWH(6, 8 - 6, 63));
        MGA_WR32(MGAREG_TEXWIDTH, TEXWH(6 + 11, 2 - 6, 63));
        /* 6: the G200's TLUT load (a hazard on the G400 family) */
        engine_tlut_load(0, 0, 16);
        warp_stop();
        engine_sync(200000);
        hx_test("hazards", !aborted, "six steps done%s", aborted ? ", the WARP stopped taking vertices" : "");
        goto out;
    }
    if (starts > 0) {
        static const vtx s1[3] = { { 20, 20, 0, 255, 255, 255 }, { 60, 22, 0, 255, 255, 255 }, { 30, 50, 0, 255, 255, 255 } };
        int k, x, y, fails = 0, vfails = 0, blank = 0;
        for (k = 0; k < starts; k++) {
            int p = reload ? ((k & 1) ? 7 : 0) : pipe, r, n = 0;
            engine_fill(0, 0, 80, 60, 0);
            engine_sync(200000);
            r = warp_select(p);
            if (!r) {
                aborted = 0;
                context(dw_i);
                warp_tri(s1);
                vfails += aborted;
                warp_stop();
                engine_sync(200000);
                for (y = 0; y < 60; y++)
                    for (x = 0; x < 80; x++)
                        n += px(x, y) != 0;
                blank += n < 100;
            } else
                fails++;
            hx_log("HX-STAT warptri start %d pipe=%d %s px=%d entry=%08lx before=%08lx after=%08lx ia0=%08lx ia1=%08lx",
                   k, p, r ? "FAILED" : "ok", n, (unsigned long)warp_diag[0], (unsigned long)warp_diag[1],
                   (unsigned long)warp_diag[2], (unsigned long)warp_diag[3], (unsigned long)warp_diag[4]);
        }
        hx_test("starts", fails == 0 && vfails == 0 && blank == 0,
                "%d of %d starts failed, %d vertex sends, %d drew nothing (pads %d, %s, %s, %s)", fails, starts, vfails,
                blank, warp_start_pads, reload ? "reloading" : "one pipe", warp_use_dma ? "dma" : "pio",
                warp_reset_before_load ? "reset before loads" : "no reset");
        aborted = 1;
        goto out;
    }

    if (seqtest) {
        /* EXPERIMENT: a fan and a strip by the WARPs' ACCEPT sequencer
         * (WACCEPTSEQ, G400 specification 3-260: after the first triangle's
         * three vertices, each vertex goes to the next of the seqdst slots),
         * with Mesa's values for pipes without a second map
         * (MGA_WA_TRIFAN_T0, MGA_WA_TRISTRIP_T0), on the left; the same
         * triangles as a list 320 pixels to the right; compared. Then a
         * list triangle, to see the WARP still takes lists. */
        static const vtx fan[6] = { { 160, 120, 0, 255, 255, 255 }, { 260, 120, 0, 255, 0, 0 }, { 230, 200, 0, 0, 255, 0 },
                                    { 160, 220, 0, 0, 0, 255 }, { 90, 200, 0, 255, 255, 0 }, { 60, 120, 0, 0, 255, 255 } };
        static const vtx strip[6] = { { 20, 300, 0, 255, 0, 0 }, { 20, 400, 0, 0, 255, 0 }, { 120, 300, 0, 0, 0, 255 },
                                      { 120, 400, 0, 255, 255, 0 }, { 220, 300, 0, 0, 255, 255 }, { 220, 400, 0, 255, 0, 255 } };
        static const vtx tail[3] = { { 240, 420, 0, 255, 255, 255 }, { 300, 420, 0, 255, 255, 255 }, { 270, 470, 0, 255, 255, 255 } };
        mga_wvtx w[6];
        vtx t[3];
        int k, cf = 0, kf = 0, df = 0, cs = 0, ks = 0, ds = 0, tl = 0, x, y;
        /* T2 pipes: 10-dword vertices, banks 16 apart (seqf=, seqs= override) */
        uint32_t fanseq = (pipe & WARP_T2) ? 0x01000810u : 0x01000408u,
                 stripseq = (pipe & WARP_T2) ? 0x02020400u : 0x02010200u,
                 listseq = (pipe & WARP_T2) ? 0x1E000000u : 0x18000000u;
        if (seq_fan) fanseq = seq_fan;
        if (seq_strip) stripseq = seq_strip;
        if (warp_select(pipe) != 0) {
            hx_test("seq", 0, "the WARP did not start");
            goto out;
        }
        engine_fill(0, 0, SW, SH, 0);
        engine_sync(200000);
        context(dw_i);
        for (k = 0; k < 6; k++)
            w[k] = wv(&fan[k]);
        MGA_WR32(MGAREG_WACCEPTSEQ, fanseq);
        if (!aborted && warp_vertices(w, 6) != 0)
            aborted = 1;
        for (k = 0; k < 6; k++)
            w[k] = wv(&strip[k]);
        MGA_WR32(MGAREG_WACCEPTSEQ, stripseq);
        if (!aborted && warp_vertices(w, 6) != 0)
            aborted = 1;
        MGA_WR32(MGAREG_WACCEPTSEQ, listseq);         /* lists again */
        warp_tri(tail);
        warp_stop();
        engine_sync(200000);
        hx_log("HX-STAT warptri seq after: status=%08lx aborted=%d failures=%lu", (unsigned long)MGA_RD32(MGAREG_STATUS),
               aborted, (unsigned long)warp_failures);
        if (aborted || warp_select(pipe) != 0)
            goto seqout;
        context(dw_i);
        for (k = 1; k + 1 < 6; k++) {   /* the fan as a list, 320 to the right */
            t[0] = fan[0]; t[1] = fan[k]; t[2] = fan[k + 1];
            t[0].x += 320; t[1].x += 320; t[2].x += 320;
            warp_tri(t);
        }
        for (k = 0; k + 2 < 6; k++) {   /* the strip as a list */
            t[0] = strip[k]; t[1] = strip[k + 1]; t[2] = strip[k + 2];
            t[0].x += 320; t[1].x += 320; t[2].x += 320;
            warp_tri(t);
        }
        warp_stop();
        engine_sync(200000);
        compare(0, 0, 320, 240, 320, 24, &cf, &kf, &df);
        compare(0, 290, 320, 410, 320, 24, &cs, &ks, &ds);
        for (y = 420; y < 470; y++)
            for (x = 240; x < 300; x++)
                tl += px(x, y) != 0;
    seqout:
        save();
        hx_test("seq-fan", !aborted && df > 1000 && cf <= df / 100 && kf <= df / 100,
                "WACCEPTSEQ %08lx: %d of %d pixels differ in coverage, %d in colour, from the list", (unsigned long)fanseq,
                cf, df, kf);
        hx_test("seq-strip", !aborted && ds > 1000 && cs <= ds / 100 && ks <= ds / 100,
                "WACCEPTSEQ %08lx: %d of %d pixels differ in coverage, %d in colour, from the list", (unsigned long)stripseq,
                cs, ds, ks);
        hx_test("seq-lists-after", !aborted && tl > 500, "a list triangle after them: %d pixels", tl);
        goto out;
    }

    /* a) the pipe */
    ok = warp_select(pipe) == 0;
    agp_log();
    {
        uint32_t lo, hi;
        MGA_WR32(MGAREG_WIMEMADDR, 0);
        lo = MGA_RD32(MGAREG_WIMEMDATA);
        hi = MGA_RD32(MGAREG_WIMEMDATA + 4);
        hx_test("select", ok, "pipe %d started: status %08lx; WARP0 word 0 %08lx:%08lx", pipe,
                (unsigned long)MGA_RD32(MGAREG_STATUS), (unsigned long)hi, (unsigned long)lo);
    }
    if (!ok) {
        aborted = 1;
        goto out;
    }

    /* b) Gouraud */
    context(dw_i);
    warp_tri(g);
    warp_stop();
    context(dw_i);
    cpu_tri(g, 320, dw_i, MGA_S_COLOR);
    engine_sync(200000);
    compare(0, 0, 320, 240, 320, 24, &cover, &colour, &drawn);
    hx_test("warp-draws", drawn > 1000, "%d pixels drawn by the WARP in the top-left quarter", drawn);
    hx_test("gouraud", drawn > 1000 && cover <= drawn / 100 && colour <= drawn / 100,
            "against setup_triangle: %d coverage, %d colour differences of %d pixels", cover, colour, drawn);
    if (aborted || warp_select(pipe) != 0)
        goto out;

    /* b2) two triangles in one batch: WARP0 and WARP1 */
    {
        static const vtx p1[3] = { { 340, 250, 0, 255, 0, 255 }, { 460, 250, 0, 255, 0, 255 }, { 400, 330, 0, 255, 0, 255 } };
        static const vtx p2[3] = { { 480, 250, 0, 0, 255, 0 }, { 600, 250, 0, 0, 255, 0 }, { 540, 330, 0, 0, 255, 0 } };
        int d1 = 0, d2 = 0, x, y;
        context(dw_i);
        warp_tri(p1);
        warp_tri(p2);
        warp_stop();
        engine_sync(200000);
        for (y = 250; y < 330; y++)
            for (x = 340; x < 600; x++)
                if (px(x, y))
                    *(x < 470 ? &d1 : &d2) += 1;
        hx_test("two-warps", d1 > 1000 && d2 > 1000, "first triangle %d pixels, second %d", d1, d2);
        engine_fill(330, 240, 300, 100, 0);
        engine_sync(200000);
    }
    if (aborted || warp_select(pipe) != 0)
        goto out;

    /* c) depth */
    context(dw_z);
    warp_tri(z1);
    warp_tri(z2);
    warp_stop();
    context(dw_z);
    cpu_tri(z1, 320, dw_z, MGA_S_COLOR | MGA_S_Z);
    cpu_tri(z2, 320, dw_z, MGA_S_COLOR | MGA_S_Z);
    engine_sync(200000);
    compare(0, 240, 320, 480, 320, 24, &cover, &colour, &drawn);
    hx_test("depth", drawn > 1000 && cover <= drawn / 100 && colour <= drawn / 100,
            "against setup_triangle: %d coverage, %d colour differences of %d pixels", cover, colour, drawn);
    save();
    if (aborted || warp_select(pipe) != 0)
        goto out;

    /* d) speed: small triangles in a grid, both ways */
    {
        vtx t[3];
        int k;
        context(dw_i);
        engine_fill(0, 0, SW, SH, 0);
        engine_sync(200000);
        t0 = rdtsc();
        for (k = 0; k < count && !aborted; k++) {
            float x = (float)((k * 13) % 600), y = (float)((k * 7) % 440);
            t[0].x = x; t[0].y = y; t[1].x = x + 12; t[1].y = y + 2; t[2].x = x + 3; t[2].y = y + 11;
            for (i = 0; i < 3; i++) { t[i].z = 0; t[i].r = (uint8_t)(k * 3); t[i].g = (uint8_t)(i * 80); t[i].b = 200; }
            warp_tri(t);
        }
        warp_stop();
        engine_sync(2000000);
        t1 = rdtsc();
        hx_log("HX-STAT warptri speed path=warp tris=%d cycles=%lu per_tri=%lu", count, (unsigned long)(t1 - t0),
               (unsigned long)((t1 - t0) / (uint64_t)count));
        context(dw_i);
        t0 = rdtsc();
        for (k = 0; k < count; k++) {
            float x = (float)((k * 13) % 600), y = (float)((k * 7) % 440);
            t[0].x = x; t[0].y = y; t[1].x = x + 12; t[1].y = y + 2; t[2].x = x + 3; t[2].y = y + 11;
            for (i = 0; i < 3; i++) { t[i].z = 0; t[i].r = (uint8_t)(k * 3); t[i].g = (uint8_t)(i * 80); t[i].b = 200; }
            cpu_tri(t, 0, dw_i, MGA_S_COLOR);
        }
        engine_sync(2000000);
        t1 = rdtsc();
        hx_log("HX-STAT warptri speed path=cpu tris=%d cycles=%lu per_tri=%lu", count, (unsigned long)(t1 - t0),
               (unsigned long)((t1 - t0) / (uint64_t)count));
    }
out:
    warp_stop();                                /* (AGP off again, as it was) */
    warp_pre_kick = NULL;
    if (agp_forced) {                           /* (cached: the card first, then the bridge, as they were) */
        warp_agp_xfer = 0;
        mga_pci_write32(mga.bus, mga.dev, mga.fn, 0xF8, agp_ccmd0);
        mga_pci_write32(0, 0, 0, agp_hcap + 8, agp_hcmd0);
    }
    MGA_WR32(MGAREG_OPMODE, opmode_saved);
    if (!keep)
        engine_reset();                         /* the next program finds the engine and the WARPs reset */
    hx_log("HX-STAT warptri warp tris=%lu failures=%lu dma_kicks=%lu engine resets=%lu timeouts=%lu clock_backs=%lu",
           (unsigned long)warp_tris, (unsigned long)warp_failures, (unsigned long)warp_dma_kicks,
           (unsigned long)engine_resets, (unsigned long)engine_timeouts, (unsigned long)sys_time_backs);
    vbe_set_text_mode();
    hx_done(hx_failures() ? HX_FAIL : HX_PASS);
    return 0;
}
