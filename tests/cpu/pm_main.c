/* V86TEST monitor (32-bit, ring 0, paging on): sets up its own GDT, IDT,
 * TSS (with the VME redirection bitmap and the I/O permission bitmap) and a
 * #DF task, then runs the CPU cases and reports each as
 *     HX-TEST cpu-<case>-<name> PASS|FAIL|INFO <detail>
 * Expectations are those of real Intel silicon (SDM vol. 3, ch. 6, 20, 21);
 * 86Box's known deviations are listed in tests/cpu/known-86box.txt.
 * Built with gcc -m32 -march=i486 -ffreestanding, linked at V86PM_BASE. */
#include "v86pm.h"

typedef unsigned short u16;
typedef unsigned char u8;

#define EF_CF   0x00000001u
#define EF_IF   0x00000200u
#define EF_TF   0x00000100u
#define EF_OF   0x00000800u
#define EF_IOPL 0x00003000u
#define EF_NT   0x00004000u
#define EF_VM   0x00020000u
#define EF_VIF  0x00080000u
#define EF_VIP  0x00100000u
#define EF_ID   0x00200000u

#define CR0_EM  0x00000004u
#define CR0_TS  0x00000008u
#define CR0_NE  0x00000020u
#define CR0_WP  0x00010000u

#define CR4_VME 0x001u
#define CR4_PVI 0x002u
#define CR4_PGE 0x080u

#define SEL_TSS     0x28
#define SEL_DFTSS   0x30
#define SEL_UCODE   0x3B
#define SEL_UDATA   0x43
#define SEL_USTACK16 0x4B
#define SEL_LIMBIG  0x50                        /* case V: 4 GB data */
#define SEL_LIMSMALL 0x58                       /* case V: 4 KB data */
#define SEL_ZLDT    0x60                        /* case Z: an LDT that ends a page early */

#define TEST_LIN    0x007F0000u                 /* a page in the 4-8 MB table for mapping tests */

struct uregs { u32 eax, ebx, ecx, edx, esi, edi, ebp, eip, cs, eflags, esp, ss, es, ds, fs, gs; };
struct frame {
    u32 edi, esi, ebp, kesp, ebx, edx, ecx, eax;        /* pushal */
    u32 gs, fs, es, ds, eax2, vec, err;                 /* trap_common */
    u32 eip, cs, eflags, esp, ss, v_es, v_ds, v_fs, v_gs; /* the CPU */
};
struct tss {
    u32 link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags, eax, ecx, edx, ebx;
    u32 esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} __attribute__((packed));

extern char stub_base[];
extern u32 enter_user(struct uregs *u);
extern void df_entry(void);
extern u32 try_wr8(u32 addr, u32 v);
extern u32 try_verr(u32 sel), try_verw(u32 sel);   /* ZF (0 or 1), or 100h + the vector of a fault */
extern u32 try_rd_cr4(u32 *v);
extern u32 try_wr_cr4(u32 v);
extern u32 fpu_fault(void);
extern u32 try_rd_fs(u32 sel, u32 off);
extern u32 iretd_r0(u32 eflags);
extern u32 try_dr_exec(void), try_dr_write(volatile u32 *p, u32 v), try_dr_read(volatile u32 *p);
extern char dr_exec_at[], dr_write_after[], dr_read_after[];
extern u32 ksave_esp, trap_vec, fixup_eip, kfault_vec;
extern struct { u32 off; u16 sel; } __attribute__((packed)) ret_farptr;
extern char __user_start[], __user_end[], user_stack[], user_stack_top[];
extern char r3_hlt[], r3_in8[], r3_cli[], r3_sti[], r3_popf[], r3_int41[], r3_int42[], r3_esp[], r3_rd[], r3_wr[],
            r3_pushfd[];
#define SNIP(n) extern const char n[], n##_end[]
SNIP(v86_b); SNIP(v86_hlt); SNIP(v86_cli); SNIP(v86_sti); SNIP(v86_pushf); SNIP(v86_popf);
SNIP(v86_pushfd); SNIP(v86_popfd); SNIP(v86_int21); SNIP(v86_iret); SNIP(v86_iretd);
SNIP(v86_int3); SNIP(v86_into); SNIP(v86_bound); SNIP(v86_in8); SNIP(v86_in16); SNIP(v86_in32);
SNIP(v86_insb); SNIP(v86_cli_pushf); SNIP(v86_sti_pushf); SNIP(v86_popf_tf); SNIP(v86_popf_if);
SNIP(v86_int60); SNIP(v86_int61); SNIP(v86_pushf_pop); SNIP(v86_rd1000); SNIP(v86_wr1000);
SNIP(v86_step_ud); SNIP(v86_step_gp); SNIP(v86_pushfd_pop); SNIP(v86_iretd_id);
extern const char v86_int60_h[];

/* ---- small runtime */

void *memcpy(void *d, const void *s, u32 n)
{
    u8 *dp = d;
    const u8 *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memset(void *d, int c, u32 n)
{
    u8 *dp = d;
    while (n--) *dp++ = (u8)c;
    return d;
}

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32 rd_cr0(void) { u32 v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline void wr_cr0(u32 v) { __asm__ volatile("mov %0, %%cr0" :: "r"(v) : "memory"); }
static inline u32 rd_cr2(void) { u32 v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline u32 rd_cr3(void) { u32 v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void wr_cr3(u32 v) { __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory"); }
static inline void invlpg(u32 a) { __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }

static void putc_(char c)
{
    int n = 0;
    while (!(inb(0x3FD) & 0x20) && ++n < 100000) ;
    outb(0x3F8, (u8)c);
}

static void puts_(const char *s) { while (*s) putc_(*s++); }

static void putnum(u32 v, u32 base)
{
    char b[12];
    int i = 0;
    do { u32 d = v % base; b[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); } while (v /= base);
    while (i) putc_(b[--i]);
}

/* %x %u %s %c, nothing else. */
static void vsay(const char *f, __builtin_va_list ap)
{
    for (; *f; f++) {
        if (*f != '%') { putc_(*f); continue; }
        switch (*++f) {
        case 'x': putnum(__builtin_va_arg(ap, u32), 16); break;
        case 'u': putnum(__builtin_va_arg(ap, u32), 10); break;
        case 's': puts_(__builtin_va_arg(ap, const char *)); break;
        case 'c': putc_((char)__builtin_va_arg(ap, int)); break;
        default: putc_(*f); break;
        }
    }
}

static u32 n_fail;

/* ok: 1 PASS, 0 FAIL, -1 INFO */
static void res(const char *name, int ok, const char *f, ...)
{
    __builtin_va_list ap;
    puts_("HX-TEST cpu-");
    puts_(name);
    puts_(ok > 0 ? " PASS " : ok == 0 ? " FAIL " : " INFO ");
    __builtin_va_start(ap, f);
    vsay(f, ap);
    __builtin_va_end(ap);
    puts_("\r\n");
    if (ok == 0) n_fail++;
}

/* ---- descriptors */

static struct bootinfo *bi;
static u8 *v86lin;
static u32 gdt[32] __attribute__((aligned(8)));         /* 16 entries */
static u32 idt[512] __attribute__((aligned(8)));
static u8 tss_area[3 * 4096] __attribute__((aligned(4096)));
#define TSS   ((struct tss *)tss_area)
#define REDIR (tss_area + 104)                          /* VME interrupt redirection bitmap */
#define IOPB  (tss_area + 136)                          /* I/O permission bitmap, then FFh */
#define TSS_LIMIT (136 + 8192)
static struct tss dftss __attribute__((aligned(16)));
static u8 df_stack[4096] __attribute__((aligned(16)));
static u8 trap_a[8192] __attribute__((aligned(16)));
static u8 trap_b[8192] __attribute__((aligned(16)));

static void set_desc(int sel, u32 base, u32 limit, u8 access, u8 flags)
{
    u32 *e = &gdt[sel / 4];
    e[0] = (limit & 0xFFFF) | (base << 16);
    e[1] = ((base >> 16) & 0xFF) | ((u32)access << 8) | (limit & 0xF0000) | ((u32)(flags & 0xF0) << 16)
         | (base & 0xFF000000);
}

static void set_gate(int v, u32 off, u16 sel, u8 type)
{
    idt[v * 2] = (off & 0xFFFF) | ((u32)sel << 16);
    idt[v * 2 + 1] = (off & 0xFFFF0000) | ((u32)type << 8);
}

static void load_tr(void)
{
    gdt[SEL_TSS / 4 + 1] &= ~0x200u;                    /* not busy */
    gdt[SEL_DFTSS / 4 + 1] &= ~0x200u;
    __asm__ volatile("ltr %w0" :: "r"(SEL_TSS));
}

static void set_tss_limit(u32 limit)
{
    set_desc(SEL_TSS, (u32)tss_area, limit, 0x89, 0x00);
    load_tr();
}

static void setup_tables(void)
{
    struct { u16 limit; u32 base; } __attribute__((packed)) d;
    int v;

    set_desc(SEL_CODE32, 0, 0xFFFFF, 0x9A, 0xC0);
    set_desc(SEL_DATA32, 0, 0xFFFFF, 0x92, 0xC0);
    set_desc(SEL_CODE16, bi->cs_base, 0xFFFF, 0x9A, 0x00);
    set_desc(SEL_DATA16, bi->ds_base, 0xFFFF, 0x92, 0x00);
    set_desc(SEL_TSS, (u32)tss_area, TSS_LIMIT, 0x89, 0x00);
    set_desc(SEL_DFTSS, (u32)&dftss, sizeof dftss - 1, 0x89, 0x00);
    set_desc(SEL_UCODE & ~3, 0, 0xFFFFF, 0xFA, 0xC0);
    set_desc(SEL_UDATA & ~3, 0, 0xFFFFF, 0xF2, 0xC0);
    set_desc(SEL_USTACK16 & ~3, (u32)user_stack, 0xFFF, 0xF2, 0x00);
    d.limit = sizeof gdt - 1;
    d.base = (u32)gdt;
    __asm__ volatile("lgdt %0" :: "m"(d));

    /* Exceptions at DPL 0 except INT3/INTO (DPL 3); everything from 20h up
       DPL 3 so software INT n from ring 3 reaches it; #DF is a task gate;
       41h is DPL 0 and 42h not present, for case J. */
    for (v = 0; v < 256; v++)
        set_gate(v, (u32)stub_base + v * 16, SEL_CODE32, (v >= 0x20 || v == 3 || v == 4) ? 0xEE : 0x8E);
    set_gate(8, 0, SEL_DFTSS, 0x85);
    set_gate(0x41, (u32)stub_base + 0x41 * 16, SEL_CODE32, 0x8E);
    set_gate(0x42, (u32)stub_base + 0x42 * 16, SEL_CODE32, 0x6E);
    d.limit = sizeof idt - 1;
    d.base = (u32)idt;
    __asm__ volatile("lidt %0" :: "m"(d));

    TSS->ss0 = SEL_DATA32;
    TSS->esp0 = (u32)trap_a + sizeof trap_a;
    TSS->iomap = 136;
    memset(REDIR, 0xFF, 32);                            /* no software interrupt redirected */
    memset(IOPB, 0xFF, 8192 + 1);                       /* every port denied, then the FFh byte */
    dftss.cr3 = rd_cr3();
    dftss.eip = (u32)df_entry;
    dftss.esp = (u32)df_stack + sizeof df_stack;
    dftss.eflags = 2;
    dftss.cs = SEL_CODE32;
    dftss.ds = dftss.es = dftss.ss = dftss.fs = dftss.gs = SEL_DATA32;
    dftss.ss0 = SEL_DATA32;
    dftss.esp0 = dftss.esp;
    dftss.iomap = sizeof dftss;
    load_tr();
}

/* ---- traps */

static struct frame tf;                 /* the last trap handed back */
static u32 tf_addr, tf_cr2, kfault_err, in_user, df_count;
static volatile u32 rtc_count, rtc_read_c = 1, irq13_count, spurious;

static void eoi(int irq)
{
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* Case W: while step_on, every trap from the code under test is logged
   (vector, EIP; 100h added to the vector of one taken in ring 0). A #DB
   resumes what it interrupted; the monitor skips an ARPL (#UD) or CLI (#GP)
   in V86 mode and returns to it, TF still set in the image. */
#define STEP_MAX 12
static u32 step_on, step_n, step_vec[STEP_MAX], step_eip[STEP_MAX], step_dr6[STEP_MAX];

static int step_trap(struct frame *f)
{
    u32 vm = f->eflags & EF_VM;
    u8 op;
    if (step_n < STEP_MAX) {
        step_vec[step_n] = f->vec | (vm ? 0 : 0x100);
        step_eip[step_n] = f->eip;
        if (f->vec == 1) {
            __asm__ volatile("mov %%dr6, %0" : "=r"(step_dr6[step_n]));
            __asm__ volatile("mov %0, %%dr6" :: "r"(0u));
        }
    }
    step_n++;
    if (f->vec == 1)
        return 1;
    if (!vm || (f->vec != 6 && f->vec != 13))
        return 0;
    op = v86lin[f->eip & 0xFFFF];
    if (op == 0x63) { f->eip += 2; return 1; }          /* ARPL AX, AX */
    if (op == 0xFA) { f->eip += 1; return 1; }          /* CLI */
    return 0;                                           /* the HLT that ends the snippet */
}

u32 trap_c(struct frame *f)
{
    u32 v = f->vec;
    if (v == 0x70) {                                    /* RTC, case Q */
        rtc_count++;
        if (rtc_read_c) { outb(0x70, 0x0C); (void)inb(0x71); }
        eoi(8);
        return 0;
    }
    if (v == 0x75) {                                    /* IRQ 13, case T */
        irq13_count++;
        outb(0xF0, 0);
        __asm__ volatile("fnclex");
        eoi(13);
        return 0;
    }
    if ((v == 0x0F || v == 0x77) && !in_user) {         /* spurious IRQ 7 or 15 */
        spurious++;
        return 0;
    }
    if (step_on && in_user && step_trap(f))             /* case W */
        return 0;
    memcpy(&tf, f, sizeof tf);
    tf_addr = (u32)f;
    tf_cr2 = rd_cr2();
    trap_vec = v;
    if (!(f->eflags & EF_VM) && (f->cs & 3) == 0) {
        if (fixup_eip) {                                /* try_* helpers */
            f->eip = fixup_eip;
            fixup_eip = 0;
            kfault_vec = v;
            kfault_err = f->err;
            return 0;
        }
        if (!in_user)
            res("panic", 0, "unexpected ring-0 trap %x at %x err %x", v, f->eip, f->err);
    }
    return 1;
}

void df_c(void)
{
    df_count++;
    load_tr();
}

/* ---- running code in V86 or ring 3 */

static u32 v86(const char *s, const char *e, u32 ip, u32 eflags, u32 eax, u32 ebx)
{
    struct uregs u;
    u32 v;
    memset(&u, 0, sizeof u);
    memcpy(v86lin, s, (u32)(e - s));
    u.eax = eax;
    u.ebx = ebx;
    u.eip = ip;
    u.cs = u.ds = u.es = u.ss = bi->v86_seg;
    u.fs = 0x1111;
    u.gs = 0x2222;
    u.esp = 0x1FF0;
    u.eflags = EF_VM | 2 | eflags;
    in_user = 1;
    v = enter_user(&u);
    in_user = 0;
    return v;
}
#define V86(n, fl, ax, bx) v86(n, n##_end, 0, fl, ax, bx)

static u32 r3x(u32 eip, u32 cs, u32 ss, u32 esp, u32 eflags, u32 eax, u32 ebx)
{
    struct uregs u;
    u32 v;
    memset(&u, 0, sizeof u);
    u.eax = eax;
    u.ebx = ebx;
    u.eip = eip;
    u.cs = cs;
    u.ds = u.es = u.fs = u.gs = SEL_UDATA;
    u.ss = ss;
    u.esp = esp;
    u.eflags = 2 | eflags;
    in_user = 1;
    v = enter_user(&u);
    in_user = 0;
    return v;
}
#define R3(fn, fl, ax, bx) r3x((u32)(fn), SEL_UCODE, SEL_UDATA, (u32)user_stack_top - 16, fl, ax, bx)

static void io_allow(u32 port, int allow)
{
    if (allow) IOPB[port >> 3] &= (u8)~(1u << (port & 7));
    else IOPB[port >> 3] |= (u8)(1u << (port & 7));
}

static void redir(int vec, int to_ivt)
{
    if (to_ivt) REDIR[vec >> 3] &= (u8)~(1u << (vec & 7));
    else REDIR[vec >> 3] |= (u8)(1u << (vec & 7));
}

static u32 cr4_now;
static int cr4_ok;

static int set_cr4(u32 v)
{
    if (!cr4_ok) return 0;
    if (try_wr_cr4(v)) return 0;
    cr4_now = v;
    return 1;
}

/* ---- the cases */

static u32 cr4_bits;                    /* bits that stick in CR4 */
static u32 has_id;                      /* POPFD can change EFLAGS.ID: the CPU has CPUID */

static void case_a(void)
{
    u32 f1, f2, a = 0, b = 0, c = 0, d = 0, i;
    static const struct { u32 bit; const char *name; } bits[] = {
        { 0x001, "vme" }, { 0x002, "pvi" }, { 0x004, "tsd" }, { 0x008, "de" }, { 0x010, "pse" },
        { 0x040, "mce" }, { 0x080, "pge" }, { 0x100, "pce" }, { 0x200, "osfxsr" },
    };
    __asm__ volatile("pushfl\n popl %0\n movl %0, %1\n xorl $0x200000, %1\n pushl %1\n popfl\n"
                     "pushfl\n popl %1\n pushl %0\n popfl" : "=&r"(f1), "=&r"(f2));
    has_id = (f1 ^ f2) & EF_ID ? 1 : 0;
    if (has_id)
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    cr4_ok = try_rd_cr4(&cr4_now) == 0;
    puts_("HX-TEST cpu-A-features INFO cpuid=");
    putnum((f1 ^ f2) & EF_ID ? 1 : 0, 10);
    puts_(" family=");
    putnum((a >> 8) & 15, 10);
    puts_(" edx=");
    putnum(d, 16);
    puts_(" cr4=");
    puts_(cr4_ok ? "yes" : "absent");
    puts_(" bits=");
    if (cr4_ok) {
        u32 base = cr4_now;
        for (i = 0; i < sizeof bits / sizeof bits[0]; i++) {
            u32 got = 0;
            if (try_wr_cr4(base | bits[i].bit) == 0 && try_rd_cr4(&got) == 0 && (got & bits[i].bit)) {
                cr4_bits |= bits[i].bit;
                puts_(bits[i].name);
                putc_(',');
            }
            try_wr_cr4(base);
        }
        cr4_now = base;
    }
    puts_("\r\n");
}

static void case_b(void)
{
    u32 v = V86(v86_b, 0, 0, 0);
    int ok = v == 13 && tf.err == 0 && (tf.eflags & EF_VM) && tf.cs == bi->v86_seg && tf.eip == 3 &&
             (tf.eax & 0xFFFF) == 0x1234 && tf.v_ds == bi->v86_seg && tf.v_fs == 0x1111 && tf.v_gs == 0x2222 &&
             (tf.ds & 0xFFFF) == 0 && (tf.es & 0xFFFF) == 0 && (tf.fs & 0xFFFF) == 0 && (tf.gs & 0xFFFF) == 0;
    res("B-v86-entry-exit", ok, "vec=%u eip=%x ax=%x pushed ds=%x fs=%x gs=%x; ring-0 ds=%x es=%x fs=%x gs=%x",
        v, tf.eip, tf.eax & 0xFFFF, tf.v_ds, tf.v_fs, tf.v_gs,
        tf.ds & 0xFFFF, tf.es & 0xFFFF, tf.fs & 0xFFFF, tf.gs & 0xFFFF);
}

/* An expected #GP(0) at IP 1 (the instruction after the NOP). */
static void gp_at(const char *name, u32 v, u32 ip)
{
    res(name, v == 13 && tf.err == 0 && tf.eip == ip, "vec=%u err=%x eip=%x (expect #GP(0) at %x)",
        v, tf.err, tf.eip, ip);
}

static void case_c(void)
{
    set_cr4(cr4_now & ~(CR4_VME | CR4_PVI));
    gp_at("C-v86-cli", V86(v86_cli, 0, 0, 0), 1);
    gp_at("C-v86-sti", V86(v86_sti, 0, 0, 0), 1);
    gp_at("C-v86-pushf", V86(v86_pushf, 0, 0, 0), 1);
    gp_at("C-v86-popf", V86(v86_popf, 0, 0, 0), 1);
    gp_at("C-v86-pushfd", V86(v86_pushfd, 0, 0, 0), 1);
    gp_at("C-v86-popfd", V86(v86_popfd, 0, 0, 0), 1);
    gp_at("C-v86-int21", V86(v86_int21, 0, 0, 0), 1);
    gp_at("C-v86-iret", V86(v86_iret, 0, 0, 0), 1);
    gp_at("C-v86-iretd", V86(v86_iretd, 0, 0, 0), 1);
}

static void case_d(void)
{
    u32 v;
    v = V86(v86_int3, 0, 0, 0);
    res("D-v86-int3", v == 3 && tf.eip == 2, "vec=%u eip=%x (expect #BP through the IDT, eip 2)", v, tf.eip);
    v = V86(v86_into, EF_OF, 0, 0);
    res("D-v86-into", v == 4 && tf.eip == 2, "vec=%u eip=%x (expect #OF through the IDT, eip 2)", v, tf.eip);
    v = V86(v86_bound, 0, 0, 0);
    res("D-v86-bound", v == 5 && tf.eip == 3, "vec=%u eip=%x (expect #BR at eip 3)", v, tf.eip);
}

static void case_e(void)
{
    u32 v = V86(v86_hlt, 0, 0, 0);
    gp_at("E-v86-hlt", v, 0);
    v = R3(r3_hlt, 0, 0, 0);
    res("E-ring3-hlt", v == 13 && tf.err == 0 && tf.eip == (u32)r3_hlt, "vec=%u err=%x eip=%x", v, tf.err, tf.eip);
}

static void io_case(const char *name, u32 v, int expect_gp, u32 gp_ip, u32 ok_ip)
{
    if (expect_gp)
        res(name, v == 13 && tf.err == 0 && tf.eip == gp_ip, "vec=%u eip=%x (expect #GP at %x)", v, tf.eip, gp_ip);
    else
        res(name, v == 13 && tf.eip == ok_ip, "vec=%u eip=%x (expect the access, then HLT at %x)", v, tf.eip, ok_ip);
}

static void case_f(void)
{
    u32 p;
    set_cr4(cr4_now & ~(CR4_VME | CR4_PVI));
    io_allow(0x2C0, 1);
    io_case("F-v86-allowed", V86(v86_in8, 0, 0, 0x2C0), 0, 3, 4);
    io_case("F-v86-denied", V86(v86_in8, 0, 0, 0x2C1), 1, 3, 4);
    io_allow(0x2C7, 1);
    io_case("F-v86-word-straddle-denied", V86(v86_in16, 0, 0, 0x2C7), 1, 3, 4);
    io_allow(0x2C8, 1);
    io_case("F-v86-word-straddle-allowed", V86(v86_in16, 0, 0, 0x2C7), 0, 3, 4);
    io_allow(0x2C6, 1);
    io_case("F-v86-dword-denied", V86(v86_in32, 0, 0, 0x2C6), 1, 3, 5);
    io_case("F-v86-insb-denied", V86(v86_insb, 0, 0, 0x2C1), 1, 5, 6);
    io_case("F-v86-iopl3-still-checked", V86(v86_in8, EF_IOPL, 0, 0x2C1), 1, 3, 4);
    io_case("F-ring3-denied", R3(r3_in8, 0, 0, 0x2C1), 1, (u32)r3_in8 + 2, (u32)r3_in8 + 3);
    io_case("F-ring3-allowed", R3(r3_in8, 0, 0, 0x2C0), 0, (u32)r3_in8 + 2, (u32)r3_in8 + 3);
    io_case("F-ring3-iopl3-bypass", R3(r3_in8, EF_IOPL, 0, 0x2C1), 0, (u32)r3_in8 + 2, (u32)r3_in8 + 3);
    /* A byte port whose bitmap byte is the last one inside the TSS limit:
       silicon reads two bitmap bytes, so without the trailing FFh byte the
       access is refused. */
    for (p = 0xFFF8; p <= 0xFFFF; p++) io_allow(p, 1);
    set_tss_limit(TSS_LIMIT - 1);
    io_case("F-v86-iopb-last-byte", V86(v86_in8, 0, 0, 0xFFF8), 1, 3, 4);
    set_tss_limit(TSS_LIMIT);
    memset(IOPB, 0xFF, 8192 + 1);
}

static void case_g(void)
{
    u32 v, hoff = (u32)(v86_int60_h - v86_int60), save60, ax, zero = 0;
    volatile u32 *ivt;
    __asm__("" : "+r"(zero));                           /* the IVT is at linear 0 */
    ivt = (volatile u32 *)zero;
    if (!(cr4_bits & CR4_VME)) {
        res("G-vme", -1, "no CR4.VME on this CPU: skipped");
        return;
    }
    set_cr4((cr4_now | CR4_VME) & ~CR4_PVI);
    save60 = ivt[0x60];
    ivt[0x60] = (bi->v86_seg << 16) | hoff;
    redir(0x60, 1);
    v = V86(v86_cli_pushf, EF_IF | EF_VIF, 0, 0);
    ax = tf.eax & 0xFFFF;
    res("G-vme-cli-pushf", v == 13 && tf.eip == 3 && !(tf.eflags & EF_VIF) && !(ax & EF_IF) && (ax & EF_IOPL) == EF_IOPL,
        "vec=%u eip=%x vif=%u pushed_if=%u pushed_iopl=%u", v, tf.eip, !!(tf.eflags & EF_VIF), !!(ax & EF_IF), (ax >> 12) & 3);
    v = V86(v86_sti_pushf, EF_IF, 0, 0);
    ax = tf.eax & 0xFFFF;
    res("G-vme-sti-pushf", v == 13 && tf.eip == 3 && (tf.eflags & EF_VIF) && (ax & EF_IF),
        "vec=%u eip=%x vif=%u pushed_if=%u", v, tf.eip, !!(tf.eflags & EF_VIF), !!(ax & EF_IF));
    v = V86(v86_sti, EF_IF | EF_VIP, 0, 0);
    gp_at("G-vme-sti-vip", v, 1);
    v = V86(v86_popf_tf, EF_IF, 0, 0);
    gp_at("G-vme-popf-tf", v, 3);
    v = V86(v86_popf_if, EF_IF, 0, 0);
    ax = tf.eax & 0xFFFF;
    res("G-vme-popf-if", v == 13 && tf.eip == 6 && (tf.eflags & EF_VIF) && (ax & EF_IF),
        "vec=%u eip=%x vif=%u pushed_if=%u", v, tf.eip, !!(tf.eflags & EF_VIF), !!(ax & EF_IF));
    v = V86(v86_pushfd, EF_IF, 0, 0);
    gp_at("G-vme-pushfd", v, 1);
    /* INT 60h redirected to the IVT: the FLAGS image must carry VIF as IF
       and IOPL 3 (patch 0106 in 86Box). */
    v = V86(v86_int60, EF_IF | EF_VIF, 0, 0);
    ax = tf.eax & 0xFFFF;
    res("G-vme-redirect-flags", v == 13 && tf.eip == hoff + 5 && !(ax & EF_IF) && (ax & EF_IOPL) == EF_IOPL,
        "vec=%u eip=%x pushed_if=%u pushed_iopl=%u (expect the IVT handler at %x with IF=VIF=0, IOPL=3)",
        v, tf.eip, !!(ax & EF_IF), (ax >> 12) & 3, hoff + 5);
    v = V86(v86_int61, EF_IF, 0, 0);
    gp_at("G-vme-not-redirected", v, 1);
    /* IOPL 3: a clear bit still redirects to the IVT (patch 0108 in 86Box),
       a set bit goes to the IDT. */
    v = V86(v86_int60, EF_IF | EF_IOPL, 0, 0);
    res("G-vme-iopl3-redirect", v == 13 && tf.eip == hoff + 5, "vec=%u eip=%x (expect the IVT handler at %x)",
        v, tf.eip, hoff + 5);
    v = V86(v86_int61, EF_IF | EF_IOPL, 0, 0);
    res("G-vme-iopl3-idt", v == 0x61 && tf.eip == 3, "vec=%x eip=%x (expect INT 61h through the IDT)", v, tf.eip);
    redir(0x60, 0);
    ivt[0x60] = save60;
    set_cr4(cr4_now & ~CR4_VME);
}

static void case_h(void)
{
    u32 v;
    if (!(cr4_bits & CR4_PVI)) {
        res("H-pvi", -1, "no CR4.PVI on this CPU: skipped");
        return;
    }
    set_cr4((cr4_now | CR4_PVI) & ~CR4_VME);
    v = R3(r3_cli, EF_IF | EF_VIF, 0, 0);
    res("H-pvi-cli", v == 13 && tf.eip == (u32)r3_cli + 3 && !(tf.eflags & EF_VIF) && (tf.eflags & EF_IF),
        "vec=%u eip=%x vif=%u if=%u", v, tf.eip - (u32)r3_cli, !!(tf.eflags & EF_VIF), !!(tf.eflags & EF_IF));
    v = R3(r3_sti, EF_IF, 0, 0);
    res("H-pvi-sti", v == 13 && tf.eip == (u32)r3_sti + 3 && (tf.eflags & EF_VIF),
        "vec=%u eip=%x vif=%u", v, tf.eip - (u32)r3_sti, !!(tf.eflags & EF_VIF));
    v = R3(r3_popf, EF_IF | EF_VIF, 0, 2);
    res("H-pvi-popf-leaves-vif", v == 13 && tf.eip == (u32)r3_popf + 4 && (tf.eflags & EF_VIF) && (tf.eflags & EF_IF),
        "vec=%u eip=%x vif=%u if=%u", v, tf.eip - (u32)r3_popf, !!(tf.eflags & EF_VIF), !!(tf.eflags & EF_IF));
    set_cr4(cr4_now & ~CR4_PVI);
}

static void case_j(void)
{
    u32 v = R3(r3_int41, 0, 0, 0);
    res("J-int-dpl0-gate", v == 13 && tf.err == 0x41 * 8 + 2, "vec=%u err=%x (expect #GP(%x))", v, tf.err, 0x41 * 8 + 2);
    v = R3(r3_int42, 0, 0, 0);
    res("J-int-not-present", v == 11 && tf.err == 0x42 * 8 + 2, "vec=%u err=%x (expect #NP(%x))", v, tf.err, 0x42 * 8 + 2);
}

static void case_k(void)
{
    u32 v;
    int in_a, in_b;
    TSS->esp0 = (u32)trap_a + sizeof trap_a;
    v = V86(v86_hlt, 0, 0, 0);
    in_a = v == 13 && tf_addr >= (u32)trap_a && tf_addr < (u32)trap_a + sizeof trap_a;
    TSS->esp0 = (u32)trap_b + sizeof trap_b;
    v = V86(v86_hlt, 0, 0, 0);
    in_b = v == 13 && tf_addr >= (u32)trap_b && tf_addr < (u32)trap_b + sizeof trap_b;
    TSS->esp0 = (u32)trap_a + sizeof trap_a;
    res("K-esp0-change", in_a && in_b, "first frame on stack a=%u, second on stack b=%u", in_a, in_b);
}

static void case_m(void)
{
    u32 v = r3x((u32)r3_esp, SEL_UCODE, SEL_USTACK16, 0xFF0, 0, 0, 0);
    u32 high = tf.eax >> 16, khigh = ksave_esp >> 16;
    res("M-iret-16bit-ss-esp-high", v == 13 && (tf.eax & 0xFFFF) == 0xFF0 && high == khigh,
        "vec=%u esp=%x: high word %x from ring 0 (kernel %x): espfix needed", v, tf.eax, high, khigh);
}

static void case_n(void)
{
    u32 v = r3x((u32)r3_hlt, 0xFB, SEL_UDATA, (u32)user_stack_top - 16, 0, 0, 0);
    res("N-iret-bad-cs", v == 13 && tf.err == 0xF8 && (tf.cs & 3) == 0 && !df_count,
        "vec=%u err=%x from cs=%x (expect a ring-0 #GP(f8))", v, tf.err, tf.cs);
    v = r3x((u32)r3_hlt, SEL_UCODE, 0x0B, (u32)user_stack_top - 16, 0, 0, 0);
    res("N-iret-bad-ss", v == 13 && tf.err == 0x08 && (tf.cs & 3) == 0 && !df_count,
        "vec=%u err=%x from cs=%x (expect a ring-0 #GP(8))", v, tf.err, tf.cs);
}

static u32 *pte0(u32 lin) { return (u32 *)bi->pt0 + (lin >> 12); }
static u32 *pte1(u32 lin) { return (u32 *)bi->pt1 + ((lin - V86PM_BASE) >> 12); }

static void case_o(void)
{
    u32 v, save, page, r;
    v = R3(r3_rd, 0, 0, (u32)gdt);
    res("O-pf-ring3-read-supervisor", v == 14 && tf.err == 5 && tf_cr2 == (u32)gdt, "vec=%u err=%x cr2=%x", v, tf.err, tf_cr2);
    v = R3(r3_wr, 0, 0, (u32)gdt);
    res("O-pf-ring3-write-supervisor", v == 14 && tf.err == 7 && tf_cr2 == (u32)gdt, "vec=%u err=%x", v, tf.err);
    page = (u32)v86lin + 0x1000;
    save = *pte0(page);
    *pte0(page) = 0;
    invlpg(page);
    v = V86(v86_rd1000, 0, 0, 0);
    res("O-pf-v86-read-not-present", v == 14 && tf.err == 4 && tf_cr2 == page, "vec=%u err=%x cr2=%x", v, tf.err, tf_cr2);
    v = V86(v86_wr1000, 0, 0, 0);
    res("O-pf-v86-write-not-present", v == 14 && tf.err == 6 && tf_cr2 == page, "vec=%u err=%x", v, tf.err);
    *pte0(page) = save;
    invlpg(page);
    *pte1(TEST_LIN) = bi->spare_a | 1;                  /* read-only, supervisor */
    invlpg(TEST_LIN);
    wr_cr0(rd_cr0() | CR0_WP);
    r = try_wr8(TEST_LIN, 1);
    res("O-pf-ring0-write-wp", r == 15 && kfault_err == 3, "fault=%u err=%x (expect #PF, err 3)", r ? r - 1 : 0, kfault_err);
    wr_cr0(rd_cr0() & ~CR0_WP);
    r = try_wr8(TEST_LIN, 1);
    res("O-pf-ring0-write-no-wp", r == 0, "fault=%u (expect none)", r ? r - 1 : 0);
    *pte1(TEST_LIN) = 0;
    invlpg(TEST_LIN);
}

static void case_p(void)
{
    volatile u8 *lin = (volatile u8 *)TEST_LIN;
    u8 first, second;
    if (!(cr4_bits & CR4_PGE)) {
        res("P-pge", -1, "no CR4.PGE on this CPU: skipped");
        return;
    }
    *(volatile u8 *)bi->spare_a = 'A';
    *(volatile u8 *)bi->spare_b = 'B';
    set_cr4(cr4_now | CR4_PGE);
    *pte1(TEST_LIN) = bi->spare_a | 0x103;              /* global */
    invlpg(TEST_LIN);
    first = *lin;
    *pte1(TEST_LIN) = bi->spare_b | 0x103;
    wr_cr3(rd_cr3());                                   /* flushes only non-global entries */
    second = *lin;
    res("P-pge-survives-cr3", -1, "first=%c after_cr3=%c (silicon keeps the global entry: A)", first, second);
    *pte1(TEST_LIN) = 0;
    invlpg(TEST_LIN);
    set_cr4(cr4_now & ~CR4_PGE);
}

/* PIT channel 2, one shot, about 54.9 ms; waits for OUT2 in port 61h. */
static void pit2_window(void)
{
    u8 g = inb(0x61);
    u32 spin = 0;
    outb(0x61, (u8)((g & ~0x02) | 0x01));
    outb(0x43, 0xB0);
    outb(0x42, 0xFF);
    outb(0x42, 0xFF);
    while (!(inb(0x61) & 0x20) && ++spin < 200000000u) ;
    outb(0x61, g);
}

static void case_q(void)
{
    u8 a, b;
    u32 n, i, n2;
    outb(0x70, 0x0A); a = inb(0x71);
    outb(0x70, 0x0B); b = inb(0x71);
    outb(0x70, 0x0A); outb(0x71, 0x26);                 /* 1024 Hz */
    outb(0x70, 0x0B); outb(0x71, (u8)(b | 0x40));       /* periodic interrupt on */
    outb(0x70, 0x0C); (void)inb(0x71);
    outb(0xA1, 0xFE);                                   /* IRQ 8 */
    outb(0x21, 0xFB);                                   /* and the cascade */
    rtc_count = 0;
    rtc_read_c = 1;
    sti();
    for (i = 0; i < 10; i++)
        pit2_window();
    cli();
    n = rtc_count;
    res("Q-rtc-1024hz", n >= 551 && n <= 574, "irq8=%u in 10 PIT windows of 54.9 ms (expect 562 +-2%%)", n);
    rtc_read_c = 0;
    rtc_count = 0;
    sti();
    pit2_window();
    pit2_window();
    cli();
    n2 = rtc_count;
    res("Q-rtc-regc-unread", -1, "irq8=%u in 110 ms without reading register C (silicon: at most 1)", n2);
    rtc_read_c = 1;
    outb(0xA1, 0xFF);
    outb(0x21, 0xFF);
    outb(0x70, 0x0B); outb(0x71, b);
    outb(0x70, 0x0A); outb(0x71, a);
    outb(0x70, 0x0C); (void)inb(0x71);
}

static void case_t(void)
{
    u32 cr0 = rd_cr0(), r;
    wr_cr0((cr0 | CR0_NE) & ~(CR0_EM | CR0_TS));
    irq13_count = 0;
    r = fpu_fault();
    res("T-fpu-ne1-mf", r == 17 && !irq13_count, "fault=%u irq13=%u (expect #MF, vector 16)", r ? r - 1 : 0, irq13_count);
    wr_cr0(cr0 & ~(CR0_NE | CR0_EM | CR0_TS));
    outb(0xA1, 0xDF);                                   /* IRQ 13 */
    outb(0x21, 0xFB);
    irq13_count = 0;
    r = fpu_fault();
    outb(0xA1, 0xFF);
    outb(0x21, 0xFF);
    res("T-fpu-ne0-irq13", r == 0 && irq13_count >= 1, "fault=%u irq13=%u (expect IRQ 13, no #MF)", r ? r - 1 : 0, irq13_count);
    wr_cr0(cr0);
}

/* The same V86 code under IOPL 0, then 3, then 0: PUSHF must trap at IOPL 0
   every time (86Box's dynarec compiles PUSHF for one IOPL). */
static void case_u(void)
{
    u32 i, v, bad = 0;
    set_cr4(cr4_now & ~(CR4_VME | CR4_PVI));
    for (i = 0; i < 5; i++) { v = V86(v86_pushf_pop, 0, 0, 0); bad += !(v == 13 && tf.eip == 0); }
    for (i = 0; i < 20; i++) { v = V86(v86_pushf_pop, EF_IOPL, 0, 0); bad += !(v == 13 && tf.eip == 2); }
    for (i = 0; i < 5; i++) { v = V86(v86_pushf_pop, 0, 0, 0); bad += !(v == 13 && tf.eip == 0); }
    res("U-iopl-change-same-code", bad == 0, "%u of 30 runs behaved wrongly", bad);
}

/* A load past a segment's limit faults, also from code the dynarec compiled
   while the limit was large: DJGPP's Ctrl-C and timer signals cut DS's limit
   to 4 KB in the IRQ handler so the next access faults (86Box's old dynarec
   checked limits on stores only; local patch 0112). */
static void case_v(void)
{
    u32 i, r, bad = 0;
    set_desc(SEL_LIMBIG, 0, 0xFFFFF, 0x92, 0xC0);
    set_desc(SEL_LIMSMALL, 0, 0xFFF, 0x92, 0x40);
    for (i = 0; i < 2000; i++)
        bad += try_rd_fs(SEL_LIMBIG, 0x2000) != 0;
    r = try_rd_fs(SEL_LIMSMALL, 0x2000);
    res("V-load-past-limit", r == 14 && bad == 0, "fault=%u (expect #GP) in-limit faults=%u", r ? r - 1 : 0, bad);
}

/* A trap EFLAGS.TF would raise follows only an instruction that completes
   (SDM vol. 3, 17.3.1.4): single-stepping V86 code onto an instruction that
   faults gives the fault, and no #DB, neither in V86 mode nor in the
   handler; the handler skips the instruction, returns with TF still set and
   stepping goes on, one #DB per instruction (86Box raised a #DB in the
   ring-0 handler after a #UD, which GLOS saw from lDebugX, and never set
   DR6.BS; local patch 0113). Log entries are vector@EIP; one taken in ring 0
   shows its EIP from stub_base (60h: the #UD stub's first instruction) and
   DR6. The handler clears DR6 after each #DB. */
static char step_txt[STEP_MAX * 24 + 4];

static char *hex_(char *p, u32 v)
{
    char b[8];
    int i = 0;
    do { b[i++] = "0123456789abcdef"[v & 15]; } while (v >>= 4);
    while (i) *p++ = b[--i];
    return p;
}

static u32 step_db, step_db_bs;         /* V86 single-step #DBs, and those with DR6.BS */

static void step_case(const char *name, const char *s, const char *e, const u32 *want, u32 n)
{
    u32 i, v, ok, r0 = 0;
    char *p = step_txt;
    step_n = 0;
    step_on = 1;
    v = v86(s, e, 0, EF_TF, 0, 0);
    step_on = 0;
    ok = v == 13 && step_n == n;
    for (i = 0; i < step_n && i < STEP_MAX; i++) {
        r0 += step_vec[i] >= 0x100;
        if (step_vec[i] == 1) {
            step_db++;
            step_db_bs += (step_dr6[i] & 0x4000) != 0;
        }
        ok &= i < n && step_vec[i] == want[2 * i] && step_eip[i] == want[2 * i + 1];
        p = hex_(p, step_vec[i] & 0xFF);
        *p++ = '@';
        if (step_vec[i] >= 0x100) {
            *p++ = 'r'; *p++ = '0'; *p++ = '+';
            p = hex_(p, step_eip[i] - (u32)stub_base);
            *p++ = '/'; *p++ = 'd'; *p++ = 'r'; *p++ = '6'; *p++ = '=';
            p = hex_(p, step_dr6[i]);
        } else
            p = hex_(p, step_eip[i]);
        *p++ = ' ';
    }
    *p = 0;
    res(name, ok, "traps %s(%u, %u in ring 0; expect no ring-0 #DB, one #DB per completed instruction)",
        step_txt, step_n, r0);
}

static void case_w(void)
{
    static const u32 ud[] = { 1, 1, 6, 1, 1, 4, 1, 5, 13, 5 };
    static const u32 gp[] = { 1, 1, 13, 1, 1, 3, 1, 4, 13, 4 };
    set_cr4(cr4_now & ~(CR4_VME | CR4_PVI));
    step_case("W-step-onto-ud", v86_step_ud, v86_step_ud_end, ud, 5);
    step_case("W-step-onto-gp", v86_step_gp, v86_step_gp_end, gp, 5);
    res("W-step-dr6-bs", step_db && step_db_bs == step_db, "%u of %u single-step #DBs set DR6.BS", step_db_bs, step_db);
}

/* EFLAGS.ID can be set only on a CPU that has CPUID, whichever instruction
   loads it: an IRETD into V86 mode, in V86 mode at IOPL 3, to ring 3 or to
   ring 0 must treat it as POPFD does, in PUSHFD and in the next trap frame.
   86Box's IRETD loaded it on any CPU (into V86 mode with the whole high
   word), so on a 486DX2 the frame showed ID: GLOS builds the V86 code's
   FLAGS image from it, and lDebugX saw ID toggle and ran CPUID into #UD.
   In V86 mode at IOPL 3 it loaded none of RF, AC or ID. Local patch 0114. */
static void case_x(void)
{
    u32 v, f;
    set_cr4(cr4_now & ~(CR4_VME | CR4_PVI));
    v = V86(v86_pushfd_pop, EF_IOPL | EF_ID, 0, 0);
    res("X-iretd-v86-id", v == 13 && !!(tf.eax & EF_ID) == has_id && !!(tf.eflags & EF_ID) == has_id,
        "vec=%u pushfd=%x trap_frame=%x (cpuid=%u: expect ID=%u in both)", v, tf.eax, tf.eflags, has_id, has_id);
    v = V86(v86_iretd_id, EF_IOPL, EF_ID | EF_IOPL | 2, 0);
    res("X-iretd-in-v86-id", v == 13 && tf.eip == (u32)(v86_iretd_id_end - v86_iretd_id) - 1 &&
        !!(tf.eax & EF_ID) == has_id && !!(tf.eflags & EF_ID) == has_id,
        "vec=%u eip=%x pushfd=%x trap_frame=%x (IOPL 3; cpuid=%u: expect ID=%u in both)",
        v, tf.eip, tf.eax, tf.eflags, has_id, has_id);
    v = R3(r3_pushfd, EF_ID, 0, 0);
    res("X-iretd-ring3-id", v == 13 && !!(tf.eax & EF_ID) == has_id && !!(tf.eflags & EF_ID) == has_id,
        "vec=%u pushfd=%x trap_frame=%x (cpuid=%u: expect ID=%u in both)", v, tf.eax, tf.eflags, has_id, has_id);
    f = iretd_r0(EF_ID | 2);
    res("X-iretd-ring0-id", kfault_vec == 6 && !!(f & EF_ID) == has_id && !!(tf.eflags & EF_ID) == has_id,
        "pushfd=%x trap_frame=%x (cpuid=%u: expect ID=%u in both)", f, tf.eflags, has_id, has_id);
}

/* DR0-DR3 breakpoints fire where DR7 enables them (SDM vol. 3, 17.2): an
   execute breakpoint is a fault before the instruction, a data breakpoint a
   trap after the access, with DR6.Bn set; in ring 0, ring 3 and V86 mode,
   also from code the dynarec compiled before DR7 was set. GLOS's DPMI
   watchpoints (0B00h) rely on them (86Box built them only with
   DEBUGREGS486; local patch 0115). */
#define WR_DR(n, v) __asm__ volatile("mov %0, %%dr" #n :: "r"((u32)(v)))
#define RD_DR(n, v) __asm__ volatile("mov %%dr" #n ", %0" : "=r"(v))
#define DR7_LE_GE   0x300u
#define DR7_L0      0x001u
#define DR7_L1      0x004u
#define DR7_RW1(rw, len) (((u32)(rw) << 20) | ((u32)(len) << 22))

static volatile u32 y_var __attribute__((aligned(4)));

static void dr_arm(u32 dr0, u32 dr1, u32 dr7)
{
    WR_DR(6, 0);
    WR_DR(0, dr0);
    WR_DR(1, dr1);
    WR_DR(7, dr7 | DR7_LE_GE);
}

static u32 dr_done(void)
{
    u32 dr6;
    RD_DR(6, dr6);
    WR_DR(7, 0);
    WR_DR(6, 0);
    return dr6;
}

static void case_y(void)
{
    u32 i, r, v, dr6, bad = 0;
    for (i = 0; i < 2000; i++)                          /* compiled by the dynarec first */
        bad += try_dr_exec() + try_dr_write(&y_var, i) + try_dr_read(&y_var);
    dr_arm((u32)dr_exec_at, 0, DR7_L0);
    r = try_dr_exec();
    dr6 = dr_done();
    res("Y-dr-exec-ring0", r == 2 && tf.eip == (u32)dr_exec_at && (dr6 & 1) && !bad,
        "fault=%u eip=%x dr6=%x (expect #DB before the instruction, B0) unarmed faults=%u",
        r ? r - 1 : 0, tf.eip - (u32)dr_exec_at, dr6, bad);
    y_var = 0;
    dr_arm(0, (u32)&y_var, DR7_L1 | DR7_RW1(1, 3));
    r = try_dr_write(&y_var, 0x5A5A);
    dr6 = dr_done();
    res("Y-dr-write-ring0", r == 2 && tf.eip == (u32)dr_write_after && (dr6 & 2) && y_var == 0x5A5A,
        "trap=%u eip=%x dr6=%x stored=%x (expect #DB after the store, B1)",
        r ? r - 1 : 0, tf.eip - (u32)dr_write_after, dr6, y_var);
    dr_arm(0, (u32)&y_var, DR7_L1 | DR7_RW1(3, 3));
    r = try_dr_read(&y_var);
    dr6 = dr_done();
    res("Y-dr-read-ring0", r == 2 && tf.eip == (u32)dr_read_after && (dr6 & 2),
        "trap=%u eip=%x dr6=%x (expect #DB after the load, B1)", r ? r - 1 : 0, tf.eip - (u32)dr_read_after, dr6);
    dr_arm(0, (u32)user_stack, DR7_L1 | DR7_RW1(1, 0));
    v = R3(r3_wr, 0, 0x77, (u32)user_stack);
    dr6 = dr_done();
    res("Y-dr-write-ring3", v == 1 && tf.eip == (u32)r3_wr + 2 && (dr6 & 2),
        "vec=%u eip=%x dr6=%x (expect #DB after the store, B1)", v, tf.eip - (u32)r3_wr, dr6);
    dr_arm((u32)v86lin + 3, 0, DR7_L0);
    v = V86(v86_b, 0, 0, 0);
    dr6 = dr_done();
    res("Y-dr-exec-v86", v == 1 && (tf.eflags & EF_VM) && tf.eip == 3 && (dr6 & 1),
        "vec=%u eip=%x dr6=%x (expect #DB before the HLT at 3, B0)", v, tf.eip, dr6);
    dr_arm(0, (u32)v86lin + 0x1000, DR7_L1 | DR7_RW1(1, 0));
    v = V86(v86_wr1000, 0, 0, 0);
    dr6 = dr_done();
    res("Y-dr-write-v86", v == 1 && (tf.eflags & EF_VM) && tf.eip == 5 && (dr6 & 2),
        "vec=%u eip=%x dr6=%x (expect #DB after the store, at 5, B1)", v, tf.eip, dr6);
}

static void open_user_pages(void)
{
    u32 a;
    for (a = (u32)__user_start; a < (u32)__user_end; a += 4096)
        *pte1(a) |= 4;
    wr_cr3(rd_cr3());
}

/* Z: VERR and VERW of a selector past the LDT's limit read no descriptor (SDM: ZF cleared, nothing touched). The
   LDT's two entries end its page and the next page isn't present, so a descriptor read past the limit faults
   (86Box until RetroGPU 0136: GLOS's memory diet found it, 2026-11-20). */
static void case_z(void)
{
    u32 base = TEST_LIN + 0x1000 - 16, *ldt = (u32 *)base, next = *pte1(TEST_LIN + 0x1000), in, r, w;
    *pte1(TEST_LIN) = bi->spare_a | 3;
    *pte1(TEST_LIN + 0x1000) = 0;
    invlpg(TEST_LIN);
    invlpg(TEST_LIN + 0x1000);
    ldt[0] = ldt[1] = 0;
    ldt[2] = 0x0000FFFFu;                               /* entry 1: flat data, DPL 0, present */
    ldt[3] = 0x00CF9200u;
    set_desc(SEL_ZLDT, base, 15, 0x82, 0x00);
    __asm__ volatile("lldt %w0" :: "r"(SEL_ZLDT));
    in = try_verr(0x0C);                                /* entry 1 (TI, RPL 0): inside the limit */
    r = try_verr(0x14);                                 /* entry 2: past it, on the page that isn't there */
    w = try_verw(0x14);
    __asm__ volatile("lldt %w0" :: "r"(0));
    *pte1(TEST_LIN + 0x1000) = next;
    *pte1(TEST_LIN) = 0;
    invlpg(TEST_LIN + 0x1000);
    invlpg(TEST_LIN);
    res("Z-verr-inside-ldt", in == 1, "result=%x (expect 1: ZF set, readable data)", in);
    res("Z-verr-past-ldt-limit", r == 0, "result=%x (expect 0: ZF clear, no fault; 10E: a page fault)", r);
    res("Z-verw-past-ldt-limit", w == 0, "result=%x (expect 0: ZF clear, no fault; 10E: a page fault)", w);
}

u32 pm_main(struct bootinfo *b)
{
    u32 cr0, cr4_init = 0;
    u8 pic1, pic2;

    bi = b;
    if (bi->magic != V86PM_MAGIC)
        return 1;
    ret_farptr.off = bi->ret_off;
    ret_farptr.sel = SEL_CODE16;
    v86lin = (u8 *)(bi->v86_seg << 4);
    pic1 = inb(0x21);
    pic2 = inb(0xA1);
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    cr0 = rd_cr0();
    setup_tables();
    open_user_pages();

    case_a();
    cr4_init = cr4_now;
    case_b();
    case_c();
    case_d();
    case_e();
    case_f();
    case_g();
    case_h();
    case_j();
    case_k();
    res("L-frame-across-not-present", -1, "skipped: GLOS keeps ring-0 stacks present (supervisor.md invariant 1); "
        "STACKPG covers the same-privilege case");
    case_m();
    case_n();
    case_o();
    case_p();
    case_q();
    case_t();
    case_u();
    case_v();
    case_w();
    case_x();
    case_y();
    case_z();
    res("monitor", df_count == 0, "double faults=%u spurious irqs=%u", df_count, spurious);

    if (cr4_ok) try_wr_cr4(cr4_init);
    wr_cr0(cr0);
    outb(0x21, pic1);
    outb(0xA1, pic2);
    bi->fails = n_fail;
    return n_fail;
}
