/* log.c - RetroGPU's device log and error text (rg/device.h): the lines a device prints go to the program's
 * logger, its last failure's reason to rg_error. Built with the HAL everywhere, Glide's Open Watcom runtime
 * included (with the runtime's own formatter there: rg_set_format). */
#include "rg/device.h"
#include <stdarg.h>
#include <string.h>
#if !defined(MGA_OW)
#include <stdio.h>
#endif

static char error[160];
static void (*log_fn)(const char *line);
#if defined(MGA_OW)
static int (*format_fn)(char *buf, size_t size, const char *fmt, va_list ap);
#else
static int (*format_fn)(char *buf, size_t size, const char *fmt, va_list ap) = vsnprintf;
#endif

void rg_set_format(int (*fn)(char *buf, size_t size, const char *fmt, va_list ap)) { format_fn = fn; }

static void format(char *buf, size_t size, const char *fmt, va_list ap)
{
    if (format_fn)
        format_fn(buf, size, fmt, ap);
    else {
        size_t n = strlen(fmt);
        if (n >= size)
            n = size - 1;
        memcpy(buf, fmt, n);
        buf[n] = 0;
    }
}

const char *rg_error(void) { return error; }

void rg_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    format(error, sizeof error, fmt, ap);
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
    format(line, sizeof line, fmt, ap);
    va_end(ap);
    log_fn(line);
}
