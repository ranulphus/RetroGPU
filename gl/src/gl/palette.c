/* palette.c - paletted textures, software tier (GL_EXT_paletted_texture,
 * GL_EXT_shared_texture_palette).
 *
 * A COLOR_INDEX texture keeps its 8-bit indices; when it goes to VRAM each
 * index is looked up in the effective palette (the shared one while
 * GL_SHARED_TEXTURE_PALETTE_EXT is enabled, else the texture's own) and
 * stored as RGB565, ARGB1555 or ARGB4444 like any other texture. Every
 * palette carries a generation number: a texture expanded with an older
 * palette is uploaded again when next drawn. The G200's hardware lookup
 * table (half the VRAM) comes with dual texturing (plan Q5). */
#include "gl_tex.h"
#include "../dgl/dgl.h"
#include <stdlib.h>
#include <string.h>

static dgl_palette shared;
static const dgl_palette empty;

void dgl_palettes_reset(void)
{
    unsigned gen = shared.gen + 1;
    memset(&shared, 0, sizeof shared);
    shared.gen = gen;
}

const dgl_palette *dgl_palette_for(const dgl_texture *t)
{
    if (dgl_gl.shared_palette)
        return &shared;
    return t->own ? t->own : &empty;
}

const unsigned char *dgl_palette_texel(const dgl_palette *p, unsigned i)
{
    static const unsigned char black[4] = { 0, 0, 0, 255 };
    return p->width ? p->rgba + (i & (unsigned)(p->width - 1)) * 4 : black;
}

/* The palette a color-table call names; NULL (with the error set) if none.
 * GL_PROXY_TEXTURE_2D gives a scratch table that is checked and dropped. */
static dgl_palette *target_palette(GLenum target)
{
    static dgl_palette proxy;
    switch (target) {
    case GL_SHARED_TEXTURE_PALETTE_EXT:
        return &shared;
    case GL_TEXTURE_2D: {
        dgl_palette *p = dgl_bound_palette();
        if (!p)
            dgl_gl_error(GL_OUT_OF_MEMORY);
        return p;
    }
    case GL_PROXY_TEXTURE_2D:
        return &proxy;
    default:
        dgl_gl_error(GL_INVALID_ENUM);
        return NULL;
    }
}

static int load(dgl_palette *p, int start, int count, GLenum format, GLenum type, const GLvoid *table)
{
    int bytes = dgl_format_bytes(format);
    if (type != GL_UNSIGNED_BYTE || !bytes || format == GL_COLOR_INDEX) {
        dgl_gl_error(GL_INVALID_ENUM);
        return -1;
    }
    if (table) {
        dgl_to_rgba(format, (const unsigned char *)table, count, p->rgba + start * 4);
        dgl_apply_ifmt(dgl_ifmt_class((GLint)p->ifmt), p->rgba + start * 4, count);
    }
    p->gen++;
    p->opaque = 1;
    {
        int i, n = start + count > p->width ? start + count : p->width;
        for (i = 0; i < n && i < 256; i++)
            if (p->rgba[i * 4 + 3] != 255)
                p->opaque = 0;
    }
    dgl_gl.dirty |= DGL_DIRTY_TEXTURE;
    return 0;
}

void APIENTRY glColorTableEXT(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type,
                              const GLvoid *table)
{
    dgl_palette *p;
    int ifc = dgl_ifmt_class((GLint)internalformat);
    if (ifc < 0 || ifc == DGL_IF_INDEX) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (width < 1 || width > 256 || (width & (width - 1))) { dgl_gl_error(GL_INVALID_VALUE); return; }
    p = target_palette(target);
    if (!p)
        return;
    p->ifmt = internalformat;
    if (load(p, 0, width, format, type, table) == 0)
        p->width = width;
}

void APIENTRY glColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type,
                                 const GLvoid *data)
{
    dgl_palette *p = target_palette(target);
    if (!p)
        return;
    if (start < 0 || count < 0 || start + count > p->width) { dgl_gl_error(GL_INVALID_VALUE); return; }
    load(p, start, count, format, type, data);
}

void APIENTRY glGetColorTableEXT(GLenum target, GLenum format, GLenum type, GLvoid *data)
{
    dgl_palette *p = target_palette(target);
    unsigned char *d = (unsigned char *)data;
    int i;
    if (!p)
        return;
    if (type != GL_UNSIGNED_BYTE || (format != GL_RGBA && format != GL_RGB)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < p->width; i++) {
        memcpy(d, p->rgba + i * 4, format == GL_RGBA ? 4 : 3);
        d += format == GL_RGBA ? 4 : 3;
    }
}

void APIENTRY glGetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params)
{
    dgl_palette *p = target_palette(target);
    int ifc;
    if (!p)
        return;
    ifc = p->width ? dgl_ifmt_class((GLint)p->ifmt) : -1;
    switch (pname) {
    case GL_COLOR_TABLE_FORMAT_EXT: params[0] = p->width ? (GLint)p->ifmt : GL_RGBA; break;
    case GL_COLOR_TABLE_WIDTH_EXT: params[0] = p->width; break;
    case GL_COLOR_TABLE_RED_SIZE_EXT: case GL_COLOR_TABLE_GREEN_SIZE_EXT: case GL_COLOR_TABLE_BLUE_SIZE_EXT:
        params[0] = ifc == DGL_IF_RGB || ifc == DGL_IF_RGBA || ifc == DGL_IF_RGB5_A1 ? 8 : 0; break;
    case GL_COLOR_TABLE_ALPHA_SIZE_EXT:
        params[0] = ifc == DGL_IF_RGBA || ifc == DGL_IF_RGB5_A1 || ifc == DGL_IF_ALPHA ||
                    ifc == DGL_IF_LUMINANCE_ALPHA ? 8 : 0; break;
    case GL_COLOR_TABLE_LUMINANCE_SIZE_EXT:
        params[0] = ifc == DGL_IF_LUMINANCE || ifc == DGL_IF_LUMINANCE_ALPHA ? 8 : 0; break;
    case GL_COLOR_TABLE_INTENSITY_SIZE_EXT: params[0] = ifc == DGL_IF_INTENSITY ? 8 : 0; break;
    default: dgl_gl_error(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params)
{
    GLint v = 0;
    glGetColorTableParameterivEXT(target, pname, &v);
    params[0] = (GLfloat)v;
}
