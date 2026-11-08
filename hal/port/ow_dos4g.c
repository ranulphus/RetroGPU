/* ow_dos4g.c - HAL port for Open Watcom code running inside a DOS/4GW
 * process (the MGA-Glide runtime and its DOS test programs).
 *
 * DOS/4GW runs a flat, zero-based model: a linear address returned by
 * DPMI 0800h is directly usable as a near pointer. */
#include "mga/sys.h"
#include <conio.h>
#include <i86.h>
#include <string.h>

uint8_t  sys_inb(uint16_t p) { return (uint8_t)inp(p); }
uint16_t sys_inw(uint16_t p) { return (uint16_t)inpw(p); }
uint32_t sys_inl(uint16_t p) { return (uint32_t)inpd(p); }
void sys_outb(uint16_t p, uint8_t v) { outp(p, v); }
void sys_outw(uint16_t p, uint16_t v) { outpw(p, v); }
void sys_outl(uint16_t p, uint32_t v) { outpd(p, v); }

volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0800;
    r.w.bx = (uint16_t)(phys >> 16);
    r.w.cx = (uint16_t)(phys & 0xFFFF);
    r.w.si = (uint16_t)(size >> 16);
    r.w.di = (uint16_t)(size & 0xFFFF);
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return NULL;
    return (volatile void *)(((uint32_t)r.w.bx << 16) | r.w.cx);
}

void sys_unmap_phys(volatile void *p, uint32_t size)
{
    union REGS r;
    MGA_UNUSED(size);
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0801;
    r.w.bx = (uint16_t)((uint32_t)p >> 16);
    r.w.cx = (uint16_t)((uint32_t)p & 0xFFFF);
    int386(0x31, &r, &r);
}

int sys_rm_int(uint8_t intno, sys_rmregs *rm)
{
    union REGS r;
    struct SREGS s;
    memset(&r, 0, sizeof r);
    segread(&s);
    r.w.ax = 0x0300;
    r.h.bl = intno;
    r.h.bh = 0;
    r.w.cx = 0;
    s.es = s.ds;
    r.x.edi = (uint32_t)rm;
    int386x(0x31, &r, &r, &s);
    return r.x.cflag ? -1 : 0;
}

/* Conventional memory: DPMI 0100h returns a real-mode segment and a
 * selector; in the flat model the linear address is segment * 16. */
#define DOS_BLOCKS 8
static struct { void *p; uint16_t sel; } dos_blocks[DOS_BLOCKS];

void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment)
{
    union REGS r;
    int i;
    for (i = 0; i < DOS_BLOCKS && dos_blocks[i].p; i++)
        ;
    if (i == DOS_BLOCKS)
        return NULL;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0100;
    r.w.bx = (uint16_t)((bytes + 15) >> 4);
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return NULL;
    *rm_segment = r.w.ax;
    dos_blocks[i].p = (void *)((uint32_t)r.w.ax << 4);
    dos_blocks[i].sel = r.w.dx;
    return dos_blocks[i].p;
}

void sys_dos_free(void *p)
{
    union REGS r;
    int i;
    for (i = 0; i < DOS_BLOCKS; i++) {
        if (dos_blocks[i].p == p) {
            memset(&r, 0, sizeof r);
            r.w.ax = 0x0101;
            r.w.dx = dos_blocks[i].sel;
            int386(0x31, &r, &r);
            dos_blocks[i].p = NULL;
            return;
        }
    }
}

/* Monotonic time from the BIOS tick counter at 0040:006C (18.2 Hz). The
 * PIT cannot be used for sub-tick resolution: the BIOS runs channel 0 in
 * mode 3, which counts through its range twice per tick, so derived time
 * would run backwards. Timeouts only need tick resolution. */
uint32_t sys_time_us(void)
{
    volatile uint32_t *ticks = (volatile uint32_t *)0x46C;
    return (uint32_t)((uint64_t)*ticks * 54925u);
}
uint32_t sys_time_backs;                /* (not counted: the BIOS tick goes back only at midnight) */

/* Short delays: each read of port 0x80 takes about a microsecond on ISA
 * timing; longer delays wait on the tick counter. */
void sys_delay_us(uint32_t us)
{
    if (us >= 55000u) {
        uint32_t start = sys_time_us();
        while ((uint32_t)(sys_time_us() - start) < us)
            ;
        return;
    }
    while (us--)
        (void)inp(0x80);
}

/* DPMI 0501h block allocator: simple bump arena with a free list is
 * provided by glide/src/rt for the runtime; the HAL only needs a few tables. */
void *sys_alloc(uint32_t bytes)
{
    union REGS r;
    uint32_t *blk;
    uint32_t total = bytes + 8;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0501;
    r.w.bx = (uint16_t)(total >> 16);
    r.w.cx = (uint16_t)(total & 0xFFFF);
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return NULL;
    blk = (uint32_t *)(((uint32_t)r.w.bx << 16) | r.w.cx);
    blk[0] = ((uint32_t)r.w.si << 16) | r.w.di;   /* DPMI handle */
    return blk + 2;
}

void sys_free(void *p)
{
    union REGS r;
    uint32_t h;
    if (!p)
        return;
    h = ((uint32_t *)p)[-2];
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0502;
    r.w.si = (uint16_t)(h >> 16);
    r.w.di = (uint16_t)(h & 0xFFFF);
    int386(0x31, &r, &r);
}

void *sys_real_ptr(uint16_t seg, uint16_t off) { return (void *)(((uint32_t)seg << 4) + off); }

/* ---- Fault and exit hooks ----------------------------------------------
 * DPMI 0202h/0203h (processor exceptions) and 0204h/0205h (protected-mode
 * interrupts). The stubs are entered with the host's selectors and stack:
 * they load our DS from a CS-relative copy, move to a private stack (the
 * C code assumes SS = DS in the flat model), call the C side, restore
 * everything and jump to the previous handler, so the host (or the game's
 * own handler) still sees the event exactly as before. */

#pragma pack(push, 1)
typedef struct { uint32_t off; uint16_t sel; } far48;
#pragma pack(pop)

#define HOOK_STACK 16384
static uint8_t  hook_stack[HOOK_STACK];
static uint16_t our_ds;
static uint32_t hk_save_esp;
static uint16_t hk_save_ss;
static int      hk_exc, hk_busy;
static uint32_t hk_err, hk_eip;
static sys_fault_fn fault_fn;
static sys_exit_fn  exit_fn;
static far48 old_de, old_ud, old_gp, old_pf, old_21;
static int faults_on, exit_on;

static void fault_dispatch(void)
{
    if (hk_busy || !fault_fn)
        return;
    hk_busy = 1;
    fault_fn(hk_exc, hk_err, hk_eip);
    hk_busy = 0;
}

static void exit_dispatch(void)
{
    if (hk_busy || !exit_fn)
        return;
    hk_busy = 1;
    exit_fn();
    hk_busy = 0;
}

/* Common body: the stub pushed the exception number and called us. The
 * DPMI exception frame follows: return EIP/CS, error code, EIP, CS... */
static void __declspec(naked) fault_body(void)
{
    _asm {
        pushad
        push    ds
        push    es
        mov     bx, word ptr cs:our_ds
        mov     eax, [esp + 44]
        mov     ecx, [esp + 56]
        mov     edx, [esp + 60]
        mov     ds, bx
        mov     es, bx
        mov     hk_exc, eax
        mov     hk_err, ecx
        mov     hk_eip, edx
        mov     hk_save_ss, ss
        mov     hk_save_esp, esp
        mov     ss, bx
        mov     esp, offset hook_stack
        add     esp, HOOK_STACK - 16
        call    fault_dispatch
        mov     ss, hk_save_ss
        mov     esp, hk_save_esp
        pop     es
        pop     ds
        popad
        ret
    }
}

#define FAULT_STUB(name, num, old) \
    static void __declspec(naked) name(void) \
    { \
        _asm { push num } \
        _asm { call fault_body } \
        _asm { add esp, 4 } \
        _asm { jmp fword ptr cs:old } \
    }

FAULT_STUB(stub_de, 0x00, old_de)
FAULT_STUB(stub_ud, 0x06, old_ud)
FAULT_STUB(stub_gp, 0x0D, old_gp)
FAULT_STUB(stub_pf, 0x0E, old_pf)

static void __declspec(naked) stub_21(void)
{
    _asm {
        cmp     ah, 0x4C
        jne     chain
        pushad
        push    ds
        push    es
        mov     bx, word ptr cs:our_ds
        mov     ds, bx
        mov     es, bx
        mov     hk_save_ss, ss
        mov     hk_save_esp, esp
        mov     ss, bx
        mov     esp, offset hook_stack
        add     esp, HOOK_STACK - 16
        call    exit_dispatch
        mov     ss, hk_save_ss
        mov     esp, hk_save_esp
        pop     es
        pop     ds
        popad
    chain:
        jmp     fword ptr cs:old_21
    }
}

static uint16_t get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax];
static uint16_t get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax];

static int dpmi_get(uint16_t fn, int n, far48 *v)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = fn;
    r.h.bl = (uint8_t)n;
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return -1;
    v->sel = r.w.cx;
    v->off = r.x.edx;
    return 0;
}

static int dpmi_set(uint16_t fn, int n, uint16_t sel, uint32_t off)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = fn;
    r.h.bl = (uint8_t)n;
    r.w.cx = sel;
    r.x.edx = off;
    int386(0x31, &r, &r);
    return r.x.cflag ? -1 : 0;
}

int sys_hook_faults(sys_fault_fn fn)
{
    uint16_t cs = get_cs();
    fault_fn = fn;
    if (faults_on)
        return 0;
    our_ds = get_ds();
    if (dpmi_get(0x0202, 0x00, &old_de) || dpmi_get(0x0202, 0x06, &old_ud) ||
        dpmi_get(0x0202, 0x0D, &old_gp) || dpmi_get(0x0202, 0x0E, &old_pf))
        return -1;
    dpmi_set(0x0203, 0x00, cs, (uint32_t)stub_de);
    dpmi_set(0x0203, 0x06, cs, (uint32_t)stub_ud);
    dpmi_set(0x0203, 0x0D, cs, (uint32_t)stub_gp);
    dpmi_set(0x0203, 0x0E, cs, (uint32_t)stub_pf);
    faults_on = 1;
    return 0;
}

void sys_unhook_faults(void)
{
    if (!faults_on)
        return;
    dpmi_set(0x0203, 0x00, old_de.sel, old_de.off);
    dpmi_set(0x0203, 0x06, old_ud.sel, old_ud.off);
    dpmi_set(0x0203, 0x0D, old_gp.sel, old_gp.off);
    dpmi_set(0x0203, 0x0E, old_pf.sel, old_pf.off);
    faults_on = 0;
}

void sys_terminate(int code)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = (uint16_t)(0x4C00 | (code & 0xFF));
    int386(0x21, &r, &r);
    for (;;)
        ;
}

int sys_hook_exit(sys_exit_fn fn)
{
    exit_fn = fn;
    if (exit_on)
        return 0;
    our_ds = get_ds();
    if (dpmi_get(0x0204, 0x21, &old_21))
        return -1;
    if (dpmi_set(0x0205, 0x21, get_cs(), (uint32_t)stub_21))
        return -1;
    exit_on = 1;
    return 0;
}

void sys_unhook_exit(void)
{
    if (!exit_on)
        return;
    dpmi_set(0x0205, 0x21, old_21.sel, old_21.off);
    exit_on = 0;
}

/* GLOS_FN_CACHE (sys.h): through the entry DPMI 0A00h "GLOS" gives (DOS/4GW
 * is then GLOS's DPMI client). */
static struct { uint32_t off; uint16_t sel; } glos_entry;
static int glos_found;                  /* 1 GLOS, -1 none */
int sys_cache_wc_err;
int glos_far(void *entry, uint32_t fn, uint32_t b, uint32_t c, uint32_t d);    /* 0, or GLOS's error code */
#pragma aux glos_far = \
    "call fword ptr [esi]" \
    "sbb edx, edx" \
    "and eax, edx" \
    parm [esi] [eax] [ebx] [ecx] [edx] value [eax] modify [ebx ecx edx esi edi];

int sys_cache_wc(uint32_t lin, uint32_t bytes)
{
    static const char name[] = "GLOS";
    union REGS r;
    struct SREGS sr;
    if (!glos_found) {
        glos_found = -1;
        memset(&r, 0, sizeof r);
        segread(&sr);
        r.w.ax = 0x0A00;
        r.x.esi = (uint32_t)name;
        int386x(0x31, &r, &r, &sr);
        if (!r.x.cflag) {
            glos_entry.off = r.x.edi;
            glos_entry.sel = sr.es;
            glos_found = 1;
        }
    }
    if (glos_found < 0) {
        sys_cache_wc_err = -1;
        return -1;
    }
    sys_cache_wc_err = glos_far(&glos_entry, 33, lin, bytes, 1);   /* GLOS_FN_CACHE, GLOS_CACHE_WC */
    return sys_cache_wc_err ? -1 : 0;
}
