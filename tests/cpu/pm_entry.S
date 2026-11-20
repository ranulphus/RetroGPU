/* V86TEST monitor: entry, interrupt stubs, entering V86 or ring 3 and coming
 * back on the next trap, the #DF task, and fault-protected kernel helpers.
 * Linked at V86PM_BASE (4 MB), paging on, CS=08h, DS=ES=SS=10h. */
        .code32

        .section .text.head,"ax"
        .globl  _start
_start:                                 /* header read by the loader */
        .ascii  "V86P"
        .long   entry32 - _start
        .long   __file_end - _start
        .long   __bss_end - _start
entry32:
        cld
        movl    $stack_top, %esp
        pushl   %esi                    /* struct bootinfo * */
        call    pm_main
        addl    $4, %esp
        cli
        ljmp    *ret_farptr             /* to the loader's 16-bit _pm_ret, EAX = failures */

        .text
/* 256 stubs, 16 bytes apart: an error code where the CPU pushes none, then
   the vector number. */
        .macro  stub n
        .align  16
        .if     (\n == 8) || ((\n >= 10) && (\n <= 14)) || (\n == 17)
        .else
        pushl   $0
        .endif
        pushl   $\n
        jmp     trap_common
        .endm

        .globl  stub_base
        .align  16
stub_base:
        .altmacro
        .set    i, 0
        .rept   256
        stub    %i
        .set    i, i + 1
        .endr
        .noaltmacro

/* Frame, low to high: pushal (edi..eax), gs, fs, es, ds, eax again, vector,
   error code, then what the CPU pushed (struct frame in pm_main.c). trap_c
   returns 0 to resume the interrupted code, or 1 to return from enter_user
   with the vector. */
trap_common:
        pushl   %eax
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs
        pushal
        movw    $0x10, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movl    %esp, %eax
        pushl   %eax
        call    trap_c
        addl    $4, %esp
        testl   %eax, %eax
        jnz     to_harness
        popal
        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popl    %eax
        addl    $8, %esp
        iretl
to_harness:
        movl    ksave_esp, %esp
        movl    trap_vec, %eax
        popl    %edi
        popl    %esi
        popl    %ebx
        popl    %ebp
        movw    $0x10, %dx
        movw    %dx, %fs
        movw    %dx, %gs
        ret

/* u32 enter_user(struct uregs *u): IRET into V86 (EFLAGS.VM set) or ring 3
   with u's registers; returns the vector of the first trap that trap_c
   hands back. struct uregs: eax 0, ebx 4, ecx 8, edx 12, esi 16, edi 20,
   ebp 24, eip 28, cs 32, eflags 36, esp 40, ss 44, es 48, ds 52, fs 56, gs 60. */
        .globl  enter_user
enter_user:
        pushl   %ebp
        pushl   %ebx
        pushl   %esi
        pushl   %edi
        movl    %esp, ksave_esp
        movl    20(%esp), %ebp
        testl   $0x20000, 36(%ebp)
        jz      1f
        pushl   60(%ebp)
        pushl   56(%ebp)
        pushl   52(%ebp)
        pushl   48(%ebp)
1:      pushl   44(%ebp)
        pushl   40(%ebp)
        pushl   36(%ebp)
        pushl   32(%ebp)
        pushl   28(%ebp)
        testl   $0x20000, 36(%ebp)
        jnz     2f
        movw    52(%ebp), %ds           /* EBP addressing goes through SS */
        movw    48(%ebp), %es
        movw    56(%ebp), %fs
        movw    60(%ebp), %gs
2:      movl    0(%ebp), %eax
        movl    4(%ebp), %ebx
        movl    8(%ebp), %ecx
        movl    12(%ebp), %edx
        movl    16(%ebp), %esi
        movl    20(%ebp), %edi
        movl    24(%ebp), %ebp
        iretl

/* The #DF task: record it, make both TSSes usable again, and return from
   enter_user with vector 8. */
        .globl  df_entry
df_entry:
        pushfl
        andl    $~0x4000, (%esp)        /* NT: no task return */
        popfl
        call    df_c
        movl    ksave_esp, %esp
        movl    $8, %eax
        popl    %edi
        popl    %esi
        popl    %ebx
        popl    %ebp
        movw    $0x10, %dx
        movw    %dx, %ds
        movw    %dx, %es
        movw    %dx, %fs
        movw    %dx, %gs
        ret

/* Kernel accesses that may fault: 0 when they don't, 1 + the vector when
   they do (trap_c continues at fixup_eip). */
        .globl  try_wr8
try_wr8:
        movl    4(%esp), %edx
        movl    8(%esp), %eax
        movl    $9f, fixup_eip
        movb    %al, (%edx)
        movl    $0, fixup_eip
        xorl    %eax, %eax
        ret
9:      movl    kfault_vec, %eax
        incl    %eax
        ret

        .globl  try_verr
try_verr:
        movl    4(%esp), %edx
        movl    $9f, fixup_eip
        xorl    %eax, %eax
        verr    %dx
        setz    %al
        movl    $0, fixup_eip
        ret
9:      movl    kfault_vec, %eax
        orl     $0x100, %eax
        ret

        .globl  try_verw
try_verw:
        movl    4(%esp), %edx
        movl    $9f, fixup_eip
        xorl    %eax, %eax
        verw    %dx
        setz    %al
        movl    $0, fixup_eip
        ret
9:      movl    kfault_vec, %eax
        orl     $0x100, %eax
        ret

        .globl  try_rd_cr4
try_rd_cr4:
        movl    $9f, fixup_eip
        movl    %cr4, %eax
        movl    $0, fixup_eip
        movl    4(%esp), %edx
        movl    %eax, (%edx)
        xorl    %eax, %eax
        ret
9:      movl    kfault_vec, %eax
        incl    %eax
        ret

        .globl  try_wr_cr4
try_wr_cr4:
        movl    4(%esp), %eax
        movl    $9f, fixup_eip
        movl    %eax, %cr4
        movl    $0, fixup_eip
        xorl    %eax, %eax
        ret
9:      movl    kfault_vec, %eax
        incl    %eax
        ret

/* u32 try_rd_fs(u32 sel, u32 off): a load through FS = sel at off: 0, or
   1 + the vector of its fault (case V). The same instruction on every call,
   so the dynarec runs it from one compiled block. */
        .globl  try_rd_fs
try_rd_fs:
        pushl   %fs
        movl    8(%esp), %eax
        movw    %ax, %fs
        movl    12(%esp), %edx
        movl    $9f, fixup_eip
        movl    %fs:(%edx), %eax
        movl    $0, fixup_eip
        popl    %fs
        xorl    %eax, %eax
        ret
9:      popl    %fs
        movl    kfault_vec, %eax
        incl    %eax
        ret

/* u32 iretd_r0(u32 eflags): IRETD at CPL 0 to the next instruction with
   EFLAGS = eflags (no IF, TF or NT), then PUSHFD: returns the EFLAGS it
   shows. A UD2 after it leaves the EFLAGS image of its #UD in the trap
   frame (tf). The caller's EFLAGS are restored (case X). */
        .globl  iretd_r0
iretd_r0:
        pushfl
        pushl   8(%esp)
        pushl   %cs
        pushl   $1f
        iretl
1:      pushfl
        movl    $2f, fixup_eip
        ud2
2:      popl    %eax
        popfl
        ret

/* Case Y, debug-register breakpoints: try_dr_exec runs the NOP at
   dr_exec_at, try_dr_write stores v at p, try_dr_read loads from p. Each
   gives 0, or 1 + the vector of a trap or fault (trap_c continues at
   fixup_eip); the instruction after the access is at dr_write_after or
   dr_read_after. */
        .globl  try_dr_exec, dr_exec_at, try_dr_write, dr_write_after, try_dr_read, dr_read_after
try_dr_exec:
        movl    $9f, fixup_eip
dr_exec_at:
        nop
        movl    $0, fixup_eip
        xorl    %eax, %eax
        ret
9:      movl    kfault_vec, %eax
        incl    %eax
        ret

try_dr_write:
        movl    4(%esp), %edx
        movl    8(%esp), %eax
        movl    $9f, fixup_eip
        movl    %eax, (%edx)
dr_write_after:
        movl    $0, fixup_eip
        xorl    %eax, %eax
        ret
9:      movl    kfault_vec, %eax
        incl    %eax
        ret

try_dr_read:
        movl    4(%esp), %edx
        movl    $9f, fixup_eip
        movl    (%edx), %eax
dr_read_after:
        movl    $0, fixup_eip
        xorl    %eax, %eax
        ret
9:      movl    kfault_vec, %eax
        incl    %eax
        ret

/* u32 fpu_fault(void): 1/0 with the zero-divide exception unmasked, then
   FWAIT with interrupts enabled. 0 if nothing happened at the FWAIT, else
   1 + the vector (#MF is 16; an IRQ 13 is counted by trap_c instead). */
        .globl  fpu_fault
fpu_fault:
        movl    $9f, fixup_eip
        fninit
        fldcw   fpu_cw
        fld1
        fdivs   fpu_zero
        sti
        fwait
        nop
        nop
        nop
        nop
        cli
        movl    $0, fixup_eip
        fnclex
        fninit
        xorl    %eax, %eax
        ret
9:      cli
        fnclex
        fninit
        movl    kfault_vec, %eax
        incl    %eax
        ret

        .data
        .align  4
        .globl  ksave_esp, trap_vec, fixup_eip, kfault_vec, ret_farptr
ksave_esp:      .long 0
trap_vec:       .long 0
fixup_eip:      .long 0
kfault_vec:     .long 0
ret_farptr:     .long 0                 /* offset, then the selector */
                .word 0
fpu_cw:         .word 0x037B            /* default control word, zero-divide unmasked */
                .align 4
fpu_zero:       .float 0.0

        .bss
        .align  16
        .space  16384
stack_top:

        .section .note.GNU-stack, "", @progbits
