/* ct_mtex.h - two-texture quads for the multitexture tests. Where the
 * implementation has two texture units (Mesa; DOS-GL on the G400 and
 * G450) they go through GL_ARB_multitexture in one pass; with one unit
 * (DOS-GL on the G200) the same picture is drawn in two passes, as the
 * games do: the second texture multiplied into the frame by blending. */
#ifndef CT_MTEX_H
#define CT_MTEX_H
#define GL_GLEXT_PROTOTYPES 1
#include "ct_tex.h"
#include <string.h>

static __attribute__((unused)) int ct_units(void)
{
    GLint n = 1;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &n);
    return n;
}

/* The second pass's blend for a unit-1 environment: MODULATE multiplies
 * (dst * tex), GL_BLEND with black darkens (dst * (1 - tex)). */
static __attribute__((unused)) void ct_second_pass(GLenum env1)
{
    glEnable(GL_BLEND);
    if (env1 == GL_BLEND)
        glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
    else
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/* A 2D quad from (x0, y0) to (x1, y1) with its own coordinate ranges per
 * unit: unit 0 [0, r0], unit 1 [o1, o1 + r1]. */
static __attribute__((unused)) void ct_quad2(float x0, float y0, float x1, float y1, float r0, float o1, float r1,
                                             int unit1)
{
    glBegin(GL_QUADS);
    if (unit1) {
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0, 0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, o1, o1); glVertex2f(x0, y0);
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, r0, 0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, o1 + r1, o1); glVertex2f(x1, y0);
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, r0, r0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, o1 + r1, o1 + r1); glVertex2f(x1, y1);
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0, r0); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, o1, o1 + r1); glVertex2f(x0, y1);
    } else {
        glTexCoord2f(0, 0); glVertex2f(x0, y0);
        glTexCoord2f(r0, 0); glVertex2f(x1, y0);
        glTexCoord2f(r0, r0); glVertex2f(x1, y1);
        glTexCoord2f(0, r0); glVertex2f(x0, y1);
    }
    glEnd();
}

/* The same quad's second pass: unit 1's coordinates on the only unit. */
static __attribute__((unused)) void ct_quad2_pass2(float x0, float y0, float x1, float y1, float o1, float r1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(o1, o1); glVertex2f(x0, y0);
    glTexCoord2f(o1 + r1, o1); glVertex2f(x1, y0);
    glTexCoord2f(o1 + r1, o1 + r1); glVertex2f(x1, y1);
    glTexCoord2f(o1, o1 + r1); glVertex2f(x0, y1);
    glEnd();
}

#endif
