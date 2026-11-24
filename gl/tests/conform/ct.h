/* ct.h - conformance test harness (PRD §11.5, D16).
 *
 * Each tests/conform/tNN_*.c is plain OpenGL 1.1 and defines ct_run(). It
 * is built twice: for DOS against DOS-GL (ct_dos.c) and for the Linux host
 * against Mesa's OSMesa (ct_host.c), whose frames are the references. Both
 * render 640x480 and save frames with ct_frame(). */
#ifndef CT_HARNESS_H
#define CT_HARNESS_H
#include <GL/gl.h>
#include <stddef.h>

#define CT_W 640
#define CT_H 480
/* Stencil bits a test asks for: the Makefile sets CT_STENCIL for those that
   use it (t33); 0 for the rest, whose contexts stay as they were. */
#ifndef CT_STENCIL
#define CT_STENCIL 0
#endif

void ct_run(void);                      /* the test */
void ct_frame(const char *name);        /* read back the frame being drawn and save <name>.ppm */
void ct_log(const char *fmt, ...);      /* free-form result line */
void ct_check(const char *name, int ok, const char *fmt, ...);   /* a check of the test's own (HX-TEST; the
                                           reference's build prints it, nothing judges it there) */

/* Whether the renderer advertises an extension. A test of a feature not everywhere skips itself without it:
 * ct_log("CT-SKIP ...") and no frames (tools/conform/run.py then reports SKIPPED). */
static __attribute__((unused)) int ct_has(const char *ext)
{
    const char *all = (const char *)glGetString(GL_EXTENSIONS), *p;
    size_t n = 0;
    while (ext[n])
        n++;
    for (p = all; p && *p; ) {
        const char *e = p;
        size_t k = 0;
        while (e[k] && e[k] != ' ')
            k++;
        if (k == n) {
            size_t i = 0;
            while (i < n && e[i] == ext[i])
                i++;
            if (i == n)
                return 1;
        }
        p = e + k;
        while (*p == ' ')
            p++;
    }
    return 0;
}

#endif
