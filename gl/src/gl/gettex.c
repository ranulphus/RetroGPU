/* gettex.c - glGetTexImage (PRD §8.3).
 *
 * A level of the active unit's texture, read from texture.c's shadow copy:
 * RGBA8 already reduced to the level's internal format class (what the
 * hardware draws), or for a colour-index level its indices looked up in the
 * effective colour table. As GL defines it, a level is first taken to RGBA
 * by its base format (luminance L as (L, 0, 0, 1), luminance-alpha as
 * (L, 0, 0, A), intensity I as (I, 0, 0, 1), alpha A as (0, 0, 0, A), RGB
 * with alpha 1) and then packed as glReadPixels would (luminance = R + G + B,
 * clamped), rows in the order they were uploaded, with GL_PACK_ALIGNMENT.
 * PrBoom-plus reads its sky texture this way for the sky's average colour. */
#include "gl_state.h"
#include "gl_tex.h"
#include <string.h>

/* A shadow texel (RGBA8: luminance and intensity replicated into R, G, B)
 * as the RGBA that GL's base format gives it. */
static void base(int ifc, const unsigned char *s, unsigned char *c)
{
    switch (ifc) {
    case DGL_IF_LUMINANCE: case DGL_IF_INTENSITY: c[0] = s[0]; c[1] = c[2] = 0; c[3] = 255; break;
    case DGL_IF_LUMINANCE_ALPHA: c[0] = s[0]; c[1] = c[2] = 0; c[3] = s[3]; break;
    case DGL_IF_ALPHA: c[0] = c[1] = c[2] = 0; c[3] = s[3]; break;
    case DGL_IF_RGB: c[0] = s[0]; c[1] = s[1]; c[2] = s[2]; c[3] = 255; break;
    default: memcpy(c, s, 4); break;            /* RGBA, RGB5_A1, colour index */
    }
}

void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
    const dgl_texture *t;
    const dgl_level *L;
    const dgl_palette *pal;
    unsigned char *row = (unsigned char *)pixels;
    int bpp, stride, x, y;
    if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (level < 0 || level >= DGL_MAX_LEVELS) { dgl_gl_error(GL_INVALID_VALUE); return; }
    switch (format) {
    case GL_RGBA: case GL_BGRA_EXT: bpp = 4; break;
    case GL_RGB: case GL_BGR_EXT: bpp = 3; break;
    case GL_LUMINANCE_ALPHA: bpp = 2; break;
    case GL_RED: case GL_GREEN: case GL_BLUE: case GL_ALPHA: case GL_LUMINANCE: bpp = 1; break;
    default: dgl_gl_error(GL_INVALID_ENUM); return;
    }
    t = dgl_unit_texture(dgl_gl.active_unit);
    if (!t)
        return;
    L = &t->level[level];
    if (!DGL_LEVEL_DEFINED(L))
        return;
    pal = dgl_palette_for(t);
    stride = L->w * bpp;
    stride = (stride + dgl_gl.pack_align - 1) / dgl_gl.pack_align * dgl_gl.pack_align;
    for (y = 0; y < L->h; y++, row += stride) {
        unsigned char *p = row;
        for (x = 0; x < L->w; x++, p += bpp) {
            size_t i = (size_t)y * L->w + x;
            const unsigned char *s = L->idx ? dgl_palette_texel(pal, L->idx[i]) : L->rgba + i * 4;
            unsigned char c[4];
            unsigned l;
            base(L->ifc, s, c);
            switch (format) {
            case GL_RGBA: memcpy(p, c, 4); break;
            case GL_BGRA_EXT: p[0] = c[2]; p[1] = c[1]; p[2] = c[0]; p[3] = c[3]; break;
            case GL_RGB: memcpy(p, c, 3); break;
            case GL_BGR_EXT: p[0] = c[2]; p[1] = c[1]; p[2] = c[0]; break;
            case GL_LUMINANCE_ALPHA: case GL_LUMINANCE:
                l = (unsigned)c[0] + c[1] + c[2];
                p[0] = (unsigned char)(l > 255 ? 255 : l);
                if (format == GL_LUMINANCE_ALPHA)
                    p[1] = c[3];
                break;
            case GL_RED: p[0] = c[0]; break;
            case GL_GREEN: p[0] = c[1]; break;
            case GL_BLUE: p[0] = c[2]; break;
            default: p[0] = c[3]; break;          /* GL_ALPHA */
            }
        }
    }
}
