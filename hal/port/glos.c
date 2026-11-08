/* glos.c - HAL port for GLOS's kernel (GLOS M7; PRD Appendix B).
 *
 * Built only inside GLOS, against its kernel headers, from this checkout
 * (GLOS's RETROGPU), with the part of the HAL that uses
 * no FPU: PCI, the chip's capabilities, the FIFO, the engine, 2D drawing,
 * the cursor, the display start and the VRAM allocator. GLOS sets modes
 * with its own VBE code, so the real-mode calls and conventional memory
 * that the HAL's vbe.c needs are not provided; nor are the fault and exit
 * hooks, which are a DOS program's.
 *
 * Physical ranges are mapped into the kernel's device region (mm_dev_map),
 * uncached. The clock is the kernel's: 1024 ticks a second, so times are
 * good to a millisecond, which is all the HAL asks of them (its timeouts). */
#include "mga/sys.h"

#include "io.h"
#include "mm.h"
#include "timer.h"

uint8_t sys_inb(uint16_t port) { return inb(port); }
uint16_t sys_inw(uint16_t port) { return inw(port); }
uint32_t sys_inl(uint16_t port) { return inl(port); }
void sys_outb(uint16_t port, uint8_t v) { outb(port, v); }
void sys_outw(uint16_t port, uint16_t v) { outw(port, v); }
void sys_outl(uint16_t port, uint32_t v) { outl(port, v); }

volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    return mm_dev_map(phys, size);
}

void sys_unmap_phys(volatile void *p, uint32_t size)
{
    mm_dev_unmap((void *)p, size);
}

uint32_t sys_time_us(void)
{
    return timer_ticks() * 15625u / 16u;        /* (976.5625 us a tick) */
}
uint32_t sys_time_backs;                /* (never moves: the kernel's tick doesn't go back) */

/* An ISA cycle a microsecond, near enough: the delays the HAL asks for
   (an engine reset's 20 us) are minimums. */
void sys_delay_us(uint32_t us)
{
    while (us--)
        outb(0x80, 0);
}

void *sys_alloc(uint32_t bytes) { return kmalloc(bytes); }
void sys_free(void *p) { kfree(p); }

int sys_rm_int(uint8_t intno, sys_rmregs *r)
{
    (void)intno;
    (void)r;
    return -1;
}

void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment)
{
    (void)bytes;
    (void)rm_segment;
    return 0;
}

void sys_dos_free(void *p) { (void)p; }
void *sys_real_ptr(uint16_t seg, uint16_t off) { return (void *)(((uint32_t)seg << 4) + off); }
int sys_hook_faults(sys_fault_fn fn) { (void)fn; return -1; }
void sys_unhook_faults(void) {}
int sys_hook_exit(sys_exit_fn fn) { (void)fn; return -1; }
void sys_unhook_exit(void) {}
int sys_cache_wc_err = -1;
int sys_cache_wc(uint32_t lin, uint32_t bytes) { (void)lin; (void)bytes; return -1; }
