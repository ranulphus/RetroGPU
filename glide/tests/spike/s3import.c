/* s3import.c - spike S3: does DOS/4GW 1.97 resolve an EXE's import from a
 * DLL by itself? Linked with an import directive for one Glide function. */
#include "hx.h"
#include "glide/glide2.h"

int main(int argc, char **argv)
{
    char ver[80] = "";
    hx_init(argc, argv, "s3import");
    grGlideGetVersion(ver);
    hx_test("import", ver[0] != 0, "version=\"%s\"", ver);
    hx_done(0);
    return 0;
}
