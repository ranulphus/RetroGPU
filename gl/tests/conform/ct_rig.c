/* ct_rig.c - Loop C side of the conformance harness: DOS-GL built for
 * Linux (make rig) drawing on a real card through the HAL's Linux port
 * (sysfs resource files of RIG_BDF). Frames go to $CT_OUT (default .) as
 * <name>.ppm, read back from VRAM; results are HX- lines on stdout. */
#include "ct.h"
#include <GL/dosgl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CT_NAME
#define CT_NAME "conform"
#endif

void ct_frame(const char *name)
{
    static unsigned char rgba[CT_W * CT_H * 4];
    char path[512];
    const char *dir = getenv("CT_OUT");
    FILE *f;
    int x, y;
    glReadPixels(0, 0, CT_W, CT_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    snprintf(path, sizeof path, "%s/%s.ppm", dir ? dir : ".", name);
    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", CT_W, CT_H);
    for (y = CT_H - 1; y >= 0; y--)            /* GL rows are bottom-up; PPM top-down */
        for (x = 0; x < CT_W; x++)
            fwrite(&rgba[(y * CT_W + x) * 4], 1, 3, f);
    fclose(f);
    printf("HX-IMG %s %dx%d\n", name, CT_W, CT_H);
}

void ct_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("HX-STAT ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

int main(void)
{
    DGLConfig c = { 0 };
    GLenum e;
    c.width = CT_W;
    c.height = CT_H;
    c.double_buffer = 1;
    c.depth_bits = 16;
    c.vsync = 0;                               /* no display: nothing to wait for */
    printf("HX-START %s %s\n", CT_NAME, dglVersion());
    if (dglInit(&c) != 0) {
        printf("HX-TEST init FAIL %s\nHX-DONE 2\n", dglGetErrorString());
        return 2;
    }
    printf("HX-STAT device %s\n", dglGetDeviceInfo()->chip_name);
    ct_run();
    e = glGetError();
    printf("HX-TEST gl-errors %s first error %04x\n", e == GL_NO_ERROR ? "PASS" : "FAIL", e);
    dglShutdown();
    printf("HX-DONE 0\n");
    return 0;
}
