/* Structure layouts must match the shipped Glide 2.x ABI (glide/docs/abi-facts.md).
 * Checked on the host; the same asserts run under Open Watcom in the DOS
 * builds via glide/include/glide/layout.h. */
#include "unit.h"
#include "glide/layout.h"

int unit_main(void)
{
    CHECK_EQ(sizeof(GrTmuVertex), 12);
    CHECK_EQ(GR_TEXFMT_RGB_565, 0xa);
    CHECK_EQ(GR_RESOLUTION_640x480, 7);
    CHECK_EQ(GR_FOG_TABLE_SIZE, 64);
    return 0;
}
