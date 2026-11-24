/* device.c - RetroGPU's devices and the choice of one (rg/device.h;
 * gl/docs/b1.md). Built with the HAL for DJGPP, the host and the rig, not
 * into Glide's Open Watcom build (which takes its device itself; the log and
 * the errors are log.c's). */
#include "rg/device.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const rg_device *const devices[] = { &rg_mga_device, &rg_virge_device, &rg_tnt2_device, &rg_r128_device,
                                            &rg_radeon_device, &rg_vbe_device, &rg_mem_device };   /* (DGL_DEVICE unset: in this order) */
#define NDEV ((int)(sizeof devices / sizeof devices[0]))


const rg_device *rg_choose_from(const rg_device *const *list, int n, const char *want)
{
    int i;
    if (want && *want) {
        for (i = 0; i < n; i++)
            if (!strcmp(list[i]->name, want))
                return list[i]->probe() == 0 ? list[i] : NULL;   /* (probe set the reason) */
        rg_set_error("DGL-DEVICE %s: not built", want);
        return NULL;
    }
    {
        char why[160] = "";
        for (i = 0; i < n; i++) {
            if (list[i]->flags & RG_DEV_BY_NAME)
                continue;
            const char *e;
            rg_set_error("%s", "");
            if (list[i]->probe() == 0)
                return list[i];
            e = rg_error();
            if (e[0] && strlen(why) + strlen(e) + 3 < sizeof why) {
                if (why[0])
                    strcat(why, "; ");
                strcat(why, e);                 /* (each probe's reason, in order) */
            }
        }
        rg_set_error("%s", why);
    }
    return NULL;
}


const rg_device *rg_choose(const char *want) { return rg_choose_from(devices, NDEV, want); }

/* The device GLOS can give a window to (its window_ok), or NULL: the one
   DGL_DEVICE names if it can (DGL_DEVICE=mem: the software renderer's window
   on a desktop the Matrox device could draw in too), else the first that can
   (the Matrox device on a Matrox card's 16-bit desktop; the memory device,
   the software renderer, on any other: gl/docs/backends.md B3). */
const rg_device *rg_window_device(void)
{
    const char *want = getenv("DGL_DEVICE");
    int i;
    if (want && *want)
        for (i = 0; i < NDEV; i++)
            if (!strcmp(devices[i]->name, want) && devices[i]->window_ok && devices[i]->window_ok())
                return devices[i];
    for (i = 0; i < NDEV; i++)
        if (devices[i]->window_ok && devices[i]->window_ok())
            return devices[i];
    return NULL;
}
