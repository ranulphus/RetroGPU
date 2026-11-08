/* math.c - exp and ln without libm (the DLL links no floating-point
 * library). Accurate to a few ulps of float over the ranges used: gamma
 * ramps and fog tables. */
#include "rt/rt.h"

double mg_exp(double x)
{
    double r = 1.0, term = 1.0;
    int i, k = 0;
    while (x > 1.0) { x *= 0.5; k++; }
    while (x < -1.0) { x *= 0.5; k++; }
    for (i = 1; i < 20; i++) { term *= x / i; r += term; }
    while (k--) r *= r;
    return r;
}

double mg_ln(double v)
{
    double z, z2, ln;
    int e = 0;
    if (v <= 0)
        return -1e300;
    while (v >= 2.0) { v *= 0.5; e++; }
    while (v < 1.0) { v *= 2.0; e--; }
    z = (v - 1.0) / (v + 1.0);
    z2 = z * z;
    ln = 2.0 * z * (1.0 + z2 * (1.0 / 3 + z2 * (1.0 / 5 + z2 * (1.0 / 7 + z2 * (1.0 / 9 + z2 / 11)))));
    return ln + e * 0.6931471805599453;
}
