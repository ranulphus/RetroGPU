/* lists.c - display lists (PRD §5.4, FR-DL-1..3).
 *
 * ClassiCube compiles each world chunk as pointer setup plus one
 * glDrawElements inside glNewList(GL_COMPILE)...glEndList, and replays it
 * with glCallList. While compiling, draw calls and immediate-mode blocks
 * are captured: the referenced vertices are copied into a driver-owned
 * buffer (the application's arrays are reused straight away) and indices
 * are rebased onto it. Replay runs the copy through the normal vertex
 * stream with the state current at glCallList. Other GL calls made while
 * compiling take effect immediately rather than being recorded (the v1.0
 * scope limit, FR-DL-3). */
#include "gl_state.h"
#include "gl_draw.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    GLenum mode;
    GLsizei count;
    dgl_vin *v;             /* the snapshot */
    GLint *idx;             /* rebased indices, or NULL for sequential */
} list_prim;

typedef struct {
    int used;
    list_prim *prim;
    int n, cap;
} list_t;

static list_t *lists;
static GLuint nlists;
static GLuint list_base;                /* glListBase, for glCallLists */
static GLuint compiling;            /* list being compiled, 0 = none */
static GLenum compile_mode;

static void clear_list(list_t *l)
{
    int i;
    for (i = 0; i < l->n; i++) {
        free(l->prim[i].v);
        free(l->prim[i].idx);
    }
    free(l->prim);
    l->prim = NULL;
    l->n = l->cap = 0;
}

static list_prim *add_prim(void)
{
    list_t *l = &lists[compiling];
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 4;
        list_prim *np = (list_prim *)realloc(l->prim, (size_t)nc * sizeof *np);
        if (!np)
            return NULL;
        l->prim = np;
        l->cap = nc;
    }
    memset(&l->prim[l->n], 0, sizeof l->prim[0]);
    return &l->prim[l->n++];
}

void dgl_fetch_vertex(GLint i, dgl_vin *v);           /* vertex.c */

static int record(GLenum mode, GLsizei count, const GLint *idx, GLint first)
{
    list_prim *p;
    GLint lo = first, hi = first + count - 1, k;
    if (!compiling)
        return 0;
    if (idx) {
        lo = hi = idx[0];
        for (k = 1; k < count; k++) {
            if (idx[k] < lo) lo = idx[k];
            if (idx[k] > hi) hi = idx[k];
        }
    }
    p = add_prim();
    if (!p || !(p->v = (dgl_vin *)malloc((size_t)(hi - lo + 1) * sizeof *p->v)) ||
        (idx && !(p->idx = (GLint *)malloc((size_t)count * sizeof *p->idx)))) {
        dgl_gl_error(GL_OUT_OF_MEMORY);
        return 1;
    }
    for (k = lo; k <= hi; k++)
        dgl_fetch_vertex(k, &p->v[k - lo]);
    if (idx)
        for (k = 0; k < count; k++)
            p->idx[k] = idx[k] - lo;
    p->mode = mode;
    p->count = count;
    return compile_mode == GL_COMPILE;       /* COMPILE_AND_EXECUTE also draws now */
}

static int record_imm(GLenum mode, GLsizei count, const dgl_vin *v)
{
    list_prim *p;
    if (!compiling)
        return 0;
    p = add_prim();
    if (!p || !(p->v = (dgl_vin *)malloc((size_t)(count ? count : 1) * sizeof *p->v))) {
        dgl_gl_error(GL_OUT_OF_MEMORY);
        return 1;
    }
    memcpy(p->v, v, (size_t)count * sizeof *v);
    p->mode = mode;
    p->count = count;
    return compile_mode == GL_COMPILE;
}

GLuint APIENTRY glGenLists(GLsizei range)
{
    GLuint start = 1, n;
    if (range < 0) { dgl_gl_error(GL_INVALID_VALUE); return 0; }
    if (!range)
        return 0;
    /* First run of 'range' unused names. */
    for (;;) {
        for (n = 0; n < (GLuint)range && start + n < nlists && !lists[start + n].used; n++)
            ;
        if (n == (GLuint)range || start + n >= nlists)
            break;
        start += n + 1;
    }
    if (start + (GLuint)range > nlists) {
        GLuint nn = start + (GLuint)range + 64;
        list_t *nl = (list_t *)realloc(lists, nn * sizeof *nl);
        if (!nl) { dgl_gl_error(GL_OUT_OF_MEMORY); return 0; }
        memset(nl + nlists, 0, (nn - nlists) * sizeof *nl);
        lists = nl;
        nlists = nn;
    }
    for (n = 0; n < (GLuint)range; n++)
        lists[start + n].used = 1;
    dgl_sink.record = record;
    dgl_sink.record_imm = record_imm;
    return start;
}

void APIENTRY glDeleteLists(GLuint list, GLsizei range)
{
    GLuint i;
    if (range < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    for (i = list; i < list + (GLuint)range && i < nlists; i++) {
        if (i == compiling)
            continue;
        clear_list(&lists[i]);
        lists[i].used = 0;
    }
}

void APIENTRY glNewList(GLuint list, GLenum mode)
{
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (!list) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (compiling) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    if (list >= nlists) {
        GLuint nn = list + 64;
        list_t *nl = (list_t *)realloc(lists, nn * sizeof *nl);
        if (!nl) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
        memset(nl + nlists, 0, (nn - nlists) * sizeof *nl);
        lists = nl;
        nlists = nn;
    }
    clear_list(&lists[list]);
    lists[list].used = 1;
    compiling = list;
    compile_mode = mode;
    dgl_sink.record = record;
    dgl_sink.record_imm = record_imm;
}

void APIENTRY glEndList(void)
{
    if (!compiling) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    compiling = 0;
}

void APIENTRY glCallList(GLuint list)
{
    list_t *l;
    int i;
    if (list >= nlists || !lists[list].used || compiling)
        return;                            /* nesting is outside the v1.0 scope (FR-DL-3) */
    l = &lists[list];
    for (i = 0; i < l->n; i++)
        dgl_assemble(l->prim[i].mode, l->prim[i].count, l->prim[i].idx, 0, l->prim[i].v);
}

GLboolean APIENTRY glIsList(GLuint list)
{
    return (GLboolean)(list && list < nlists && lists[list].used);
}

void APIENTRY glListBase(GLuint base) { list_base = base; }
GLuint dgl_list_base(void) { return list_base; }

/* glCallLists: each name (of the given type) offset by the list base. */
void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *names)
{
    GLsizei i;
    if (n < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        GLuint name;
        switch (type) {
        case GL_BYTE: name = (GLuint)((const signed char *)names)[i]; break;
        case GL_UNSIGNED_BYTE: name = ((const unsigned char *)names)[i]; break;
        case GL_SHORT: name = (GLuint)((const short *)names)[i]; break;
        case GL_UNSIGNED_SHORT: name = ((const unsigned short *)names)[i]; break;
        case GL_INT: name = (GLuint)((const int *)names)[i]; break;
        case GL_UNSIGNED_INT: name = ((const unsigned int *)names)[i]; break;
        case GL_FLOAT: name = (GLuint)((const float *)names)[i]; break;
        default: dgl_gl_error(GL_INVALID_ENUM); return;
        }
        glCallList(list_base + name);
    }
}
