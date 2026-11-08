/* ct_host.c - host side of the conformance harness: Mesa OSMesa renders the
 * reference frames (D16). Frames go to $CT_OUT (default .) as <name>.ppm. */
#include "ct.h"
#include <GL/osmesa.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char buffer[CT_W * CT_H * 4];

void ct_frame(const char *name)
{
    static unsigned char rgba[CT_W * CT_H * 4];
    char path[512];
    const char *dir = getenv("CT_OUT");
    FILE *f;
    int y, x;
    glFinish();
    glReadPixels(0, 0, CT_W, CT_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    snprintf(path, sizeof path, "%s/%s.ppm", dir ? dir : ".", name);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", CT_W, CT_H);
    for (y = CT_H - 1; y >= 0; y--)
        for (x = 0; x < CT_W; x++)
            fwrite(&rgba[(y * CT_W + x) * 4], 1, 3, f);
    fclose(f);
}

void ct_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

int main(void)
{
    /* RGBA8 with a 16-bit depth buffer, as DOS-GL's context. */
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buffer, GL_UNSIGNED_BYTE, CT_W, CT_H)) {
        fprintf(stderr, "ct_host: OSMesa context failed\n");
        return 2;
    }
    glViewport(0, 0, CT_W, CT_H);
    ct_run();
    if (glGetError() != GL_NO_ERROR)
        fprintf(stderr, "ct_host: GL error\n");
    OSMesaDestroyContext(ctx);
    return 0;
}
