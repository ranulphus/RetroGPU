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
static uint32_t buf[64];
static int nbuf;

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
    return 0;
}

/* The buffered dwords to the FIFO in pairs, a type-2 packet padding an odd count, room waited for (bounded). */
void r128_cce_flush(void)
{
    int i = 0;
    if (!running || !nbuf)
        return;
    if (nbuf & 1)
        buf[nbuf++] = R128_CCE_PACKET2;
    while (i < nbuf) {
        uint32_t t0 = sys_time_us();
        int k = nbuf - i;
        if (k > 32)
            k = 32;
        while ((int)(r128_rd(R128_PM4_STAT) & R128_PM4_FIFOCNT_MASK) < k && sys_time_us() - t0 < 200000u)
            ;
        for (; k > 0; k -= 2, i += 2) {
            r128_wr(R128_PM4_FIFO_DATA_EVEN, buf[i]);
            r128_wr(R128_PM4_FIFO_DATA_ODD, buf[i + 1]);
        }
    }
    nbuf = 0;
}

void r128_cce_out(const uint32_t *d, int n)
{
    while (n-- > 0) {
        if (nbuf >= 62)
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
        if ((int)(st & R128_PM4_FIFOCNT_MASK) >= FIFO_SIZE && !(st & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE)))
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
}

/* (after an engine reset the CCE is stopped: the DRM's r128_do_engine_reset) */
void r128_cce_forget(void) { running = 0; nbuf = 0; }
