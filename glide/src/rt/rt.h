/* rt.h - the runtime's minimal C support.
 *
 * The DLL links no C-library startup (it has no main and DOS/4GW may not
 * call DLL init), so it provides its own formatter and logging. Only
 * self-contained C-library routines (mem*, str*) are linked; see
 * tools/abi/check_clib.py. */
#ifndef MGA_RT_H
#define MGA_RT_H
#include <stdarg.h>
#include "mga/types.h"

int mg_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int mg_snprintf(char *buf, size_t size, const char *fmt, ...);

enum { MG_LOG_ERROR = 0, MG_LOG_WARN = 1, MG_LOG_INFO = 2, MG_LOG_DEBUG = 3, MG_LOG_TRACE = 4 };

void mg_log_init(void);
void mg_log_set_level(int level);
int  mg_log_level(void);
void mg_log(int level, const char *fmt, ...);
/* Raw protocol line for the harness, e.g. "MGL-INIT ..." (always emitted). */
void mg_line(const char *fmt, ...);

/* env.c: whole-file and streamed reads (DOS handles; -1 on error) */
int  mg_file_open(const char *path);
int  mg_file_read(int handle, void *buf, int size);
int  mg_file_create(const char *path);            /* create / truncate for writing */
int  mg_file_write(int handle, const void *buf, int size);
void mg_file_close(int handle);

/* math.c */
double mg_exp(double x);
double mg_ln(double v);

#endif
