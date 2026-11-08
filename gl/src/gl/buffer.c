/* buffer.c - GL_ARB_vertex_buffer_object and GL_EXT_draw_range_elements.
 *
 * DOS-GL transforms every vertex on the CPU, so a buffer object is a block
 * of system memory: glBufferDataARB copies into it, glMapBufferARB hands it
 * out. What the extension changes is the meaning of the pointers: with a
 * buffer bound to GL_ARRAY_BUFFER_ARB, gl*Pointer's pointer is an offset
 * into that buffer (the array remembers which), and with one bound to
 * GL_ELEMENT_ARRAY_BUFFER_ARB, glDrawElements' indices are. The arrays keep
 * the buffer object, not its memory, so a later glBufferDataARB (which may
 * move the memory) is seen at the next draw, as GL requires.
 *
 * Renderers written for GL 1.5 use buffer objects to reach their fastest
 * path (Xash3D draws world lightmaps in the second texture unit only with
 * them); here the gain is that path, not the storage. */
#include "gl_state.h"
#include <stdlib.h>
#include <string.h>

static dgl_buffer **bufs;                  /* by name; slot 0 unused */
static GLuint nbufs;
static dgl_buffer *bound_array, *bound_elements;

static dgl_buffer *lookup(GLuint name)
{
    return name && name < nbufs ? bufs[name] : NULL;
}

static dgl_buffer **binding(GLenum target)
{
    switch (target) {
    case GL_ARRAY_BUFFER_ARB: return &bound_array;
    case GL_ELEMENT_ARRAY_BUFFER_ARB: return &bound_elements;
    default: dgl_gl_error(GL_INVALID_ENUM); return NULL;
    }
}

const dgl_buffer *dgl_array_buffer(void) { return bound_array; }
const dgl_buffer *dgl_element_buffer(void) { return bound_elements; }

/* Where an array's or the indices' data starts: the buffer's memory plus the
 * offset the pointer held, or the pointer itself without a buffer. */
const void *dgl_buffer_address(const dgl_buffer *b, const void *p)
{
    return b ? (const void *)((const unsigned char *)b->data + (size_t)p) : p;
}

/* A new, empty buffer object called name (the table grows to hold it). */
static dgl_buffer *create(GLuint name)
{
    while (name >= nbufs) {
        GLuint cap = nbufs ? nbufs * 2 : 64;
        dgl_buffer **nb = (dgl_buffer **)realloc(bufs, cap * sizeof *nb);
        if (!nb) { dgl_gl_error(GL_OUT_OF_MEMORY); return NULL; }
        memset(nb + nbufs, 0, (cap - nbufs) * sizeof *nb);
        bufs = nb;
        nbufs = cap;
    }
    bufs[name] = (dgl_buffer *)calloc(1, sizeof **bufs);
    if (!bufs[name]) { dgl_gl_error(GL_OUT_OF_MEMORY); return NULL; }
    bufs[name]->name = name;
    bufs[name]->usage = GL_STATIC_DRAW_ARB;
    bufs[name]->access = GL_READ_WRITE_ARB;
    return bufs[name];
}

void APIENTRY glGenBuffersARB(GLsizei n, GLuint *names)
{
    GLsizei i;
    GLuint name = 1;
    if (n < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        while (name < nbufs && bufs[name])
            name++;
        if (!create(name))
            return;
        names[i] = name;
    }
}

void APIENTRY glDeleteBuffersARB(GLsizei n, const GLuint *names)
{
    GLsizei i;
    if (n < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        dgl_buffer *b = lookup(names[i]);
        if (!b)
            continue;
        /* A deleted buffer is unbound everywhere it was bound. */
        if (bound_array == b) bound_array = NULL;
        if (bound_elements == b) bound_elements = NULL;
        dgl_arrays_forget_buffer(b);
        free(b->data);
        free(b);
        bufs[names[i]] = NULL;
    }
}

GLboolean APIENTRY glIsBufferARB(GLuint name)
{
    return lookup(name) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindBufferARB(GLenum target, GLuint name)
{
    dgl_buffer **slot = binding(target);
    if (!slot)
        return;
    /* The extension lets a program bind a name it made up: the object is
     * created then. */
    if (name && !lookup(name) && !create(name))
        return;
    *slot = lookup(name);
}

void APIENTRY glBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage)
{
    dgl_buffer **slot = binding(target), *b;
    unsigned char *mem;
    if (!slot)
        return;
    if (size < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (!(b = *slot) || b->mapped) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    mem = size ? (unsigned char *)malloc((size_t)size) : NULL;
    if (size && !mem) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
    if (mem && data)
        memcpy(mem, data, (size_t)size);
    free(b->data);
    b->data = mem;
    b->size = size;
    b->usage = usage;
}

void APIENTRY glBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data)
{
    dgl_buffer **slot = binding(target), *b;
    if (!slot)
        return;
    if (!(b = *slot) || b->mapped) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    if (offset < 0 || size < 0 || offset + size > b->size) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (size)
        memcpy((unsigned char *)b->data + offset, data, (size_t)size);
}

void APIENTRY glGetBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, GLvoid *data)
{
    dgl_buffer **slot = binding(target), *b;
    if (!slot)
        return;
    if (!(b = *slot) || b->mapped) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    if (offset < 0 || size < 0 || offset + size > b->size) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (size)
        memcpy(data, (const unsigned char *)b->data + offset, (size_t)size);
}

GLvoid *APIENTRY glMapBufferARB(GLenum target, GLenum access)
{
    dgl_buffer **slot = binding(target), *b;
    if (!slot)
        return NULL;
    if (access != GL_READ_ONLY_ARB && access != GL_WRITE_ONLY_ARB && access != GL_READ_WRITE_ARB) {
        dgl_gl_error(GL_INVALID_ENUM);
        return NULL;
    }
    if (!(b = *slot) || b->mapped) { dgl_gl_error(GL_INVALID_OPERATION); return NULL; }
    b->mapped = 1;
    b->access = access;
    return b->data;
}

GLboolean APIENTRY glUnmapBufferARB(GLenum target)
{
    dgl_buffer **slot = binding(target), *b;
    if (!slot)
        return GL_FALSE;
    if (!(b = *slot) || !b->mapped) { dgl_gl_error(GL_INVALID_OPERATION); return GL_FALSE; }
    b->mapped = 0;
    return GL_TRUE;                         /* system memory is never lost */
}

void APIENTRY glGetBufferParameterivARB(GLenum target, GLenum pname, GLint *v)
{
    dgl_buffer **slot = binding(target), *b;
    if (!slot)
        return;
    if (!(b = *slot)) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    switch (pname) {
    case GL_BUFFER_SIZE_ARB: v[0] = (GLint)b->size; break;
    case GL_BUFFER_USAGE_ARB: v[0] = (GLint)b->usage; break;
    case GL_BUFFER_ACCESS_ARB: v[0] = (GLint)b->access; break;
    case GL_BUFFER_MAPPED_ARB: v[0] = b->mapped; break;
    default: dgl_gl_error(GL_INVALID_ENUM); break;
    }
}

void APIENTRY glGetBufferPointervARB(GLenum target, GLenum pname, GLvoid **v)
{
    dgl_buffer **slot = binding(target), *b;
    if (!slot)
        return;
    if (pname != GL_BUFFER_MAP_POINTER_ARB) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (!(b = *slot)) { dgl_gl_error(GL_INVALID_OPERATION); return; }
    v[0] = b->mapped ? b->data : NULL;
}

/* glGetIntegerv's buffer bindings (get.c) */
int dgl_buffer_query(GLenum p, double *v)
{
    switch (p) {
    case GL_ARRAY_BUFFER_BINDING_ARB: v[0] = bound_array ? bound_array->name : 0; return 1;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB: v[0] = bound_elements ? bound_elements->name : 0; return 1;
    case GL_VERTEX_ARRAY_BUFFER_BINDING_ARB: v[0] = dgl_gl.va.buf ? dgl_gl.va.buf->name : 0; return 1;
    case GL_COLOR_ARRAY_BUFFER_BINDING_ARB: v[0] = dgl_gl.ca.buf ? dgl_gl.ca.buf->name : 0; return 1;
    case GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING_ARB: {
        const dgl_array *a = dgl_gl.client_unit ? &dgl_gl.ta1 : &dgl_gl.ta;
        v[0] = a->buf ? a->buf->name : 0;
        return 1;
    }
    case GL_MAX_ELEMENTS_VERTICES_EXT: v[0] = 65536; return 1;   /* only a hint to the program */
    case GL_MAX_ELEMENTS_INDICES_EXT: v[0] = 65536; return 1;
    default: return 0;
    }
}

/* GL_EXT_draw_range_elements: the range is a promise DOS-GL has no use for */
void APIENTRY glDrawRangeElementsEXT(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                     const GLvoid *indices)
{
    if (end < start) { dgl_gl_error(GL_INVALID_VALUE); return; }
    glDrawElements(mode, count, type, indices);
}

/* Context teardown: every buffer object goes. */
void dgl_buffers_reset(void)
{
    GLuint i;
    for (i = 0; i < nbufs; i++)
        if (bufs[i]) {
            free(bufs[i]->data);
            free(bufs[i]);
        }
    free(bufs);
    bufs = NULL;
    nbufs = 0;
    bound_array = bound_elements = NULL;
}
