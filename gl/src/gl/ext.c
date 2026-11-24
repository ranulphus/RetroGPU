/* ext.c - GL_ARB_multitexture, and GL_SGIS_multitexture's names for it
 * (period GLQuake builds): two texture units on the G400 and G450, drawn in
 * one pass by the chip's two texture maps (emit.c). The G200 has one unit:
 * GL_MAX_TEXTURE_UNITS_ARB is 1 and the extensions are not advertised. */
#include "gl_state.h"
#include "../dgl/dgl.h"
#include "../backend/backend.h"
#include <string.h>

/* The back end's caps, zeroed first and made consistent; kept for the device and back end they were asked of
 * (glMultiTexCoord asks for the units each call, GLQuake's world thousands of times a frame). */
void dgl_get_caps(dgl_caps *out)
{
    static const void *be, *dev;
    static dgl_caps c;
    if (be != (const void *)dgl_be || dev != (const void *)dgl_dev) {
        memset(&c, 0, sizeof c);
        dgl_be->caps(&c);
        if (!(c.max_anisotropy >= 1.0f))
            c.max_anisotropy = 1.0f;
        if (!(c.max_lod_bias >= 0.0f))
            c.max_lod_bias = 0.0f;
        be = dgl_be, dev = dgl_dev;
    }
    *out = c;
}

int dgl_texture_units(void)
{
    static const void *be, *dev;
    static int n;
    if (be != (const void *)dgl_be || dev != (const void *)dgl_dev) {
        dgl_caps c;
        dgl_get_caps(&c);
        n = c.texture_units;
        be = dgl_be, dev = dgl_dev;
    }
    return n;
}

/* The unit a GL_TEXTUREi_ARB or GL_TEXTUREi_SGIS names; -1 (and
 * GL_INVALID_ENUM) for any other. */
static int unit_of(GLenum target)
{
    int u = target >= GL_TEXTURE0_ARB && target < GL_TEXTURE0_ARB + 32 ? (int)(target - GL_TEXTURE0_ARB)
          : target == GL_TEXTURE0_SGIS ? 0 : target == GL_TEXTURE1_SGIS ? 1 : -1;
    if (u < 0 || u >= dgl_texture_units()) {
        dgl_gl_error(GL_INVALID_ENUM);
        return -1;
    }
    return u;
}

void APIENTRY glActiveTextureARB(GLenum target)
{
    int u = unit_of(target);
    if (u >= 0)
        dgl_gl.active_unit = u;
}

void APIENTRY glClientActiveTextureARB(GLenum target)
{
    int u = unit_of(target);
    if (u >= 0)
        dgl_gl.client_unit = u;
}

/* Texture coordinates for a unit; q divides, as glTexCoord4 does here. */
static void coords4(GLenum target, float s, float t, float r, float q)
{
    int u = unit_of(target);
    float *c;
    if (u < 0)
        return;
    c = u ? dgl_gl.cur_tex1 : dgl_gl.cur_tex;
    if (q != 0 && q != 1) {
        s /= q;
        t /= q;
        r /= q;
    }
    c[0] = s;
    c[1] = t;
    dgl_gl.cur_texr[u] = r;
}
#define coords(u, s, t, q) coords4(u, s, t, 0, q)

/* glMultiTexCoord{1,2,3,4}{d,f,i,s}[v]ARB, written out so dglGetProcAddress's
 * generated table finds them. */
void APIENTRY glMultiTexCoord1dARB(GLenum u, GLdouble s) { coords(u, (float)s, 0, 1); }
void APIENTRY glMultiTexCoord2dARB(GLenum u, GLdouble s, GLdouble t) { coords(u, (float)s, (float)t, 1); }
void APIENTRY glMultiTexCoord3dARB(GLenum u, GLdouble s, GLdouble t, GLdouble r) { coords4(u, (float)s, (float)t, (float)r, 1); }
void APIENTRY glMultiTexCoord4dARB(GLenum u, GLdouble s, GLdouble t, GLdouble r, GLdouble q) { coords4(u, (float)s, (float)t, (float)r, (float)q); }
void APIENTRY glMultiTexCoord1dvARB(GLenum u, const GLdouble *v) { coords(u, (float)v[0], 0, 1); }
void APIENTRY glMultiTexCoord2dvARB(GLenum u, const GLdouble *v) { coords(u, (float)v[0], (float)v[1], 1); }
void APIENTRY glMultiTexCoord3dvARB(GLenum u, const GLdouble *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], 1); }
void APIENTRY glMultiTexCoord4dvARB(GLenum u, const GLdouble *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glMultiTexCoord1fARB(GLenum u, GLfloat s) { coords(u, (float)s, 0, 1); }
void APIENTRY glMultiTexCoord2fARB(GLenum u, GLfloat s, GLfloat t) { coords(u, (float)s, (float)t, 1); }
void APIENTRY glMultiTexCoord3fARB(GLenum u, GLfloat s, GLfloat t, GLfloat r) { coords4(u, (float)s, (float)t, (float)r, 1); }
void APIENTRY glMultiTexCoord4fARB(GLenum u, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { coords4(u, (float)s, (float)t, (float)r, (float)q); }
void APIENTRY glMultiTexCoord1fvARB(GLenum u, const GLfloat *v) { coords(u, (float)v[0], 0, 1); }
void APIENTRY glMultiTexCoord2fvARB(GLenum u, const GLfloat *v) { coords(u, (float)v[0], (float)v[1], 1); }
void APIENTRY glMultiTexCoord3fvARB(GLenum u, const GLfloat *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], 1); }
void APIENTRY glMultiTexCoord4fvARB(GLenum u, const GLfloat *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glMultiTexCoord1iARB(GLenum u, GLint s) { coords(u, (float)s, 0, 1); }
void APIENTRY glMultiTexCoord2iARB(GLenum u, GLint s, GLint t) { coords(u, (float)s, (float)t, 1); }
void APIENTRY glMultiTexCoord3iARB(GLenum u, GLint s, GLint t, GLint r) { coords4(u, (float)s, (float)t, (float)r, 1); }
void APIENTRY glMultiTexCoord4iARB(GLenum u, GLint s, GLint t, GLint r, GLint q) { coords4(u, (float)s, (float)t, (float)r, (float)q); }
void APIENTRY glMultiTexCoord1ivARB(GLenum u, const GLint *v) { coords(u, (float)v[0], 0, 1); }
void APIENTRY glMultiTexCoord2ivARB(GLenum u, const GLint *v) { coords(u, (float)v[0], (float)v[1], 1); }
void APIENTRY glMultiTexCoord3ivARB(GLenum u, const GLint *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], 1); }
void APIENTRY glMultiTexCoord4ivARB(GLenum u, const GLint *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glMultiTexCoord1sARB(GLenum u, GLshort s) { coords(u, (float)s, 0, 1); }
void APIENTRY glMultiTexCoord2sARB(GLenum u, GLshort s, GLshort t) { coords(u, (float)s, (float)t, 1); }
void APIENTRY glMultiTexCoord3sARB(GLenum u, GLshort s, GLshort t, GLshort r) { coords4(u, (float)s, (float)t, (float)r, 1); }
void APIENTRY glMultiTexCoord4sARB(GLenum u, GLshort s, GLshort t, GLshort r, GLshort q) { coords4(u, (float)s, (float)t, (float)r, (float)q); }
void APIENTRY glMultiTexCoord1svARB(GLenum u, const GLshort *v) { coords(u, (float)v[0], 0, 1); }
void APIENTRY glMultiTexCoord2svARB(GLenum u, const GLshort *v) { coords(u, (float)v[0], (float)v[1], 1); }
void APIENTRY glMultiTexCoord3svARB(GLenum u, const GLshort *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], 1); }
void APIENTRY glMultiTexCoord4svARB(GLenum u, const GLshort *v) { coords4(u, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }

/* GL_SGIS_multitexture: one selector for both texture state and coordinates. */
void APIENTRY glSelectTextureSGIS(GLenum target)
{
    int u = unit_of(target);
    if (u >= 0)
        dgl_gl.active_unit = dgl_gl.client_unit = u;
}

void APIENTRY glSelectTextureCoordSetSGIS(GLenum target)
{
    int u = unit_of(target);
    if (u >= 0)
        dgl_gl.client_unit = u;
}

void APIENTRY glMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t) { coords(target, s, t, 1); }
void APIENTRY glMTexCoord2fvSGIS(GLenum target, const GLfloat *v) { coords(target, v[0], v[1], 1); }
