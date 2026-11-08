/* raster.c - state-setting entry points (PRD §6.1/6.2). Each records state
 * and marks the dirty group it affects; nothing touches the hardware until
 * a draw or clear validates it (FR-ST-1). */
#include "gl_state.h"

static int *cap_flag(GLenum cap)
{
    switch (cap) {
    case GL_DEPTH_TEST:   return &dgl_gl.depth_test;
    case GL_CULL_FACE:    return &dgl_gl.cull_face;
    case GL_BLEND:        return &dgl_gl.blend;
    case GL_ALPHA_TEST:   return &dgl_gl.alpha_test;
    case GL_FOG:          return &dgl_gl.fog;
    case GL_SCISSOR_TEST: return &dgl_gl.scissor_test;
    case GL_TEXTURE_2D:   return dgl_gl.active_unit ? &dgl_gl.texture_2d1 : &dgl_gl.texture_2d;
    case GL_DITHER:       return &dgl_gl.dither;
    case GL_POLYGON_OFFSET_FILL: return &dgl_gl.offset_fill;
    case GL_STENCIL_TEST: return &dgl_gl.stencil_test;
    case GL_SHARED_TEXTURE_PALETTE_EXT: return &dgl_gl.shared_palette;
    default:              return 0;
    }
}

static void set_cap(GLenum cap, int on)
{
    int *f = cap_flag(cap);
    if (!f) {
        switch (cap) {                                       /* accepted and ignored (PRD §2.2) */
        case GL_LIGHTING: case GL_NORMALIZE: case GL_POINT_SMOOTH: case GL_LINE_SMOOTH: case GL_POLYGON_SMOOTH:
        case GL_LINE_STIPPLE: case GL_POLYGON_STIPPLE: case GL_POLYGON_OFFSET_POINT: case GL_POLYGON_OFFSET_LINE:
        case GL_COLOR_MATERIAL: case GL_TEXTURE_1D: case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T:
        case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2: case GL_CLIP_PLANE3:
        case GL_CLIP_PLANE4: case GL_CLIP_PLANE5:
            break;
        default:
            dgl_gl_error(GL_INVALID_ENUM);
        }
        return;
    }
    if (*f != on) {
        *f = on;
        dgl_gl.dirty |= DGL_DIRTY_RASTER | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_FOG;
    }
}

void APIENTRY glEnable(GLenum cap) { set_cap(cap, 1); }
void APIENTRY glDisable(GLenum cap) { set_cap(cap, 0); }

GLboolean APIENTRY glIsEnabled(GLenum cap)
{
    int *f = cap_flag(cap);
    if (f)
        return (GLboolean)(*f != 0);
    switch (cap) {
    case GL_VERTEX_ARRAY:        return (GLboolean)dgl_gl.va.enabled;
    case GL_COLOR_ARRAY:         return (GLboolean)dgl_gl.ca.enabled;
    case GL_TEXTURE_COORD_ARRAY: return (GLboolean)(dgl_gl.client_unit ? dgl_gl.ta1 : dgl_gl.ta).enabled;
    case GL_LIGHTING: case GL_NORMALIZE: case GL_POINT_SMOOTH: case GL_LINE_SMOOTH: case GL_POLYGON_SMOOTH:
    case GL_LINE_STIPPLE: case GL_POLYGON_STIPPLE: case GL_POLYGON_OFFSET_POINT: case GL_POLYGON_OFFSET_LINE:
    case GL_COLOR_MATERIAL: case GL_TEXTURE_1D: case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T:
    case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2: case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4: case GL_CLIP_PLANE5:
        return GL_FALSE;
    default: dgl_gl_error(GL_INVALID_ENUM); return GL_FALSE;
    }
}

static int is_func(GLenum f) { return f >= GL_NEVER && f <= GL_ALWAYS; }

void APIENTRY glDepthFunc(GLenum func)
{
    if (!is_func(func)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.depth_func = func;
    dgl_gl.dirty |= DGL_DIRTY_RASTER;
}

void APIENTRY glDepthMask(GLboolean flag)
{
    dgl_gl.depth_mask = flag != 0;
    dgl_gl.dirty |= DGL_DIRTY_RASTER;
}

void APIENTRY glAlphaFunc(GLenum func, GLfloat ref)
{
    if (!is_func(func)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.alpha_func = func;
    dgl_gl.alpha_ref = ref < 0 ? 0 : ref > 1 ? 1 : ref;
    dgl_gl.dirty |= DGL_DIRTY_RASTER;
}

void APIENTRY glBlendFunc(GLenum s, GLenum d)
{
    /* GL 1.1: SRC_COLOR factors are destination-only, DST_COLOR and
     * SRC_ALPHA_SATURATE source-only. */
    int s_ok = s == GL_ZERO || s == GL_ONE || s == GL_DST_COLOR || s == GL_ONE_MINUS_DST_COLOR ||
               (s >= GL_SRC_ALPHA && s <= GL_ONE_MINUS_DST_ALPHA) || s == GL_SRC_ALPHA_SATURATE;
    int d_ok = d == GL_ZERO || d == GL_ONE || d == GL_SRC_COLOR || d == GL_ONE_MINUS_SRC_COLOR ||
               (d >= GL_SRC_ALPHA && d <= GL_ONE_MINUS_DST_ALPHA);
    if (!s_ok || !d_ok) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.blend_src = s;
    dgl_gl.blend_dst = d;
    dgl_gl.dirty |= DGL_DIRTY_RASTER;
}

void APIENTRY glCullFace(GLenum mode)
{
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.cull_mode = mode;
}

void APIENTRY glFrontFace(GLenum mode)
{
    if (mode != GL_CW && mode != GL_CCW) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.front_face = mode;
}

void APIENTRY glShadeModel(GLenum mode)
{
    if (mode != GL_FLAT && mode != GL_SMOOTH) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.shade_model = mode;
}

void APIENTRY glPolygonMode(GLenum face, GLenum mode)
{
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
        (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL)) {
        dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    dgl_gl.polygon_mode = mode;          /* fill only (PRD §6.2); others recorded */
}

void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    dgl_gl.color_mask[0] = r; dgl_gl.color_mask[1] = g; dgl_gl.color_mask[2] = b; dgl_gl.color_mask[3] = a;
    dgl_gl.dirty |= DGL_DIRTY_RASTER;
}

void APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (w < 0 || h < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    dgl_gl.viewport[0] = x; dgl_gl.viewport[1] = y; dgl_gl.viewport[2] = w; dgl_gl.viewport[3] = h;
}

void APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (w < 0 || h < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    dgl_gl.scissor[0] = x; dgl_gl.scissor[1] = y; dgl_gl.scissor[2] = w; dgl_gl.scissor[3] = h;
    dgl_gl.dirty |= DGL_DIRTY_TARGET;
}

static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    dgl_gl.clear_color[0] = clampf(r); dgl_gl.clear_color[1] = clampf(g);
    dgl_gl.clear_color[2] = clampf(b); dgl_gl.clear_color[3] = clampf(a);
}

void APIENTRY glClearDepth(GLclampd d) { dgl_gl.clear_depth = d < 0 ? 0 : d > 1 ? 1 : d; }

void APIENTRY glDepthRange(GLclampd n, GLclampd f)
{
    dgl_gl.depth_near = n < 0 ? 0 : n > 1 ? 1 : n;
    dgl_gl.depth_far = f < 0 ? 0 : f > 1 ? 1 : f;
}

void APIENTRY glHint(GLenum target, GLenum mode)
{
    if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST) { dgl_gl_error(GL_INVALID_ENUM); return; }
    switch (target) {
    case GL_PERSPECTIVE_CORRECTION_HINT: dgl_gl.hint_perspective = mode; break;
    case GL_FOG_HINT: dgl_gl.hint_fog = mode; break;
    case GL_POINT_SMOOTH_HINT: case GL_LINE_SMOOTH_HINT: case GL_POLYGON_SMOOTH_HINT: break;
    default: dgl_gl_error(GL_INVALID_ENUM);
    }
}

void APIENTRY glPixelStorei(GLenum pname, GLint param)
{
    if (param != 1 && param != 2 && param != 4 && param != 8) { dgl_gl_error(GL_INVALID_VALUE); return; }
    if (pname == GL_PACK_ALIGNMENT) dgl_gl.pack_align = param;
    else if (pname == GL_UNPACK_ALIGNMENT) dgl_gl.unpack_align = param;
    else dgl_gl_error(GL_INVALID_ENUM);
}

/* ---- Fog (the factor is computed per vertex, PRD §7) ---------------------- */
static void fog_param(GLenum pname, const GLfloat *v)
{
    switch (pname) {
    case GL_FOG_MODE: {
        GLenum m = (GLenum)v[0];
        if (m != GL_LINEAR && m != GL_EXP && m != GL_EXP2) { dgl_gl_error(GL_INVALID_ENUM); return; }
        dgl_gl.fog_mode = m;
        break;
    }
    case GL_FOG_DENSITY:
        if (v[0] < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
        dgl_gl.fog_density = v[0];
        break;
    case GL_FOG_START: dgl_gl.fog_start = v[0]; break;
    case GL_FOG_END:   dgl_gl.fog_end = v[0]; break;
    case GL_FOG_COLOR:
        dgl_gl.fog_color[0] = clampf(v[0]); dgl_gl.fog_color[1] = clampf(v[1]);
        dgl_gl.fog_color[2] = clampf(v[2]); dgl_gl.fog_color[3] = clampf(v[3]);
        break;
    case GL_FOG_INDEX: break;
    default: dgl_gl_error(GL_INVALID_ENUM); return;
    }
    dgl_gl.dirty |= DGL_DIRTY_FOG;
}

void APIENTRY glFogf(GLenum pname, GLfloat param)
{
    if (pname == GL_FOG_COLOR) { dgl_gl_error(GL_INVALID_ENUM); return; }
    fog_param(pname, &param);
}

void APIENTRY glFogi(GLenum pname, GLint param)
{
    GLfloat f = (GLfloat)param;
    if (pname == GL_FOG_COLOR) { dgl_gl_error(GL_INVALID_ENUM); return; }
    fog_param(pname, &f);
}

void APIENTRY glFogfv(GLenum pname, const GLfloat *params) { fog_param(pname, params); }

void APIENTRY glFogiv(GLenum pname, const GLint *params)
{
    GLfloat f[4];
    int i;
    if (pname == GL_FOG_COLOR) {
        for (i = 0; i < 4; i++)          /* integer colours map [0, INT_MAX] to [0, 1] */
            f[i] = (GLfloat)((double)params[i] / 2147483647.0);
    } else
        f[0] = (GLfloat)params[0];
    fog_param(pname, f);
}

/* ---- Points, lines, polygon offset (GL 1.1) --------------------------------- */
void APIENTRY glPointSize(GLfloat size)
{
    if (size <= 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    dgl_gl.point_size = size;
}

void APIENTRY glLineWidth(GLfloat width)
{
    if (width <= 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    dgl_gl.line_width = width;
}

/* Applied to filled polygons at setup (emit.c): factor x the triangle's
 * steepest depth slope plus units, in 16-bit depth-buffer steps. */
void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units)
{
    dgl_gl.offset_factor = factor;
    dgl_gl.offset_units = units;
}

/* ---- Stencil: accepted with no stencil buffer (0 bits) --------------------
 * GL: without a stencil buffer the test always passes and nothing is
 * written, so the state is only recorded for queries. */
static int is_stencil_op(GLenum op)
{
    return op == GL_KEEP || op == GL_ZERO || op == GL_REPLACE || op == GL_INCR || op == GL_DECR || op == GL_INVERT;
}

void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    if (!is_func(func)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.stencil_func = func;
    dgl_gl.stencil_ref = ref;
    dgl_gl.stencil_mask = mask;
}

void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    if (!is_stencil_op(fail) || !is_stencil_op(zfail) || !is_stencil_op(zpass)) { dgl_gl_error(GL_INVALID_ENUM); return; }
    dgl_gl.stencil_fail = fail;
    dgl_gl.stencil_zfail = zfail;
    dgl_gl.stencil_zpass = zpass;
}

void APIENTRY glStencilMask(GLuint mask) { dgl_gl.stencil_writemask = mask; }
void APIENTRY glClearStencil(GLint s) { dgl_gl.clear_stencil = s; }

/* ---- Accepted, no effect (PRD §2.2, §6.2) ------------------------------ */
void APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) { (void)nx; (void)ny; (void)nz; }
