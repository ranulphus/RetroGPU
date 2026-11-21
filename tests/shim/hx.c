/* hx.c - guest test shim implementation. Toolchain-neutral: port I/O and
 * real-mode memory go through the HAL port (mga/sys.h), so the same file
 * builds with Open Watcom (DOS/4GW) and DJGPP (CWSDPMI). */
#include "hx.h"
#include "mga/serial.h"
#include "mga/sys.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__)
#  include <i86.h>
#  define irq_off() _disable()
#  define irq_on()  _enable()
#elif defined(__DJGPP__)
#  include <dos.h>
#  define irq_off() disable()
#  define irq_on()  enable()
#else
#  define irq_off() ((void)0)
#  define irq_on()  ((void)0)
#endif
#ifndef HX_BUILD_ID
#  define HX_BUILD_ID "unknown"
#endif
#define inp(p)     sys_inb(p)
#define outp(p, v) sys_outb((p), (uint8_t)(v))

hx_args_t hx_args;
static int hx_nfail;
static int ut_base;

#define UT_IOBASE 0x0E80

/* Echo to the console only in a text mode: in a graphics mode the BIOS
 * draws the glyphs through the legacy VGA window, which lands in VRAM (the
 * second colour buffer at 640x480) and spoils the frames under test. */
static int text_mode(void)
{
#if defined(__WATCOMC__) || defined(__DJGPP__)
    uint8_t mode = *(volatile uint8_t *)sys_real_ptr(0x40, 0x49);  /* BIOS data area: video mode */
    return mode <= 3 || mode == 7;
#else
    return 1;
#endif
}

/* HX_LOGFILE: every line appended to that file too (a GLOS direct-mode session owns the PC's ports: its COM1
 * lines go to the real port, not to GLOS's log). */
static void to_logfile(const char *s)
{
    static int tried;
    static FILE *f;
    if (!tried) {
        const char *e = getenv("HX_LOGFILE");
        tried = 1;
        if (e && *e)
            f = fopen(e, "a");
    }
    if (f) {
        fputs(s, f);
        fputc('\n', f);
        fflush(f);
    }
}

static void line(const char *s)
{
    serial_puts(s);
    serial_puts("\n");
    to_logfile(s);
    if (text_mode()) {
        fputs(s, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
}

static void vline(const char *prefix, const char *fmt, va_list ap)
{
    char buf[300];
    int n = snprintf(buf, sizeof buf, "%s", prefix);
    vsnprintf(buf + n, sizeof buf - n, fmt, ap);
    line(buf);
}

void hx_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vline("", fmt, ap);
    va_end(ap);
}

void hx_stat(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vline("HX-STAT ", fmt, ap);
    va_end(ap);
}

void hx_test(const char *name, int pass, const char *fmt, ...)
{
    char pre[96];
    va_list ap;
    if (!pass)
        hx_nfail++;
    snprintf(pre, sizeof pre, "HX-TEST %s %s ", name, pass ? "PASS" : "FAIL");
    va_start(ap, fmt);
    vline(pre, fmt ? fmt : "", ap);
    va_end(ap);
}

int hx_failures(void) { return hx_nfail; }

/* ---- 86Box unit tester ---------------------------------------------- */
static void ut_setbase(int base)
{
    static const char magic[] = "86Box";
    int i;
    irq_off();
    for (i = 0; i < 5; i++)
        outp(0x80, magic[i]);
    outp(0x80, base & 0xFF);
    outp(0x80, (base >> 8) & 0xFF);
    irq_on();
}

int hx_ut_present(void) { return ut_base != 0; }

static int ut_wait(int mask)
{
    long spin = 0;
    int st;
    do {
        st = inp(ut_base);
        if (st & 0x08)
            return -1;
    } while (!(st & mask) && ++spin < 50000000L);
    return (st & mask) ? 0 : -1;
}

static void ut_put16(int v) { outp(ut_base + 1, v & 0xFF); outp(ut_base + 1, (v >> 8) & 0xFF); }
static int ut_get16(void) { int lo = inp(ut_base + 1); return lo | (inp(ut_base + 1) << 8); }

int hx_ut_capture(uint16_t *w, uint16_t *h)
{
    int i, v[6];
    if (!ut_base)
        return -1;
    outp(ut_base, 0x01);
    if (ut_wait(0x02) < 0)
        return -1;
    outp(ut_base + 1, 0x01);             /* primary monitor */
    if (ut_wait(0x01) < 0)
        return -1;
    for (i = 0; i < 6; i++)
        v[i] = ut_get16();
    *w = (uint16_t)v[0];
    *h = (uint16_t)v[1];
    return 0;
}

static int ut_rect_cmd(int cmd, int x, int y, int w, int h)
{
    outp(ut_base, cmd);
    if (ut_wait(0x02) < 0)
        return -1;
    ut_put16(w); ut_put16(h); ut_put16(x); ut_put16(y);
    return ut_wait(0x01);
}

uint32_t hx_ut_crc(int x, int y, int w, int h)
{
    uint32_t c;
    if (!ut_base || ut_rect_cmd(0x03, x, y, w, h) < 0)
        return 0;
    c = (uint32_t)inp(ut_base + 1);
    c |= (uint32_t)inp(ut_base + 1) << 8;
    c |= (uint32_t)inp(ut_base + 1) << 16;
    c |= (uint32_t)inp(ut_base + 1) << 24;
    return c;
}

int hx_ut_read(int x, int y, int w, int h, uint8_t *bgrx)
{
    long n = (long)w * h * 4, i;
    if (!ut_base || ut_rect_cmd(0x02, x, y, w, h) < 0)
        return -1;
    for (i = 0; i < n; i++)
        bgrx[i] = (uint8_t)inp(ut_base + 1);
    return 0;
}

void hx_ut_exit(int code)
{
    if (!ut_base)
        return;
    outp(ut_base, 0x04);
    if (ut_wait(0x02) == 0)
        outp(ut_base + 1, code & 0x7F);
}

/* ---- Images ---------------------------------------------------------- */
uint32_t hx_crc32(uint32_t crc, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    int k;
    crc = ~crc;
    while (n--) {
        crc ^= *b++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int hx_save_ppm(const char *name, int w, int h, const uint8_t *rgb)
{
    char path[128];
    FILE *f;
    uint32_t crc = hx_crc32(0, rgb, (uint32_t)w * h * 3);
    snprintf(path, sizeof path, "%s\\%s.PPM", hx_args.out, name);
    f = fopen(path, "wb");
    if (!f) {
        hx_log("HX-IMG %s ERROR cannot write %s", name, path);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 3, (size_t)w * h, f);
    fclose(f);
    hx_log("HX-IMG %s %dx%d crc=%08lx", name, w, h, (unsigned long)crc);
    return 0;
}

/* On a bench PC there is no unit tester: the host grabs the frame from its
 * capture device when it sees HX-CAPTURE (tools/bench/run.py). HX_CAPWAIT
 * (seconds, set by the bench job) holds the frame on screen meanwhile. */
static int hx_bench_capture(const char *name)
{
    const char *e = getenv("HX_CAPWAIT");
    long ticks = e ? atol(e) * 182L / 10 : 0;
#if defined(__WATCOMC__) || defined(__DJGPP__)
    volatile uint32_t *bios_ticks = (volatile uint32_t *)sys_real_ptr(0x40, 0x6C);
    uint32_t t0;
#endif
    if (ticks <= 0)
        return -1;
    hx_log("HX-CAPTURE %s", name);
#if defined(__WATCOMC__) || defined(__DJGPP__)
    t0 = *bios_ticks;
    while ((long)(*bios_ticks - t0) < ticks && (long)(*bios_ticks - t0) >= 0)
        ;
#endif
    return 0;
}

int hx_snap_screen(const char *name)
{
    uint16_t w, h;
    uint8_t *px, *rgb;
    long i, n;
    int rc, f;
    long spin;
    /* The emulator redraws its screen buffer scanline by scanline, so a
     * capture straight after drawing can hold part of the previous frame.
     * Let two full frames go by first (bounded, in case retrace is dead). */
    for (f = 0; f < 3; f++) {
        for (spin = 0; spin < 2000000L && (inp(0x3DA) & 8); spin++)
            ;
        for (spin = 0; spin < 2000000L && !(inp(0x3DA) & 8); spin++)
            ;
    }
    if (!ut_base)
        return hx_bench_capture(name);
    if (hx_ut_capture(&w, &h) < 0 || !w || !h)
        return -1;
    n = (long)w * h;
    px = (uint8_t *)malloc(n * 4);
    rgb = (uint8_t *)malloc(n * 3);
    if (!px || !rgb) { free(px); free(rgb); return -1; }
    rc = hx_ut_read(0, 0, w, h, px);
    for (i = 0; i < n && rc == 0; i++) {
        rgb[i * 3 + 0] = px[i * 4 + 2];
        rgb[i * 3 + 1] = px[i * 4 + 1];
        rgb[i * 3 + 2] = px[i * 4 + 0];
    }
    if (rc == 0)
        rc = hx_save_ppm(name, w, h, rgb);
    free(px);
    free(rgb);
    return rc;
}

/* ---- Lifecycle ------------------------------------------------------- */
static void parse_args(int argc, char **argv)
{
    int i;
    hx_args.frames = 60;
    hx_args.capture = -1;
    hx_args.out = "C:\\OUT";
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--frames") && i + 1 < argc) hx_args.frames = atoi(argv[++i]);
        else if (!strcmp(a, "--capture") && i + 1 < argc) hx_args.capture = atoi(argv[++i]);
        else if (!strcmp(a, "--out") && i + 1 < argc) hx_args.out = argv[++i];
        else if (!strncmp(a, "--glide=", 8)) hx_args.glide = a + 8;
        else if (!strcmp(a, "--bench")) hx_args.bench = 1;
        else if (!strcmp(a, "--fail")) hx_args.fail = 1;
        else if (!strcmp(a, "--hang")) hx_args.hang = 1;
        else if (!strcmp(a, "--crash")) hx_args.crash = 1;
        else if (!strcmp(a, "--noexit")) hx_args.noexit = 1;
        else { hx_log("HX-ARGS unknown argument %s", a); hx_done(HX_BAD_ARGS); }
    }
}

void hx_init(int argc, char **argv, const char *prog)
{
    serial_init(SERIAL_COM1, 115200);
    parse_args(argc, argv);
    ut_setbase(UT_IOBASE);
    ut_base = 0;
    if (inp(UT_IOBASE) != 0xFF) {
        /* 86Box's unit tester answers a no-op with "idle" (04h) and an unknown
         * command with "idle, unsupported" (0Ch). A real board's port there
         * reads something else (bench C's 486: a capture of 15420x15420). */
        int s1, s2, s3;
        outp(UT_IOBASE, 0x00);
        s1 = inp(UT_IOBASE);
        outp(UT_IOBASE, 0xFE);
        s2 = inp(UT_IOBASE);
        outp(UT_IOBASE, 0x00);
        s3 = inp(UT_IOBASE);
        if (s1 == 0x04 && s2 == 0x0C && s3 == 0x04)
            ut_base = UT_IOBASE;
    }
    if (!ut_base)
        ut_setbase(0xFFFF);
    hx_log("HX-START %s %s ut=%d ovl=%s", prog, HX_BUILD_ID, ut_base != 0,
           hx_args.glide ? hx_args.glide : "-");
}

void hx_done(int code)
{
    if (code == 0 && hx_nfail)
        code = HX_FAIL;
    hx_log("HX-DONE %d", code);
    if (!hx_args.noexit)
        hx_ut_exit(code);
    exit(code);
}
