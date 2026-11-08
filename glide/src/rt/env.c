/* env.c - environment and small file reads for the DLL (no C startup).
 *
 * Under DOS/4GW, INT 21h AH=62h returns the PSP as a selector; its base
 * comes from DPMI 0006h. The environment field (PSP:2Ch) may hold a
 * selector or a real-mode segment, so both are tried. */
#include "rt/rt.h"
#include <string.h>

#if defined(__WATCOMC__) && defined(__386__)
#include <i86.h>

static uint32_t sel_base(uint16_t sel, int *ok)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0006;
    r.w.bx = sel;
    int386(0x31, &r, &r);
    *ok = !r.x.cflag;
    return ((uint32_t)r.w.cx << 16) | r.w.dx;
}

static const char *env_block(void)
{
    union REGS r;
    int ok;
    uint32_t psp, envbase;
    uint16_t envw;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x62;
    int386(0x21, &r, &r);
    psp = sel_base(r.w.bx, &ok);
    if (!ok)
        psp = (uint32_t)r.w.bx << 4;
    envw = *(const uint16_t *)(psp + 0x2C);
    envbase = sel_base(envw, &ok);
    if (!ok || envbase == 0)
        envbase = (uint32_t)envw << 4;
    return (const char *)envbase;
}

const char *mg_getenv(const char *name)
{
    const char *e = env_block();
    size_t n = strlen(name);
    int guard = 0;
    while (*e && guard++ < 512) {
        if (!strncmp(e, name, n) && e[n] == '=')
            return e + n + 1;
        e += strlen(e) + 1;
    }
    return NULL;
}

int mg_read_file(const char *path, char *buf, int size)
{
    union REGS r;
    int handle, got;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x3D00;
    r.x.edx = (uint32_t)path;
    int386(0x21, &r, &r);
    if (r.x.cflag)
        return -1;
    handle = r.w.ax;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x3F;
    r.w.bx = (uint16_t)handle;
    r.x.ecx = (uint32_t)(size - 1);
    r.x.edx = (uint32_t)buf;
    int386(0x21, &r, &r);
    got = r.x.cflag ? -1 : (int)r.x.eax;      /* 32-bit count: files may exceed 64 KB */
    memset(&r, 0, sizeof r);
    r.h.ah = 0x3E;
    r.w.bx = (uint16_t)handle;
    int386(0x21, &r, &r);
    if (got >= 0)
        buf[got] = 0;
    return got;
}

int mg_file_open(const char *path)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x3D00;
    r.x.edx = (uint32_t)path;
    int386(0x21, &r, &r);
    return r.x.cflag ? -1 : (int)r.w.ax;
}

int mg_file_read(int handle, void *buf, int size)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x3F;
    r.w.bx = (uint16_t)handle;
    r.x.ecx = (uint32_t)size;
    r.x.edx = (uint32_t)buf;
    int386(0x21, &r, &r);
    return r.x.cflag ? -1 : (int)r.x.eax;
}

int mg_file_create(const char *path)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x3C;
    r.w.cx = 0;
    r.x.edx = (uint32_t)path;
    int386(0x21, &r, &r);
    return r.x.cflag ? -1 : (int)r.w.ax;
}

int mg_file_write(int handle, const void *buf, int size)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x40;
    r.w.bx = (uint16_t)handle;
    r.x.ecx = (uint32_t)size;
    r.x.edx = (uint32_t)buf;
    int386(0x21, &r, &r);
    return r.x.cflag ? -1 : (int)r.x.eax;
}

void mg_file_close(int handle)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x3E;
    r.w.bx = (uint16_t)handle;
    int386(0x21, &r, &r);
}

int mg_write_file(const char *path, const void *data, int size, int append)
{
    union REGS r;
    int handle;
    memset(&r, 0, sizeof r);
    if (append) {
        r.w.ax = 0x3D01;
        r.x.edx = (uint32_t)path;
        int386(0x21, &r, &r);
    }
    if (!append || r.x.cflag) {
        memset(&r, 0, sizeof r);
        r.h.ah = 0x3C;
        r.w.cx = 0;
        r.x.edx = (uint32_t)path;
        int386(0x21, &r, &r);
        if (r.x.cflag)
            return -1;
    } else {
        handle = r.w.ax;
        memset(&r, 0, sizeof r);
        r.w.ax = 0x4202;
        r.w.bx = (uint16_t)handle;
        int386(0x21, &r, &r);
        r.w.ax = (uint16_t)handle;
    }
    handle = r.w.ax;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x40;
    r.w.bx = (uint16_t)handle;
    r.x.ecx = (uint32_t)size;
    r.x.edx = (uint32_t)data;
    int386(0x21, &r, &r);
    memset(&r, 0, sizeof r);
    r.h.ah = 0x3E;
    r.w.bx = (uint16_t)handle;
    int386(0x21, &r, &r);
    return 0;
}
#else
#include <stdio.h>
#include <stdlib.h>
const char *mg_getenv(const char *name) { return getenv(name); }
int mg_read_file(const char *path, char *buf, int size)
{
    FILE *f = fopen(path, "rb");
    int n;
    if (!f) return -1;
    n = (int)fread(buf, 1, (size_t)size - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}
static FILE *host_files[8];
int mg_file_open(const char *path)
{
    int i;
    for (i = 0; i < 8; i++)
        if (!host_files[i]) {
            host_files[i] = fopen(path, "rb");
            return host_files[i] ? i : -1;
        }
    return -1;
}
int mg_file_read(int h, void *buf, int size) { return (int)fread(buf, 1, (size_t)size, host_files[h]); }
int mg_file_create(const char *path)
{
    int i;
    for (i = 0; i < 8; i++)
        if (!host_files[i]) {
            host_files[i] = fopen(path, "wb");
            return host_files[i] ? i : -1;
        }
    return -1;
}
int mg_file_write(int h, const void *buf, int size) { return (int)fwrite(buf, 1, (size_t)size, host_files[h]); }
void mg_file_close(int h) { fclose(host_files[h]); host_files[h] = NULL; }
int mg_write_file(const char *path, const void *data, int size, int append)
{
    FILE *f = fopen(path, append ? "ab" : "wb");
    if (!f) return -1;
    fwrite(data, 1, (size_t)size, f);
    fclose(f);
    return 0;
}
#endif
