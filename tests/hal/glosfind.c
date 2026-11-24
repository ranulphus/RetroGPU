/* glosfind.c - whether a DOS/4GW program finds GLOS's entry (hal/port/ow_dos4g.c: DPMI 0A00h "GLOS", then INT 2Fh
 * AX=168Ah, which DOS/4GW passes to the host). One DRAW CACHE call with no address and no size: GLOS refuses it
 * (GLOS_E_ARGUMENT, 3) and changes nothing; without GLOS (plain DOS, another host) the port answers -1 itself.
 *   GLOSFIND           GLOSFIND found=1 err=3 under GLOS; found=0 err=-1 elsewhere (both PASS: what was found is
 *                      the line's news; --glos makes "not found" a FAIL) */
#include "hx.h"
#include "mga/sys.h"
#include <string.h>

int main(int argc, char **argv)
{
    int i, want_glos = 0, r;
    hx_init(1, argv, "glosfind");               /* (its own arguments only) */
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--glos"))
            want_glos = 1;
    r = sys_cache_type(0, 0, 1u);
    hx_log("GLOSFIND found=%d err=%d", sys_cache_wc_err != -1, sys_cache_wc_err);
    hx_test("lookup", r != 0 && (!want_glos || sys_cache_wc_err != -1), "err %d", sys_cache_wc_err);
    hx_done(0);
    return 0;
}
