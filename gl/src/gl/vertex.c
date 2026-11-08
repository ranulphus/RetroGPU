/* vertex.c - vertex arrays, immediate mode and primitive assembly
 * (PRD §6.1, D6). Both paths produce the same stream: each vertex is
 * fetched, transformed to clip coordinates, and primitives are cut into
 * triangles, lines and points for the sinks (emit.c on the card; unit
 * tests capture them). */
#include "gl_state.h"
#include "gl_draw.h"
#include <stdlib.h>
#include <string.h>

dgl_sinks dgl_sink;

/* ---- Array pointers ------------------------------------------------------ */
static int set_array(dgl_array *a, GLint size, GLenum type, GLsizei stride, const GLvoid *p, int min_size,
                     const GLenum *types)
{
    int i;
    if (size < min_size || size > 4 || stride < 0) { dgl_gl_error(GL_INVALID_VALUE); return 0; }
    for (i = 0; types[i] && types[i] != type; i++)
        ;
    if (!types[i]) { dgl_gl_error(GL_INVALID_ENUM); return 0; }
    a->size = size; a->type = type; a->stride = stride; a->ptr = p;
    a->buf = dgl_array_buffer();            /* p is an offset into it, if any (buffer.c) */
    return 1;
}

void dgl_arrays_forget_buffer(const dgl_buffer *b)
{
    dgl_array *a[4] = { &dgl_gl.va, &dgl_gl.ca, &dgl_gl.ta, &dgl_gl.ta1 };
    int i;
    for (i = 0; i < 4; i++)
        if (a[i]->buf == b) {
            a[i]->buf = NULL;
            a[i]->ptr = NULL;
        }
}

static const GLenum pos_types[] = { GL_FLOAT, GL_DOUBLE, GL_SHORT, GL_INT, 0 };
static const GLenum col_types[] = { GL_UNSIGNED_BYTE, GL_FLOAT, GL_DOUBLE, 0 };

void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{ set_array(&dgl_gl.va, size, type, stride, p, 2, pos_types); }
void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{ set_array(&dgl_gl.ca, size, type, stride, p, 3, col_types); }
void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{ set_array(dgl_gl.client_unit ? &dgl_gl.ta1 : &dgl_gl.ta, size, type, stride, p, 1, pos_types); }

static dgl_array *client_array(GLenum a)
{
    switch (a) {
    case GL_VERTEX_ARRAY: return &dgl_gl.va;
    case GL_COLOR_ARRAY: return &dgl_gl.ca;
    case GL_TEXTURE_COORD_ARRAY: return dgl_gl.client_unit ? &dgl_gl.ta1 : &dgl_gl.ta;
    default: return NULL;
    }
}

void APIENTRY glEnableClientState(GLenum a)
{
    dgl_array *p = client_array(a);
    if (p) p->enabled = 1;
    else if (a != GL_NORMAL_ARRAY) dgl_gl_error(GL_INVALID_ENUM);
}

void APIENTRY glDisableClientState(GLenum a)
{
    dgl_array *p = client_array(a);
    if (p) p->enabled = 0;
    else if (a != GL_NORMAL_ARRAY) dgl_gl_error(GL_INVALID_ENUM);
}

/* ---- Fetch ------------------------------------------------------------ */
static int type_size(GLenum t)
{
    switch (t) {
    case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: return 2;
    case GL_DOUBLE: return 8;
    default: return 4;
    }
}

#define UBF1(i) ((float)(i) / 255.0f)
#define UBF4(i) UBF1(i), UBF1((i) + 1), UBF1((i) + 2), UBF1((i) + 3)
#define UBF16(i) UBF4(i), UBF4((i) + 4), UBF4((i) + 8), UBF4((i) + 12)
#define UBF64(i) UBF16(i), UBF16((i) + 16), UBF16((i) + 32), UBF16((i) + 48)
const float dgl_ub_f[256] = { UBF64(0), UBF64(64), UBF64(128), UBF64(192) };

static void read_comps(const dgl_array *a, GLint i, float *out, int n, int normalise)
{
    const unsigned char *p = (const unsigned char *)dgl_buffer_address(a->buf, a->ptr) +
                             (size_t)i * (a->stride ? (size_t)a->stride : (size_t)(a->size * type_size(a->type)));
    int k;
    for (k = 0; k < n && k < a->size; k++) {
        switch (a->type) {
        case GL_FLOAT: out[k] = ((const float *)p)[k]; break;
        case GL_DOUBLE: out[k] = (float)((const double *)p)[k]; break;
        case GL_SHORT: out[k] = ((const short *)p)[k]; break;
        case GL_INT: out[k] = (float)((const int *)p)[k]; break;
        case GL_UNSIGNED_BYTE: out[k] = normalise ? DGL_UB(p[k]) : p[k]; break;
        }
    }
}

/* Transform one input vertex (object position + attributes) to clip space. */
void dgl_transform(const dgl_vin *in, dgl_cvtx *o)
{
    const dgl_mat4 *m = dgl_mvp();
    const float *mv = dgl_gl.mv.stack[dgl_gl.mv.depth - 1].m;
    const float *p = in->pos;
    o->x = m->m[0] * p[0] + m->m[4] * p[1] + m->m[8] * p[2] + m->m[12] * p[3];
    o->y = m->m[1] * p[0] + m->m[5] * p[1] + m->m[9] * p[2] + m->m[13] * p[3];
    o->z = m->m[2] * p[0] + m->m[6] * p[1] + m->m[10] * p[2] + m->m[14] * p[3];
    o->w = m->m[3] * p[0] + m->m[7] * p[1] + m->m[11] * p[2] + m->m[15] * p[3];
    o->r = in->col[0]; o->g = in->col[1]; o->b = in->col[2]; o->a = in->col[3];
    if (dgl_gl.tex_identity) {
        o->s = in->tex[0]; o->t = in->tex[1];
    } else {
        const float *t = dgl_gl.tex.stack[dgl_gl.tex.depth - 1].m;
        float q = t[3] * in->tex[0] + t[7] * in->tex[1] + t[15];
        q = q != 0 ? q : 1;
        o->s = (t[0] * in->tex[0] + t[4] * in->tex[1] + t[12]) / q;
        o->t = (t[1] * in->tex[0] + t[5] * in->tex[1] + t[13]) / q;
    }
    if (dgl_gl.tex1_identity) {
        o->s1 = in->tex1[0]; o->t1 = in->tex1[1];
    } else {
        const float *t = dgl_gl.tex1.stack[dgl_gl.tex1.depth - 1].m;
        float q = t[3] * in->tex1[0] + t[7] * in->tex1[1] + t[15];
        q = q != 0 ? q : 1;
        o->s1 = (t[0] * in->tex1[0] + t[4] * in->tex1[1] + t[12]) / q;
        o->t1 = (t[1] * in->tex1[0] + t[5] * in->tex1[1] + t[13]) / q;
    }
    /* Eye distance for fog: -z_eye (PRD §7). */
    o->eye_d = dgl_gl.fog ? -(mv[2] * p[0] + mv[6] * p[1] + mv[10] * p[2] + mv[14] * p[3]) : 0.0f;
}

void dgl_fetch_vertex(GLint i, dgl_vin *v);

/* The arrays resolved once a draw (assemble): each one's first element and
 * stride, so a vertex is fetched without dgl_buffer_address, type_size and a
 * type switch for every component. The values are read_comps's. */
typedef dgl_aplan aplan;
dgl_aplan dgl_pl_v, dgl_pl_c, dgl_pl_t, dgl_pl_t1;
#define pl_v dgl_pl_v
#define pl_c dgl_pl_c
#define pl_t dgl_pl_t
#define pl_t1 dgl_pl_t1

static void plan_one(const dgl_array *a, int on, aplan *o)
{
    o->on = on;
    if (!on)
        return;
    o->p = (const unsigned char *)dgl_buffer_address(a->buf, a->ptr);
    o->stride = a->stride ? (size_t)a->stride : (size_t)(a->size * type_size(a->type));
    o->type = a->type;
    o->size = a->size;
}

/* Immediate mode's vertices (glEnd) as arrays, for the fast path: the
 * values fetch() would read from them are these. */
static void plan_imm(const dgl_vin *v)
{
    pl_v.p = (const unsigned char *)v->pos; pl_v.stride = sizeof *v; pl_v.type = GL_FLOAT; pl_v.size = 4; pl_v.on = 1;
    pl_c.p = (const unsigned char *)v->col; pl_c.stride = sizeof *v; pl_c.type = GL_FLOAT; pl_c.size = 4; pl_c.on = 1;
    pl_t.p = (const unsigned char *)v->tex; pl_t.stride = sizeof *v; pl_t.type = GL_FLOAT; pl_t.size = 2; pl_t.on = 1;
    pl_t1.p = (const unsigned char *)v->tex1; pl_t1.stride = sizeof *v; pl_t1.type = GL_FLOAT; pl_t1.size = 2;
    pl_t1.on = 1;
}

static void plan_arrays(void)
{
    plan_one(&dgl_gl.va, 1, &pl_v);
    plan_one(&dgl_gl.ca, dgl_gl.ca.enabled, &pl_c);
    plan_one(&dgl_gl.ta, dgl_gl.ta.enabled, &pl_t);
    plan_one(&dgl_gl.ta1, dgl_gl.ta1.enabled, &pl_t1);
}

static void read_plan(const aplan *a, GLint i, float *out, int n, int normalise)
{
    const unsigned char *p = a->p + (size_t)i * a->stride;
    int k, m = n < a->size ? n : a->size;
    switch (a->type) {
    case GL_FLOAT:
        for (k = 0; k < m; k++) out[k] = ((const float *)p)[k];
        break;
    case GL_DOUBLE:
        for (k = 0; k < m; k++) out[k] = (float)((const double *)p)[k];
        break;
    case GL_SHORT:
        for (k = 0; k < m; k++) out[k] = ((const short *)p)[k];
        break;
    case GL_INT:
        for (k = 0; k < m; k++) out[k] = (float)((const int *)p)[k];
        break;
    case GL_UNSIGNED_BYTE:
        for (k = 0; k < m; k++) out[k] = normalise ? DGL_UB(p[k]) : p[k];
        break;
    }
}

static void fetch(GLint i, dgl_vin *v)
{
    v->pos[0] = v->pos[1] = v->pos[2] = 0; v->pos[3] = 1;
    read_plan(&pl_v, i, v->pos, 4, 0);
    if (pl_c.on) {
        v->col[3] = 1;
        read_plan(&pl_c, i, v->col, 4, 1);
    } else
        memcpy(v->col, dgl_gl.cur_color, sizeof v->col);
    if (pl_t.on) {
        v->tex[0] = v->tex[1] = 0;
        read_plan(&pl_t, i, v->tex, 2, 0);
    } else
        memcpy(v->tex, dgl_gl.cur_tex, sizeof v->tex);
    if (pl_t1.on) {
        v->tex1[0] = v->tex1[1] = 0;
        read_plan(&pl_t1, i, v->tex1, 2, 0);
    } else
        memcpy(v->tex1, dgl_gl.cur_tex1, sizeof v->tex1);
}

void dgl_fetch_vertex(GLint i, dgl_vin *v)
{
    v->pos[0] = v->pos[1] = v->pos[2] = 0; v->pos[3] = 1;
    read_comps(&dgl_gl.va, i, v->pos, 4, 0);
    if (dgl_gl.ca.enabled) {
        v->col[3] = 1;
        read_comps(&dgl_gl.ca, i, v->col, 4, 1);
    } else
        memcpy(v->col, dgl_gl.cur_color, sizeof v->col);
    if (dgl_gl.ta.enabled) {
        v->tex[0] = v->tex[1] = 0;
        read_comps(&dgl_gl.ta, i, v->tex, 2, 0);
    } else
        memcpy(v->tex, dgl_gl.cur_tex, sizeof v->tex);
    if (dgl_gl.ta1.enabled) {
        v->tex1[0] = v->tex1[1] = 0;
        read_comps(&dgl_gl.ta1, i, v->tex1, 2, 0);
    } else
        memcpy(v->tex1, dgl_gl.cur_tex1, sizeof v->tex1);
}

/* ---- Assembly --------------------------------------------------------- */
/* A small direct-mapped cache of transformed vertices by source index:
 * shared vertices (quads, strips, indexed meshes) are transformed once.
 * The slots are dgl_vslot (gl_draw.h). */
#define VCACHE DGL_VSLOTS
typedef struct { GLint key[VCACHE]; unsigned gen[VCACHE], cur; } vcache;
static vcache vc;
dgl_cvtx dgl_vslot[DGL_VSLOTS];
unsigned char dgl_vslot_sink[DGL_VSLOTS];

static const dgl_cvtx *get(GLint idx, const dgl_vin *imm)
{
    unsigned slot = (unsigned)idx & (VCACHE - 1);
    dgl_vin in;
    if (vc.gen[slot] == vc.cur && vc.key[slot] == idx)
        return &dgl_vslot[slot];
    if (imm)
        dgl_transform(&imm[idx], &dgl_vslot[slot]);    /* the same values as through a copy */
    else {
        fetch(idx, &in);
        dgl_transform(&in, &dgl_vslot[slot]);
    }
    vc.key[slot] = idx;
    vc.gen[slot] = vc.cur;
    dgl_vslot_sink[slot] = 0;
    return &dgl_vslot[slot];
}

/* Triangles carry their provoking vertex for flat shading (GL: the last
 * vertex, except the first for GL_POLYGON, so it is one of a, b, c). The
 * sink gets the cache's slots unless two of the vertices share one (a
 * later get() would refill it); then copies, as it always had. */
static void tri(GLint a, GLint b, GLint c, GLint prov, const dgl_vin *imm)
{
#define CLASH(x, y) ((x) != (y) && !(((x) ^ (y)) & (VCACHE - 1)))
    if (!CLASH(a, b) && !CLASH(a, c) && !CLASH(b, c)) {
        const dgl_cvtx *va = get(a, imm), *vb = get(b, imm), *vcx = get(c, imm), *vp = get(prov, imm);
        if (dgl_sink.triangle)
            dgl_sink.triangle(va, vb, vcx, vp);
    } else {
        dgl_cvtx v[3], pv;
        v[0] = *get(a, imm); v[1] = *get(b, imm); v[2] = *get(c, imm);
        pv = *get(prov, imm);
        if (dgl_sink.triangle)
            dgl_sink.triangle(&v[0], &v[1], &v[2], &pv);
    }
#undef CLASH
}

void dgl_tri_general(GLint a, GLint b, GLint c, GLint provoking)
{
    tri(a, b, c, provoking, NULL);
}

static void line(GLint a, GLint b, const dgl_vin *imm)
{
    dgl_cvtx v0 = *get(a, imm), v1 = *get(b, imm);
    if (dgl_sink.line)
        dgl_sink.line(&v0, &v1);
}

static void point(GLint a, const dgl_vin *imm)
{
    dgl_cvtx v = *get(a, imm);
    if (dgl_sink.point)
        dgl_sink.point(&v);
}

static void assemble(GLenum mode, GLsizei count, const GLint *idx, GLint first, const dgl_vin *imm)
{
    GLsizei i;
#define I(k) (idx ? idx[k] : first + (k))
    vc.cur++;
    dgl_prims.begins++;
    if (dgl_sink.begin) {
        int ok, o = PROF_SWITCH(PROF_D_VALID);
        ok = dgl_sink.begin();
        PROF_BACK(o);
        if (!ok) {
            dgl_prims.skipped++;
            return;
        }
    }
    if (imm)
        plan_imm(imm);
    else
        plan_arrays();
    if (dgl_sink.fast && dgl_sink.fast(mode, count, idx, first)) {     /* (a list's: indices into its vertices) */
        if (dgl_sink.end)
            dgl_sink.end();
        return;
    }
    switch (mode) {
    case GL_TRIANGLES:
        for (i = 0; i + 2 < count; i += 3)
            tri(I(i), I(i + 1), I(i + 2), I(i + 2), imm);
        break;
    case GL_TRIANGLE_STRIP:
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) tri(I(i + 1), I(i), I(i + 2), I(i + 2), imm);   /* keep the winding */
            else       tri(I(i), I(i + 1), I(i + 2), I(i + 2), imm);
        }
        break;
    case GL_TRIANGLE_FAN:
        for (i = 1; i + 1 < count; i++)
            tri(I(0), I(i), I(i + 1), I(i + 1), imm);
        break;
    case GL_QUADS:
        for (i = 0; i + 3 < count; i += 4) {
            tri(I(i), I(i + 1), I(i + 2), I(i + 3), imm);
            tri(I(i), I(i + 2), I(i + 3), I(i + 3), imm);
        }
        break;
    case GL_QUAD_STRIP:
        for (i = 0; i + 3 < count; i += 2) {
            tri(I(i), I(i + 1), I(i + 3), I(i + 3), imm);
            tri(I(i), I(i + 3), I(i + 2), I(i + 3), imm);
        }
        break;
    case GL_POLYGON:
        for (i = 1; i + 1 < count; i++)
            tri(I(0), I(i), I(i + 1), I(0), imm);
        break;
    case GL_LINES:
        for (i = 0; i + 1 < count; i += 2)
            line(I(i), I(i + 1), imm);
        break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        for (i = 0; i + 1 < count; i++)
            line(I(i), I(i + 1), imm);
        if (mode == GL_LINE_LOOP && count > 2)
            line(I(count - 1), I(0), imm);
        break;
    case GL_POINTS:
        for (i = 0; i < count; i++)
            point(I(i), imm);
        break;
    }
#undef I
    if (dgl_sink.end)
        dgl_sink.end();
}

void dgl_assemble(GLenum mode, GLsizei count, const GLint *idx, GLint first, const dgl_vin *imm)
{
    PROF_SCOPE(PROF_D_XFORM);
    DGL_FPU_ENTER();
    assemble(mode, count, idx, first, imm);
    DGL_FPU_LEAVE();
}

static int valid_mode(GLenum mode) { return mode <= GL_POLYGON; }

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (!valid_mode(mode)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (count < 0 || first < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (!dgl_gl.va.enabled || !count)
        return;
    if (dgl_sink.record && dgl_sink.record(mode, count, NULL, first))
        return;                             /* captured into a display list */
    dgl_assemble(mode, count, NULL, first, NULL);
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    static GLint *buf;
    static GLsizei cap;
    GLsizei i;
    if (!valid_mode(mode)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (count < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
        dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    if (!dgl_gl.va.enabled || !count)
        return;
    indices = dgl_buffer_address(dgl_element_buffer(), indices);   /* an offset with a buffer bound */
    if (count > cap) {
        GLint *n = (GLint *)realloc(buf, (size_t)count * sizeof *n);
        if (!n) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
        buf = n; cap = count;
    }
    for (i = 0; i < count; i++)
        buf[i] = type == GL_UNSIGNED_SHORT ? ((const unsigned short *)indices)[i] :
                 type == GL_UNSIGNED_BYTE ? ((const unsigned char *)indices)[i] :
                 (GLint)((const unsigned int *)indices)[i];
    if (dgl_sink.record && dgl_sink.record(mode, count, buf, 0))
        return;
    dgl_assemble(mode, count, buf, 0, NULL);
}

/* ---- Immediate mode (D6): the same stream from a vertex buffer -------- */
static struct { int active; GLenum mode; dgl_vin *v; GLsizei n, cap; } imm;

void APIENTRY glBegin(GLenum mode)
{
    if (imm.active) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    if (!valid_mode(mode)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    imm.active = 1; imm.mode = mode; imm.n = 0;
}

void dgl_imm_vertex(float x, float y, float z, float w);

static void imm_vertex(float x, float y, float z, float w)
{
    dgl_vin *v;
    if (!imm.active)
        return;
    if (imm.n == imm.cap) {
        GLsizei nc = imm.cap ? imm.cap * 2 : 1024;
        dgl_vin *nv = (dgl_vin *)realloc(imm.v, (size_t)nc * sizeof *nv);
        if (!nv) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
        imm.v = nv; imm.cap = nc;
    }
    v = &imm.v[imm.n++];
    v->pos[0] = x; v->pos[1] = y; v->pos[2] = z; v->pos[3] = w;
    memcpy(v->col, dgl_gl.cur_color, sizeof v->col);
    memcpy(v->tex, dgl_gl.cur_tex, sizeof v->tex);
    memcpy(v->tex1, dgl_gl.cur_tex1, sizeof v->tex1);
}

void APIENTRY glEnd(void)
{
    if (!imm.active) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    imm.active = 0;
    if (dgl_sink.record_imm && dgl_sink.record_imm(imm.mode, imm.n, imm.v))
        return;
    dgl_assemble(imm.mode, imm.n, NULL, 0, imm.v);
}

void dgl_imm_vertex(float x, float y, float z, float w) { imm_vertex(x, y, z, w); }

/* glArrayElement: the attribute commands for element i of each enabled array,
 * then (with the vertex array enabled) the vertex itself, as GL 1.1 defines. */
void APIENTRY glArrayElement(GLint i)
{
    dgl_vin v;
    memset(&v, 0, sizeof v);
    v.pos[3] = 1;
    if (dgl_gl.ca.enabled) {
        v.col[3] = 1;
        read_comps(&dgl_gl.ca, i, v.col, 4, 1);
        memcpy(dgl_gl.cur_color, v.col, sizeof v.col);
    }
    if (dgl_gl.ta.enabled) {
        read_comps(&dgl_gl.ta, i, v.tex, 2, 0);
        memcpy(dgl_gl.cur_tex, v.tex, sizeof v.tex);
    }
    if (dgl_gl.ta1.enabled) {
        read_comps(&dgl_gl.ta1, i, v.tex1, 2, 0);
        memcpy(dgl_gl.cur_tex1, v.tex1, sizeof v.tex1);
    }
    if (dgl_gl.va.enabled) {
        read_comps(&dgl_gl.va, i, v.pos, 4, 0);
        imm_vertex(v.pos[0], v.pos[1], v.pos[2], v.pos[3]);
    }
}

void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z) { imm_vertex(x, y, z, 1); }
void APIENTRY glVertex2f(GLfloat x, GLfloat y) { imm_vertex(x, y, 0, 1); }
void APIENTRY glVertex3fv(const GLfloat *v) { imm_vertex(v[0], v[1], v[2], 1); }
void APIENTRY glTexCoord2f(GLfloat s, GLfloat t) { dgl_gl.cur_tex[0] = s; dgl_gl.cur_tex[1] = t; }

void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    dgl_gl.cur_color[0] = r; dgl_gl.cur_color[1] = g; dgl_gl.cur_color[2] = b; dgl_gl.cur_color[3] = a;
}
void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1.0f); }
void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    glColor4f(DGL_UB(r), DGL_UB(g), DGL_UB(b), DGL_UB(a));
}
void APIENTRY glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor4ub(r, g, b, 255); }
