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
/* Whether a window opened with these flags at this size is shown full screen (GLOS_WS_FULL): 1 at the screen's size
   (too big for a frame, or asked for with RG_GLOS_FULL at the screen's size); 2 asked for and smaller than the
   screen, keeping its own size (GLOS 2.8's GLOS_WF_OWN_MODE: shown centred on black, or in a screen mode of its
   size); 0 framed. */
int      rg_glos_full(unsigned glos_flags, int w, int h);
/* GLOS_WIN_CREATE for a window full (rg_glos_full's) or framed: its flags set here (fixed, anywhere, bare when asked
   or full, GLOS_WF_OWN_MODE for 2); an older GLOS that refuses the own mode (GLOS_E_ARGUMENT) makes it framed,
   *full then 0. GLOS's answer. (w: a struct glos_win_create, its size, kind, size and title set.) */
int      rg_glos_win_create(void *w, unsigned glos_flags, int *full);

#endif
