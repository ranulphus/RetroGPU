/* ct.h - conformance test harness (PRD §11.5, D16).
 *
 * Each tests/conform/tNN_*.c is plain OpenGL 1.1 and defines ct_run(). It
 * is built twice: for DOS against DOS-GL (ct_dos.c) and for the Linux host
 * against Mesa's OSMesa (ct_host.c), whose frames are the references. Both
 * render 640x480 and save frames with ct_frame(). */
#ifndef CT_HARNESS_H
#define CT_HARNESS_H
#include <GL/gl.h>

#define CT_W 640
#define CT_H 480

void ct_run(void);                      /* the test */
void ct_frame(const char *name);        /* read back the frame being drawn and save <name>.ppm */
void ct_log(const char *fmt, ...);      /* free-form result line */

#endif
