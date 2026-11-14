/* device.c - RetroGPU's devices and the choice of one (rg/device.h;
 * gl/docs/b1.md). Built with the HAL for DJGPP, the host and the rig, not
 * into Glide's Open Watcom build. */
#include "rg/device.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


static const rg_device *const devices[] = { &rg_mga_device, &rg_virge_device };   /* (DGL_DEVICE unset: in this order) */
#define NDEV ((int)(sizeof devices / sizeof devices[0]))


static char error[160];
static void (*log_fn)(const char *line);

const char *rg_error(void) { return error; }

void rg_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error, sizeof error, fmt, ap);
    va_end(ap);
}

void rg_set_log(void (*fn)(const char *line)) { log_fn = fn; }

void rg_log_line(const char *line)
{
    if (log_fn)
        log_fn(line);
}

void rg_logf(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    if (!log_fn)
        return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    log_fn(line);
}

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
        char why[sizeof error] = "";
        for (i = 0; i < n; i++) {
            error[0] = 0;
            if (list[i]->probe() == 0)
                return list[i];
            if (error[0] && strlen(why) + strlen(error) + 3 < sizeof why) {
                if (why[0])
                    strcat(why, "; ");
                strcat(why, error);             /* (each probe's reason, in order) */
            }
        }
        memcpy(error, why, sizeof error);
    }
    return NULL;
}


const rg_device *rg_choose(const char *want) { return rg_choose_from(devices, NDEV, want); }

/* The device GLOS can give a window to (its window_ok), or NULL. */
const rg_device *rg_window_device(void)
{
    int i;
    for (i = 0; i < NDEV; i++)
        if (devices[i]->window_ok && devices[i]->window_ok())
            return devices[i];
    return NULL;
}
