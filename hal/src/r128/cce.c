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

#if defined(__GNUC__) && defined(__i386__)
static void line_out(void) { __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory"); }
#elif defined(__WATCOMC__) && defined(__386__)
static void line_out(void);
#pragma aux line_out = "lock or dword ptr [esp], 0" modify exact [];
#else
static void line_out(void) { }
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

/* The CCE in 192BM with its ring in VRAM at off (bytes a power of two, 4 KB to 1 MB, and RING_MIRROR dwords more
 * after it), as the DRM's r128_cce_init_ring_buffer: the ring's address in the chip's own space, where VRAM is
 * below 32 MB (AGP from R128_AGP_OFFSET); no read-pointer write-back (NOUPDATE: the register is read). */
int r128_cce_init_ring(const uint32_t *pairs, uint32_t off, uint32_t bytes)
{
    int i, l2qw = 0;
    if (!r128.mmio || (bytes & (bytes - 1)) || bytes < 4096u || bytes > (1u << 20) || (off & 4095u) ||
        off + bytes + RING_MIRROR * 4u > r128.vram_bytes)
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
        r128_wr(R128_PM4_MICROCODE_DATAL, pairs[i * 2 + 1]);
    }
    while ((8u << l2qw) < bytes)
        l2qw++;
    ring = (volatile uint32_t *)(r128.fb + off);
    ring_dw = bytes / 4u, ring_mask = ring_dw - 1u;
    for (i = 0; i < (int)ring_dw + RING_MIRROR; i++)
        ring[i] = R128_CCE_PACKET2;
    r128_vram_written(off + bytes + RING_MIRROR * 4u - 4u);
    wptr = rptr_seen = wptr_given = 0;
    r128_wr(R128_PM4_BUFFER_OFFSET, off);
    r128_wr(R128_PM4_BUFFER_DL_WPTR, 0);
    r128_wr(R128_PM4_BUFFER_DL_RPTR, 0);
    r128_wr(R128_PM4_BUFFER_WM_CNTL, (16u / 4u) << 0 | (8u / 4u) << 8 | (8u / 4u) << 16 | (128u / 64u) << 24);
    r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_192BM | (uint32_t)l2qw | R128_PM4_BUFFER_CNTL_NOUPDATE);
    (void)r128_rd(R128_PM4_BUFFER_ADDR);
    r128_wr(R128_PM4_MICRO_CNTL, R128_PM4_MICRO_FREERUN);
    running = 1;
    nbuf = 0;
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
    r128_vram_written((uint32_t)((volatile uint8_t *)ring - r128.fb) + (wptr ? wptr - 1u : ring_dw - 1u) * 4u);
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
    ring_room(n);
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
    if (!running)
        return;
    r128_cce_idle_wait(500000);
    r128_wr(R128_PM4_MICRO_CNTL, 0);
    r128_wr(R128_PM4_BUFFER_CNTL, R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE);
    running = 0;
    ring = NULL;
    burst = NULL;
}

/* (after an engine reset the CCE is stopped: the DRM's r128_do_engine_reset) */
void r128_cce_forget(void) { running = 0; nbuf = 0; room = 0; ring = NULL; burst = NULL; }
