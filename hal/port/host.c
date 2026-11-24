/* host.c - HAL port for Linux host builds (unit tests, trace replay).
 *
 * There is no hardware: port I/O goes to an optional hook (the simulated
 * PCI configuration space and UART), physical mappings come from regions
 * registered by the test harness, and real-mode calls fail. */
#include "mga/sys.h"
#include "mga/host.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static host_io_hooks io_hooks;

void host_set_io_hooks(const host_io_hooks *h)
{
    if (h) io_hooks = *h; else memset(&io_hooks, 0, sizeof io_hooks);
}

static uint32_t in_n(uint16_t p, int size)
{
    return io_hooks.in ? io_hooks.in(io_hooks.ctx, p, size) : 0xFFFFFFFFu;
}

static void out_n(uint16_t p, uint32_t v, int size)
{
    if (io_hooks.out)
        io_hooks.out(io_hooks.ctx, p, v, size);
}

uint8_t  sys_inb(uint16_t p) { return (uint8_t)in_n(p, 1); }
uint16_t sys_inw(uint16_t p) { return (uint16_t)in_n(p, 2); }
uint32_t sys_inl(uint16_t p) { return in_n(p, 4); }
void sys_outb(uint16_t p, uint8_t v) { out_n(p, v, 1); }
void sys_outw(uint16_t p, uint16_t v) { out_n(p, v, 2); }
void sys_outl(uint16_t p, uint32_t v) { out_n(p, v, 4); }

#define HOST_REGIONS 8
static struct { uint32_t phys, size; void *mem; } regions[HOST_REGIONS];

void host_register_phys(uint32_t phys, uint32_t size, void *mem)
{
    int i;
    for (i = 0; i < HOST_REGIONS; i++) {
        if (!regions[i].mem || regions[i].phys == phys) {
            regions[i].phys = phys;
            regions[i].size = size;
            regions[i].mem = mem;
            return;
        }
    }
}

void host_clear_phys(void) { memset(regions, 0, sizeof regions); }

volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    int i;
    for (i = 0; i < HOST_REGIONS; i++) {
        if (regions[i].mem && phys >= regions[i].phys &&
            (uint64_t)phys + size <= (uint64_t)regions[i].phys + regions[i].size)
            return (uint8_t *)regions[i].mem + (phys - regions[i].phys);
    }
    return NULL;
}

void sys_unmap_phys(volatile void *p, uint32_t size) { MGA_UNUSED(p); MGA_UNUSED(size); }

/* Real-mode interrupts go to an optional hook (the host trace replayer
 * answers the VBE calls); conventional memory is a private first megabyte. */
static host_rm_hook rm_hook;
static void *rm_ctx;
static uint8_t *lowmem;
static uint32_t lowmem_next = 0x10000;

void host_set_rm_hook(host_rm_hook fn, void *ctx) { rm_hook = fn; rm_ctx = ctx; }

int sys_rm_int(uint8_t intno, sys_rmregs *r) { return rm_hook ? rm_hook(rm_ctx, intno, r) : -1; }

void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment)
{
    uint8_t *p;
    if (!lowmem && !(lowmem = (uint8_t *)calloc(1, 1u << 20)))
        return NULL;
    if (lowmem_next + bytes > 0xA0000u)
        return NULL;
    p = lowmem + lowmem_next;
    *rm_segment = (uint16_t)(lowmem_next >> 4);
    lowmem_next = (lowmem_next + bytes + 15u) & ~15u;
    return p;
}

void sys_dos_free(void *p) { MGA_UNUSED(p); }

uint32_t sys_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}
uint32_t sys_time_backs;                /* (never moves: CLOCK_MONOTONIC doesn't go back) */

void sys_delay_us(uint32_t us) { MGA_UNUSED(us); }

void *sys_alloc(uint32_t bytes) { return calloc(1, bytes); }
void sys_free(void *p) { free(p); }

void *sys_real_ptr(uint16_t seg, uint16_t off)
{
    if (!lowmem && !(lowmem = (uint8_t *)calloc(1, 1u << 20)))
        return NULL;
    return lowmem + (((uint32_t)seg << 4) + off);
}

int  sys_hook_faults(sys_fault_fn fn) { (void)fn; return -1; }
void sys_unhook_faults(void) { }
int  sys_hook_exit(sys_exit_fn fn) { (void)fn; return -1; }
void sys_terminate(int code) { exit(code); }
void sys_unhook_exit(void) { }
int sys_cache_wc_err = -1;
int sys_cache_wc(uint32_t lin, uint32_t bytes) { (void)lin; (void)bytes; return -1; }
int sys_cache_type(uint32_t lin, uint32_t bytes, unsigned type) { (void)lin; (void)bytes; (void)type; return -1; }
uint32_t sys_linear(const volatile void *p) { return (uint32_t)(uintptr_t)p; }
