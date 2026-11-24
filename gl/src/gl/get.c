/* get.c - state queries and strings (PRD §6.1: glGetIntegerv, glGetString;
 * §6.2: glGetFloatv, glGetBooleanv). */
#include "gl_state.h"
#include "../dgl/dgl.h"
#include "../backend/backend.h"
#include <stdio.h>
#include <string.h>

/* Every query as doubles; the typed entry points convert. Returns the
 * number of values, 0 for an unknown name. */
static int query(GLenum p, double *v)
{
    const dgl_mat4 *m = NULL;
    int i;
    switch (p) {
    case GL_MATRIX_MODE: v[0] = dgl_gl.matrix_mode; return 1;
    case GL_VIEWPORT: for (i = 0; i < 4; i++) v[i] = dgl_gl.viewport[i]; return 4;
    case GL_SCISSOR_BOX: for (i = 0; i < 4; i++) v[i] = dgl_gl.scissor[i]; return 4;
    case GL_MAX_TEXTURE_SIZE: { dgl_caps c; dgl_get_caps(&c); v[0] = c.max_texture_size; return 1; }
    case GL_MAX_MODELVIEW_STACK_DEPTH: v[0] = DGL_MV_DEPTH; return 1;
    case GL_MAX_PROJECTION_STACK_DEPTH: v[0] = DGL_PROJ_DEPTH; return 1;
    case GL_MAX_TEXTURE_STACK_DEPTH: v[0] = DGL_TEX_DEPTH; return 1;
    case GL_MAX_VIEWPORT_DIMS: v[0] = v[1] = 4096; return 2;
    case GL_RED_BITS: v[0] = dgl_ctx.color_bits == 32 ? 8 : dgl_ctx.color_bits == 8 ? 3 : 5; return 1;
    case GL_GREEN_BITS: v[0] = dgl_ctx.color_bits == 32 ? 8 : dgl_ctx.color_bits == 8 ? 3 : 6; return 1;
    case GL_BLUE_BITS: v[0] = dgl_ctx.color_bits == 32 ? 8 : dgl_ctx.color_bits == 8 ? 2 : 5; return 1;
    case GL_ALPHA_BITS: v[0] = 0; return 1;
    case GL_DEPTH_BITS: v[0] = dgl_ctx.depth_bits ? 16 : 0; return 1;
    case GL_DEPTH_FUNC: v[0] = dgl_gl.depth_func; return 1;
    case GL_DEPTH_WRITEMASK: v[0] = dgl_gl.depth_mask; return 1;
    case GL_DEPTH_CLEAR_VALUE: v[0] = dgl_gl.clear_depth; return 1;
    case GL_DEPTH_RANGE: v[0] = dgl_gl.depth_near; v[1] = dgl_gl.depth_far; return 2;
    case GL_COLOR_CLEAR_VALUE: for (i = 0; i < 4; i++) v[i] = dgl_gl.clear_color[i]; return 4;
    case GL_COLOR_WRITEMASK: for (i = 0; i < 4; i++) v[i] = dgl_gl.color_mask[i]; return 4;
    case GL_BLEND_SRC: v[0] = dgl_gl.blend_src; return 1;
    case GL_BLEND_DST: v[0] = dgl_gl.blend_dst; return 1;
    case GL_ALPHA_TEST_FUNC: v[0] = dgl_gl.alpha_func; return 1;
    case GL_ALPHA_TEST_REF: v[0] = dgl_gl.alpha_ref; return 1;
    case GL_CULL_FACE_MODE: v[0] = dgl_gl.cull_mode; return 1;
    case GL_FRONT_FACE: v[0] = dgl_gl.front_face; return 1;
    case GL_SHADE_MODEL: v[0] = dgl_gl.shade_model; return 1;
    case GL_FOG_MODE: v[0] = dgl_gl.fog_mode; return 1;
    case GL_FOG_DENSITY: v[0] = dgl_gl.fog_density; return 1;
    case GL_FOG_START: v[0] = dgl_gl.fog_start; return 1;
    case GL_FOG_END: v[0] = dgl_gl.fog_end; return 1;
    case GL_FOG_COLOR: for (i = 0; i < 4; i++) v[i] = dgl_gl.fog_color[i]; return 4;
    case GL_PERSPECTIVE_CORRECTION_HINT: v[0] = dgl_gl.hint_perspective; return 1;
    case GL_FOG_HINT: v[0] = dgl_gl.hint_fog; return 1;
    case GL_PACK_ALIGNMENT: v[0] = dgl_gl.pack_align; return 1;
    case GL_UNPACK_ALIGNMENT: v[0] = dgl_gl.unpack_align; return 1;
    case GL_MODELVIEW_MATRIX: m = &dgl_gl.mv.stack[dgl_gl.mv.depth - 1]; break;
    case GL_PROJECTION_MATRIX: m = &dgl_gl.proj.stack[dgl_gl.proj.depth - 1]; break;
    case GL_TEXTURE_MATRIX:
        m = dgl_gl.active_unit ? &dgl_gl.tex1.stack[dgl_gl.tex1.depth - 1] : &dgl_gl.tex.stack[dgl_gl.tex.depth - 1];
        break;
    case GL_CULL_FACE: v[0] = dgl_gl.cull_face; return 1;
    case GL_DEPTH_TEST: v[0] = dgl_gl.depth_test; return 1;
    case GL_BLEND: v[0] = dgl_gl.blend; return 1;
    case GL_ALPHA_TEST: v[0] = dgl_gl.alpha_test; return 1;
    case GL_FOG: v[0] = dgl_gl.fog; return 1;
    case GL_SCISSOR_TEST: v[0] = dgl_gl.scissor_test; return 1;
    case GL_TEXTURE_2D: v[0] = dgl_gl.active_unit ? dgl_gl.texture_2d1 : dgl_gl.texture_2d; return 1;
    case GL_ACTIVE_TEXTURE_ARB: v[0] = GL_TEXTURE0_ARB + dgl_gl.active_unit; return 1;
    case GL_CLIENT_ACTIVE_TEXTURE_ARB: v[0] = GL_TEXTURE0_ARB + dgl_gl.client_unit; return 1;
    case GL_TEXTURE_BINDING_2D: { extern GLuint dgl_bound_name(void); v[0] = dgl_bound_name(); return 1; }
    case GL_TEXTURE_BINDING_CUBE_MAP_ARB: { extern GLuint dgl_bound_cube_name(void); v[0] = dgl_bound_cube_name(); return 1; }
    case GL_TEXTURE_CUBE_MAP_ARB: v[0] = dgl_gl.texture_cube[dgl_gl.active_unit]; return 1;
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB: { dgl_caps c; dgl_get_caps(&c); v[0] = c.cube ? c.max_texture_size : 0; return 1; }
    case GL_DITHER: v[0] = dgl_gl.dither; return 1;
    case GL_POLYGON_OFFSET_FILL: v[0] = dgl_gl.offset_fill; return 1;
    case GL_STENCIL_TEST: v[0] = dgl_gl.stencil_test; return 1;
    case GL_SHARED_TEXTURE_PALETTE_EXT: v[0] = dgl_gl.shared_palette; return 1;
    case GL_VERTEX_ARRAY: v[0] = dgl_gl.va.enabled; return 1;
    case GL_COLOR_ARRAY: v[0] = dgl_gl.ca.enabled; return 1;
    case GL_NORMAL_ARRAY: v[0] = dgl_gl.na.enabled; return 1;
    case GL_NORMAL_ARRAY_TYPE: v[0] = dgl_gl.na.type ? dgl_gl.na.type : GL_FLOAT; return 1;
    case GL_NORMAL_ARRAY_STRIDE: v[0] = dgl_gl.na.stride; return 1;
    case GL_CURRENT_NORMAL: for (i = 0; i < 3; i++) v[i] = dgl_gl.cur_normal[i]; return 3;
    /* Lighting (light.c) */
    case GL_LIGHTING: v[0] = dgl_gl.lighting; return 1;
    case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3: case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6:
    case GL_LIGHT7: v[0] = dgl_gl.light[p - GL_LIGHT0].enabled; return 1;
    case GL_COLOR_MATERIAL: v[0] = dgl_gl.color_material; return 1;
    case GL_COLOR_MATERIAL_FACE: v[0] = dgl_gl.color_material_face; return 1;
    case GL_COLOR_MATERIAL_PARAMETER: v[0] = dgl_gl.color_material_mode; return 1;
    case GL_NORMALIZE: v[0] = dgl_gl.normalize; return 1;
    case GL_RESCALE_NORMAL_EXT: v[0] = dgl_gl.rescale_normal; return 1;
    case GL_LIGHT_MODEL_AMBIENT: for (i = 0; i < 4; i++) v[i] = dgl_gl.light_model_ambient[i]; return 4;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = dgl_gl.light_local_viewer; return 1;
    case GL_LIGHT_MODEL_TWO_SIDE: v[0] = dgl_gl.light_two_side; return 1;
    case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2: case GL_CLIP_PLANE3: case GL_CLIP_PLANE4:
    case GL_CLIP_PLANE5: v[0] = (dgl_gl.clip_enabled >> (p - GL_CLIP_PLANE0)) & 1; return 1;
    case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
        v[0] = (dgl_gl.texgen_on[dgl_gl.active_unit] >> (p == GL_TEXTURE_GEN_S ? 0 : p == GL_TEXTURE_GEN_T ? 1
                                                          : p == GL_TEXTURE_GEN_R ? 2 : 3)) & 1;
        return 1;
    case GL_TEXTURE_COORD_ARRAY: v[0] = (dgl_gl.client_unit ? dgl_gl.ta1 : dgl_gl.ta).enabled; return 1;
    case GL_POLYGON_MODE: v[0] = v[1] = dgl_gl.polygon_mode; return 2;
    /* The framebuffer DOS-GL has (16-bit RGB, no stencil, accumulation or aux buffers). */
    case GL_RGBA_MODE: case GL_SUBPIXEL_BITS: v[0] = p == GL_RGBA_MODE ? 1 : 4; return 1;
    case GL_DOUBLEBUFFER: v[0] = dgl_ctx.double_buffer; return 1;
    case GL_STENCIL_BITS: v[0] = dgl_ctx.active ? dgl_ctx.stencil_bits : 0; return 1;
    case GL_INDEX_BITS: case GL_AUX_BUFFERS: case GL_STEREO: case GL_INDEX_MODE:
    case GL_ACCUM_RED_BITS: case GL_ACCUM_GREEN_BITS: case GL_ACCUM_BLUE_BITS: case GL_ACCUM_ALPHA_BITS:
        v[0] = 0; return 1;
    case GL_DRAW_BUFFER: v[0] = dgl_gl.draw_buffer; return 1;
    case GL_READ_BUFFER: v[0] = dgl_gl.read_buffer; return 1;
    case GL_POINT_SIZE: v[0] = dgl_gl.point_size; return 1;
    case GL_LINE_WIDTH: v[0] = dgl_gl.line_width; return 1;
    case GL_POINT_SIZE_RANGE: case GL_LINE_WIDTH_RANGE: v[0] = 1; v[1] = 64; return 2;
    case GL_POINT_SIZE_GRANULARITY: case GL_LINE_WIDTH_GRANULARITY: v[0] = 1.0 / 16; return 1;
    case GL_POLYGON_OFFSET_FACTOR: v[0] = dgl_gl.offset_factor; return 1;
    case GL_POLYGON_OFFSET_UNITS: v[0] = dgl_gl.offset_units; return 1;
    case GL_STENCIL_FUNC: v[0] = dgl_gl.stencil_func; return 1;
    case GL_STENCIL_REF: v[0] = dgl_gl.stencil_ref; return 1;
    case GL_STENCIL_VALUE_MASK: v[0] = dgl_gl.stencil_mask; return 1;
    case GL_STENCIL_WRITEMASK: v[0] = dgl_gl.stencil_writemask; return 1;
    case GL_STENCIL_FAIL: v[0] = dgl_gl.stencil_fail; return 1;
    case GL_STENCIL_PASS_DEPTH_FAIL: v[0] = dgl_gl.stencil_zfail; return 1;
    case GL_STENCIL_PASS_DEPTH_PASS: v[0] = dgl_gl.stencil_zpass; return 1;
    case GL_STENCIL_CLEAR_VALUE: v[0] = dgl_gl.clear_stencil; return 1;
    case GL_UNPACK_ROW_LENGTH: v[0] = dgl_gl.unpack_row_length; return 1;
    case GL_PACK_ROW_LENGTH: case GL_PACK_SKIP_ROWS: case GL_PACK_SKIP_PIXELS:
    case GL_UNPACK_SKIP_ROWS: case GL_UNPACK_SKIP_PIXELS: case GL_UNPACK_SWAP_BYTES: case GL_UNPACK_LSB_FIRST:
    case GL_PACK_SWAP_BYTES: case GL_PACK_LSB_FIRST:
        v[0] = 0; return 1;
    case GL_CURRENT_COLOR: for (i = 0; i < 4; i++) v[i] = dgl_gl.cur_color[i]; return 4;
    case GL_CURRENT_TEXTURE_COORDS: {
        const GLfloat *c = dgl_gl.active_unit ? dgl_gl.cur_tex1 : dgl_gl.cur_tex;
        v[0] = c[0]; v[1] = c[1]; v[2] = 0; v[3] = 1;
        return 4;
    }
    case GL_MODELVIEW_STACK_DEPTH: v[0] = dgl_gl.mv.depth; return 1;
    case GL_PROJECTION_STACK_DEPTH: v[0] = dgl_gl.proj.depth; return 1;
    case GL_TEXTURE_STACK_DEPTH: v[0] = dgl_gl.active_unit ? dgl_gl.tex1.depth : dgl_gl.tex.depth; return 1;
    case GL_LIST_BASE: { extern GLuint dgl_list_base(void); v[0] = dgl_list_base(); return 1; }
    case GL_MAX_LIST_NESTING: v[0] = 64; return 1;
    case GL_MAX_LIGHTS: v[0] = 8; return 1;
    case GL_MAX_CLIP_PLANES: v[0] = 6; return 1;
    case GL_MAX_ATTRIB_STACK_DEPTH: case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH: v[0] = 16; return 1;
    case GL_MAX_TEXTURE_UNITS_ARB: { extern int dgl_texture_units(void); v[0] = dgl_texture_units(); return 1; }
    case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: { dgl_caps c; dgl_get_caps(&c); v[0] = c.max_anisotropy; return 1; }
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS_ARB: v[0] = 4; return 1;
    case GL_COMPRESSED_TEXTURE_FORMATS_ARB:
        v[0] = GL_COMPRESSED_RGB_S3TC_DXT1_EXT, v[1] = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        v[2] = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, v[3] = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        return 4;
    case GL_TEXTURE_COMPRESSION_HINT_ARB: v[0] = GL_DONT_CARE; return 1;
    case GL_MAX_TEXTURE_LOD_BIAS_EXT: { dgl_caps c; dgl_get_caps(&c); v[0] = c.max_lod_bias; return 1; }
    default: return dgl_buffer_query(p, v);     /* buffer bindings, element limits (buffer.c) */
    }
    for (i = 0; i < 16; i++)
        v[i] = m->m[i];
    return 16;
}

void APIENTRY glGetIntegerv(GLenum p, GLint *out)
{
    double v[16];
    int n = query(p, v), i;
    if (!n) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++)
        out[i] = (p == GL_COLOR_CLEAR_VALUE || p == GL_DEPTH_CLEAR_VALUE || p == GL_FOG_COLOR ||
                  p == GL_DEPTH_RANGE || p == GL_ALPHA_TEST_REF)
                 ? (GLint)(v[i] * 2147483647.0) : (GLint)(v[i] < 0 ? v[i] - 0.5 : v[i] + 0.5);
}

void APIENTRY glGetFloatv(GLenum p, GLfloat *out)
{
    double v[16];
    int n = query(p, v), i;
    if (!n) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++)
        out[i] = (GLfloat)v[i];
}

void APIENTRY glGetDoublev(GLenum p, GLdouble *out)
{
    double v[16];
    int n = query(p, v), i;
    if (!n) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++)
        out[i] = v[i];
}

void APIENTRY glGetPointerv(GLenum p, GLvoid **out)
{
    switch (p) {
    case GL_VERTEX_ARRAY_POINTER: *out = (GLvoid *)dgl_gl.va.ptr; break;
    case GL_COLOR_ARRAY_POINTER: *out = (GLvoid *)dgl_gl.ca.ptr; break;
    case GL_NORMAL_ARRAY_POINTER: *out = (GLvoid *)dgl_gl.na.ptr; break;
    case GL_TEXTURE_COORD_ARRAY_POINTER: *out = (GLvoid *)(dgl_gl.client_unit ? dgl_gl.ta1 : dgl_gl.ta).ptr; break;
    default: *out = NULL; dgl_gl_error(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetBooleanv(GLenum p, GLboolean *out)
{
    double v[16];
    int n = query(p, v), i;
    if (!n) { dgl_gl_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++)
        out[i] = v[i] != 0.0;
}

/* Every name ends in a space, as programs that search for "NAME " expect. Buffer objects and draw_range_elements
 * everywhere (buffer.c); multitexture and texture_env_combine where there are two texture units (the combiners);
 * the rest as the card's caps say (dgl_get_caps). Built again when the back end or device changes. */
static const char *extensions(void)
{
    static char buf[1024];
    static const void *be, *dev;
    if (be != (const void *)dgl_be || dev != (const void *)dgl_dev || !buf[0]) {
        dgl_caps c;
        dgl_get_caps(&c);
        strcpy(buf, "GL_EXT_bgra "
                    "GL_EXT_texture_edge_clamp GL_SGIS_texture_edge_clamp "
                    "GL_EXT_paletted_texture GL_EXT_shared_texture_palette "
                    "GL_ARB_vertex_buffer_object GL_EXT_draw_range_elements "
                    "GL_ARB_texture_compression GL_EXT_texture_compression_s3tc GL_EXT_rescale_normal ");
        if (c.texture_units >= 2)
            strcat(buf, "GL_ARB_multitexture GL_SGIS_multitexture "
                        "GL_ARB_texture_env_combine GL_EXT_texture_env_combine ");
        if (c.texture_units >= 2 && c.dot3)
            strcat(buf, "GL_ARB_texture_env_dot3 ");
        if (c.cube)
            strcat(buf, "GL_ARB_texture_cube_map GL_EXT_texture_cube_map ");
        if (c.max_anisotropy > 1.0f)
            strcat(buf, "GL_EXT_texture_filter_anisotropic ");
        if (c.max_lod_bias > 0.0f)
            strcat(buf, "GL_EXT_texture_lod_bias ");
        be = dgl_be, dev = dgl_dev;
    }
    return buf;
}

const GLubyte *APIENTRY glGetString(GLenum name)
{
    static char renderer[64];
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)"DOS-GL";
    case GL_RENDERER:
        dgl_caps c;
        dgl_get_caps(&c);
        snprintf(renderer, sizeof renderer, "DOS-GL on %s", c.renderer);
        return (const GLubyte *)renderer;
    case GL_VERSION: return (const GLubyte *)"1.1 DOS-GL 0.3";
    case GL_EXTENSIONS: return (const GLubyte *)extensions();
    default: dgl_gl_error(GL_INVALID_ENUM); return (const GLubyte *)"";
    }
}
