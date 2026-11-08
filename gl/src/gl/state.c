/* state.c - the GL state object, defaults and the error flag (FR-ST-1, FR-ST-3). */
#include "gl_state.h"
#include <string.h>

dgl_gl_state dgl_gl;

void dgl_gl_set_window(int w, int h)
{
    dgl_gl.viewport[0] = dgl_gl.viewport[1] = 0;
    dgl_gl.viewport[2] = w; dgl_gl.viewport[3] = h;
    dgl_gl.scissor[0] = dgl_gl.scissor[1] = 0;
    dgl_gl.scissor[2] = w; dgl_gl.scissor[3] = h;
    dgl_gl.dirty |= DGL_DIRTY_TARGET;
}

void dgl_gl_reset(void)
{
    memset(&dgl_gl, 0, sizeof dgl_gl);
    dgl_matrix_reset();
    dgl_gl.error = GL_NO_ERROR;
    dgl_gl.depth_func = GL_LESS;
    dgl_gl.depth_mask = 1;
    dgl_gl.blend_src = GL_ONE;
    dgl_gl.blend_dst = GL_ZERO;
    dgl_gl.alpha_func = GL_ALWAYS;
    dgl_gl.cull_mode = GL_BACK;
    dgl_gl.front_face = GL_CCW;
    dgl_gl.shade_model = GL_SMOOTH;
    dgl_gl.polygon_mode = GL_FILL;
    dgl_gl.dither = 1;
    dgl_gl.color_mask[0] = dgl_gl.color_mask[1] = dgl_gl.color_mask[2] = dgl_gl.color_mask[3] = GL_TRUE;
    dgl_gl.clear_depth = 1.0;
    dgl_gl.depth_near = 0.0;
    dgl_gl.depth_far = 1.0;
    dgl_gl.fog_mode = GL_EXP;
    dgl_gl.fog_density = 1.0f;
    dgl_gl.fog_start = 0.0f;
    dgl_gl.fog_end = 1.0f;
    dgl_gl.hint_perspective = dgl_gl.hint_fog = GL_DONT_CARE;
    dgl_gl.pack_align = dgl_gl.unpack_align = 4;
    dgl_gl.point_size = dgl_gl.line_width = 1.0f;
    dgl_gl.stencil_func = GL_ALWAYS;
    dgl_gl.stencil_fail = dgl_gl.stencil_zfail = dgl_gl.stencil_zpass = GL_KEEP;
    dgl_gl.stencil_mask = dgl_gl.stencil_writemask = ~0u;
    dgl_gl.draw_buffer = dgl_gl.read_buffer = GL_BACK;
    dgl_gl.cur_color[0] = dgl_gl.cur_color[1] = dgl_gl.cur_color[2] = dgl_gl.cur_color[3] = 1.0f;
    dgl_gl.va.size = 4; dgl_gl.va.type = GL_FLOAT;
    dgl_gl.ca.size = 4; dgl_gl.ca.type = GL_FLOAT;
    dgl_gl.ta.size = 4; dgl_gl.ta.type = GL_FLOAT;
    dgl_gl.dirty = ~0u;
}

void (*dgl_gl_error_hook)(GLenum e, void *caller);
dgl_prim_counts dgl_prims;

void dgl_gl_error(GLenum e)
{
    if (dgl_gl_error_hook)
        dgl_gl_error_hook(e, __builtin_return_address(0));
    if (dgl_gl.error == GL_NO_ERROR)       /* GL keeps the first error until read */
        dgl_gl.error = e;
}

GLenum APIENTRY glGetError(void)
{
    GLenum e = dgl_gl.error;
    dgl_gl.error = GL_NO_ERROR;
    return e;
}
