/* cce.c - the Rage 128's Concurrent Command Engine in PIO mode (docs/rage128.md, R2): the microcode loaded, the
 * engine started in 192PIO (the CPU writes packet dwords to PM4_FIFO_DATA_EVEN and ODD in pairs: no ring, no
 * GART), packets out, the idle wait, and the stop. The sequences are Linux's DRM r128 driver's (r128_cce.c:
 * r128_cce_load_microcode, r128_do_cce_start, r128_do_cce_stop, r128_do_cce_idle; the 2000 DRM's
 * r128_submit_packets_pio for the pairs; MIT). With the CCE running the chip takes no MMIO writes to its drawing
 * registers (0x1400-0x1FFF): the 2D engine's go as type-0 packets (engine2d.c). */
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/sys.h"

#define FIFO_SIZE 192                   /* (192PIO's) */

static int running;
uint32_t r128_cce_waits;                /* (PIO: FIFO-room reads that found less room than wanted: RPIO counts them) */
#define BUF_DWORDS 256
static uint32_t buf[BUF_DWORDS];
static int nbuf;
static int room;                        /* (PIO: FIFO entries known free since the last read of PM4_STAT) */
/* The burst window (r128_cce_burst_window): the FIFO page 0x1000-0x13FF, where the FIFO takes dwords at any address
 * (RPIO on the P3), through a mapping that may be write-combining: whole 32-byte lines written in address order,
 * then the CPU's buffers drained, so each line goes as one burst and the lines in order. NULL: EVEN and ODD. */
static volatile uint32_t *burst;

/* A line goes out whole or not at all: with the interrupt flag clear while it is written (an interrupt mid-line
 * flushes the part written as pieces in no set order, and the FIFO takes dwords in the order they come: GLQuake
 * through the page froze the P3, 2026-11-21), then a locked OR to send it before the next. The flag is real only
 * at IOPL 3 (GLOS's direct-mode sessions; elsewhere CLI is emulated and the kernel still takes every interrupt):
 * r128_cce_burst_wc refuses the page otherwise. */
#if defined(__GNUC__) && defined(__i386__)
static void line_in(void) { __asm__ __volatile__("cli" ::: "memory"); }
static void line_out(void) { __asm__ __volatile__("lock; orl $0, (%%esp)\n\tsti" ::: "memory"); }
static int iopl3(void)
{
    uint32_t f;
    __asm__ __volatile__("pushfl\n\tpopl %0" : "=r"(f));
    return ((f >> 12) & 3u) == 3u;
}
#elif defined(__WATCOMC__) && defined(__386__)
static void line_in(void);
#pragma aux line_in = "cli" modify exact [];
static void line_out(void);
#pragma aux line_out = "lock or dword ptr [esp], 0" "sti" modify exact [];
static uint32_t eflags(void);
#pragma aux eflags = "pushfd" "pop eax" value [eax] modify exact [eax];
static int iopl3(void) { return ((eflags() >> 12) & 3u) == 3u; }
#else
static void line_in(void) { }
static void line_out(void) { }
static int iopl3(void) { return 0; }
#endif

void r128_cce_burst_window(volatile void *page) { r128_cce_flush(); burst = (volatile uint32_t *)page; }


/* The ring (r128_cce_init_ring): dwords in VRAM the CCE fetches itself (192BM), the CPU's write index, the read
 * index last seen, the index last given to the chip. NULL: PIO. */
static volatile uint32_t *ring;
static uint32_t ring_dw, ring_mask, wptr, rptr_seen, wptr_given;
#define RING_COMMIT 256                 /* (dwords written before the chip is told of them anyway) */
#define RING_MIRROR 32                  /* (early Rage 128s read 32 dwords past the end before wrapping: the DRM's
                                           R128_BROKEN_CCE; the first 32 kept there too) */

int r128_cce_running(void) { return running; }

static int wait_engine_idle(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    while ((r128_rd(R128_GUI_STAT) & R128_GUI_FIFOCNT_MASK) < 64 || (r128_rd(R128_GUI_STAT) & R128_GUI_ACTIVE))
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return 0;
}

int r128_cce_init(const uint32_t *pairs)
{
    int i;
    if (!r128.mmio)
        return -1;
    if (running)
        r128_cce_stop();
    if (wait_engine_idle(500000) != 0)
        return -1;
    if (!pairs)
        pairs = r128_cce_microcode;
    r128_wr(R128_PM4_MICROCODE_ADDR, 0);
    for (i = 0; i < 256; i++) {
        r128_wr(R128_PM4_MICROCODE_DATAH, pairs[i * 2]);
        r128_wr(R128_PM4_MICROCODE_DATAL, pairs[i * 2 + 1]);     /* (DATAL's write moves the address on) */
    }
    r128_wr(R128_PM4_BUFFER_DL_WPTR, 0);
    r128_wr(R128_PM4_BUFFER_DL_RPTR, 0);
    r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_192PIO | R128_PM4_BUFFER_CNTL_NOUPDATE);
    (void)r128_rd(R128_PM4_BUFFER_ADDR);                         /* ("as per the sample code") */
    r128_wr(R128_PM4_MICRO_CNTL, R128_PM4_MICRO_FREERUN);
    running = 1;
    nbuf = 0;
    room = 0;
    ring = NULL;
    return 0;
}

/* The CCE in 192BM with its ring in system memory, fetched by the chip through its own PCI GART (the DRM's PCI way,
 * r128_cce_init_ring_buffer and drm_ati_pcigart; X.org's R128DRIPciInit for AGP boards: BM_CHUNK_0_VAL's
 * force-to-PCI bits, PCI_GART_PAGE's table): the ring, a mirror page after it (early chips read 32 dwords past the
 * end: the DRM's R128_BROKEN_CCE) and the table's first page in conventional memory (linear = physical), the table
 * 32 KB aligned; ring offset 0 is the table's entry 0. Found on the P3 (RRINGP): it fetches with the force bits,
 * not without them; a ring at a VRAM offset never fetched and left the chip's bus mastering wedged until a power
 * cycle. A first fetch is checked: on failure the CCE is stopped, everything put back, -1 (PIO then). */
static void *ring_block;
static uint32_t saved_gart, saved_chunk, saved_bus;

static void ring_release(void)
{
    if (!ring_block)
        return;
    r128_wr(R128_PCI_GART_PAGE, saved_gart);
    r128_wr(R128_BM_CHUNK_0_VAL, saved_chunk);
    r128_wr(R128_BUS_CNTL, saved_bus);
    sys_dos_free(ring_block);
    ring_block = NULL;
}

int r128_cce_init_ring(const uint32_t *pairs, uint32_t bytes)
{
    int i, l2qw = 0, pages;
    uint16_t seg;
    uint32_t lin, tbl, rb, t0;
    volatile uint32_t *t;
    uint8_t *blk;
    if (!r128.mmio || (bytes & (bytes - 1)) || bytes < 4096u || bytes > (1u << 18))
        return -1;
    if (running)
        r128_cce_stop();
    r128_engine_reset();                /* (as the DRM's r128_do_cce_reset: a PIO CCE before, or a stuck one) */
    if (wait_engine_idle(500000) != 0)
        return -1;
    blk = (uint8_t *)sys_dos_alloc(bytes + 4096u + 4096u + 0x8000u, &seg);
    if (!blk)
        return -1;
    ring_block = blk;
    lin = (uint32_t)seg << 4;
    tbl = (lin + 0x7FFFu) & ~0x7FFFu;           /* (the table: 32 KB aligned) */
    rb = tbl + 4096u;                           /* (the ring, then its mirror page) */
    t = (volatile uint32_t *)(blk + (tbl - lin));
    pages = (int)(bytes / 4096u) + 1;
    for (i = 0; i < 1024; i++)
        t[i] = i < pages ? rb + (uint32_t)i * 4096u : rb;
    if (!pairs)
        pairs = r128_cce_microcode;
    r128_wr(R128_PM4_MICROCODE_ADDR, 0);
    for (i = 0; i < 256; i++) {
        r128_wr(R128_PM4_MICROCODE_DATAH, pairs[i * 2]);
        r128_wr(R128_PM4_MICROCODE_DATAL, pairs[i * 2 + 1]);
    }
    while ((8u << l2qw) < bytes)
        l2qw++;
    ring = (volatile uint32_t *)(blk + (rb - lin));
    ring_dw = bytes / 4u, ring_mask = ring_dw - 1u;
    for (i = 0; i < (int)ring_dw + RING_MIRROR; i++)
        ring[i] = R128_CCE_PACKET2;
    saved_gart = r128_rd(R128_PCI_GART_PAGE);
    saved_chunk = r128_rd(R128_BM_CHUNK_0_VAL);
    saved_bus = r128_rd(R128_BUS_CNTL);
    r128_wr(R128_PCI_GART_PAGE, tbl);           /* (bit 0, PCI_GART_DIS, clear) */
    r128_wr(R128_BM_CHUNK_0_VAL, saved_chunk | R128_BM_FORCE_TO_PCI);
    wptr = rptr_seen = wptr_given = 0;
    r128_wr(R128_PM4_BUFFER_OFFSET, 0);         /* (offset 0 in the GART's space) */
    r128_wr(R128_PM4_BUFFER_DL_WPTR, 0);
    r128_wr(R128_PM4_BUFFER_DL_RPTR, 0);
    r128_wr(R128_PM4_BUFFER_WM_CNTL, (16u / 4u) << 0 | (8u / 4u) << 8 | (8u / 4u) << 16 | (128u / 64u) << 24);
    (void)r128_rd(R128_PM4_BUFFER_ADDR);
    r128_wr(R128_BUS_CNTL, saved_bus & ~R128_BUS_MASTER_DIS);
    r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_192BM | (uint32_t)l2qw | R128_PM4_BUFFER_CNTL_NOUPDATE);
    (void)r128_rd(R128_PM4_BUFFER_ADDR);
    r128_wr(R128_PM4_MICRO_CNTL, R128_PM4_MICRO_FREERUN);
    running = 1;
    nbuf = 0;
    /* The first fetch: eight no-ops, the read index past them within 50 ms. */
    wptr = 8;
    r128_wr(R128_PM4_BUFFER_DL_WPTR, wptr);
    wptr_given = wptr;
    t0 = sys_time_us();
    while ((r128_rd(R128_PM4_BUFFER_DL_RPTR) & ring_mask) != wptr)
        if (sys_time_us() - t0 > 50000u) {
            r128_wr(R128_PM4_MICRO_CNTL, 0);
            r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE);
            running = 0;
            ring = NULL;
            r128_engine_reset();
            ring_release();
            return -1;
        }
    rptr_seen = wptr;
    return 0;
}

int r128_cce_ring(void) { return running && ring != NULL; }

/* Everything sent fetched and the engine idle now (r128_idle's, with the CCE running). */
int r128_cce_drained(void)
{
    uint32_t st;
    r128_cce_flush();
    st = r128_rd(R128_PM4_STAT);
    if (st & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE))
        return 0;
    return ring ? (r128_rd(R128_PM4_BUFFER_DL_RPTR) & ring_mask) == wptr : (int)(st & R128_PM4_FIFOCNT_MASK) >= FIFO_SIZE;
}

/* The ring's new dwords to the chip: out of the CPU's buffers, then the write index. */
static void ring_commit(void)
{
    if (wptr == wptr_given)
        return;
    if (wptr & 1u) {                    /* (the CCE fetches qwords: an odd index is never reached; a type-2 pads it) */
        ring[wptr] = R128_CCE_PACKET2;
        if (wptr < RING_MIRROR)
            ring[ring_dw + wptr] = R128_CCE_PACKET2;
        wptr = (wptr + 1u) & ring_mask;
    }
    /* (the ring is ordinary memory: x86 keeps these stores before the uncached write of the index, and the
     * chip's reads are snooped) */
    r128_wr(R128_PM4_BUFFER_DL_WPTR, wptr);
    wptr_given = wptr;
}

/* Room for n dwords (and the one that keeps the write index off the read index): the read index read again while
 * short, the chip told of what is written first (bounded). */
static void ring_room(int n)
{
    uint32_t t0 = 0;
    int spins = 0;
    while ((int)((rptr_seen - wptr - 1u) & ring_mask) < n) {
        ring_commit();
        rptr_seen = r128_rd(R128_PM4_BUFFER_DL_RPTR) & ring_mask;
        if ((++spins & 255) == 0) {
            if (!t0)
                t0 = sys_time_us();
            else if (sys_time_us() - t0 > 500000u)
                break;                  /* (the engine stuck: r128_sync's time-out reports it) */
        }
    }
}

static void ring_out(const uint32_t *d, int n)
{
    if (n > (int)ring_dw / 2)
        n = (int)ring_dw / 2;
    ring_room(n + 2);                   /* (and the pad a commit may add) */
    while (n-- > 0) {
        uint32_t v = *d++;
        ring[wptr] = v;
        if (wptr < RING_MIRROR)
            ring[ring_dw + wptr] = v;
        wptr = (wptr + 1u) & ring_mask;
    }
    if (((wptr - wptr_given) & ring_mask) >= RING_COMMIT)
        ring_commit();
}

/* The buffered dwords to the FIFO in pairs, a type-2 packet padding an odd count: as many at a time as the FIFO
 * was last seen to have room for, PM4_STAT read again only when that is used up, the clock only while waiting
 * (each a slow read: RPIO on the P3, 183 cycles a dword with a read and a clock per 32, 138 for the writes
 * alone). */
void r128_cce_flush(void)
{
    volatile uint32_t *even = (volatile uint32_t *)(r128.mmio + R128_PM4_FIFO_DATA_EVEN);
    volatile uint32_t *odd = (volatile uint32_t *)(r128.mmio + R128_PM4_FIFO_DATA_ODD);
    int i = 0;
    if (running && ring) {
        ring_commit();
        return;
    }
    if (!running || !nbuf)
        return;
    if (burst) {
        while (nbuf & 7)
            buf[nbuf++] = R128_CCE_PACKET2;
        while (i < nbuf) {
            if (room < 8) {
                uint32_t t0 = 0;
                for (;;) {
                    room = (int)(r128_rd(R128_PM4_STAT) & R128_PM4_FIFOCNT_MASK);
                    if (room >= 8)
                        break;
                    r128_cce_waits++;
                    if (!t0)
                        t0 = sys_time_us();
                    else if (sys_time_us() - t0 >= 200000u)
                        break;
                }
            }
            line_in();
            burst[0] = buf[i], burst[1] = buf[i + 1], burst[2] = buf[i + 2], burst[3] = buf[i + 3];
            burst[4] = buf[i + 4], burst[5] = buf[i + 5], burst[6] = buf[i + 6], burst[7] = buf[i + 7];
            line_out();
            i += 8, room -= 8;
        }
        nbuf = 0;
        return;
    }
    if (nbuf & 1)
        buf[nbuf++] = R128_CCE_PACKET2;
    while (i < nbuf) {
        int k = nbuf - i;
        if (room < 2) {
            uint32_t t0 = 0;
            for (;;) {
                room = (int)(r128_rd(R128_PM4_STAT) & R128_PM4_FIFOCNT_MASK);
                if (room >= 2 && (room >= k || room >= 32))
                    break;
                r128_cce_waits++;
                if (!t0)
                    t0 = sys_time_us();
                else if (sys_time_us() - t0 >= 200000u)
                    break;              /* (stuck: the idle wait's time-out reports it) */
            }
        }
        if (k > (room & ~1))
            k = room & ~1;
        if (k <= 0)
            k = 2;
        room -= k;
        for (; k > 0; k -= 2, i += 2) {
            *even = buf[i];
            *odd = buf[i + 1];
        }
    }
    nbuf = 0;
}

void r128_cce_out(const uint32_t *d, int n)
{
    if (ring) {
        ring_out(d, n);
        return;
    }
    while (n-- > 0) {
        if (nbuf >= BUF_DWORDS - 8)
            r128_cce_flush();
        buf[nbuf++] = *d++;
    }
}

void r128_cce_reg(uint32_t reg, uint32_t v)
{
    uint32_t p[2];
    p[0] = R128_CCE_PACKET0 | (reg >> 2);       /* (one register, one dword: the count field 0) */
    p[1] = v;
    r128_cce_out(p, 2);
}

/* Everything sent drawn: the FIFO empty, the engine idle, then the pixel cache flushed (r128_do_cce_idle). */
int r128_cce_idle_wait(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    r128_cce_flush();
    for (;;) {
        uint32_t st = r128_rd(R128_PM4_STAT);
        if (ring ? (r128_rd(R128_PM4_BUFFER_DL_RPTR) & ring_mask) == wptr && !(st & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE))
                 : (int)(st & R128_PM4_FIFOCNT_MASK) >= FIFO_SIZE && !(st & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE)))
            break;
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    }
    r128_wr(R128_PC_NGUI_CTLSTAT, r128_rd(R128_PC_NGUI_CTLSTAT) | R128_PC_FLUSH_ALL);
    while (r128_rd(R128_PC_NGUI_CTLSTAT) & R128_PC_BUSY)
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return 0;
}

void r128_cce_stop(void)
{
    int had_ring = ring != NULL;
    if (!running)
        return;
    if (had_ring) {
        /* The DRM's order for a bus-master ring (r128_cce_stop: r128_do_cce_flush, _idle, _stop, _reset, then
         * r128_do_engine_reset): the download marked done, the engine idle, the CCE stopped, its pointers zeroed,
         * the engine reset; only then bus mastering off and the ring's memory given back (ring_release). */
        ring_commit();
        r128_wr(R128_PM4_BUFFER_DL_WPTR, wptr | R128_PM4_BUFFER_DL_DONE);
    }
    r128_cce_idle_wait(500000);
    r128_wr(R128_PM4_MICRO_CNTL, 0);
    r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE);
    running = 0;
    ring = NULL;
    burst = NULL;
    if (had_ring) {
        r128_wr(R128_PM4_BUFFER_DL_WPTR, 0);
        r128_wr(R128_PM4_BUFFER_DL_RPTR, 0);
        r128_engine_reset();            /* (r128_cce_forget's ring_release in it) */
    }
    ring_release();
}

/* (after an engine reset the CCE is stopped: the DRM's r128_do_engine_reset) */
void r128_cce_forget(void) { running = 0; nbuf = 0; room = 0; ring = NULL; burst = NULL; ring_release(); }

/* The burst window write-combining: the second register aperture's FIFO page (BAR2 + 0x3000: aperture 1 at
 * 0x2000, its FIFO at 0x1000) made write-combining by GLOS (API 2.11's DRAW CACHE for a display BAR's page; the
 * first aperture's pages stay uncached), then PIO through it. 0, or -1 (no GLOS, an older one, or refused:
 * sys_cache_wc_err) and the EVEN/ODD path kept. */
int r128_cce_burst_wc(void)
{
    volatile uint8_t *page;
    if (!running || ring || !r128.mmio)
        return -1;
    if (!iopl3()) {
        sys_cache_wc_err = -2;          /* (no real interrupt flag: lines could be split by an interrupt) */
        return -1;
    }
    page = r128.mmio + 0x3000;
    if (sys_cache_type(sys_linear(page), 4096u, 1u) != 0)
        return -1;
    r128_cce_burst_window(page);
    return 0;
}
