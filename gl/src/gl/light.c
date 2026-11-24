/* light.c - OpenGL 1.1's lighting on the CPU (docs/dx7.md, X2): lights, the light model, materials, colour
 * material, and each vertex's colour from them (dgl_light_vertex, called by dgl_transform while GL_LIGHTING is on).
 * It is the reference and the fallback for the Radeons' transform-and-lighting engine (X3), and how every other
 * card lights.
 *
 * A vertex's colour, as GL 1.1's section 2.13.1 says: the material's emission, plus the scene's ambient times the
 * material's, plus for each enabled light its attenuation times its spotlight factor times (its ambient times the
 * material's, plus n.L times their diffuses, plus (n.h)^shininess times their speculars where n.L > 0), clamped;
 * the alpha is the material's diffuse alpha. Positions and directions are in eye coordinates: a light's position
 * and spot direction are transformed by the modelview current at glLight, a vertex's normal by the inverse
 * transpose of the current one. GL_NORMALIZE and GL_RESCALE_NORMAL_EXT both normalise. */
#include "gl_state.h"
#include "gl_draw.h"
#include <math.h>
#include <string.h>

/* The inverse transpose of the modelview's upper 3x3 (row-major here: n_eye[i] = sum_j nm[i][j] n[j]), made again
 * when the modelview changes (dgl_mvp clears nm_ok). */
static float nm[3][3];
int dgl_nm_ok;

static void normal_matrix(void)
{
    const float *m = dgl_gl.mv.stack[dgl_gl.mv.depth - 1].m;   /* column-major: a(r, c) = m[c * 4 + r] */
#define A(r, c) m[(c) * 4 + (r)]
    float c00 = A(1, 1) * A(2, 2) - A(1, 2) * A(2, 1), c01 = A(1, 2) * A(2, 0) - A(1, 0) * A(2, 2),
          c02 = A(1, 0) * A(2, 1) - A(1, 1) * A(2, 0), c10 = A(0, 2) * A(2, 1) - A(0, 1) * A(2, 2),
          c11 = A(0, 0) * A(2, 2) - A(0, 2) * A(2, 0), c12 = A(0, 1) * A(2, 0) - A(0, 0) * A(2, 1),
          c20 = A(0, 1) * A(1, 2) - A(0, 2) * A(1, 1), c21 = A(0, 2) * A(1, 0) - A(0, 0) * A(1, 2),
          c22 = A(0, 0) * A(1, 1) - A(0, 1) * A(1, 0);
    float det = A(0, 0) * c00 + A(0, 1) * c01 + A(0, 2) * c02, k = det != 0 ? 1.0f / det : 0.0f;
#undef A
    /* (the inverse is the cofactors' transpose over the determinant; its transpose is the cofactors over it) */
    nm[0][0] = c00 * k, nm[0][1] = c01 * k, nm[0][2] = c02 * k;
    nm[1][0] = c10 * k, nm[1][1] = c11 * k, nm[1][2] = c12 * k;
    nm[2][0] = c20 * k, nm[2][1] = c21 * k, nm[2][2] = c22 * k;
    dgl_nm_ok = 1;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* A face's material with colour material applied (the vertex's colour in place of the tracked parameters). */
static void face_material(int face, const float *col, dgl_material *m)
{
    *m = dgl_gl.material[face];
    if (dgl_gl.color_material &&
        (dgl_gl.color_material_face == GL_FRONT_AND_BACK || (dgl_gl.color_material_face == GL_FRONT) == !face)) {
        switch (dgl_gl.color_material_mode) {
        case GL_AMBIENT: memcpy(m->ambient, col, sizeof m->ambient); break;
        case GL_DIFFUSE: memcpy(m->diffuse, col, sizeof m->diffuse); break;
        case GL_SPECULAR: memcpy(m->specular, col, sizeof m->specular); break;
        case GL_EMISSION: memcpy(m->emission, col, sizeof m->emission); break;
        default:                        /* GL_AMBIENT_AND_DIFFUSE */
            memcpy(m->ambient, col, sizeof m->ambient);
            memcpy(m->diffuse, col, sizeof m->diffuse);
            break;
        }
    }
}

static void shade(const float pe[3], const float n[3], const dgl_material *m, float out[4])
{
    float c[3];
    int i, k;
    for (k = 0; k < 3; k++)
        c[k] = m->emission[k] + dgl_gl.light_model_ambient[k] * m->ambient[k];
    for (i = 0; i < 8; i++) {
        const dgl_light *l = &dgl_gl.light[i];
        float L[3], att = 1.0f, spot = 1.0f, d, ndl, f;
        if (!l->enabled)
            continue;
        if (l->position[3] == 0) {
            d = sqrtf(l->position[0] * l->position[0] + l->position[1] * l->position[1] +
                      l->position[2] * l->position[2]);
            d = d > 0 ? 1.0f / d : 0.0f;
            for (k = 0; k < 3; k++)
                L[k] = l->position[k] * d;
        } else {
            for (k = 0; k < 3; k++)
                L[k] = l->position[k] / l->position[3] - pe[k];
            d = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            att = l->att[0] + l->att[1] * d + l->att[2] * d * d;
            att = att > 0 ? 1.0f / att : 1.0f;
            if (d > 0)
                for (k = 0; k < 3; k++)
                    L[k] /= d;
        }
        if (l->spot_cutoff != 180.0f) {
            float sd = sqrtf(l->spot_dir[0] * l->spot_dir[0] + l->spot_dir[1] * l->spot_dir[1] +
                             l->spot_dir[2] * l->spot_dir[2]), cosang;
            cosang = sd > 0 ? -(L[0] * l->spot_dir[0] + L[1] * l->spot_dir[1] + L[2] * l->spot_dir[2]) / sd : 0.0f;
            spot = cosang >= cosf(l->spot_cutoff * 3.14159265358979f / 180.0f)
                       ? (l->spot_exp != 0 ? powf(cosang > 0 ? cosang : 0, l->spot_exp) : 1.0f) : 0.0f;
        }
        f = att * spot;
        if (f == 0)
            continue;
        ndl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
        for (k = 0; k < 3; k++)
            c[k] += f * l->ambient[k] * m->ambient[k];
        if (ndl > 0) {
            float h[3], hl, ndh, s;
            if (dgl_gl.light_local_viewer) {
                float v = sqrtf(pe[0] * pe[0] + pe[1] * pe[1] + pe[2] * pe[2]);
                v = v > 0 ? 1.0f / v : 0.0f;
                h[0] = L[0] - pe[0] * v, h[1] = L[1] - pe[1] * v, h[2] = L[2] - pe[2] * v;
            } else
                h[0] = L[0], h[1] = L[1], h[2] = L[2] + 1.0f;
            hl = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            ndh = hl > 0 ? (n[0] * h[0] + n[1] * h[1] + n[2] * h[2]) / hl : 0.0f;
            s = ndh > 0 ? (m->shininess != 0 ? powf(ndh, m->shininess) : 1.0f) : (m->shininess == 0 ? 1.0f : 0.0f);
            for (k = 0; k < 3; k++)
                c[k] += f * (ndl * l->diffuse[k] * m->diffuse[k] + s * l->specular[k] * m->specular[k]);
        }
    }
    for (k = 0; k < 3; k++)
        out[k] = clamp01(c[k]);
    out[3] = clamp01(m->diffuse[3]);
}

void dgl_light_vertex(const dgl_vin *in, dgl_cvtx *o)
{
    const float *mv = dgl_gl.mv.stack[dgl_gl.mv.depth - 1].m, *p = in->pos;
    float pe[4], n[3], len, col[4];
    dgl_material m;
    int k;
    if (!dgl_nm_ok)
        normal_matrix();
    for (k = 0; k < 4; k++)
        pe[k] = mv[k] * p[0] + mv[4 + k] * p[1] + mv[8 + k] * p[2] + mv[12 + k] * p[3];
    if (pe[3] != 0 && pe[3] != 1)
        for (k = 0; k < 3; k++)
            pe[k] /= pe[3];
    for (k = 0; k < 3; k++)
        n[k] = nm[k][0] * in->nrm[0] + nm[k][1] * in->nrm[1] + nm[k][2] * in->nrm[2];
    if (dgl_gl.normalize || dgl_gl.rescale_normal) {
        len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 0)
            for (k = 0; k < 3; k++)
                n[k] /= len;
    }
    face_material(0, in->col, &m);
    shade(pe, n, &m, col);
    o->r = col[0], o->g = col[1], o->b = col[2], o->a = col[3];
    if (dgl_gl.light_two_side) {                /* the back's colour too: the assembly picks by facing */
        float nb[3] = { -n[0], -n[1], -n[2] };
        face_material(1, in->col, &m);
        shade(pe, nb, &m, col);
        o->br = col[0], o->bg = col[1], o->bb = col[2], o->ba = col[3];
    }
}

/* ---- The state ---------------------------------------------------------------------------------------------- */

void dgl_light_reset(void)
{
    int i;
    static const dgl_material def = { { 0.2f, 0.2f, 0.2f, 1 }, { 0.8f, 0.8f, 0.8f, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, 0 };
    for (i = 0; i < 8; i++) {
        dgl_light *l = &dgl_gl.light[i];
        memset(l, 0, sizeof *l);
        l->ambient[3] = 1;
        if (i == 0)
            l->diffuse[0] = l->diffuse[1] = l->diffuse[2] = l->specular[0] = l->specular[1] = l->specular[2] = 1;
        l->diffuse[3] = l->specular[3] = 1;
        l->position[2] = 1;
        l->spot_dir[2] = -1;
        l->spot_cutoff = 180;
        l->att[0] = 1;
    }
    dgl_gl.material[0] = dgl_gl.material[1] = def;
    dgl_gl.light_model_ambient[0] = dgl_gl.light_model_ambient[1] = dgl_gl.light_model_ambient[2] = 0.2f;
    dgl_gl.light_model_ambient[3] = 1;
    dgl_gl.light_local_viewer = dgl_gl.light_two_side = 0;
    dgl_gl.lighting = dgl_gl.color_material = dgl_gl.normalize = dgl_gl.rescale_normal = 0;
    dgl_gl.color_material_face = GL_FRONT_AND_BACK;
    dgl_gl.color_material_mode = GL_AMBIENT_AND_DIFFUSE;
    dgl_gl.cur_normal[0] = dgl_gl.cur_normal[1] = 0, dgl_gl.cur_normal[2] = 1;
    dgl_nm_ok = 0;
}

/* The values a parameter takes: 4 for colours and positions, 3 for the spot direction, 1 for the rest. */
static int light_count(GLenum p)
{
    switch (p) {
    case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION: return 4;
    case GL_SPOT_DIRECTION: return 3;
    case GL_SPOT_EXPONENT: case GL_SPOT_CUTOFF: case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION: return 1;
    default: return 0;
    }
}

static void light(GLenum which, GLenum p, const GLfloat *v)
{
    dgl_light *l;
    const float *mv = dgl_gl.mv.stack[dgl_gl.mv.depth - 1].m;
    int k;
    if (which < GL_LIGHT0 || which > GL_LIGHT7 || !light_count(p)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    l = &dgl_gl.light[which - GL_LIGHT0];
    switch (p) {
    case GL_AMBIENT: memcpy(l->ambient, v, sizeof l->ambient); break;
    case GL_DIFFUSE: memcpy(l->diffuse, v, sizeof l->diffuse); break;
    case GL_SPECULAR: memcpy(l->specular, v, sizeof l->specular); break;
    case GL_POSITION:                   /* (into eye coordinates now, by the current modelview) */
        for (k = 0; k < 4; k++)
            l->position[k] = mv[k] * v[0] + mv[4 + k] * v[1] + mv[8 + k] * v[2] + mv[12 + k] * v[3];
        break;
    case GL_SPOT_DIRECTION:
        for (k = 0; k < 3; k++)
            l->spot_dir[k] = mv[k] * v[0] + mv[4 + k] * v[1] + mv[8 + k] * v[2];
        break;
    case GL_SPOT_EXPONENT:
        if (v[0] < 0 || v[0] > 128) { dgl_gl_error(GL_INVALID_VALUE); return; }
        l->spot_exp = v[0];
        break;
    case GL_SPOT_CUTOFF:
        if ((v[0] < 0 || v[0] > 90) && v[0] != 180) { dgl_gl_error(GL_INVALID_VALUE); return; }
        l->spot_cutoff = v[0];
        break;
    default:                            /* the attenuations */
        if (v[0] < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
        l->att[p - GL_CONSTANT_ATTENUATION] = v[0];
        break;
    }
}

void APIENTRY glLightfv(GLenum which, GLenum p, const GLfloat *v) { light(which, p, v); }
void APIENTRY glLightf(GLenum which, GLenum p, GLfloat v)
{
    if (light_count(p) != 1) { dgl_gl_error(GL_INVALID_ENUM); return; }
    light(which, p, &v);
}
void APIENTRY glLightiv(GLenum which, GLenum p, const GLint *v)
{
    GLfloat f[4];
    int k, n = light_count(p);
    for (k = 0; k < n; k++)             /* (colours: integers map [-2^31, 2^31-1] to [-1, 1]) */
        f[k] = p == GL_AMBIENT || p == GL_DIFFUSE || p == GL_SPECULAR ? (GLfloat)((2.0 * v[k] + 1) / 4294967295.0)
                                                                      : (GLfloat)v[k];
    light(which, p, f);
}
void APIENTRY glLighti(GLenum which, GLenum p, GLint v)
{
    GLfloat f = (GLfloat)v;
    if (light_count(p) != 1) { dgl_gl_error(GL_INVALID_ENUM); return; }
    light(which, p, &f);
}

void APIENTRY glGetLightfv(GLenum which, GLenum p, GLfloat *v)
{
    const dgl_light *l;
    if (which < GL_LIGHT0 || which > GL_LIGHT7 || !light_count(p)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    l = &dgl_gl.light[which - GL_LIGHT0];
    switch (p) {
    case GL_AMBIENT: memcpy(v, l->ambient, sizeof l->ambient); break;
    case GL_DIFFUSE: memcpy(v, l->diffuse, sizeof l->diffuse); break;
    case GL_SPECULAR: memcpy(v, l->specular, sizeof l->specular); break;
    case GL_POSITION: memcpy(v, l->position, sizeof l->position); break;
    case GL_SPOT_DIRECTION: memcpy(v, l->spot_dir, sizeof l->spot_dir); break;
    case GL_SPOT_EXPONENT: v[0] = l->spot_exp; break;
    case GL_SPOT_CUTOFF: v[0] = l->spot_cutoff; break;
    default: v[0] = l->att[p - GL_CONSTANT_ATTENUATION]; break;
    }
}

void APIENTRY glGetLightiv(GLenum which, GLenum p, GLint *v)
{
    GLfloat f[4];
    int k, n = light_count(p);
    glGetLightfv(which, p, f);
    for (k = 0; k < n; k++)
        v[k] = p == GL_AMBIENT || p == GL_DIFFUSE || p == GL_SPECULAR ? (GLint)(f[k] * 2147483647.0f) : (GLint)f[k];
}

static void light_model(GLenum p, const GLfloat *v)
{
    switch (p) {
    case GL_LIGHT_MODEL_AMBIENT: memcpy(dgl_gl.light_model_ambient, v, sizeof dgl_gl.light_model_ambient); break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: dgl_gl.light_local_viewer = v[0] != 0; break;
    case GL_LIGHT_MODEL_TWO_SIDE: dgl_gl.light_two_side = v[0] != 0; break;
    default: dgl_gl_error(GL_INVALID_ENUM); return;
    }
}

void APIENTRY glLightModelfv(GLenum p, const GLfloat *v) { light_model(p, v); }
void APIENTRY glLightModelf(GLenum p, GLfloat v)
{
    if (p == GL_LIGHT_MODEL_AMBIENT) { dgl_gl_error(GL_INVALID_ENUM); return; }
    light_model(p, &v);
}
void APIENTRY glLightModeliv(GLenum p, const GLint *v)
{
    GLfloat f[4];
    int k;
    if (p == GL_LIGHT_MODEL_AMBIENT)
        for (k = 0; k < 4; k++)
            f[k] = (GLfloat)((2.0 * v[k] + 1) / 4294967295.0);
    else
        f[0] = (GLfloat)v[0];
    light_model(p, f);
}
void APIENTRY glLightModeli(GLenum p, GLint v)
{
    GLfloat f = (GLfloat)v;
    if (p == GL_LIGHT_MODEL_AMBIENT) { dgl_gl_error(GL_INVALID_ENUM); return; }
    light_model(p, &f);
}

static void material(GLenum face, GLenum p, const GLfloat *v)
{
    int f;
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (f = 0; f < 2; f++) {
        dgl_material *m = &dgl_gl.material[f];
        if ((face == GL_FRONT && f) || (face == GL_BACK && !f))
            continue;
        switch (p) {
        case GL_AMBIENT: memcpy(m->ambient, v, sizeof m->ambient); break;
        case GL_DIFFUSE: memcpy(m->diffuse, v, sizeof m->diffuse); break;
        case GL_SPECULAR: memcpy(m->specular, v, sizeof m->specular); break;
        case GL_EMISSION: memcpy(m->emission, v, sizeof m->emission); break;
        case GL_AMBIENT_AND_DIFFUSE:
            memcpy(m->ambient, v, sizeof m->ambient);
            memcpy(m->diffuse, v, sizeof m->diffuse);
            break;
        case GL_SHININESS:
            if (v[0] < 0 || v[0] > 128) { dgl_gl_error(GL_INVALID_VALUE); return; }
            m->shininess = v[0];
            break;
        case GL_COLOR_INDEXES: break;   /* (no colour-index lighting) */
        default: dgl_gl_error(GL_INVALID_ENUM); return;
        }
    }
}

void APIENTRY glMaterialfv(GLenum face, GLenum p, const GLfloat *v) { material(face, p, v); }
void APIENTRY glMaterialf(GLenum face, GLenum p, GLfloat v)
{
    if (p != GL_SHININESS) { dgl_gl_error(GL_INVALID_ENUM); return; }
    material(face, p, &v);
}
void APIENTRY glMaterialiv(GLenum face, GLenum p, const GLint *v)
{
    GLfloat f[4];
    int k;
    if (p == GL_SHININESS || p == GL_COLOR_INDEXES)
        for (k = 0; k < (p == GL_SHININESS ? 1 : 3); k++)
            f[k] = (GLfloat)v[k];
    else
        for (k = 0; k < 4; k++)
            f[k] = (GLfloat)((2.0 * v[k] + 1) / 4294967295.0);
    material(face, p, f);
}
void APIENTRY glMateriali(GLenum face, GLenum p, GLint v)
{
    GLfloat f = (GLfloat)v;
    if (p != GL_SHININESS) { dgl_gl_error(GL_INVALID_ENUM); return; }
    material(face, p, &f);
}

void APIENTRY glGetMaterialfv(GLenum face, GLenum p, GLfloat *v)
{
    const dgl_material *m;
    if (face != GL_FRONT && face != GL_BACK) { dgl_gl_error(GL_INVALID_ENUM); return; }
    m = &dgl_gl.material[face == GL_BACK];
    switch (p) {
    case GL_AMBIENT: memcpy(v, m->ambient, sizeof m->ambient); break;
    case GL_DIFFUSE: memcpy(v, m->diffuse, sizeof m->diffuse); break;
    case GL_SPECULAR: memcpy(v, m->specular, sizeof m->specular); break;
    case GL_EMISSION: memcpy(v, m->emission, sizeof m->emission); break;
    case GL_SHININESS: v[0] = m->shininess; break;
    case GL_COLOR_INDEXES: v[0] = 0, v[1] = 1, v[2] = 1; break;
    default: dgl_gl_error(GL_INVALID_ENUM); break;
    }
}

void APIENTRY glGetMaterialiv(GLenum face, GLenum p, GLint *v)
{
    GLfloat f[4];
    int k, n = p == GL_SHININESS ? 1 : p == GL_COLOR_INDEXES ? 3 : 4;
    glGetMaterialfv(face, p, f);
    for (k = 0; k < n; k++)
        v[k] = p == GL_SHININESS || p == GL_COLOR_INDEXES ? (GLint)f[k] : (GLint)(f[k] * 2147483647.0f);
}

/* While GL_COLOR_MATERIAL is on, the current colour is the tracked parameters' (vertex.c's glColor4f, and
 * glEnable and glColorMaterial themselves). */
void dgl_color_material_apply(void)
{
    if (dgl_gl.color_material)
        material(dgl_gl.color_material_face, dgl_gl.color_material_mode, dgl_gl.cur_color);
}

void APIENTRY glColorMaterial(GLenum face, GLenum mode)
{
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
        (mode != GL_EMISSION && mode != GL_AMBIENT && mode != GL_DIFFUSE && mode != GL_SPECULAR &&
         mode != GL_AMBIENT_AND_DIFFUSE)) {
        dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    dgl_gl.color_material_face = face;
    dgl_gl.color_material_mode = mode;
    dgl_color_material_apply();
}
