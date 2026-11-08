/* matrix.c - matrix stacks (FR-ST-2): modelview 32, projection 2, texture 2,
 * column-major as in GL; the combined projection * modelview is cached. */
#include "gl_state.h"
#include <math.h>
#include <string.h>

void dgl_mat_identity(dgl_mat4 *r)
{
    memset(r, 0, sizeof *r);
    r->m[0] = r->m[5] = r->m[10] = r->m[15] = 1.0f;
}

void dgl_mat_mul(dgl_mat4 *r, const dgl_mat4 *a, const dgl_mat4 *b)
{
    dgl_mat4 t;
    int i, j;
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
            t.m[j * 4 + i] = a->m[i] * b->m[j * 4] + a->m[4 + i] * b->m[j * 4 + 1] +
                             a->m[8 + i] * b->m[j * 4 + 2] + a->m[12 + i] * b->m[j * 4 + 3];
    *r = t;
}

static dgl_mstack *current(void)
{
    switch (dgl_gl.matrix_mode) {
    case GL_PROJECTION: return &dgl_gl.proj;
    case GL_TEXTURE:    return dgl_gl.active_unit ? &dgl_gl.tex1 : &dgl_gl.tex;
    default:            return &dgl_gl.mv;
    }
}

static dgl_mat4 *top(void)
{
    dgl_mstack *s = current();
    return &s->stack[s->depth - 1];
}

static void changed(void)
{
    if (dgl_gl.matrix_mode == GL_TEXTURE) {
        static const dgl_mat4 id = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
        *(dgl_gl.active_unit ? &dgl_gl.tex1_identity : &dgl_gl.tex_identity) = !memcmp(top(), &id, sizeof id);
    } else
        dgl_gl.dirty |= DGL_DIRTY_MVP;
}

void dgl_matrix_reset(void)
{
    dgl_gl.mv.stack = dgl_gl.mv_stack; dgl_gl.mv.max = DGL_MV_DEPTH; dgl_gl.mv.depth = 1;
    dgl_gl.proj.stack = dgl_gl.proj_stack; dgl_gl.proj.max = DGL_PROJ_DEPTH; dgl_gl.proj.depth = 1;
    dgl_gl.tex.stack = dgl_gl.tex_stack; dgl_gl.tex.max = DGL_TEX_DEPTH; dgl_gl.tex.depth = 1;
    dgl_gl.tex1.stack = dgl_gl.tex1_stack; dgl_gl.tex1.max = DGL_TEX_DEPTH; dgl_gl.tex1.depth = 1;
    dgl_mat_identity(&dgl_gl.mv_stack[0]);
    dgl_mat_identity(&dgl_gl.proj_stack[0]);
    dgl_mat_identity(&dgl_gl.tex_stack[0]);
    dgl_mat_identity(&dgl_gl.tex1_stack[0]);
    dgl_gl.matrix_mode = GL_MODELVIEW;
    dgl_gl.tex_identity = dgl_gl.tex1_identity = 1;
    dgl_gl.dirty |= DGL_DIRTY_MVP;
}

const dgl_mat4 *dgl_mvp(void)
{
    if (dgl_gl.dirty & DGL_DIRTY_MVP) {
        dgl_mat_mul(&dgl_gl.mvp, &dgl_gl.proj.stack[dgl_gl.proj.depth - 1], &dgl_gl.mv.stack[dgl_gl.mv.depth - 1]);
        dgl_gl.dirty &= ~DGL_DIRTY_MVP;
    }
    return &dgl_gl.mvp;
}

void APIENTRY glMatrixMode(GLenum mode)
{
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) {
        dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    dgl_gl.matrix_mode = mode;
}

void APIENTRY glLoadIdentity(void)
{
    dgl_mat_identity(top());
    changed();
}

void APIENTRY glLoadMatrixf(const GLfloat *m)
{
    memcpy(top()->m, m, sizeof top()->m);
    changed();
}

void APIENTRY glMultMatrixf(const GLfloat *m)
{
    dgl_mat4 b;
    memcpy(b.m, m, sizeof b.m);
    dgl_mat_mul(top(), top(), &b);
    changed();
}

void APIENTRY glPushMatrix(void)
{
    dgl_mstack *s = current();
    if (s->depth == s->max) {
        dgl_gl_error(GL_STACK_OVERFLOW);
        return;
    }
    s->stack[s->depth] = s->stack[s->depth - 1];
    s->depth++;
}

void APIENTRY glPopMatrix(void)
{
    dgl_mstack *s = current();
    if (s->depth == 1) {
        dgl_gl_error(GL_STACK_UNDERFLOW);
        return;
    }
    s->depth--;
    changed();
}

void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    m[12] = x; m[13] = y; m[14] = z;
    glMultMatrixf(m);
}

void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat m[16] = { 0 };
    m[0] = x; m[5] = y; m[10] = z; m[15] = 1;
    glMultMatrixf(m);
}

void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat m[16] = { 0 };
    double len = sqrt((double)x * x + (double)y * y + (double)z * z), c, s, t;
    if (len == 0.0)
        return;
    x = (GLfloat)(x / len); y = (GLfloat)(y / len); z = (GLfloat)(z / len);
    c = cos(angle * M_PI / 180.0); s = sin(angle * M_PI / 180.0); t = 1.0 - c;
    m[0] = (GLfloat)(x * x * t + c);     m[4] = (GLfloat)(x * y * t - z * s); m[8] = (GLfloat)(x * z * t + y * s);
    m[1] = (GLfloat)(y * x * t + z * s); m[5] = (GLfloat)(y * y * t + c);     m[9] = (GLfloat)(y * z * t - x * s);
    m[2] = (GLfloat)(z * x * t - y * s); m[6] = (GLfloat)(z * y * t + x * s); m[10] = (GLfloat)(z * z * t + c);
    m[15] = 1;
    glMultMatrixf(m);
}

void APIENTRY glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    GLfloat m[16] = { 0 };
    if (l == r || b == t || n == f) {
        dgl_gl_error(GL_INVALID_VALUE);
        return;
    }
    m[0] = (GLfloat)(2 / (r - l));
    m[5] = (GLfloat)(2 / (t - b));
    m[10] = (GLfloat)(-2 / (f - n));
    m[12] = (GLfloat)(-(r + l) / (r - l));
    m[13] = (GLfloat)(-(t + b) / (t - b));
    m[14] = (GLfloat)(-(f + n) / (f - n));
    m[15] = 1;
    glMultMatrixf(m);
}

void APIENTRY glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    GLfloat m[16] = { 0 };
    if (n <= 0 || f <= 0 || l == r || b == t || n == f) {
        dgl_gl_error(GL_INVALID_VALUE);
        return;
    }
    m[0] = (GLfloat)(2 * n / (r - l));
    m[5] = (GLfloat)(2 * n / (t - b));
    m[8] = (GLfloat)((r + l) / (r - l));
    m[9] = (GLfloat)((t + b) / (t - b));
    m[10] = (GLfloat)(-(f + n) / (f - n));
    m[11] = -1;
    m[14] = (GLfloat)(-2 * f * n / (f - n));
    glMultMatrixf(m);
}

/* ---- Double-precision forms (GL 1.1) -------------------------------------- */
static void to_float(const GLdouble *d, GLfloat *f)
{
    int i;
    for (i = 0; i < 16; i++)
        f[i] = (GLfloat)d[i];
}

void APIENTRY glLoadMatrixd(const GLdouble *m) { GLfloat f[16]; to_float(m, f); glLoadMatrixf(f); }
void APIENTRY glMultMatrixd(const GLdouble *m) { GLfloat f[16]; to_float(m, f); glMultMatrixf(f); }
void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z) { glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z); }
void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z) { glScalef((GLfloat)x, (GLfloat)y, (GLfloat)z); }
void APIENTRY glRotated(GLdouble a, GLdouble x, GLdouble y, GLdouble z)
{
    glRotatef((GLfloat)a, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}
