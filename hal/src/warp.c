/* warp.c - the WARP setup engine of the G400 (docs/g400-warp.md) and the
 * G200 (docs/g200-warp.md).
 *
 * The microcode is linux-firmware's matrox/g400_warp.fw and g200_warp.fw
 * (warp_g400.h, warp_g200.h; MIT, Copyright 1999 Matrox Graphics Inc.).
 * With microcode caching off the WARPs run from their own 2 KB instruction
 * memories, written through WIMEMADDR and WIMEMDATA (and the G400's second
 * WARP's WIMEMDATA1); the values written around a pipe are those of Linux's
 * mga DRM driver (mga_warp_init, mga_g400_emit_pipe, mga_g200_emit_pipe).
 * Vertices and register writes go by bus-master DMA (below), or the
 * vertices through the DMA window with OPMODE in vertex mode.
 *
 * The G200 differs (docs/g200-warp.md): one WARP, started through WIADDR;
 * eight pipes, none with two textures; its own WR constants; texture sizes
 * in WR24/WR34 with tw = log2 + 28; no ACCEPT sequencer; AGP only with
 * sideband addressing, and 2x only with AGP_PLL. It is driven only when
 * warp_g200 is set, until the real card has passed (docs/g200-warp.md). */
#include "mga/warp.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include "mga/prof.h"
#include "warp_g400.h"
#include "warp_g200.h"
#include <string.h>

uint32_t warp_tris, warp_failures, warp_dma_kicks, warp_seqs, warp_seqs_t2;
int warp_use_dma = 1;
uint32_t warp_ring_bytes = 262144;     /* the rings' size at most: halved until DOS has it */
uint32_t warp_kick_bytes = 6144;       /* vertex bytes between kicks */
int warp_list_div = 8;                 /* the list's share of the block: 1/warp_list_div */
int warp_use_agp = 1;
int warp_agp_rate;                  /* 0: the fastest the card and the bridge both have; 1 or 2: at most 1x or 2x */
int warp_ring_mode;                 /* 0 none, 1 cached rings (PCI), 2 write-combining (PCI), 3 write-combining by AGP */
int warp_wc_status;                 /* write-combining rings: 0 not asked, 1 given, -1 no DOS memory for them, -2 refused */
uint32_t warp_ring_kb;
int warp_start_pads = 3;
int warp_reset_before_load = 1;
int warp_agp_xfer;                  /* the channels fetch by AGP cycles (AGP on, write-combining rings) */
void (*warp_pre_kick)(void);        /* EXPERIMENT: called before each kick (a cache flush for AGP cycles) */
uint32_t warp_diag[5];
int warp_g200 = 1;                  /* the G200's WARP driven (on since the real card passed: docs/g200-warp.md, G6) */

static int g200(void)
{
    return mga.family == MGA_FAMILY_G200;
}

/* Bus-master DMA, as Linux's mga DRM does it (G400 specification 4.1.9): a
 * primary display list in general-purpose mode, each packet a header of
 * four register indexes and their four values, and runs of vertices beside
 * it, which the list hands to the secondary channel in vertex mode (DMAPAD,
 * DMAPAD, SECADDRESS, SECEND). While the stream is open (mga_cmd_open)
 * MGA_WR32 queues writes to the registers a list can reach (mmio.h); any
 * other write first sends the list and waits until it is fetched.
 *
 * The list and the vertices are two rings in conventional memory, whose
 * physical address is the linear one (under DOS and GLOS alike). A kick
 * hands the chip what is new by moving PRIMEND on, which the specification
 * allows while the chip is still fetching (4-16: the last packet before an
 * end is DMAPADs). Once PRIMADDRESS is past a kick's end the chip has
 * fetched its vertices, and their room is free (reap). The CPU waits only
 * when a ring is full. PRIMADDRESS is written only with the chip idle, so the list
 * starts again from its beginning only when all of it has been fetched. */
#define RING_LEAST 32768u
#define KICKS 64                    /* kicks not yet known fetched, at most (a power of two) */
#define LIST_SLACK 32u              /* list dwords kept for a run's packet and a kick's */
#define NO_RUN 0xFFFFFFFFu
#define DMAREG(off) ((off) < 0x2000u ? ((off) - 0x1C00u) >> 2 : (((off) - 0x2C00u) >> 2) | 0x80u)
static uint32_t *lring, *vring;     /* the list's ring, the vertices' */
static uint32_t lphys, vphys;
static uint32_t lcap, vcap;         /* their sizes: dwords, bytes */
static int dma_state;               /* 0 not tried, 1 in use, -1 not available */
static uint32_t lused;              /* the list's dwords written */
static uint32_t lkick;              /* ... and handed to the chip */
static int rewound = 1;             /* the next kick writes PRIMADDRESS: the list from its start */
static uint32_t vused;              /* the vertex ring's next byte */
static uint32_t vhead;              /* its oldest byte the chip may yet read (or the CPU has yet to hand it) */
static uint32_t vkick;              /* vused at the last kick */
static uint32_t vsince;             /* vertex bytes since the last kick */
static uint32_t vrun = NO_RUN;      /* the open vertex run's first byte */
static uint32_t lhdr, lhval;        /* the open packet's header dword, and its value so far */
static int lslots;                  /* registers in the open packet (0: none open) */
static struct { uint32_t lend, vend; } kq[KICKS];   /* a kick's ends: list dwords, vertex bytes */
static unsigned kq_head, kq_tail;   /* kicks not yet known fetched */
static void ring_idle(void);
static int ring_wc;                     /* the rings are write-combining (GLOS) */
static int agp_hcap;                    /* the bridge's AGP capability (0: no AGP) */
static int agp_on;
static int agp_enable(void);
static void agp_disable(void);

static void dma_flush(void);
static void stream_close(void);

static int cur_pipe = -1;           /* in the instruction memories */
static int sel_pipe = -1;           /* the one warp_select asked for */
static int running;                 /* started, not suspended */

/* Both rings empty, from their starts (the chip idle, or reset). */
static void ring_reset(void)
{
    lused = lkick = 0;
    lslots = 0;
    rewound = 1;
    vused = vhead = vkick = vsince = 0;
    vrun = NO_RUN;
    kq_head = kq_tail = 0;
}

/* engine_init and engine_resync (a reset, or GLOS's GPU lock changing
 * hands) leave OPMODE unknown: then nothing is known of the WARPs either. */

static void check_lost(void)
{
    if (mga_opmode == MGA_OPMODE_UNKNOWN) {
        if (running)
            ring_idle();                /* (a reset, or another program's turn: the chip idle) */
        running = 0;
        cur_pipe = -1;
    }
}
static uint32_t dmawin_pos;

int warp_available(void)
{
    return mga.family == MGA_FAMILY_G400 || (g200() && warp_g200);
}

/* The pipe's record in the chip's microcode: 0, or -1 when there is none
 * (the G200 has pipes 0-7 only: no WARP_T2). */
static int pipe_code(int pipe, const unsigned char **code, int *len)
{
    const unsigned char *fw = g200() ? warp_g200_fw : warp_g400_fw;
    const unsigned long size = g200() ? sizeof warp_g200_fw : sizeof warp_g400_fw;
    unsigned long off = 0;
    int i = 0;
    while (off + 6 <= size) {
        int n = (fw[off + 4] << 8) | fw[off + 5];
        if (!n)
            break;
        if (i++ == pipe) {
            *code = fw + off + 6;
            *len = n;
            return 0;
        }
        off = (off + 6 + (unsigned long)n + 3) & ~3ul;
    }
    return -1;
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Room for n writes in the bus FIFO, waiting a limited time: unlike
 * fifo_reserve this never writes into a full FIFO. */
static int fifo_room(int n)
{
    uint32_t polls = 0, start = sys_time_us();
    for (;;) {
        if ((int)(MGA_RD8(MGAREG_FIFOSTATUS) & mga.fifo_mask) >= n) {
            mga_fifo_free = 0;          /* (fifo_reserve reads it again) */
            return 0;
        }
        if ((++polls & 1023) == 0 && (uint32_t)(sys_time_us() - start) > 500000u)
            return -1;                  /* (a time limit: see dma_wait) */
    }
}

static void dma_mode(int vertex)
{
    const uint32_t want = vertex ? OPMODE_DMAMOD_VERTEX : 0;
    if (mga_opmode == want)
        return;
    engine_sync(200000);                /* OPMODE is not queued behind drawing */
    MGA_WR32(MGAREG_OPMODE, want);
    mga_opmode = want;
    dmawin_pos = 0;
}

static void suspend(void)
{
    if (running) {
        fifo_reserve(1);
        MGA_WR32(g200() ? MGAREG_WIADDR : MGAREG_WIADDR2, WMODE_SUSPEND);
        running = 0;
    }
}

void warp_stop(void)
{
    stream_close();
    dma_mode(0);
    suspend();
    if (agp_on) {                       /* (the next start turns it on again) */
        agp_disable();
        warp_agp_xfer = 0;
    }
}

static void fail(void);

/* The CPU's write-combining buffers out to memory: a locked instruction
 * (the DRM's mga_flush_write_combine), before the chip is told to fetch. */
#if defined(__GNUC__) && defined(__i386__)
static void wc_drain(void) { __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory"); }
#elif defined(__WATCOMC__) && defined(__386__)
void wc_drain(void);
#pragma aux wc_drain = "lock or dword ptr [esp], 0" modify exact [];
#else
static void wc_drain(void) { }
#endif

/* The chip idle (a reset, a wait for the end, another program's turn):
 * every kick fetched, and the next one tells it where the list goes on. */
static void ring_idle(void)
{
    kq_head = kq_tail;
    vhead = vkick;                      /* (what is not yet kicked starts there) */
    rewound = 1;
}

/* Kicks the chip has fetched: their room free. As Linux's DRM ages its
 * buffers, a kick is fetched once PRIMADDRESS is past its end (the primary
 * channel goes on only when a run's secondary transfer is done), and all of
 * them once the channels have ended there. Not DWGSYNC tags: on bench-g400 a
 * DWGSYNC write among the vertex runs was now and then never taken (it read
 * the tag before forever, with everything fetched and the engine idle), and
 * engine_sync timed out. */
static void reap(void)
{
    uint32_t pa;
    if (kq_head == kq_tail)
        return;
    pa = (MGA_RD32(MGAREG_PRIMADDRESS) & ~3u) - lphys;
    if (pa & 3u || pa / 4u > lcap)
        return;                         /* (not within the list: nothing known) */
    pa /= 4u;
    if (pa == lkick && (MGA_RD32(MGAREG_STATUS) & (1u << 17)))
        pa = lkick + 1u;                /* (ended: everything kicked fetched) */
    while (kq_head != kq_tail && pa > kq[kq_head & (KICKS - 1)].lend) {
        vhead = kq[kq_head & (KICKS - 1)].vend;
        kq_head++;
    }
}

/* Wait until the chip has fetched everything kicked: STATUS.endprdmasts
 * (the primary and secondary channels both done) with PRIMADDRESS at the
 * end last handed over. endprdmasts alone can still read set just after a
 * kick that moved PRIMEND on from where the channel had stopped; trusting it
 * started the rings again under a fetch in progress, and on bench-g400
 * Glide's engine then hung (12 engine_sync timeouts in 617 frames). */
static int dma_wait(void)
{
    uint32_t polls = 0, start;
    int o, done = 0;
    if (kq_head == kq_tail)
        return 0;
    o = PROF_SWITCH(PROF_FIFO);         /* (a wait for the chip, as fifo_reserve's) */
    start = sys_time_us();
    for (;;) {
        if ((MGA_RD32(MGAREG_STATUS) & (1u << 17)) &&
            (MGA_RD32(MGAREG_PRIMADDRESS) & ~3u) == lphys + lkick * 4u) {
            done = 1;
            break;
        }
        /* A time limit, not a count of reads: in 86Box the CPU can read
         * far faster than the engine works through a heavy batch. */
        if ((++polls & 1023) == 0 && (uint32_t)(sys_time_us() - start) > 1000000u)
            break;
    }
    PROF_BACK(o);
    if (!done)
        return -1;
    kq_head = kq_tail;
    vhead = vkick;
    return 0;
}

/* A register write into the list. The packet's header is written once,
 * whole, when its fourth register is in: the rings may be write-combining,
 * where a read is uncached (and slow). */
static void list_put(uint32_t idx, uint32_t v)
{
    if (!lslots) {
        lhdr = lused++;
        lhval = 0;
    }
    lhval |= idx << (8 * lslots);
    lring[lused++] = v;
    lslots = (lslots + 1) & 3;
    if (!lslots)
        lring[lhdr] = lhval;
}

static void list_pad(void)
{
    while (lslots)
        list_put(DMAREG(MGAREG_DMAPAD), 0);
}

/* The open vertex run handed to the secondary channel, in its own packet. */
static void close_run(void)
{
    if (vrun == NO_RUN)
        return;
    if (vused != vrun) {
        list_pad();
        list_put(DMAREG(MGAREG_DMAPAD), 0);
        list_put(DMAREG(MGAREG_DMAPAD), 0);
        list_put(DMAREG(MGAREG_SECADDRESS), (vphys + vrun) | 3u);   /* vertex write */
        list_put(DMAREG(MGAREG_SECEND), (vphys + vused) | (warp_agp_xfer ? 2u : 0u));
    }
    vrun = NO_RUN;
}

/* What is new in the list handed to the chip: PRIMEND moved on. */
static void dma_kick(void)
{
    int i;
    close_run();
    list_pad();
    if (lused == lkick)
        return;
    for (i = 0; i < 4; i++)
        list_put(DMAREG(MGAREG_DMAPAD), 0);    /* (the last packet before an end: 4-16) */
    while (kq_tail - kq_head >= KICKS) {
        reap();
        if (kq_tail - kq_head >= KICKS && dma_wait() != 0) {
            fail();
            return;
        }
    }
    kq[kq_tail & (KICKS - 1)].lend = lused;
    kq[kq_tail & (KICKS - 1)].vend = vused;
    kq_tail++;
    if (warp_pre_kick)
        warp_pre_kick();
    if (ring_wc)
        wc_drain();
    if (rewound) {
        MGA_WR32_DIRECT(MGAREG_PRIMADDRESS, lphys + lkick * 4u);        /* general purpose */
        rewound = 0;
    }
    MGA_WR32_DIRECT(MGAREG_PRIMEND, (lphys + lused * 4u) | (warp_agp_xfer ? 2u : 0u));  /* starts it, or goes on */
    lkick = lused;
    vkick = vused;
    vsince = 0;
    warp_dma_kicks++;
}

/* mga_dma_flush: everything queued sent, and fetched; both rings from their
 * starts. */
static void dma_flush(void)
{
    mga_dma_pending = 0;
    dma_kick();
    if (!mga_cmd_open)
        return;                         /* (a failed kick) */
    if (dma_wait() != 0) {
        fail();
        return;
    }
    lused = lkick = 0;
    lslots = 0;
    rewound = 1;
    vused = vhead = vkick = vsince = 0;
    mga_fifo_free = 0;
}

/* Room for n list dwords (and LIST_SLACK more): at the list's end,
 * everything sent and fetched first, and the list from its start. */
static int list_room(uint32_t n)
{
    if (lused + n + LIST_SLACK <= lcap)
        return 0;
    dma_flush();
    return mga_cmd_open ? 0 : -1;
}

/* Room for n bytes of vertices at vused, in one piece: the run goes on
 * from the ring's start when its end is too short, and the CPU waits for
 * the chip when the ring is full. */
static int vert_room(uint32_t n)
{
    uint32_t polls = 0, start = 0;
    int o = -1;
    for (;;) {
        if (vused >= vhead) {           /* (from vhead to vused in use) */
            if (vused + n <= vcap)
                break;
            if (n < vhead) {
                close_run();
                vused = 0;
                break;
            }
        } else if (vused + n < vhead)   /* (from vhead to the end, and from the start to vused) */
            break;
        if (vsince)
            dma_kick();                 /* (the chip has what there is) */
        if (!mga_cmd_open)
            return -1;
        if (kq_head == kq_tail) {       /* (nothing out: all free but what is not yet kicked) */
            if (vhead == vkick)
                return -1;              /* (more than the ring) */
            vhead = vkick;
            continue;
        }
        reap();
        if (o < 0) {
            o = PROF_SWITCH(PROF_FIFO);
            start = sys_time_us();
        }
        if ((++polls & 63) == 0 && (uint32_t)(sys_time_us() - start) > 1000000u) {
            PROF_BACK(o);
            fail();
            return -1;
        }
    }
    if (o >= 0)
        PROF_BACK(o);
    return 0;
}

/* mga_cmd_write: a register write into the list. */
static void cmd_write(uint32_t off, uint32_t v)
{
    if (list_room(2) != 0 || !mga_cmd_open) {   /* (a failed flush: the stream is closed) */
        MGA_WR32_DIRECT(off, v);
        return;
    }
    close_run();
    list_put(DMAREG(off), v);
    mga_dma_pending = 1;
}

/* mga_cmd_route: into the list, or the list sent first (mmio.h). */
static int cmd_route(uint32_t off, uint32_t v)
{
    if (MGA_CMD_REG(off)) {
        cmd_write(off, v);
        return 1;
    }
    if (mga_dma_pending)
        dma_flush();
    return 0;
}

static void stream_open(void)
{
    if (dma_state > 0) {
        mga_cmd_route = cmd_route;
        mga_cmd_open = 1;
    }
}

/* Everything queued sent and fetched; register writes direct again. */
static void stream_close(void)
{
    if (mga_cmd_open && mga_dma_pending)
        dma_flush();
    mga_cmd_open = 0;
    fifo_reset();
}

/* mga_reset_hook: a soft reset has stopped the chip's DMA and the WARPs:
 * nothing queued is sent, and the stream opens again at the next select. */
static void on_reset(void)
{
    mga_cmd_open = 0;
    mga_dma_pending = 0;
    ring_reset();
    running = 0;
    cur_pipe = -1;
}

/* AGP (docs/g400-warp.md): the channels fetch by AGP cycles, which don't
 * snoop the CPU's caches, so only from write-combining rings (GLOS's DRAW CACHE).
 * On: the target (the host bridge's AGP capability) first, then the card
 * (AGP_CMD, F8h), at the fastest rate both have (2x or 1x) with sideband
 * addressing if both have it; the card's request queue the bridge's depth.
 * The G200 needs sideband addressing, and AGP_PLL for 2x (docs/g200-warp.md).
 * Off: the card first, then the bridge, both as they were. */
static uint32_t agp_hcmd0, agp_ccmd0;   /* the commands as they were */
static uint32_t agp_pll0;               /* the G200's AGP_PLL as it was */

/* The G200's AGP_PLL: written only when it changes, then 10 ms for the
 * clock to settle before the next transfer (a real G200 drew two batches
 * by AGP 2x, then hung, with the PLL switched at every start and stop and
 * no wait: bench-g400's PC, 2026-11-03). */
static void pll_set(uint32_t v)
{
    uint32_t t0;
    if ((MGA_RD32(MGAREG_AGP_PLL) & 1u) == (v & 1u))
        return;
    MGA_WR32_DIRECT(MGAREG_AGP_PLL, v);
    for (t0 = sys_time_us(); (uint32_t)(sys_time_us() - t0) < 10000u;)
        ;
}

static int agp_enable(void)
{
    uint32_t hsts, csts, both, rate, cmd;
    int k;
    if (!agp_hcap) {
        int cap = (int)(mga_pci_read32(0, 0, 0, 0x34) & 0xFC);
        for (k = 0; k < 16 && cap && (mga_pci_read32(0, 0, 0, (uint8_t)cap) & 0xFF) != 2; k++)
            cap = (int)((mga_pci_read32(0, 0, 0, (uint8_t)cap) >> 8) & 0xFC);
        if (!cap || (mga_pci_read32(0, 0, 0, (uint8_t)cap) & 0xFF) != 2 ||
            (mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF0) & 0xFF) != 2)
            return -1;
        agp_hcap = cap;
        agp_hcmd0 = mga_pci_read32(0, 0, 0, (uint8_t)(cap + 8));
        agp_ccmd0 = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF8);
    }
    hsts = mga_pci_read32(0, 0, 0, (uint8_t)(agp_hcap + 4));
    csts = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0xF4);
    both = hsts & csts;
    rate = (both & 2u) && warp_agp_rate != 1 ? 2u : (both & 1u) ? 1u : 0;
    if (!rate)
        return -1;
    if (g200() && !(both & (1u << 9)))
        return -1;                      /* (the G200 masters by AGP only with sideband addressing: 3-3, 5.2.2) */
    cmd = rate | (1u << 8) | (both & (1u << 9));     /* (enable; sideband if both) */
    mga_pci_write32(0, 0, 0, (uint8_t)(agp_hcap + 8), cmd);
    mga_pci_write32(mga.bus, mga.dev, mga.fn, 0xF8,
                    cmd | ((((hsts >> 24) & 0xFF) > 31 ? 31u : ((hsts >> 24) & 0xFF)) << 24));
    if (g200()) {                       /* agp2xpllen for 2x, after the modes, as the DRM's mga_dma.c */
        agp_pll0 = MGA_RD32(MGAREG_AGP_PLL);
        pll_set(rate == 2u ? 1u : 0u);
    }
    agp_on = 1;
    return 0;
}

static void agp_disable(void)
{
    if (!agp_on)
        return;
    mga_pci_write32(mga.bus, mga.dev, mga.fn, 0xF8, agp_ccmd0);
    mga_pci_write32(0, 0, 0, (uint8_t)(agp_hcap + 8), agp_hcmd0);
    if (g200())
        pll_set(agp_pll0);
    agp_on = 0;
}

static void rings_at(void *p, uint32_t phys, uint32_t bytes)
{
    lring = (uint32_t *)p;
    lphys = phys;
    lcap = bytes / (uint32_t)(warp_list_div >= 2 ? warp_list_div : 2) / 4u;   /* the list's share */
    vring = lring + lcap;
    vphys = lphys + lcap * 4u;
    vcap = bytes - lcap * 4u;
}

static void dma_setup(void)
{
    uint16_t seg;
    uint32_t cmd, bytes;
    void *p = NULL;
    dma_state = -1;
    if (!warp_use_dma)
        return;
    /* Write-combining rings: whole 64 KB units, so a block 64 KB larger
       than the rings to align them in. */
    /* Not on the G200 unless warp_agp_rate asks: a real G200 drew nothing by
       AGP, 1x or 2x, and PCI's bandwidth isn't what limits it (docs/g200-warp.md). */
    if (warp_use_agp && (!g200() || warp_agp_rate) &&
        (p = sys_dos_alloc(warp_ring_bytes + 0x10000u - 16u, &seg)) == NULL)
        warp_wc_status = -1;
    if (p) {
        uint32_t lin = (uint32_t)seg << 4, al = (lin + 0xFFFFu) & ~0xFFFFu;
        warp_wc_status = -2;
        if (sys_cache_wc(al, warp_ring_bytes) == 0) {
            warp_wc_status = 1;
            rings_at((char *)p + (al - lin), al, warp_ring_bytes);
            ring_wc = 1;
            if (agp_enable() == 0)
                warp_agp_xfer = 1;
        } else {
            sys_dos_free(p);            /* (no GLOS, or it can't: the plain rings) */
            p = NULL;
        }
    }
    if (!p) {
        for (bytes = warp_ring_bytes; bytes >= RING_LEAST && !p; bytes /= 2)
            p = sys_dos_alloc(bytes, &seg);
        if (!p)
            return;
        rings_at(p, (uint32_t)seg << 4, bytes * 2u);    /* (the loop's last halving) */
    }
    ring_reset();
    warp_ring_kb = (lcap * 4u + vcap) >> 10;
    warp_ring_mode = !ring_wc ? 1 : warp_agp_xfer ? 3 : 2;
    cmd = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0x04);
    if (!(cmd & 4u))
        mga_pci_write32(mga.bus, mga.dev, mga.fn, 0x04, cmd | 4u);   /* bus master */
    mga_dma_flush = dma_flush;
    mga_reset_hook = on_reset;
    dma_state = 1;
}

static void fail(void)
{
    mga_cmd_open = 0;                   /* (nothing more into the list: the reset is direct) */
    mga_dma_pending = 0;
    ring_reset();
    warp_failures++;
    MGA_WR32_DIRECT(g200() ? MGAREG_WIADDRNB : MGAREG_WIADDRNB2, WMODE_SUSPEND);   /* (bypassing the FIFO) */
    running = 0;
    cur_pipe = -1;
    engine_reset();                     /* (OPMODE unknown after it) */
}

void warp_abort(void)
{
    mga_cmd_open = 0;
    mga_dma_pending = 0;
    ring_reset();
    if (running)
        MGA_WR32_DIRECT(g200() ? MGAREG_WIADDRNB : MGAREG_WIADDRNB2, WMODE_SUSPEND);   /* (bypassing the FIFO) */
    running = 0;
    cur_pipe = -1;
}

static int started(uint32_t busy);

/* The G200's start, as Linux's mga_warp_init and mga_g200_emit_pipe write
 * it (WGETMSB, which the DRM wrote once, at every start here: the HAL
 * resets the engine before loads). No WACCEPTSEQ, WIADDR2 or WFLAG1: on the
 * G200 those addresses are reserved. */
static int g200_start(void)
{
    int i;
    fifo_reserve(10);
    MGA_WR32(MGAREG_WGETMSB, 0x00001606);
    MGA_WR32(MGAREG_WVRTXSZ, 7);                   /* 8 dwords a vertex */
    MGA_WR32(MGAREG_WFLAG, 0);
    MGA_WR32(MGAREG_WR(24), 0);
    MGA_WR32(MGAREG_WR(25), 0x00000100);
    MGA_WR32(MGAREG_WR(34), 0);
    MGA_WR32(MGAREG_WR(42), 0x0000FFFF);
    MGA_WR32(MGAREG_WR(60), 0x0000FFFF);
    for (i = 0; i < warp_start_pads; i++) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_DMAPAD, 0xFFFFFFFFu);      /* "required due to hardware bug" (the DRM) */
    }
    warp_diag[1] = MGA_RD32(MGAREG_STATUS);
    fifo_reserve(1);
    MGA_WR32(MGAREG_WIADDR, 0 | WMODE_START);
    return started(STATUS_WBUSY);
}

int warp_select(int pipe)
{
    const unsigned char *code;
    int len, words, i;
    if (!warp_available() || pipe_code(pipe, &code, &len) != 0)
        return -1;
    if (!dma_state)
        dma_setup();
    check_lost();
    sel_pipe = pipe;
    if (running && pipe == cur_pipe)
        return 0;
    stream_close();                     /* (the start is direct work: loads, a reset, polling) */
    warp_diag[0] = MGA_RD32(MGAREG_STATUS);
    dma_mode(0);
    suspend();
    engine_sync(200000);
    if (pipe != cur_pipe) {
        /* On a real G400 the third load of the instruction memories after a
         * soft reset leaves the WARPs dead (the start reads no busy bits), at
         * every pipe: so a soft reset before each load (docs/g400-warp.md).
         * It clears the drawing registers: callers see engine_resets move. */
        if (warp_reset_before_load) {
            engine_reset();
            dma_mode(0);
        }
        MGA_WR32(MGAREG_WMISC, 0);                  /* no microcode caching: the instruction memories */
        words = (len + 7) / 8;
        MGA_WR32(MGAREG_WIMEMADDR, 0);
        for (i = 0; i < words; i++) {
            MGA_WR32(MGAREG_WIMEMDATA, le32(code + 8 * i));
            MGA_WR32(MGAREG_WIMEMDATA + 4, le32(code + 8 * i + 4));
        }
        if (!g200()) {                              /* (the G400's second WARP) */
            MGA_WR32(MGAREG_WIMEMADDR, 0);
            for (i = 0; i < words; i++) {
                MGA_WR32(MGAREG_WIMEMDATA1, le32(code + 8 * i));
                MGA_WR32(MGAREG_WIMEMDATA1 + 4, le32(code + 8 * i + 4));
            }
        }
        cur_pipe = pipe;
    }
    if (g200())
        return g200_start();
    fifo_reserve(12);
    MGA_WR32(MGAREG_WGETMSB, 0x00000E00);
    MGA_WR32(MGAREG_WVRTXSZ, (pipe & WARP_T2) ? 0x00001E09 : 0x00001807);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, (pipe & WARP_T2) ? 0x1E000000 : 0x18000000);
    MGA_WR32(MGAREG_WFLAG, 0);
    MGA_WR32(MGAREG_WFLAG1, 0);
    MGA_WR32(MGAREG_WR(56), 0x46480000);           /* 12800.0f, as the DRM writes */
    MGA_WR32(MGAREG_WR(49), 0);
    MGA_WR32(MGAREG_WR(57), 0);
    MGA_WR32(MGAREG_WR(53), 0);
    fifo_reserve(1);
    MGA_WR32(MGAREG_WR(61), 0);
    for (i = 0; i < warp_start_pads; i++) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_DMAPAD, 0xFFFFFFFFu);
    }
    warp_diag[1] = MGA_RD32(MGAREG_STATUS);
    fifo_reserve(1);
    MGA_WR32(MGAREG_WIADDR2, 0 | WMODE_START);
    return started(STATUS_WBUSY | STATUS_WBUSY1);
}

/* After the start: the WARP busy (it waits for vertices), or the start
 * failed and the engine is reset. Then the stream opens. */
static int started(uint32_t busy)
{
    long spin;
    uint32_t st = 0, t0;
    engine_sync(200000);
    for (spin = 0, t0 = sys_time_us();
         !((st = MGA_RD32(MGAREG_STATUS)) & busy) && ((++spin & 1023) || (uint32_t)(sys_time_us() - t0) < 200000u);)
        ;
    warp_diag[2] = st;
    warp_diag[3] = MGA_RD32(MGAREG_WIADDRNB);
    warp_diag[4] = g200() ? 0 : MGA_RD32(MGAREG_WIADDRNB1);
    if (!(st & busy)) {
        fail();
        return -1;
    }
    if (ring_wc && agp_hcap && !agp_on && agp_enable() == 0)
        warp_agp_xfer = 1;              /* (after a warp_stop) */
    running = 1;
    stream_open();
    warp_tex_sizes(0, 0, 0, 0);         /* (and wtex: nothing else known after a start) */
    return 0;
}

/* What warp_tex and warp_tex_sizes last wrote, written again when
 * setup_triangle has written the sizes its own way since. */
static struct { int set, map1; uint32_t w0, h0, w1, h1, texctl2; } wtex;

static void wtex_again(void)
{
    setup_wrote_texsize = 0;
    if (!wtex.set)
        return;
    fifo_reserve(8);
    if (wtex.w0 || wtex.h0) {
        MGA_WR32(MGAREG_TEXWIDTH, wtex.w0);
        MGA_WR32(MGAREG_TEXHEIGHT, wtex.h0);
    }
    if (wtex.map1) {
        MGA_WR32(MGAREG_TEXCTL2, wtex.texctl2 | TEXCTL2_MAP1);
        MGA_WR32(MGAREG_TEXWIDTH, wtex.w1);
        MGA_WR32(MGAREG_TEXHEIGHT, wtex.h1);
        MGA_WR32(MGAREG_TEXCTL2, wtex.texctl2);
    }
}

void warp_tex_sizes(uint32_t w0, uint32_t h0, uint32_t w1, uint32_t h1)
{
    wtex.set = 1;
    wtex.map1 = 0;
    wtex.w0 = w0; wtex.h0 = h0; wtex.w1 = w1; wtex.h1 = h1;
    fifo_reserve(4);
    if (g200()) {                                  /* (mga_g200_emit_tex0: no magic bit, no map 1) */
        MGA_WR32(MGAREG_WR(24), w0);
        MGA_WR32(MGAREG_WR(34), h0);
        return;
    }
    MGA_WR32(MGAREG_WR(54), w0 | 0x40);            /* | MGA_G400_WR_MAGIC */
    MGA_WR32(MGAREG_WR(62), h0 | 0x40);
    MGA_WR32(MGAREG_WR(52), w1 | 0x40);
    MGA_WR32(MGAREG_WR(60), h1 | 0x40);
}

/* tw/th = log2 + 11 on the G400, + 28 on the G200 (Mesa's mga_texstate.c:
 * the scales of the TMRs each chip's microcode writes), modulo 64. */
static uint32_t mesa_wh(int lg)
{
    return ((uint32_t)((1 << lg) - 1) << 18) | ((uint32_t)((2 - lg) & 63) << 9) |
           ((uint32_t)(lg + (g200() ? 28 : 11)) & 63);
}

void warp_tex(int map, int log2w, int log2h, uint32_t texctl2)
{
    const uint32_t w = mesa_wh(log2w), h = mesa_wh(log2h);
    wtex.set = 1;
    if (!map) {
        wtex.w0 = w; wtex.h0 = h;
        wtex.map1 = 0;
        fifo_reserve(4);
        MGA_WR32(MGAREG_TEXWIDTH, w);
        MGA_WR32(MGAREG_TEXHEIGHT, h);
        MGA_WR32(g200() ? MGAREG_WR(24) : MGAREG_WR(54), g200() ? w : w | 0x40);
        MGA_WR32(g200() ? MGAREG_WR(34) : MGAREG_WR(62), g200() ? h : h | 0x40);
    } else if (!g200()) {                          /* (the G200 has one map) */
        wtex.w1 = w; wtex.h1 = h;
        wtex.map1 = 1;
        wtex.texctl2 = texctl2;
        fifo_reserve(6);
        MGA_WR32(MGAREG_TEXCTL2, texctl2 | TEXCTL2_MAP1);
        MGA_WR32(MGAREG_TEXWIDTH, w);
        MGA_WR32(MGAREG_TEXHEIGHT, h);
        MGA_WR32(MGAREG_TEXCTL2, texctl2);
        MGA_WR32(MGAREG_WR(52), w | 0x40);
        MGA_WR32(MGAREG_WR(60), h | 0x40);
    }
    setup_stale = 1;                    /* (setup_triangle's TEXWIDTH/TEXHEIGHT shadows) */
}

/* Diagnostic: bytes of DMAPAD packets fetched by the primary channel, and
 * the TSC ticks it took (0 when there is no stream). */
uint32_t warp_dma_bandwidth(uint32_t bytes, uint64_t (*tsc)(void), uint64_t *ticks, void (*flush)(void))
{
    uint32_t n = 0, packets;
    uint64_t t0;
    if (dma_state <= 0 || !mga_cmd_open)
        return 0;
    if (mga_dma_pending)
        dma_flush();
    packets = bytes / 20u;
    if (packets * 5u + LIST_SLACK > lcap)
        packets = (lcap - LIST_SLACK) / 5u;
    for (n = 0; n < packets * 4u; n++)
        list_put(DMAREG(MGAREG_DMAPAD), 0);
    if (flush)
        flush();                        /* (the list out of the CPU's caches, for AGP cycles) */
    t0 = tsc();
    dma_kick();
    dma_wait();
    *ticks = tsc() - t0;
    return packets * 20u;
}

/* Diagnostic: ntris copies of triangle t (3 vertices) put in one batch, the
 * CPU's caches flushed, then the ticks of tsc() from the kick until the
 * chip has fetched them (*fetch) and until the engine is idle (*done). */
int warp_dma_vertex_bandwidth(const mga_wvtx *t, int ntris, uint64_t (*tsc)(void), uint64_t *fetch, uint64_t *done,
                              void (*flush)(void))
{
    int i, k, dw;
    uint64_t t0;
    if (dma_state <= 0 || !mga_cmd_open || !running)
        return -1;
    if (mga_dma_pending)
        dma_flush();
    dw = (cur_pipe & WARP_T2) ? 10 : 8;
    if ((uint32_t)(ntris * 3 * dw) * 4u >= vcap)
        ntris = (int)((vcap - 4u) / 4u / (uint32_t)(3 * dw));
    vrun = vused;
    for (i = 0; i < ntris; i++)
        for (k = 0; k < 3; k++) {
            memcpy(vring + vused / 4u, &t[k], (size_t)dw * 4u);
            vused += (uint32_t)dw * 4u;
        }
    if (flush)
        flush();
    t0 = tsc();
    dma_kick();
    dma_wait();
    *fetch = tsc() - t0;
    engine_sync(2000000);
    *done = tsc() - t0;
    warp_tris += (uint32_t)ntris;
    return ntris;
}

uint32_t *warp_reserve(int n, int *dwords)
{
    int dw;
    check_lost();
    if (!running || !mga_cmd_open)
        return NULL;
    dw = (cur_pipe & WARP_T2) ? 10 : 8;
    if (setup_wrote_texsize)
        wtex_again();
    if (list_room(16) != 0 || vert_room((uint32_t)(n * dw) * 4u) != 0 || !running || !mga_cmd_open)
        return NULL;                    /* (a failed kick, or more than the ring) */
    if (vrun == NO_RUN)
        vrun = vused;
    *dwords = dw;
    return vring + vused / 4u;
}

void warp_commit(int n)
{
    uint32_t b = (uint32_t)(n * ((cur_pipe & WARP_T2) ? 10 : 8)) * 4u;
    vused += b;
    vsince += b;
    setup_stale = 1;
    mga_dma_pending = 1;
    mga_warp_pending = 1;
    warp_tris += (uint32_t)(n / 3);
    if (vsince >= warp_kick_bytes)
        dma_kick();
}

/* Strips and fans by the WARPs' ACCEPT sequencer (WACCEPTSEQ, G400
 * specification 3-260; Mesa's MGA_WA_TRIFAN_T0 and MGA_WA_TRISTRIP_T0, which
 * Mesa never used): after the first triangle's three vertices, each vertex
 * makes the next triangle with two of the last three, as GL_TRIANGLE_FAN
 * and GL_TRIANGLE_STRIP have it (in their windings: nothing is culled here).
 * A real G400 draws them pixel for pixel as the same triangles in a list
 * (WARPTRI seq, 2026-10-27). One run of vertices between two WACCEPTSEQ
 * writes, the second back to lists (warp_select's value). The T2 pipes'
 * 10-dword vertices sit in banks 16 registers apart (Mesa's T0T1 strip,
 * 0x02010400, is wrong: the second bank is 8); WARPTRI seq pipe=8 and
 * pipe=9 on a real G400, 2026-10-27. */
#define SEQ_FAN   0x01000408u           /* seqdst 8, 16: the first vertex stays */
#define SEQ_STRIP 0x02010200u           /* seqdst 0, 8, 16: the oldest goes */
#define SEQ_LIST  0x18000000u           /* seqoff (and wsametag): three a triangle */
#define SEQ_FAN2   0x01000810u          /* T2: seqdst 16, 32 */
#define SEQ_STRIP2 0x02020400u          /* T2: seqdst 0, 16, 32 */
#define SEQ_LIST2  0x1E000000u

uint32_t *warp_reserve_seq(int fan, int n, int *dwords)
{
    int t2;
    check_lost();
    if (!running || !mga_cmd_open || n < 3 || g200())
        return NULL;                    /* (the G200 has no ACCEPT sequencer: the caller sends triangles) */
    t2 = (cur_pipe & WARP_T2) != 0;
    if (setup_wrote_texsize)
        wtex_again();
    if (list_room(32) != 0 || vert_room((uint32_t)n * (t2 ? 40u : 32u)) != 0 || !running || !mga_cmd_open)
        return NULL;
    cmd_write(MGAREG_WACCEPTSEQ, t2 ? (fan ? SEQ_FAN2 : SEQ_STRIP2) : (fan ? SEQ_FAN : SEQ_STRIP));
    vrun = vused;                       /* (the write ended the open run; room kept above) */
    *dwords = t2 ? 10 : 8;
    return vring + vused / 4u;
}

void warp_commit_seq(int n)
{
    const int t2 = (cur_pipe & WARP_T2) != 0;
    uint32_t b = (uint32_t)n * (t2 ? 40u : 32u);
    vused += b;
    vsince += b;
    setup_stale = 1;
    mga_dma_pending = 1;
    mga_warp_pending = 1;
    warp_tris += (uint32_t)(n - 2);
    warp_seqs++;
    warp_seqs_t2 += (uint32_t)t2;
    cmd_write(MGAREG_WACCEPTSEQ, t2 ? SEQ_LIST2 : SEQ_LIST);  /* lists again (ends the run) */
    if (vsince >= warp_kick_bytes)
        dma_kick();
}

int warp_vertices(const mga_wvtx *v, int n)
{
    int i, k, dw;
    check_lost();
    if (!running && (sel_pipe < 0 || warp_select(sel_pipe) != 0))
        return -1;
    dw = (cur_pipe & WARP_T2) ? 10 : 8;
    if (setup_wrote_texsize)
        wtex_again();
    setup_stale = 1;
    if (mga_cmd_open) {
        if (list_room(16) != 0 || vert_room((uint32_t)(n * dw) * 4u) != 0 || !running || !mga_cmd_open)
            return -1;                  /* (a failed kick, or more than the ring) */
        if (vrun == NO_RUN)
            vrun = vused;
        for (i = 0; i < n; i++) {
            uint32_t *d;
            union { float f; uint32_t u; } c;
            d = vring + vused / 4u;
            c.f = v[i].x; d[0] = c.u;
            c.f = v[i].y; d[1] = c.u;
            c.f = v[i].z; d[2] = c.u;
            c.f = v[i].rhw; d[3] = c.u;
            d[4] = v[i].bgra;
            d[5] = v[i].spec;
            c.f = v[i].u0; d[6] = c.u;
            c.f = v[i].v0; d[7] = c.u;
            if (dw == 10) {
                c.f = v[i].u1; d[8] = c.u;
                c.f = v[i].v1; d[9] = c.u;
            }
            vused += (uint32_t)dw * 4u;
        }
        vsince += (uint32_t)(n * dw) * 4u;
        mga_dma_pending = 1;
        warp_tris += (uint32_t)(n / 3);
        mga_warp_pending = 1;
        if (vsince >= warp_kick_bytes)
            dma_kick();
        return 0;
    }
    dma_mode(1);
    for (i = 0; i < n; i++) {
        union { float f; uint32_t u; } c;
        uint32_t d[10];
        c.f = v[i].x; d[0] = c.u;
        c.f = v[i].y; d[1] = c.u;
        c.f = v[i].z; d[2] = c.u;
        c.f = v[i].rhw; d[3] = c.u;
        d[4] = v[i].bgra;
        d[5] = v[i].spec;
        c.f = v[i].u0; d[6] = c.u;
        c.f = v[i].v0; d[7] = c.u;
        c.f = v[i].u1; d[8] = c.u;
        c.f = v[i].v1; d[9] = c.u;
        if (fifo_room(dw) != 0) {
            fail();
            return -1;
        }
        for (k = 0; k < dw; k++) {
            MGA_WR32(MGAREG_DMAWIN + dmawin_pos, d[k]);
            dmawin_pos = dmawin_pos + 4 < MGA_DMAWIN_SIZE ? dmawin_pos + 4 : 0;
        }
    }
    warp_tris += (uint32_t)(n / 3);
    mga_warp_pending = 1;
    return 0;
}
