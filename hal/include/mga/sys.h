/* sys.h - platform port interface for the Matrox HAL.
 *
 * One implementation per environment (hal/port/):
 *   ow_dos4g.c  Open Watcom inside a DOS/4GW process (MGA-Glide runtime)
 *   djgpp.c     DJGPP + CWSDPMI (DOS-GL)
 *   host.c      Linux host: fake PCI + VRAM for unit tests and replay
 *   linux.c     Linux userspace on real hardware via sysfs (cuda6 rig)
 *   glos.c      GLOS's kernel (built only there, with the FPU-free files:
 *               no real-mode calls, conventional memory or hooks)
 */
#ifndef MGA_SYS_H
#define MGA_SYS_H
#include "mga/types.h"

/* Port I/O. */
uint8_t  sys_inb(uint16_t port);
uint16_t sys_inw(uint16_t port);
uint32_t sys_inl(uint16_t port);
void     sys_outb(uint16_t port, uint8_t v);
void     sys_outw(uint16_t port, uint16_t v);
void     sys_outl(uint16_t port, uint32_t v);

/* Map a physical range (PCI aperture) and return a CPU pointer to it.
 * Returns NULL on failure. */
volatile void *sys_map_phys(uint32_t phys, uint32_t size);
void sys_unmap_phys(volatile void *p, uint32_t size);

/* Real-mode interrupt call (VBE, PCI BIOS). Segment registers and the
 * general registers use the DPMI "real mode call structure" layout. */
typedef struct {
    uint32_t edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    uint16_t flags, es, ds, fs, gs, ip, cs, sp, ss;
} sys_rmregs;
int sys_rm_int(uint8_t intno, sys_rmregs *r);

/* Conventional-memory buffer for real-mode calls: returns a linear pointer
 * and the real-mode segment. */
void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment);
void  sys_dos_free(void *p);

/* GLOS (API 2.0, DRAW CACHE; 1.4 had it as GLOS_FN_CACHE): conventional memory the program has from
 * DOS made write-combining, `lin` its linear address (the segment times
 * 16), whole 64 KB units below 80000h. 0, or -1 without GLOS or when it
 * can't. */
int sys_cache_wc(uint32_t lin, uint32_t bytes);
extern int sys_cache_wc_err;    /* why the last call failed: -1 no GLOS, else GLOS's error code */

/* Pointer to real-mode memory seg:off (first megabyte). */
void *sys_real_ptr(uint16_t seg, uint16_t off);

/* Monotonic microsecond-ish clock for timeouts (resolution varies). */
uint32_t sys_time_us(void);
void     sys_delay_us(uint32_t us);
extern uint32_t sys_time_backs; /* times the port's clock read behind its last reading and was held (DJGPP's uclock) */

/* Fault and exit hooks. The fault hook runs for divide error, invalid
 * opcode, #GP and #PF, on a private stack, with the exception number,
 * error code and faulting EIP; the exit hook runs when the program asks
 * DOS to terminate (INT 21h, AH=4Ch). Both always chain to the previous
 * handler afterwards. Ports without them return -1. */
typedef void (*sys_fault_fn)(int exc, uint32_t err, uint32_t eip);
typedef void (*sys_exit_fn)(void);
int  sys_hook_faults(sys_fault_fn fn);
void sys_unhook_faults(void);
int  sys_hook_exit(sys_exit_fn fn);
void sys_unhook_exit(void);

/* End the whole program now with this exit code (DOS: INT 21h, AH=4Ch, so
 * an installed exit hook runs first). Does not return. */
void sys_terminate(int code);

/* Plain memory for HAL-owned tables (not VRAM). */
void *sys_alloc(uint32_t bytes);
void  sys_free(void *p);

#endif
