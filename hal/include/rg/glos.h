/* glos.h - GLOS's API 2.0 for RetroGPU's devices (hal/src/rg/glos.c). */
#ifndef RG_GLOS_H
#define RG_GLOS_H
#include "rg/device.h"
#if defined(__DJGPP__) && !defined(DGL_RIG)
#include <glos/api2.h>                  /* (GLOS's numbers and structures, from its checkout) */
#endif

int      rg_glos_call(unsigned fn, void *s);    /* an API 2.0 call: 0, or GLOS's error */
unsigned rg_glos_caps(void);                    /* GLOS_MACHINE_ bits; 0 without a GLOS that speaks 2.0 */
void    *rg_glos_ptr(unsigned linear);          /* a linear address GLOS gave, as a near pointer */
/* A window of w x h on GLOS's desktop: 1 when it fits the work area with a frame and a title bar, 2 when only full
   screen (up to the screen's size), 0 when not at all. */
int      rg_glos_fits(int w, int h);
/* Whether a window opened with these flags at this size is shown full screen (GLOS_WS_FULL): too big for a
   frame, or asked for (RG_GLOS_FULL) at the screen's size. */
int      rg_glos_full(unsigned glos_flags, int w, int h);

#endif
