/* fifo.c - FIFO pacing: every burst of register writes reserves its slots
 * here first, against the depth the chip reports (PRD R3). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/prof.h"

#ifdef MGA_PROF
uint64_t prof_cyc[PROF_N];
uint32_t prof_n[PROF_N];
uint64_t prof_t;
int      prof_cur;
uint32_t prof_wr, prof_fifo_rd;
uint64_t prof_wcyc[PROF_W_N];
uint32_t prof_wn[PROF_W_N], prof_fl[PROF_FL_N];
uint32_t prof_nc_off[8], prof_nc_n[8];
#endif

int mga_fifo_free;                      /* slots known free (fifo_need's fast path) */

void fifo_reset(void) { mga_fifo_free = 0; }

void fifo_reserve(int n)
{
    uint32_t spin = 0;
    if (mga_cmd_open && mga_dma_pending) {
        /* Queued in the DMA list (which makes its own room), or a write
         * the list goes before (MGA_WR32): no FIFO slots to count. */
        mga_fifo_free = 1 << 24;
        return;
    }
    if (mga_dma_pending && mga_dma_flush)
        mga_dma_flush();                /* (sets mga_fifo_free to 0: read below) */
#ifdef MGA_PROF
    int o;
    if (mga_fifo_free >= n) {
        mga_fifo_free -= n;
        return;
    }
    o = PROF_SWITCH(PROF_FIFO);
#endif
    while (mga_fifo_free < n) {
        mga_fifo_free = MGA_RD8(MGAREG_FIFOSTATUS) & mga.fifo_mask;
#ifdef MGA_PROF
        prof_fifo_rd++;
#endif
        if (++spin > 20000000u) {        /* engine wedged: let the caller notice via sync */
            mga_fifo_free = n;
            break;
        }
    }
    mga_fifo_free -= n;
#ifdef MGA_PROF
    prof_wcyc[PROF_W_FIFO] += prof_rdtsc() - prof_t;
    prof_wn[PROF_W_FIFO]++;
    PROF_BACK(o);
#endif
}
