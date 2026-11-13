/* glos.c - GLOS's API 2.0 for RetroGPU's devices (rg/glos.h; gl/docs/b1.md):
 * finding GLOS, what the machine can do, its calls, and GLOS's GPU lock
 * (rg_lock). Moved from DOS-GL's src/dgl/glos.c in B1; a device's window
 * (the Matrox one's: mga_glos.c) is built on these.
 *
 * GLOS's own numbers and structures come from its checkout (GLOS in DOS-GL's
 * config.mk); the calls are made here, not through libglos. */
#include "rg/glos.h"
#include <string.h>

rg_gpu_lock rg_lock;

#if defined(__DJGPP__) && !defined(DGL_RIG)
#include <dpmi.h>
#include <sys/nearptr.h>

static struct {
    unsigned long off;
    unsigned short sel;
} entry;
static int found;                       /* 1 GLOS, -1 none */

/* An API 2.0 call: the function in EAX, its structure (its size first) in EBX; the carry flag says it failed,
   with GLOS's error in EAX. 0, or the GLOS_E_ code. */
int rg_glos_call(unsigned fn, void *s)
{
    unsigned a = fn, b = (unsigned)s, c = 0, d = 0;
    unsigned char failed;
    __asm__ __volatile__("lcall *%[e]\n\tsetc %[f]"
                         : "+a"(a), "+b"(b), "+c"(c), "+d"(d), [f] "=m"(failed)
                         : [e] "m"(entry)
                         : "esi", "edi", "cc", "memory");
    return failed ? (int)a : 0;
}

/* What the machine can do (GLOS_MACHINE_), 0 without a GLOS that speaks 2.0. */
unsigned rg_glos_caps(void)
{
    static struct glos_version v;
    __dpmi_paddr p;
    if (!found) {
        found = -1;
        if (__dpmi_get_vendor_specific_api_entry_point("GLOS", &p) == 0) {
            entry.off = p.offset32;
            entry.sel = p.selector;
            found = 1;
        }
    }
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    if (found < 0 || rg_glos_call(GLOS_CORE_VERSION, &v) != 0 || v.major != GLOS_MAJOR)
        return 0;
    return v.machine;
}

void *rg_glos_ptr(unsigned linear)      /* a linear address GLOS gave, as a near pointer */
{
    if (!__djgpp_nearptr_enable())
        return NULL;
    return (void *)(linear + __djgpp_conventional_base);
}

#else   /* the rig and the host: no GLOS */

int rg_glos_call(unsigned fn, void *s) { (void)fn; (void)s; return -1; }
unsigned rg_glos_caps(void) { return 0; }
void *rg_glos_ptr(unsigned linear) { (void)linear; return NULL; }

#endif
