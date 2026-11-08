/* dllmain.c - LE DLL entry point.
 *
 * Whether DOS/4GW calls a DLL's initialisation routine, and how, is an
 * open question (spike S2), so nothing here may be required: all real
 * initialisation happens in grGlideInit. */
#include "rt/rt.h"
#include "glide/entry.h"
#include "api_names.h"

int mg_dll_started;
FxU32 mg_state_gen = 1;

int __DLLstart_(void *inst, unsigned reason)
{
    mg_dll_started++;
    mg_log_init();
    mg_line("MGL-DLLSTART inst=%p reason=%u count=%d", inst, reason, mg_dll_started);
    return 1;
}

static uint8_t stub_seen[MGA_API_COUNT];

void mg_stub_hit(int id)
{
    if (id < 0 || id >= MGA_API_COUNT || stub_seen[id])
        return;
    stub_seen[id] = 1;
    mg_log(MG_LOG_WARN, "unimplemented %s", mga_api_names[id]);
}
