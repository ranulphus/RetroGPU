/* readpix.c - colour buffer selection and glReadPixels (PRD §8.4).
 * glReadPixels reads the glReadBuffer buffer (the back buffer by default when
 * double-buffered): RGB565 expanded to 8 bits per channel by bit
 * replication, alpha 255, rows bottom-up as GL numbers them. */
#include "gl_state.h"
#include "gl_tex.h"
#include "../dgl/dgl.h"
#include <string.h>

void APIENTRY glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
    const volatile uint16_t *fb;
    unsigned char *row = (unsigned char *)pixels;
    int bpp, stride, i, j;
    if (w < 0 || h < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (type != GL_UNSIGNED_BYTE) { dgl_gl_error(GL_INVALID_ENUM); return; }
    switch (format) {
    case GL_RGBA: case GL_BGRA_EXT: bpp = 4; break;
    case GL_RGB: case GL_BGR_EXT: bpp = 3; break;
    default: dgl_gl_error(GL_INVALID_ENUM); return;
    }
    if (!dgl_ctx.active)
        return;
    stride = w * bpp;
    stride = (stride + dgl_gl.pack_align - 1) / dgl_gl.pack_align * dgl_gl.pack_align;
    dgl_sync();                             /* the LFB is not ordered with queued draws */
    DGL_GPU_QUIET();                        /* (the CPU's work from here) */
    fb = (const volatile uint16_t *)(mga_fb + dgl_color_off(dgl_ctx.read_front));
    for (j = 0; j < h; j++, row += stride) {
        int sy = dgl_ctx.height - 1 - (y + j);     /* GL row -> screen row */
        unsigned char *p = row;
        if ((j & 15) == 15)
            dgl_yield_maybe(0);                    /* (a whole frame read back takes a while: the sound's turn) */
        for (i = 0; i < w; i++, p += bpp) {
            int sx = x + i;
            unsigned r = 0, g = 0, b = 0;
            if (sx >= 0 && sx < dgl_ctx.width && sy >= 0 && sy < dgl_ctx.height) {
                uint16_t c = fb[sy * dgl_ctx.pitch_px + sx];
                r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31;
                r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
            }
            if (format == GL_RGBA || format == GL_RGB) { p[0] = (unsigned char)r; p[2] = (unsigned char)b; }
            else { p[0] = (unsigned char)b; p[2] = (unsigned char)r; }
            p[1] = (unsigned char)g;
            if (bpp == 4)
                p[3] = 255;
        }
    }
}

/* ---- Colour buffer selection --------------------------------------------- */
/* 1 = the shown buffer, 0 = the hidden one, -1 = not a buffer DOS-GL has.
 * Both-buffer modes draw into the hidden one only (logged once). */
static int which_buffer(GLenum mode)
{
    static int warned;
    switch (mode) {
    case GL_FRONT: case GL_FRONT_LEFT:
        return 1;
    case GL_BACK: case GL_BACK_LEFT:
        return dgl_ctx.active && !dgl_ctx.double_buffer ? -1 : 0;
    case GL_FRONT_AND_BACK: case GL_LEFT:
        if (!warned++)
            DGL_WARN("glDrawBuffer 0x%x draws the back buffer only", (unsigned)mode);
        return 0;
    default:
        return -1;
    }
}

void APIENTRY glDrawBuffer(GLenum mode)
{
    int front = which_buffer(mode);
    if (front < 0) { dgl_gl_error(mode == GL_BACK || mode == GL_BACK_LEFT ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    dgl_gl.draw_buffer = mode;
    if (front == dgl_ctx.draw_front)
        return;
    dgl_ctx.draw_front = front;
    dgl_retarget();
    dgl_gl.dirty |= DGL_DIRTY_TARGET | DGL_DIRTY_RASTER;    /* the target write reset MACCESS */
}

void APIENTRY glReadBuffer(GLenum mode)
{
    int front = mode == GL_FRONT_AND_BACK || mode == GL_LEFT ? -1 : which_buffer(mode);
    if (front < 0) { dgl_gl_error(mode == GL_BACK || mode == GL_BACK_LEFT ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    dgl_gl.read_buffer = mode;
    dgl_ctx.read_front = front;
}
