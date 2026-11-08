/* ct_dos.c - DOS side of the conformance harness: DOS-GL + the HX shim. */
#include "ct.h"
#include "hx.h"
#include <GL/dosgl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CT_NAME
#define CT_NAME "conform"
#endif

void ct_frame(const char *name)
{
    static unsigned char rgba[CT_W * CT_H * 4], rgb[CT_W * CT_H * 3];
    int x, y;
    glReadPixels(0, 0, CT_W, CT_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (y = 0; y < CT_H; y++)                 /* GL rows are bottom-up; PPM top-down */
        for (x = 0; x < CT_W; x++) {
            const unsigned char *s = &rgba[((CT_H - 1 - y) * CT_W + x) * 4];
            unsigned char *d = &rgb[(y * CT_W + x) * 3];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    hx_save_ppm(name, CT_W, CT_H, rgb);
}

void ct_log(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    hx_log("HX-STAT %s", buf);
}

int main(int argc, char **argv)
{
    GLenum e;
    hx_init(argc, argv, CT_NAME);
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    ct_run();
    e = glGetError();
    hx_test("gl-errors", e == GL_NO_ERROR, "first error %04x", e);
    dglShutdown();
    hx_done(0);
    return 0;
}
