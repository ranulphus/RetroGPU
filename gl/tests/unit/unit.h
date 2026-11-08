/* unit.h - minimal host unit-test harness. */
#ifndef DGL_UNIT_H
#define DGL_UNIT_H
#include <math.h>
#include <stdio.h>

extern int unit_failures, unit_checks;

#define CHECK(cond) do { unit_checks++; if (!(cond)) { unit_failures++; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); unit_checks++; \
    if (fabs(_a - _b) > (eps)) { unit_failures++; fprintf(stderr, "%s:%d: %s = %g, expected %g\n", \
    __FILE__, __LINE__, #a, _a, _b); } } while (0)

void unit_run(void);
#endif
