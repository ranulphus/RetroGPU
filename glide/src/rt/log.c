/* log.c - runtime logging over COM1 (and, from M1, a log file). */
#include "rt/rt.h"
#include "mga/serial.h"

static int log_level = MG_LOG_INFO;
static int log_ready;

void mg_log_init(void)
{
    if (log_ready)
        return;
    serial_init(SERIAL_COM1, 115200);
    log_ready = 1;
}

void mg_log_set_level(int level) { log_level = level; }
int  mg_log_level(void) { return log_level; }

static void emit(const char *prefix, const char *fmt, va_list ap)
{
    char buf[256];
    int n = 0;
    if (!log_ready)
        mg_log_init();
    if (prefix)
        n = mg_snprintf(buf, sizeof buf, "%s", prefix);
    mg_vsnprintf(buf + n, sizeof buf - (size_t)n - 1, fmt, ap);
    serial_puts(buf);
    serial_puts("\n");
}

void mg_log(int level, const char *fmt, ...)
{
    static const char *const tag[] = { "MGL-E ", "MGL-W ", "MGL-I ", "MGL-D ", "MGL-T " };
    va_list ap;
    if (level > log_level)
        return;
    va_start(ap, fmt);
    emit(tag[level < 0 ? 0 : level > 4 ? 4 : level], fmt, ap);
    va_end(ap);
}

void mg_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(NULL, fmt, ap);
    va_end(ap);
}
