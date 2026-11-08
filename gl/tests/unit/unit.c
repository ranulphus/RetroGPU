/* unit.c - runs unit_run() and reports. */
#include "unit.h"

int unit_failures, unit_checks;

int main(void)
{
    unit_run();
    printf("%d checks, %d failures\n", unit_checks, unit_failures);
    return unit_failures != 0;
}
