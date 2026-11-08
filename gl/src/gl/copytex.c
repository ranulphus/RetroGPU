/* copytex.c - glCopyTexImage2D and glCopyTexSubImage2D (PRD §8.3, §8.4).
 *
 * The rectangle is read from the read buffer as glReadPixels reads it
 * (RGB565 widened to 8 bits, alpha 255) and loaded as glTexImage2D or
 * glTexSubImage2D would load it, so the texture's shadow copy, format class
 * and VRAM upload follow the usual paths. PrBoom-plus's GL screen wipes copy
 * the frame into a texture this way. */
#include "gl_state.h"
#include <stdlib.h>

/* The rectangle as tightly packed RGBA8 rows, bottom-up; NULL on no memory. */
static unsigned char *read_rect(GLint x, GLint y, GLsizei w, GLsizei h)
{
    unsigned char *buf = (unsigned char *)malloc((size_t)(w ? w : 1) * (h ? h : 1) * 4);
    GLint pack = dgl_gl.pack_align;
    if (!buf) {
        dgl_gl_error(GL_OUT_OF_MEMORY);
        return NULL;
    }
    dgl_gl.pack_align = 1;
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    dgl_gl.pack_align = pack;
    return buf;
}

void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y,
                               GLsizei w, GLsizei h, GLint border)
{
    unsigned char *buf;
    GLint unpack = dgl_gl.unpack_align, rowlen = dgl_gl.unpack_row_length;
    if (target != GL_TEXTURE_2D) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (border != 0 || w < 0 || h < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (!(buf = read_rect(x, y, w, h)))
        return;
    dgl_gl.unpack_align = 1;
    dgl_gl.unpack_row_length = 0;
    glTexImage2D(target, level, (GLint)internalformat, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    dgl_gl.unpack_align = unpack;
    dgl_gl.unpack_row_length = rowlen;
    free(buf);
}

void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y,
                                  GLsizei w, GLsizei h)
{
    unsigned char *buf;
    GLint unpack = dgl_gl.unpack_align, rowlen = dgl_gl.unpack_row_length;
    if (target != GL_TEXTURE_2D) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (w < 0 || h < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (!w || !h)
        return;
    if (!(buf = read_rect(x, y, w, h)))
        return;
    dgl_gl.unpack_align = 1;
    dgl_gl.unpack_row_length = 0;
    glTexSubImage2D(target, level, xoffset, yoffset, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    dgl_gl.unpack_align = unpack;
    dgl_gl.unpack_row_length = rowlen;
    free(buf);
}
