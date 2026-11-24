/* cp.c - the Radeon's command processor (docs/radeon.md, RD3): the microcode loaded, a ring in system memory the
 * chip fetches through its own PCI GART, packets out, the idle wait, and the stop. The sequences are Linux 2.6.29's
 * DRM radeon driver's (radeon_cp.c: radeon_cp_load_microcode, radeon_set_pcigart, radeon_cp_init_ring_buffer,
 * radeon_do_cp_start, radeon_do_cp_idle, radeon_do_cp_stop) and, for the memory map, X.org's
 * RADEONRestoreMemMapRegisters; both MIT. Lessons kept from the Rage 128 (docs/rage128.md): the ring in
 * conventional memory (linear = physical), never in video memory; a first fetch checked, with everything put back
 * on failure (plain register writes then); no set-up register changed while the ring runs; idle is the ring
 * fetched, the FIFO empty and the engine not active. The chip writes nothing to system memory: the read index and
 * the scratch registers stay in the chip (RB_NO_UPDATE, SCRATCH_UMSK 0) and are read by register. */
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "mga/sys.h"
#include <stddef.h>

#define GART_BYTES  (4u << 20)          /* the GART's range: one page of table entries */
#define COMMIT_DW   256u                /* the write index given to the chip at least this often */

static int running;
static volatile uint32_t *ring;
static uint32_t ring_dw, ring_mask, wptr, wptr_given, rptr_seen;
static void *block;
static uint32_t saved_aic, saved_bus;
uint32_t radeon_cp_gart_start;          /* where the GART's range begins in the chip's address space (0: none) */
void (*radeon_cp_trace)(const char *stage, uint32_t a, uint32_t b);    /* (a test's log of each step, before it) */
#define TRACE(s, a, b) do { if (radeon_cp_trace) radeon_cp_trace(s, a, b); } while (0)

int radeon_cp_running(void) { return running; }

static int engine_idle_wait(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    while (!radeon_idle())
        if (sys_time_us() - t0 > timeout_us)
            return -1;
    return 0;
}

/* Room in the chip's address space above the frame buffer for the GART's range: the BIOS leaves MC_FB_LOCATION
 * ffff0000, the frame buffer claiming every address. Its top is lowered to the megabyte below gart_start, X.org's
 * way: only when it must change, with the engine idle, the display's memory requests stopped and the memory
 * controller idle, then the engine reset; the display's registers put back. 0, or -1 with nothing written. */
static int mc_make_room(uint32_t gart_start)
{
    uint32_t have = radeon_rd(RADEON_MC_FB_LOCATION), want, ext, gen, t0;
    if (((have >> 16) << 16 | 0xFFFFu) < gart_start)
        return 0;                               /* (the frame buffer already ends below it) */
    want = ((gart_start - 1u) & 0xFFFF0000u) | (radeon.fb_location >> 16);
    TRACE("mc: MC_FB_LOCATION have, want", have, want);
    if (engine_idle_wait(500000u) != 0)
        return -1;
    ext = radeon_rd(RADEON_CRTC_EXT_CNTL);
    gen = radeon_rd(RADEON_CRTC_GEN_CNTL);
    radeon_wr(RADEON_CRTC_EXT_CNTL, ext | RADEON_CRTC_DISPLAY_DIS);
    radeon_vsync_wait(50000u);
    radeon_wr(RADEON_CRTC_GEN_CNTL, (gen & ~(RADEON_CRTC_CUR_EN | RADEON_CRTC_ICON_EN)) | RADEON_CRTC_DISP_REQ_EN_B |
                                    RADEON_CRTC_EXT_DISP_EN);
    sys_delay_us(100000u);
    t0 = sys_time_us();
    while (!(radeon_rd(RADEON_MC_STATUS) & RADEON_MC_IDLE))
        if (sys_time_us() - t0 > 1000000u) {
            radeon_wr(RADEON_CRTC_GEN_CNTL, gen);
            radeon_wr(RADEON_CRTC_EXT_CNTL, ext);
            return -1;
        }
    TRACE("mc: display requests off, MC_STATUS", radeon_rd(RADEON_MC_STATUS), 0);
    radeon_wr(RADEON_MC_FB_LOCATION, want);
    (void)radeon_rd(RADEON_MC_FB_LOCATION);
    TRACE("mc: written, reads", radeon_rd(RADEON_MC_FB_LOCATION), 0);
    radeon_engine_reset();
    radeon_wr(RADEON_CRTC_GEN_CNTL, gen);
    radeon_wr(RADEON_CRTC_EXT_CNTL, ext);
    return 0;
}

static void release(void)
{
    if (!block)
        return;
    radeon_wr(RADEON_AIC_CNTL, saved_aic);
    radeon_wr(RADEON_BUS_CNTL, saved_bus);
    sys_dos_free(block);
    block = NULL;
}

static void stop_now(void)
{
    radeon_wr(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIDIS_INDDIS);
    running = 0;
    ring = NULL;
    radeon_engine_reset();
    release();
}

/* The block in conventional memory: a page of table (4 KB aligned), the ring, a spare page (given as the read
 * index's address, though the chip is told not to write it). */
int radeon_cp_init(const uint32_t *pairs, uint32_t bytes)
{
    int i, l2qw = 0, pages;
    uint16_t seg;
    uint32_t lin, tbl, rb, spare, t0, gart, rp;
    volatile uint32_t *t;
    uint8_t *blk;
    if (!radeon.mmio || (bytes & (bytes - 1)) || bytes < 4096u || bytes > (1u << 18))
        return -1;
    if (running)
        radeon_cp_stop();
    gart = radeon.fb_location + radeon_rd(RADEON_CONFIG_APER_SIZE);     /* (the DRM's: after the frame buffer) */
    if (!gart || (gart & 0xFFFFu))
        return -1;
    if (mc_make_room(gart) != 0)
        return -1;
    radeon_engine_reset();
    if (engine_idle_wait(500000u) != 0)
        return -1;
    blk = (uint8_t *)sys_dos_alloc(bytes + 4096u + 4096u + 4096u, &seg);
    if (!blk)
        return -1;
    block = blk;
    lin = (uint32_t)seg << 4;
    tbl = (lin + 4095u) & ~4095u;
    rb = tbl + 4096u;
    spare = rb + bytes;
    t = (volatile uint32_t *)(blk + (tbl - lin));
    pages = (int)(bytes / 4096u);
    for (i = 0; i < 1024; i++)                  /* (entries past the ring: its first page, never anything else's) */
        t[i] = i < pages ? rb + (uint32_t)i * 4096u : rb;
    if (!pairs)
        pairs = radeon_cp_microcode;
    TRACE("microcode: table, ring (physical)", tbl, rb);
    radeon_wr(RADEON_CP_ME_RAM_ADDR, 0);
    for (i = 0; i < 256; i++) {
        radeon_wr(RADEON_CP_ME_RAM_DATAH, pairs[i * 2 + 1]);
        radeon_wr(RADEON_CP_ME_RAM_DATAL, pairs[i * 2]);
    }
    while ((8u << l2qw) < bytes)
        l2qw++;
    ring = (volatile uint32_t *)(blk + (rb - lin));
    ring_dw = bytes / 4u, ring_mask = ring_dw - 1u;
    for (i = 0; i < (int)ring_dw; i++)
        ring[i] = RADEON_CP_PACKET2;
    saved_aic = radeon_rd(RADEON_AIC_CNTL);
    saved_bus = radeon_rd(RADEON_BUS_CNTL);
    TRACE("gart: AIC_CNTL, range start", saved_aic, gart);
    radeon_wr(RADEON_AIC_PT_BASE, tbl);
    radeon_wr(RADEON_AIC_LO_ADDR, gart);
    radeon_wr(RADEON_AIC_HI_ADDR, gart + GART_BYTES - 1u);
    radeon_wr(RADEON_AIC_CNTL, saved_aic | RADEON_PCIGART_TRANSLATE_EN);
    TRACE("ring: CP_RB_RPTR, CP_RB_CNTL before", radeon_rd(RADEON_CP_RB_RPTR), radeon_rd(RADEON_CP_RB_CNTL));
    radeon_wr(RADEON_CP_RB_BASE, gart);         /* (the ring at the range's start: the table's entry 0) */
    radeon_wr(RADEON_CP_RB_WPTR_DELAY, 0);
    rp = radeon_rd(RADEON_CP_RB_RPTR) & ring_mask;
    radeon_wr(RADEON_CP_RB_WPTR, rp);
    wptr = wptr_given = rptr_seen = rp;
    radeon_wr(RADEON_CP_RB_RPTR_ADDR, spare);
    radeon_wr(RADEON_CP_RB_CNTL, RADEON_RB_NO_UPDATE | 1u << 18 | 9u << 8 | (uint32_t)l2qw);
    radeon_wr(RADEON_SCRATCH_ADDR, spare + 32u);
    radeon_wr(RADEON_SCRATCH_UMSK, 0);
    radeon_wr(RADEON_BUS_CNTL, saved_bus & ~RADEON_BUS_MASTER_DIS);
    if (engine_idle_wait(500000u) != 0) {
        ring = NULL;
        release();
        return -1;
    }
    radeon_wr(RADEON_ISYNC_CNTL, RADEON_ISYNC_ANY2D_IDLE3D | RADEON_ISYNC_ANY3D_IDLE2D | RADEON_ISYNC_WAIT_IDLEGUI |
                                 RADEON_ISYNC_CPSCRATCH_IDLEGUI);
    TRACE("start: RBBM_STATUS, CP_CSQ_STAT", radeon_rd(RADEON_RBBM_STATUS), radeon_rd(RADEON_CP_CSQ_STAT));
    radeon_wr(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIBM_INDBM);
    running = 1;
    radeon_cp_gart_start = gart;
    /* The first fetch: sixteen no-ops, the read index past them within 50 ms. */
    wptr = (wptr + 16u) & ring_mask;
    radeon_wr(RADEON_CP_RB_WPTR, wptr);
    (void)radeon_rd(RADEON_CP_RB_RPTR);
    wptr_given = wptr;
    t0 = sys_time_us();
    while ((radeon_rd(RADEON_CP_RB_RPTR) & ring_mask) != wptr)
        if (sys_time_us() - t0 > 50000u) {
            TRACE("first fetch FAILED: CP_RB_RPTR, RBBM_STATUS", radeon_rd(RADEON_CP_RB_RPTR), radeon_rd(RADEON_RBBM_STATUS));
            TRACE("first fetch FAILED: AIC_STAT, CP_CSQ_STAT", radeon_rd(RADEON_AIC_STAT), radeon_rd(RADEON_CP_CSQ_STAT));
            stop_now();
            return -1;
        }
    rptr_seen = wptr;
    TRACE("first fetch done: CP_RB_RPTR, RBBM_STATUS", radeon_rd(RADEON_CP_RB_RPTR), radeon_rd(RADEON_RBBM_STATUS));
    /* (the DRM's radeon_do_cp_start: the caches purged and the engines idle before anything else) */
    radeon_cp_reg(RADEON_ISYNC_CNTL, RADEON_ISYNC_ANY2D_IDLE3D | RADEON_ISYNC_ANY3D_IDLE2D | RADEON_ISYNC_WAIT_IDLEGUI |
                                     RADEON_ISYNC_CPSCRATCH_IDLEGUI);
    return radeon_cp_idle_wait(500000u);
}

/* (diagnostics: the ring's read and write indexes, its dwords) */
void radeon_cp_peek(uint32_t *rp, uint32_t *wp, const volatile uint32_t **base, uint32_t *dwords)
{
    *rp = ring ? radeon_rd(RADEON_CP_RB_RPTR) : 0;
    *wp = wptr;
    *base = ring;
    *dwords = ring_dw;
}

/* The ring's new dwords to the chip (the ring is ordinary memory: x86 keeps these stores before the uncached write
 * of the index; the read after it posts the write, as the DRM's COMMIT_RING). */
void radeon_cp_flush(void)
{
    if (!running || wptr == wptr_given)
        return;
    radeon_wr(RADEON_CP_RB_WPTR, wptr);
    (void)radeon_rd(RADEON_CP_RB_RPTR);
    wptr_given = wptr;
}

/* Room for n dwords (and the one that keeps the write index off the read index); given up only when the read index
 * stops moving for half a second (the engine stuck: the next sync's time-out reports it). */
static void room(int n)
{
    uint32_t t0 = 0, last = rptr_seen;
    int spins = 0;
    while ((int)((rptr_seen - wptr - 1u) & ring_mask) < n) {
        radeon_cp_flush();
        rptr_seen = radeon_rd(RADEON_CP_RB_RPTR) & ring_mask;
        if (rptr_seen != last) {
            last = rptr_seen, t0 = 0, spins = 0;
            continue;
        }
        if ((++spins & 255) == 0) {
            if (!t0)
                t0 = sys_time_us();
            else if (sys_time_us() - t0 > 500000u)
                break;
        }
    }
}

void radeon_cp_out(const uint32_t *d, int n)
{
    if (!running)
        return;
    if (n > (int)ring_dw / 2)
        n = (int)ring_dw / 2;
    room(n);
    while (n-- > 0) {
        ring[wptr] = *d++;
        wptr = (wptr + 1u) & ring_mask;
    }
    if (((wptr - wptr_given) & ring_mask) >= COMMIT_DW)
        radeon_cp_flush();
}

void radeon_cp_reg(uint32_t reg, uint32_t v)
{
    uint32_t p[2];
    p[0] = RADEON_CP_PACKET0(reg, 0), p[1] = v;
    radeon_cp_out(p, 2);
}

/* A drawing-engine register: a packet while the command processor runs, else the register itself with room in
 * the FIFO first. */
void radeon_out(uint32_t reg, uint32_t v)
{
    if (running) {
        radeon_cp_reg(reg, v);
        return;
    }
    {
        uint32_t t0 = sys_time_us();
        while ((radeon_rd(RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK) < 1u && sys_time_us() - t0 < 200000u)
            ;
    }
    radeon_wr(reg, v);
}

/* Everything sent fetched and drawn: the caches purged and the engines waited for in the stream (the DRM's
 * radeon_do_cp_idle), then the ring fetched to the write index, the FIFO empty and the engine not active. */
int radeon_cp_idle_wait(uint32_t timeout_us)
{
    uint32_t t0, p[6];
    if (!running)
        return 0;
    p[0] = RADEON_CP_PACKET0(RADEON_RB3D_DSTCACHE_CTLSTAT, 0), p[1] = RADEON_RB3D_DC_FLUSH | RADEON_RB3D_DC_FREE;
    p[2] = RADEON_CP_PACKET0(RADEON_RB3D_ZCACHE_CTLSTAT, 0), p[3] = RADEON_RB3D_ZC_FLUSH | RADEON_RB3D_ZC_FREE;
    p[4] = RADEON_CP_PACKET0(RADEON_WAIT_UNTIL, 0);
    p[5] = RADEON_WAIT_2D_IDLECLEAN | RADEON_WAIT_3D_IDLECLEAN | RADEON_WAIT_HOST_IDLECLEAN;
    radeon_cp_out(p, 6);
    radeon_cp_flush();
    t0 = sys_time_us();
    for (;;) {
        if ((radeon_rd(RADEON_CP_RB_RPTR) & ring_mask) == wptr && radeon_idle()) {
            rptr_seen = wptr;
            return 0;
        }
        if (sys_time_us() - t0 > timeout_us)
            return -1;
    }
}

/* The command processor stopped after what was sent is drawn (bounded), the engine reset, the GART and the bus
 * master bit as they were, the block freed. MC_FB_LOCATION stays as lowered: every driver leaves it so. */
void radeon_cp_stop(void)
{
    if (!running)
        return;
    radeon_cp_idle_wait(1000000u);
    stop_now();
}

/* (after a fault: the layer's own state only) */
void radeon_cp_forget(void) { running = 0; ring = NULL; }
