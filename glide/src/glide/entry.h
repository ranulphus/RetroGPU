/* entry.h - how a Glide export is defined.
 *
 * Every public function is __stdcall and exported by name; the linker
 * (option nocaseexact) produces the uppercase decorated names games import,
 * e.g. _GRDRAWTRIANGLE@12. */
#ifndef MGA_GLIDE_ENTRY_H
#define MGA_GLIDE_ENTRY_H
#include "glide/glide2.h"
#include "api_ids.h"

#if defined(__WATCOMC__)
#  define GR_EXPORT __export
#else
#  define GR_EXPORT
#endif

/* GR_ENTRY defines the implementation, impl_<name>; the exported <name> is
 * a generated thunk (build/gen/thunks.c) that records the call when
 * tracing and then calls it. Internal calls use the public name, so they
 * go through the thunk too (and are not recorded: only the outermost
 * call is). */
#define GR_ENTRY(ret, name, args) ret GLIDE_API impl_##name args

/* Bumped by every exported non-draw call (thunks.c): draw-time plans are
 * recomputed only when it has changed. */
extern FxU32 mg_state_gen;

/* First-call bookkeeping for stubs and the call tracer. */
void mg_stub_hit(int id);

#endif
