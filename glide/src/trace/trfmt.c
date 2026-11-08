/* trfmt.c - trace-format helpers shared by the runtime and GLPLAY. */
#include "trace/trace.h"

uint32_t tr_vhash(const void *v)
{
    const uint8_t *p = (const uint8_t *)v;
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < 60; i++)
        h = (h ^ p[i]) * 16777619u;
    return h & (TR_VCACHE - 1);
}

void tr_bhash(const void *p, uint32_t n, uint32_t *h1, uint32_t *h2)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t a = 2166136261u, c = 0x811C9DC5u ^ n, i;
    for (i = 0; i < n; i++) {
        a = (a ^ b[i]) * 16777619u;
        c = (c * 31u) + b[i] + (c >> 17);
    }
    *h1 = a;
    *h2 = c;
}
