/* texgen.c - OpenGL 1.1's user clip planes and texture-coordinate generation on the CPU (docs/dx7.md, X2).
 *
 * Clip planes: glClipPlane's equation goes into eye coordinates by the inverse of the modelview current then; at a
 * draw it is taken on into clip coordinates by the inverse of the projection (a point's distance is linear in
 * either), and the assembly cuts a primitive that crosses an enabled plane (dgl_clip_user) before the back end
 * sees it: the back ends clip the near and far planes and their guard bands as before.
 *
 * Texture coordinates: GL_OBJECT_LINEAR (the plane with the object's position), GL_EYE_LINEAR (the plane, into eye
 * coordinates at glTexGen, with the eye position) and GL_SPHERE_MAP (the eye-space reflection's), for s and t of
 * each unit, before the texture matrix (dgl_texgen_vertex). r and q, the reflection and normal maps (cube maps)
 * come later. */
#include "gl_state.h"
#include "gl_draw.h"
#include <math.h>
#include <string.h>

/* r = m's inverse (column-major); 0 when m is singular (r: the identity then). */
int dgl_mat_inverse(dgl_mat4 *r, const dgl_mat4 *m)
{
    const float *a = m->m;
    float inv[16], det;
    int i;
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0) {
        dgl_mat_identity(r);
        return 0;
    }
    det = 1.0f / det;
    for (i = 0; i < 16; i++)
        r->m[i] = inv[i] * det;
    return 1;
}

/* A plane (a row vector) times a matrix: p' = p M, so that p' . x = p . (M x). */
static void plane_times(const float p[4], const dgl_mat4 *m, float out[4])
{
    int c;
    for (c = 0; c < 4; c++)
        out[c] = p[0] * m->m[c * 4 + 0] + p[1] * m->m[c * 4 + 1] + p[2] * m->m[c * 4 + 2] + p[3] * m->m[c * 4 + 3];
}

/* ---- Clip planes ---------------------------------------------------------------------------------------- */

static float clip_eq[6][4];             /* the enabled planes in clip coordinates, for the draw being assembled */
static unsigned clip_on;                /* their bits (dgl_clip_user_begin) */

void APIENTRY glClipPlane(GLenum plane, const GLdouble *eq)
{
    dgl_mat4 inv;
    float p[4];
    int k;
    if (plane < GL_CLIP_PLANE0 || plane > GL_CLIP_PLANE5) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (k = 0; k < 4; k++)
        p[k] = (float)eq[k];
    dgl_mat_inverse(&inv, &dgl_gl.mv.stack[dgl_gl.mv.depth - 1]);
    plane_times(p, &inv, dgl_gl.clip_plane[plane - GL_CLIP_PLANE0]);
}

void APIENTRY glGetClipPlane(GLenum plane, GLdouble *eq)
{
    int k;
    if (plane < GL_CLIP_PLANE0 || plane > GL_CLIP_PLANE5) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (k = 0; k < 4; k++)
        eq[k] = dgl_gl.clip_plane[plane - GL_CLIP_PLANE0][k];
}

/* The enabled planes into clip coordinates for this draw; nonzero when there are any. */
int dgl_clip_user_begin(void)
{
    dgl_mat4 inv;
    int i;
    clip_on = 0;
    if (!dgl_gl.clip_enabled)
        return 0;
    dgl_mat_inverse(&inv, &dgl_gl.proj.stack[dgl_gl.proj.depth - 1]);
    for (i = 0; i < 6; i++)
        if (dgl_gl.clip_enabled & (1u << i)) {
            plane_times(dgl_gl.clip_plane[i], &inv, clip_eq[i]);
            clip_on |= 1u << i;
        }
    return 1;
}

static float udist(const dgl_cvtx *v, int i)
{
    return clip_eq[i][0] * v->x + clip_eq[i][1] * v->y + clip_eq[i][2] * v->z + clip_eq[i][3] * v->w;
}

/* The planes a vertex is outside (bits), for a quick accept or reject. */
unsigned dgl_clip_user_code(const dgl_cvtx *v)
{
    unsigned c = 0;
    int i;
    for (i = 0; i < 6; i++)
        if ((clip_on & (1u << i)) && udist(v, i) < 0)
            c |= 1u << i;
    return c;
}

static void lerp(dgl_cvtx *o, const dgl_cvtx *a, const dgl_cvtx *b, float t)
{
#define L(f) o->f = a->f + (b->f - a->f) * t
    L(x); L(y); L(z); L(w); L(r); L(g); L(b); L(a); L(s); L(t); L(s1); L(t1); L(eye_d); L(tr); L(tr1); L(br); L(bg);
    L(bb); L(ba);
#undef L
}

/* A convex polygon cut by the enabled planes (out holds n + 6); the vertices left, 0 when none. n is 2 for a line
 * (cut as a segment). */
int dgl_clip_user(const dgl_cvtx *in, int n, dgl_cvtx *out)
{
    dgl_cvtx buf[2][16];
    const dgl_cvtx *src = in;
    int p, i, m, cur = 0, seg = n == 2;
    for (p = 0; p < 6; p++) {
        dgl_cvtx *dst = buf[cur];
        if (!(clip_on & (1u << p)))
            continue;
        m = 0;
        for (i = 0; i < (seg ? 1 : n); i++) {
            const dgl_cvtx *a = &src[i], *b = &src[(i + 1) % n];
            float da = udist(a, p), db = udist(b, p);
            if (seg) {                  /* a segment: keep its inside part */
                if (da < 0 && db < 0)
                    return 0;
                dst[0] = da >= 0 ? *a : *b, dst[1] = db >= 0 ? *b : *a;
                if (da < 0)
                    lerp(&dst[0], a, b, da / (da - db));
                if (db < 0)
                    lerp(&dst[1], a, b, da / (da - db));
                m = 2;
                break;
            }
            if (da >= 0)
                dst[m++] = *a;
            if ((da >= 0) != (db >= 0) && m < 16)
                lerp(&dst[m++], a, b, da / (da - db));
        }
        n = m;
        if (n < (seg ? 2 : 3))
            return 0;
        src = dst;
        cur ^= 1;
    }
    for (i = 0; i < n; i++)
        out[i] = src[i];
    return n;
}

/* ---- Texture-coordinate generation ------------------------------------------------------------------------ */

static dgl_texgen *coord(GLenum c)
{
    int unit = dgl_gl.active_unit;
    return c == GL_S ? &dgl_gl.texgen[unit][0] : c == GL_T ? &dgl_gl.texgen[unit][1]
         : c == GL_R ? &dgl_gl.texgen[unit][2] : c == GL_Q ? &dgl_gl.texgen[unit][3] : NULL;
}

static void texgen(GLenum c, GLenum p, const GLfloat *v)
{
    dgl_texgen *g = coord(c);
    if (!g) { dgl_gl_error(GL_INVALID_ENUM); return; }
    switch (p) {
    case GL_TEXTURE_GEN_MODE: {
        GLenum m = (GLenum)(GLint)v[0];
        if (m != GL_OBJECT_LINEAR && m != GL_EYE_LINEAR && (m != GL_SPHERE_MAP || c == GL_R || c == GL_Q) &&
            ((m != GL_REFLECTION_MAP_ARB && m != GL_NORMAL_MAP_ARB) || c == GL_Q)) {
            dgl_gl_error(GL_INVALID_ENUM);
            return;
        }
        g->mode = m;
        break;
    }
    case GL_OBJECT_PLANE: memcpy(g->object, v, sizeof g->object); break;
    case GL_EYE_PLANE: {                /* (into eye coordinates now) */
        dgl_mat4 inv;
        dgl_mat_inverse(&inv, &dgl_gl.mv.stack[dgl_gl.mv.depth - 1]);
        plane_times(v, &inv, g->eye);
        break;
    }
    default: dgl_gl_error(GL_INVALID_ENUM); return;
    }
}

void APIENTRY glTexGenfv(GLenum c, GLenum p, const GLfloat *v) { texgen(c, p, v); }
void APIENTRY glTexGenf(GLenum c, GLenum p, GLfloat v)
{
    if (p != GL_TEXTURE_GEN_MODE) { dgl_gl_error(GL_INVALID_ENUM); return; }
    texgen(c, p, &v);
}
void APIENTRY glTexGeni(GLenum c, GLenum p, GLint v) { glTexGenf(c, p, (GLfloat)v); }
void APIENTRY glTexGend(GLenum c, GLenum p, GLdouble v) { glTexGenf(c, p, (GLfloat)v); }
void APIENTRY glTexGeniv(GLenum c, GLenum p, const GLint *v)
{
    GLfloat f[4];
    int k;
    for (k = 0; k < (p == GL_TEXTURE_GEN_MODE ? 1 : 4); k++)
        f[k] = (GLfloat)v[k];
    texgen(c, p, f);
}
void APIENTRY glTexGendv(GLenum c, GLenum p, const GLdouble *v)
{
    GLfloat f[4];
    int k;
    for (k = 0; k < (p == GL_TEXTURE_GEN_MODE ? 1 : 4); k++)
        f[k] = (GLfloat)v[k];
    texgen(c, p, f);
}

void APIENTRY glGetTexGenfv(GLenum c, GLenum p, GLfloat *v)
{
    const dgl_texgen *g = coord(c);
    if (!g) { dgl_gl_error(GL_INVALID_ENUM); return; }
    switch (p) {
    case GL_TEXTURE_GEN_MODE: v[0] = (GLfloat)g->mode; break;
    case GL_OBJECT_PLANE: memcpy(v, g->object, sizeof g->object); break;
    case GL_EYE_PLANE: memcpy(v, g->eye, sizeof g->eye); break;
    default: dgl_gl_error(GL_INVALID_ENUM); break;
    }
}
void APIENTRY glGetTexGeniv(GLenum c, GLenum p, GLint *v)
{
    GLfloat f[4];
    int k;
    glGetTexGenfv(c, p, f);
    for (k = 0; k < (p == GL_TEXTURE_GEN_MODE ? 1 : 4); k++)
        v[k] = (GLint)f[k];
}
void APIENTRY glGetTexGendv(GLenum c, GLenum p, GLdouble *v)
{
    GLfloat f[4];
    int k;
    glGetTexGenfv(c, p, f);
    for (k = 0; k < (p == GL_TEXTURE_GEN_MODE ? 1 : 4); k++)
        v[k] = f[k];
}

void dgl_texgen_reset(void)
{
    int u, c;
    for (u = 0; u < 2; u++)
        for (c = 0; c < 4; c++) {
            dgl_texgen *g = &dgl_gl.texgen[u][c];
            memset(g, 0, sizeof *g);
            g->mode = GL_EYE_LINEAR;
            g->object[c] = g->eye[c] = 1;   /* (s: (1, 0, 0, 0), t: (0, 1, 0, 0); r and q: zero planes) */
            if (c >= 2)
                g->object[c] = g->eye[c] = 0;
        }
}

/* A unit's generated coordinates into tc (s, t, r, q; only those enabled; a generated q divides the others);
 * pe, ne: the eye position and normal. */
void dgl_texgen_vertex(int unit, const dgl_vin *in, const float pe[4], const float ne[3], float tc[4])
{
    int c, k, have_r = 0, have_m = 0;
    float rr[3] = { 0, 0, 0 }, m = 1;
    for (c = 0; c < 4; c++) {
        const dgl_texgen *g = &dgl_gl.texgen[unit][c];
        if (!(dgl_gl.texgen_on[unit] & (1u << c)))
            continue;
        switch (g->mode) {
        case GL_OBJECT_LINEAR:
            tc[c] = g->object[0] * in->pos[0] + g->object[1] * in->pos[1] + g->object[2] * in->pos[2] +
                    g->object[3] * in->pos[3];
            break;
        case GL_EYE_LINEAR:
            tc[c] = g->eye[0] * pe[0] + g->eye[1] * pe[1] + g->eye[2] * pe[2] + g->eye[3] * pe[3];
            break;
        case GL_NORMAL_MAP_ARB:
            tc[c] = ne[c];
            break;
        default:                        /* GL_SPHERE_MAP, GL_REFLECTION_MAP_ARB: the eye vector reflected */
            if (!have_r) {
                float u[3], len = sqrtf(pe[0] * pe[0] + pe[1] * pe[1] + pe[2] * pe[2]), d;
                for (k = 0; k < 3; k++)
                    u[k] = len > 0 ? pe[k] / len : 0;
                d = ne[0] * u[0] + ne[1] * u[1] + ne[2] * u[2];
                for (k = 0; k < 3; k++)
                    rr[k] = u[k] - 2 * ne[k] * d;
                have_r = 1;
            }
            if (g->mode == GL_REFLECTION_MAP_ARB) {
                tc[c] = rr[c];
                break;
            }
            if (!have_m) {
                m = 2.0f * sqrtf(rr[0] * rr[0] + rr[1] * rr[1] + (rr[2] + 1) * (rr[2] + 1));
                if (m == 0)
                    m = 1e-30f;
                have_m = 1;
            }
            tc[c] = rr[c] / m + 0.5f;
            break;
        }
    }
}
