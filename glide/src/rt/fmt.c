/* fmt.c - small printf-style formatter (%d %i %u %x %X %p %s %c %f %%,
 * with width, zero padding, '-' and 'l'). No floating-point beyond %f
 * with fixed precision, which the logs use for vertex dumps. */
#include "rt/rt.h"

typedef struct { char *p; size_t left; int n; } out_t;

static void put(out_t *o, char c)
{
    if (o->left > 1) { *o->p++ = c; o->left--; }
    o->n++;
}

static void put_str(out_t *o, const char *s, int width, int left)
{
    int len = 0, i;
    const char *t = s ? s : "(null)";
    while (t[len]) len++;
    if (!left) for (i = len; i < width; i++) put(o, ' ');
    for (i = 0; i < len; i++) put(o, t[i]);
    if (left) for (i = len; i < width; i++) put(o, ' ');
}

static void put_num(out_t *o, uint32_t v, int neg, unsigned base, int upper,
                    int width, int zero, int left)
{
    char tmp[16];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0, len, i;
    do { tmp[n++] = dig[v % base]; v /= base; } while (v);
    len = n + (neg ? 1 : 0);
    if (!left && !zero) for (i = len; i < width; i++) put(o, ' ');
    if (neg) put(o, '-');
    if (!left && zero) for (i = len; i < width; i++) put(o, '0');
    while (n) put(o, tmp[--n]);
    if (left) for (i = len; i < width; i++) put(o, ' ');
}

int mg_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    out_t o;
    o.p = buf; o.left = size; o.n = 0;
    for (; *fmt; fmt++) {
        int width = 0, zero = 0, left = 0, prec = -1;
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') { prec = 0; fmt++; while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0'); }
        while (*fmt == 'l' || *fmt == 'h') fmt++;
        switch (*fmt) {
        case 'd': case 'i': {
            int32_t v = va_arg(ap, int32_t);
            put_num(&o, v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v, v < 0, 10, 0, width, zero, left);
            break; }
        case 'u': put_num(&o, va_arg(ap, uint32_t), 0, 10, 0, width, zero, left); break;
        case 'x': put_num(&o, va_arg(ap, uint32_t), 0, 16, 0, width, zero, left); break;
        case 'X': put_num(&o, va_arg(ap, uint32_t), 0, 16, 1, width, zero, left); break;
        case 'p': put(&o, '0'); put(&o, 'x');
                  put_num(&o, (uint32_t)(uintptr_t)va_arg(ap, void *), 0, 16, 0, 8, 1, 0); break;
        case 's': put_str(&o, va_arg(ap, const char *), width, left); break;
        case 'c': put(&o, (char)va_arg(ap, int)); break;
        case 'f': {
            double v = va_arg(ap, double);
            uint32_t ip, fp, scale = 1;
            int i, neg = v < 0;
            if (prec < 0) prec = 3;
            if (prec > 6) prec = 6;
            for (i = 0; i < prec; i++) scale *= 10;
            if (neg) v = -v;
            if (v > 4.0e9) { put_str(&o, neg ? "-big" : "big", width, left); break; }
            ip = (uint32_t)v;
            fp = (uint32_t)((v - (double)ip) * scale + 0.5);
            if (fp >= scale) { ip++; fp -= scale; }
            put_num(&o, ip, neg, 10, 0, 0, 0, 0);
            if (prec) { put(&o, '.'); put_num(&o, fp, 0, 10, 0, prec, 1, 0); }
            break; }
        case '%': put(&o, '%'); break;
        default: put(&o, '%'); if (*fmt) put(&o, *fmt); else fmt--; break;
        }
    }
    if (size) *o.p = '\0';
    return o.n;
}

int mg_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = mg_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
