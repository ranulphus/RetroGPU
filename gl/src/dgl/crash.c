/* crash.c - leave the machine usable after any crash (PRD G7, FR-DBG-7..9).
 *
 * The HAL port's hooks run on #GP, #PF, #UD and divide error, on normal
 * exit (atexit) and on Ctrl-Break; each resets the drawing engine and
 * restores text mode, and a fault also logs the exception and, in builds
 * with the register write trace, the last register writes. */
#include "dgl.h"
#include "mga/sys.h"

static int installed;

void dgl_teardown(void)
{
    dgl_note_exit();
    if (dgl_dev)
        dgl_dev->close();               /* (nothing when it never left text mode) */
    dgl_ctx.active = 0;
}

static void on_fault(int exc, uint32_t err, uint32_t eip)
{
    dgl_teardown();
    DGL_ERR("DGL-FAULT exc=%d err=%lx eip=%08lx", exc, (unsigned long)err, (unsigned long)eip);
    if (dgl_dev)
        dgl_dev->crash();               /* (the last register writes, in builds with the trace) */
}

static void on_exit_hook(void)
{
    dgl_ftrace_write();
    dgl_teardown();
}

void dgl_crash_install(void)
{
    if (installed)
        return;
    if (sys_hook_faults(on_fault) < 0 || sys_hook_exit(on_exit_hook) < 0)
        DGL_WARN("DGL-WARN crash hooks unavailable in this port");
    installed = 1;
}
