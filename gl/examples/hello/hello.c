/* hello - M0: a DJGPP program linked against libGL.a that reports over the
 * shared HX- serial protocol, for the Loop A and bench self-tests:
 * PASS normally, FAIL with --fail, never finishes with --hang. */
#include "hx.h"
#include <GL/dosgl.h>

int main(int argc, char **argv)
{
    hx_init(argc, argv, "hello");
    hx_test("library", dglVersion()[0] == 'D', "%s", dglVersion());
    if (hx_args.fail)
        hx_test("forced", 0, "--fail given");
    if (hx_args.hang)
        for (;;)
            ;
    hx_snap_screen("hello");
    hx_done(0);
    return 0;
}
