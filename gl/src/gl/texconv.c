/* texconv.c - texel conversion (PRD §8.3): GL's side. The consumer uploads
 * any GL 1.1 format; each level is kept as RGBA8 reduced to its internal
 * format's class, and its alpha classed (opaque, 0/255 only, anything else)
 * for the drawing back end to choose its own format (the Matrox one's are
 * src/backend/mga/texconv.c). Pure C, unit-tested on the host. */
#include "gl_tex.h"

int dgl_alpha_class(const unsigned char *rgba, long n)
{
    int cls = DGL_ALPHA_OPAQUE;
    long i;
    for (i = 0; i < n; i++) {
        unsigned a = rgba[i * 4 + 3];
        if (a == 255)
            continue;
        if (a != 0)
            return DGL_ALPHA_GRADIENT;
        cls = DGL_ALPHA_BINARY;
    }
    return cls;
}

/* Any client format DOS-GL accepts -> RGBA8 (the shadow copy's format). */
int dgl_to_rgba(GLenum format, const unsigned char *src, long n, unsigned char *dst)
{
    long i;
#define EACH(step, body) for (i = 0; i < n; i++, dst += 4, src += (step)) { body; } break
    switch (format) {                           /* (once a row, not once a texel) */
    case GL_RGBA: EACH(4, dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3]);
    case GL_BGRA_EXT: EACH(4, dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3]);
    case GL_RGB: EACH(3, dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255);
    case GL_BGR_EXT: EACH(3, dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255);
    case GL_LUMINANCE: EACH(1, dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255);
    case GL_LUMINANCE_ALPHA: EACH(2, dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1]);
    case GL_ALPHA: EACH(1, dst[0] = dst[1] = dst[2] = 255; dst[3] = src[0]);
    default: return -1;
    }
#undef EACH
    return 0;
}

/* GL 1.1 internal formats (§3.8.1): the base format decides which
 * components a texture keeps; the sized variants are hints DOS-GL maps to
 * its three 16-bit formats by alpha (dgl_hwfmt_for_class), except that
 * RGB5_A1 never takes ARGB4444. */
int dgl_ifmt_class(GLint f)
{
    switch (f) {
    case 4: case GL_RGBA: case GL_RGBA2: case GL_RGBA4: case GL_RGBA8: case GL_RGB10_A2: case GL_RGBA12:
    case GL_RGBA16:
        return DGL_IF_RGBA;
    case 3: case GL_RGB: case GL_R3_G3_B2: case GL_RGB4: case GL_RGB5: case GL_RGB8: case GL_RGB10: case GL_RGB12:
    case GL_RGB16: case GL_RGB2_EXT:
        return DGL_IF_RGB;
    case GL_RGB5_A1:
        return DGL_IF_RGB5_A1;
    case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8: case GL_LUMINANCE12: case GL_LUMINANCE16:
        return DGL_IF_LUMINANCE;
    case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2:
    case GL_LUMINANCE8_ALPHA8: case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12: case GL_LUMINANCE16_ALPHA16:
        return DGL_IF_LUMINANCE_ALPHA;
    case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: case GL_ALPHA12: case GL_ALPHA16:
        return DGL_IF_ALPHA;
    case GL_INTENSITY: case GL_INTENSITY4: case GL_INTENSITY8: case GL_INTENSITY12: case GL_INTENSITY16:
        return DGL_IF_INTENSITY;
    case GL_COLOR_INDEX1_EXT: case GL_COLOR_INDEX2_EXT: case GL_COLOR_INDEX4_EXT: case GL_COLOR_INDEX8_EXT:
    case GL_COLOR_INDEX12_EXT: case GL_COLOR_INDEX16_EXT:
        return DGL_IF_INDEX;                      /* 8-bit indices, masked by the palette's width */
    default:
        return -1;
    }
}

/* The texel as the texture environment will see it: luminance and
 * intensity come from red (GL's conversion from RGBA), missing colour is
 * white and missing alpha is 1. */
void dgl_apply_ifmt(int ifc, unsigned char *p, long n)
{
    long i;
#define EACH(body) for (i = 0; i < n; i++, p += 4) { body; } break
    switch (ifc) {                              /* (once a row, not once a texel) */
    case DGL_IF_RGB: EACH(p[3] = 255);
    case DGL_IF_LUMINANCE: EACH(p[1] = p[2] = p[0]; p[3] = 255);
    case DGL_IF_LUMINANCE_ALPHA: EACH(p[1] = p[2] = p[0]);
    case DGL_IF_ALPHA: EACH(p[0] = p[1] = p[2] = 255);
    case DGL_IF_INTENSITY: EACH(p[1] = p[2] = p[3] = p[0]);
    default: break;
    }
#undef EACH
}

int dgl_format_bytes(GLenum format)
{
    switch (format) {
    case GL_RGBA: case GL_BGRA_EXT: return 4;
    case GL_RGB: case GL_BGR_EXT: return 3;
    case GL_LUMINANCE_ALPHA: return 2;
    case GL_LUMINANCE: case GL_ALPHA: case GL_COLOR_INDEX: return 1;
    default: return 0;
    }
}
