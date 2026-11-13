/* texture.c - texture objects (PRD §6.1, §8.3, M4): GL's side.
 *
 * Every level keeps an RGBA8 shadow copy, so the drawing back end can build
 * its own copy from it in any format, and again after eviction (the Matrox
 * back end's residency is src/backend/mga/tex.c; docs/b1.md). This file
 * keeps the names, bindings, parameters, environments and the levels, and
 * tells the back end when a texture's levels change (dirty, tex_sub) or it
 * goes (tex_free). */
#include "gl_tex.h"
#include "../dgl/dgl.h"
#include "../backend/backend.h"
#include "mga/sys.h"                   /* sys_time_us */
#include <stdlib.h>
#include <string.h>

#define MAX_TEXTURES 4096

static dgl_texture *tex[MAX_TEXTURES];
/* Names from MAX_TEXTURES up: GL 1.1 lets a program bind any name without
 * glGenTextures, and some pick large ones (Xash3D binds its sky sides to
 * 5800-5805). Few enough for a list. */
static dgl_texture **far_tex;
static int nfar, capfar;
static GLuint bound_u[2];                /* names bound to GL_TEXTURE_2D, per texture unit */
#define bound (bound_u[dgl_gl.active_unit])   /* the active unit's */
static dgl_texenv env_u[2];             /* per texture unit (env_defaults) */
#define cur_env (env_u[dgl_gl.active_unit])

dgl_tex_counts dgl_texc;

const dgl_texenv *dgl_tex_env(int unit) { return &env_u[unit]; }
GLenum dgl_tex_env_mode(int unit) { return env_u[unit].mode; }
const GLfloat *dgl_tex_env_color(int unit) { return env_u[unit].color; }

/* GL's initial environment, with texture_env_combine's defaults. */
static void env_defaults(dgl_texenv *e)
{
    memset(e, 0, sizeof *e);
    e->mode = GL_MODULATE;
    e->combine_rgb = e->combine_alpha = GL_MODULATE;
    e->src_rgb[0] = e->src_alpha[0] = GL_TEXTURE;
    e->src_rgb[1] = e->src_alpha[1] = GL_PREVIOUS_ARB;
    e->src_rgb[2] = e->src_alpha[2] = GL_CONSTANT_ARB;
    e->op_rgb[0] = e->op_rgb[1] = GL_SRC_COLOR;
    e->op_rgb[2] = GL_SRC_ALPHA;
    e->op_alpha[0] = e->op_alpha[1] = e->op_alpha[2] = GL_SRC_ALPHA;
    e->rgb_scale = e->alpha_scale = 1.0f;
}

/* Everything drawn: the device's sync, which runs context.c's hook (the
 * epoch moves on, retired blocks come back) when it completes. */
int dgl_sync(void)
{
    return dgl_dev ? dgl_dev->sync() : -1;
}
GLuint dgl_bound_name(void) { return bound; }

static int far_index(GLuint name)
{
    int i;
    for (i = 0; i < nfar; i++)
        if (far_tex[i]->name == name)
            return i;
    return -1;
}

static dgl_texture *get(GLuint name, int create)
{
    dgl_texture *t;
    if (name >= MAX_TEXTURES) {
        int i = far_index(name);
        t = i >= 0 ? far_tex[i] : NULL;
        if (t || !create)
            return t;
        if (nfar == capfar) {
            dgl_texture **n = (dgl_texture **)realloc(far_tex, (size_t)(capfar ? capfar * 2 : 16) * sizeof *n);
            if (!n)
                return NULL;
            far_tex = n;
            capfar = capfar ? capfar * 2 : 16;
        }
    } else
        t = tex[name];
    if (!t && create) {
        t = (dgl_texture *)calloc(1, sizeof *t);
        if (!t)
            return NULL;
        t->name = name;
        t->min_filter = GL_NEAREST_MIPMAP_LINEAR;         /* GL defaults */
        t->mag_filter = GL_LINEAR;
        t->wrap_s = t->wrap_t = GL_REPEAT;
        t->max_level = 1000;
        if (name >= MAX_TEXTURES)
            far_tex[nfar++] = t;
        else
            tex[name] = t;
    }
    return t;
}

/* Every texture object: the table, then the list. */
static dgl_texture *nth(int i)
{
    return i < MAX_TEXTURES ? tex[i] : far_tex[i - MAX_TEXTURES];
}
#define ALL_TEXTURES (MAX_TEXTURES + nfar)

/* Every texture object, in the table's order (the back end's eviction looks
 * through them): how many places, and the object in each (or NULL). */
int dgl_texture_count(void) { return ALL_TEXTURES; }
dgl_texture *dgl_texture_nth(int i) { return nth(i); }

static void destroy(GLuint name)
{
    dgl_texture *t = get(name, 0);
    int l, far = name >= MAX_TEXTURES ? far_index(name) : -1;   /* before t is freed */
    if (!t)
        return;
    dgl_be->tex_free(t);
    for (l = 0; l < DGL_MAX_LEVELS; l++) {
        free(t->level[l].rgba);
        free(t->level[l].idx);
    }
    free(t->own);
    free(t);
    if (far >= 0)
        far_tex[far] = far_tex[--nfar];
    else
        tex[name] = NULL;
}

void dgl_textures_reset(uint32_t heap_start, uint32_t heap_end)
{
    GLuint i;
    for (i = 0; i < MAX_TEXTURES; i++)
        destroy(i);
    while (nfar)
        destroy(far_tex[nfar - 1]->name);
    bound_u[0] = bound_u[1] = 0;
    dgl_palettes_reset();
    env_defaults(&env_u[0]);
    env_defaults(&env_u[1]);
    dgl_be->tex_reset(heap_start, heap_end);
}

dgl_texture *dgl_unit_texture(int unit)
{
    dgl_texture *t = get(bound_u[unit], 0);
    return t && DGL_LEVEL_DEFINED(&t->level[0]) ? t : NULL;
}

dgl_palette *dgl_bound_palette(void)
{
    dgl_texture *t = get(bound, 1);
    if (t && !t->own)
        t->own = (dgl_palette *)calloc(1, sizeof *t->own);
    return t ? t->own : NULL;
}

/* ---- Names -------------------------------------------------------------- */
void APIENTRY glGenTextures(GLsizei n, GLuint *names)
{
    GLuint next = 1;
    GLsizei i;
    if (n < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        while (next < MAX_TEXTURES && tex[next])
            next++;
        if (next == MAX_TEXTURES || !get(next, 1)) {
            dgl_gl_error(GL_OUT_OF_MEMORY);
            names[i] = 0;
            continue;
        }
        names[i] = next++;
    }
}

void APIENTRY glBindTexture(GLenum target, GLuint name)
{
    if (target != GL_TEXTURE_2D) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (name && !get(name, 1)) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
    bound = name;
    dgl_gl.dirty |= DGL_DIRTY_TEXTURE;
}

void APIENTRY glDeleteTextures(GLsizei n, const GLuint *names)
{
    GLsizei i;
    if (n < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        if (!names[i])
            continue;
        if (names[i] == bound_u[0] || names[i] == bound_u[1]) {
            if (names[i] == bound_u[0]) bound_u[0] = 0;
            if (names[i] == bound_u[1]) bound_u[1] = 0;
            dgl_gl.dirty |= DGL_DIRTY_TEXTURE;
        }
        destroy(names[i]);
    }
}

GLboolean APIENTRY glIsTexture(GLuint name)
{
    return (GLboolean)(name && get(name, 0) != NULL);
}

/* ---- Parameters and environment ---------------------------------------- */
static void param(GLenum target, GLenum pname, GLint v)
{
    dgl_texture *t;
    if (target != GL_TEXTURE_2D) { dgl_gl_error(GL_INVALID_ENUM); return; }
    t = get(bound, 1);
    if (!t) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        if (v != GL_NEAREST && v != GL_LINEAR && (v < GL_NEAREST_MIPMAP_NEAREST || v > GL_LINEAR_MIPMAP_LINEAR)) {
            dgl_gl_error(GL_INVALID_ENUM);
            return;
        }
        t->min_filter = (GLenum)v;
        t->dirty = 1;                   /* the mip window may change */
        break;
    case GL_TEXTURE_MAG_FILTER:
        if (v != GL_NEAREST && v != GL_LINEAR) { dgl_gl_error(GL_INVALID_ENUM); return; }
        t->mag_filter = (GLenum)v;
        break;
    case GL_TEXTURE_WRAP_S: case GL_TEXTURE_WRAP_T:
        if (v != GL_REPEAT && v != GL_CLAMP && v != GL_CLAMP_TO_EDGE) { dgl_gl_error(GL_INVALID_ENUM); return; }
        if (pname == GL_TEXTURE_WRAP_S) t->wrap_s = (GLenum)v; else t->wrap_t = (GLenum)v;
        t->dirty = 1;                   /* small levels are widened differently */
        break;
    case GL_TEXTURE_MAX_LEVEL:
        if (v < 0) { dgl_gl_error(GL_INVALID_VALUE); return; }
        t->max_level = v;
        t->dirty = 1;
        break;
    case GL_TEXTURE_PRIORITY: case GL_TEXTURE_MAX_ANISOTROPY_EXT:
        return;                         /* accepted, no effect */
    default:
        dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    dgl_gl.dirty |= DGL_DIRTY_TEXTURE;
}

void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint v) { param(target, pname, v); }
void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat v) { param(target, pname, (GLint)v); }

void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *v)
{
    if (pname == GL_TEXTURE_BORDER_COLOR) {         /* borders are not drawn (PRD §8.3) */
        if (target != GL_TEXTURE_2D) dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    param(target, pname, (GLint)v[0]);
}

void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *v)
{
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        if (target != GL_TEXTURE_2D) dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    param(target, pname, v[0]);
}

static int get_param(GLenum target, GLenum pname, GLfloat *v)
{
    dgl_texture *t = get(bound, 1);
    if (target != GL_TEXTURE_2D || !t) { dgl_gl_error(GL_INVALID_ENUM); return 0; }
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: v[0] = (GLfloat)t->min_filter; return 1;
    case GL_TEXTURE_MAG_FILTER: v[0] = (GLfloat)t->mag_filter; return 1;
    case GL_TEXTURE_WRAP_S: v[0] = (GLfloat)t->wrap_s; return 1;
    case GL_TEXTURE_WRAP_T: v[0] = (GLfloat)t->wrap_t; return 1;
    case GL_TEXTURE_MAX_LEVEL: v[0] = (GLfloat)t->max_level; return 1;
    case GL_TEXTURE_PRIORITY: v[0] = 1.0f; return 1;
    case GL_TEXTURE_RESIDENT: { int rgba[4]; v[0] = (GLfloat)dgl_be->tex_info(t, rgba); return 1; }
    case GL_TEXTURE_BORDER_COLOR: v[0] = v[1] = v[2] = v[3] = 0; return 4;
    default: dgl_gl_error(GL_INVALID_ENUM); return 0;
    }
}

void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *out)
{
    GLfloat v[4];
    int n = get_param(target, pname, v), i;
    for (i = 0; i < n; i++)
        out[i] = v[i];
}

void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *out)
{
    GLfloat v[4];
    int n = get_param(target, pname, v), i;
    for (i = 0; i < n; i++)
        out[i] = (GLint)v[i];
}

/* Level parameters of the bound texture, as uploaded (the internal format
 * is the base format DOS-GL stores). */
void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *out)
{
    dgl_texture *t = get(bound, 0);
    const dgl_level *L;
    if (target != GL_TEXTURE_2D || level < 0 || level >= DGL_MAX_LEVELS) { dgl_gl_error(GL_INVALID_VALUE); return; }
    L = t ? &t->level[level] : NULL;
    switch (pname) {
    case GL_TEXTURE_WIDTH: out[0] = L ? L->w : 0; break;
    case GL_TEXTURE_HEIGHT: out[0] = L ? L->h : 0; break;
    case GL_TEXTURE_BORDER: out[0] = 0; break;
    case GL_TEXTURE_INTERNAL_FORMAT: out[0] = L && DGL_LEVEL_DEFINED(L) ? L->ifmt : 1; break;
    case GL_TEXTURE_INDEX_SIZE_EXT: out[0] = L && L->idx ? 8 : 0; break;
    case GL_TEXTURE_RED_SIZE: case GL_TEXTURE_GREEN_SIZE: case GL_TEXTURE_BLUE_SIZE: case GL_TEXTURE_ALPHA_SIZE: {
        int rgba[4];                            /* (the back end's copy: what it stores) */
        if (!t) { out[0] = 0; break; }
        dgl_be->tex_info(t, rgba);
        out[0] = rgba[pname == GL_TEXTURE_RED_SIZE ? 0 : pname == GL_TEXTURE_GREEN_SIZE ? 1 : pname == GL_TEXTURE_BLUE_SIZE ? 2 : 3];
        break;
    }
    case GL_TEXTURE_LUMINANCE_SIZE: case GL_TEXTURE_INTENSITY_SIZE: out[0] = 0; break;
    default: dgl_gl_error(GL_INVALID_ENUM);
    }
}

void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *out)
{
    GLint v = 0;
    glGetTexLevelParameteriv(target, level, pname, &v);
    out[0] = (GLfloat)v;
}

/* ---- Texture environment ------------------------------------------------- */
/* texture_env_combine's enumerants, for validation. */
static int one_of(GLenum x, const GLenum *set)
{
    for (; *set; set++)
        if (*set == x)
            return 1;
    return 0;
}
static const GLenum combine_rgb_funcs[] = { GL_REPLACE, GL_MODULATE, GL_ADD, GL_ADD_SIGNED_ARB, GL_INTERPOLATE_ARB,
                                            GL_SUBTRACT_ARB, 0 };
static const GLenum combine_srcs[] = { GL_TEXTURE, GL_CONSTANT_ARB, GL_PRIMARY_COLOR_ARB, GL_PREVIOUS_ARB, 0 };
static const GLenum rgb_operands[] = { GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, 0 };
static const GLenum alpha_operands[] = { GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, 0 };

static void env(GLenum target, GLenum pname, const GLfloat *v)
{
    dgl_texenv *e = &cur_env;
    GLenum x = (GLenum)(GLint)v[0];
    int i;
    if (target != GL_TEXTURE_ENV) { dgl_gl_error(GL_INVALID_ENUM); return; }
    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        /* GL_BLEND is drawn by the G400's combiner (with a black environment
         * colour; otherwise as GL_MODULATE, logged); GL_ADD (GL 1.3) and
         * GL_COMBINE_ARB by the combiner (combine.c: what it cannot do is
         * drawn as GL_MODULATE, logged) */
        if ((x != GL_MODULATE && x != GL_REPLACE && x != GL_DECAL && x != GL_BLEND && x != GL_ADD &&
             x != GL_COMBINE_ARB) ||
            ((x == GL_ADD || x == GL_COMBINE_ARB) && dgl_texture_units() < 2)) {   /* no combiner */
            dgl_gl_error(GL_INVALID_ENUM);
            return;
        }
        e->mode = x;
        break;
    case GL_TEXTURE_ENV_COLOR:
        for (i = 0; i < 4; i++)
            e->color[i] = v[i] < 0 ? 0 : v[i] > 1 ? 1 : v[i];
        break;
    case GL_COMBINE_RGB_ARB:
    case GL_COMBINE_ALPHA_ARB:
        if (!one_of(x, combine_rgb_funcs)) { dgl_gl_error(GL_INVALID_ENUM); return; }
        *(pname == GL_COMBINE_RGB_ARB ? &e->combine_rgb : &e->combine_alpha) = x;
        break;
    case GL_SOURCE0_RGB_ARB: case GL_SOURCE1_RGB_ARB: case GL_SOURCE2_RGB_ARB:
        if (!one_of(x, combine_srcs)) { dgl_gl_error(GL_INVALID_ENUM); return; }
        e->src_rgb[pname - GL_SOURCE0_RGB_ARB] = x;
        break;
    case GL_SOURCE0_ALPHA_ARB: case GL_SOURCE1_ALPHA_ARB: case GL_SOURCE2_ALPHA_ARB:
        if (!one_of(x, combine_srcs)) { dgl_gl_error(GL_INVALID_ENUM); return; }
        e->src_alpha[pname - GL_SOURCE0_ALPHA_ARB] = x;
        break;
    case GL_OPERAND0_RGB_ARB: case GL_OPERAND1_RGB_ARB: case GL_OPERAND2_RGB_ARB:
        if (!one_of(x, rgb_operands)) { dgl_gl_error(GL_INVALID_ENUM); return; }
        e->op_rgb[pname - GL_OPERAND0_RGB_ARB] = x;
        break;
    case GL_OPERAND0_ALPHA_ARB: case GL_OPERAND1_ALPHA_ARB: case GL_OPERAND2_ALPHA_ARB:
        if (!one_of(x, alpha_operands)) { dgl_gl_error(GL_INVALID_ENUM); return; }
        e->op_alpha[pname - GL_OPERAND0_ALPHA_ARB] = x;
        break;
    case GL_RGB_SCALE_ARB:
    case GL_ALPHA_SCALE:
        if (v[0] != 1.0f && v[0] != 2.0f && v[0] != 4.0f) { dgl_gl_error(GL_INVALID_VALUE); return; }
        *(pname == GL_RGB_SCALE_ARB ? &e->rgb_scale : &e->alpha_scale) = v[0];
        break;
    default:
        dgl_gl_error(GL_INVALID_ENUM);
        return;
    }
    dgl_gl.dirty |= DGL_DIRTY_TEXTURE | DGL_DIRTY_RASTER;
}

/* One value of glGetTexEnv (colour aside); 0 for an unknown name. */
static int env_value(const dgl_texenv *e, GLenum pname, GLfloat *v)
{
    switch (pname) {
    case GL_TEXTURE_ENV_MODE: *v = (GLfloat)e->mode; return 1;
    case GL_COMBINE_RGB_ARB: *v = (GLfloat)e->combine_rgb; return 1;
    case GL_COMBINE_ALPHA_ARB: *v = (GLfloat)e->combine_alpha; return 1;
    case GL_SOURCE0_RGB_ARB: case GL_SOURCE1_RGB_ARB: case GL_SOURCE2_RGB_ARB:
        *v = (GLfloat)e->src_rgb[pname - GL_SOURCE0_RGB_ARB]; return 1;
    case GL_SOURCE0_ALPHA_ARB: case GL_SOURCE1_ALPHA_ARB: case GL_SOURCE2_ALPHA_ARB:
        *v = (GLfloat)e->src_alpha[pname - GL_SOURCE0_ALPHA_ARB]; return 1;
    case GL_OPERAND0_RGB_ARB: case GL_OPERAND1_RGB_ARB: case GL_OPERAND2_RGB_ARB:
        *v = (GLfloat)e->op_rgb[pname - GL_OPERAND0_RGB_ARB]; return 1;
    case GL_OPERAND0_ALPHA_ARB: case GL_OPERAND1_ALPHA_ARB: case GL_OPERAND2_ALPHA_ARB:
        *v = (GLfloat)e->op_alpha[pname - GL_OPERAND0_ALPHA_ARB]; return 1;
    case GL_RGB_SCALE_ARB: *v = e->rgb_scale; return 1;
    case GL_ALPHA_SCALE: *v = e->alpha_scale; return 1;
    default: return 0;
    }
}

void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint p)
{
    GLfloat f = (GLfloat)p;
    if (pname == GL_TEXTURE_ENV_COLOR) { dgl_gl_error(GL_INVALID_ENUM); return; }
    env(target, pname, &f);
}

void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat p)
{
    if (pname == GL_TEXTURE_ENV_COLOR) { dgl_gl_error(GL_INVALID_ENUM); return; }
    env(target, pname, &p);
}

void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *v) { env(target, pname, v); }

void APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint *v)
{
    GLfloat f[4];
    int i;
    if (pname == GL_TEXTURE_ENV_COLOR)
        for (i = 0; i < 4; i++)          /* integer colours map [0, INT_MAX] to [0, 1] */
            f[i] = (GLfloat)((double)v[i] / 2147483647.0);
    else
        f[0] = (GLfloat)v[0];
    env(target, pname, f);
}

void APIENTRY glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *v)
{
    int i;
    if (target != GL_TEXTURE_ENV) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (pname == GL_TEXTURE_ENV_COLOR) for (i = 0; i < 4; i++) v[i] = cur_env.color[i];
    else if (!env_value(&cur_env, pname, v)) dgl_gl_error(GL_INVALID_ENUM);
}

void APIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint *v)
{
    int i;
    GLfloat f;
    if (target != GL_TEXTURE_ENV) { dgl_gl_error(GL_INVALID_ENUM); return; }
    if (pname == GL_TEXTURE_ENV_COLOR) for (i = 0; i < 4; i++) v[i] = (GLint)(cur_env.color[i] * 2147483647.0);
    else if (env_value(&cur_env, pname, &f)) v[0] = (GLint)f;
    else dgl_gl_error(GL_INVALID_ENUM);
}

/* ---- Uploads ------------------------------------------------------------- */
static int is_pow2(int v) { return v > 0 && !(v & (v - 1)); }

static int check_upload(GLenum target, GLint level, GLenum format, GLenum type)
{
    if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE || !dgl_format_bytes(format)) {
        dgl_gl_error(GL_INVALID_ENUM);
        return 0;
    }
    if (level < 0 || level >= DGL_MAX_LEVELS) {
        dgl_gl_error(GL_INVALID_VALUE);
        return 0;
    }
    return 1;
}

/* Copy client rows (with GL_UNPACK_ALIGNMENT) into RGBA8. */
static void unpack(GLenum format, const void *pixels, int w, int h, unsigned char *dst, int dst_stride, int ifc)
{
    int bytes = dgl_format_bytes(format), y;
    int stride = (w * bytes + dgl_gl.unpack_align - 1) / dgl_gl.unpack_align * dgl_gl.unpack_align;
    for (y = 0; y < h; y++) {
        unsigned char *d = dst + (size_t)y * dst_stride;
        if ((y & 15) == 15)
            dgl_yield_maybe(0);
        dgl_to_rgba(format, (const unsigned char *)pixels + (size_t)y * stride, w, d);
        dgl_apply_ifmt(ifc, d, w);
    }
}

/* Copy client rows of 8-bit colour indices. */
static void unpack_idx(const void *pixels, int w, int h, unsigned char *dst, int dst_stride)
{
    int stride = (w + dgl_gl.unpack_align - 1) / dgl_gl.unpack_align * dgl_gl.unpack_align, y;
    for (y = 0; y < h; y++)
        memcpy(dst + (size_t)y * dst_stride, (const unsigned char *)pixels + (size_t)y * stride, (size_t)w);
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h,
                           GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    dgl_texture *t;
    dgl_level *L;
    dgl_caps caps;
    int max, ifc = dgl_ifmt_class(internalformat);
    FT_START();
    PROF_SCOPE(PROF_D_TEX);
    DGL_GPU_QUIET();                            /* (the texels into the shadow copy: the CPU's work) */
    dgl_be->caps(&caps);
    max = caps.max_texture_size;
    if (!check_upload(target, level, format, type))
        return;
    if (ifc < 0 || border != 0 || !is_pow2(w) || !is_pow2(h) || w > max || h > max) {
        dgl_gl_error(GL_INVALID_VALUE);
        return;
    }
    if ((ifc == DGL_IF_INDEX) != (format == GL_COLOR_INDEX)) {
        dgl_gl_error(GL_INVALID_OPERATION);          /* indices only into COLOR_INDEX textures (no pixel maps) */
        return;
    }
    t = get(bound, 1);
    if (!t) { dgl_gl_error(GL_OUT_OF_MEMORY); return; }
    L = &t->level[level];
    free(L->rgba);
    free(L->idx);
    L->rgba = L->idx = NULL;
    if (ifc == DGL_IF_INDEX)
        L->idx = (unsigned char *)calloc((size_t)w * h, 1);
    else
        L->rgba = (unsigned char *)calloc((size_t)w * h, 4);
    if (!DGL_LEVEL_DEFINED(L)) { L->w = L->h = 0; dgl_gl_error(GL_OUT_OF_MEMORY); return; }
    L->w = w; L->h = h;
    L->ifc = ifc;
    L->ifmt = internalformat;
    if (L->idx) {
        if (pixels)
            unpack_idx(pixels, w, h, L->idx, w);
    } else if (pixels) {
        unpack(format, pixels, w, h, L->rgba, w * 4, ifc);
    } else {
        dgl_apply_ifmt(ifc, L->rgba, (long)w * h);   /* undefined texels: at least the right class */
    }
    t->dirty = 1;
    dgl_gl.dirty |= DGL_DIRTY_TEXTURE;
    FT_END();
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                              GLenum type, const GLvoid *pixels)
{
    dgl_texture *t;
    dgl_level *L;
    FT_START();
    PROF_SCOPE(PROF_D_TEX);
    DGL_GPU_QUIET();                            /* (the texels into the shadow copy; the engine only if it loads them) */
    if (!check_upload(target, level, format, type))
        return;
    t = get(bound, 0);
    L = t ? &t->level[level] : NULL;
    if (!L || !DGL_LEVEL_DEFINED(L) || (L->idx != NULL) != (format == GL_COLOR_INDEX)) {
        dgl_gl_error(GL_INVALID_OPERATION);
        return;
    }
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > L->w || y + h > L->h) {
        dgl_gl_error(GL_INVALID_VALUE);
        return;
    }
    if (L->idx)
        unpack_idx(pixels, w, h, L->idx + (size_t)y * L->w + x, L->w);
    else
        unpack(format, pixels, w, h, L->rgba + ((size_t)y * L->w + x) * 4, L->w * 4, L->ifc);
    if (w && h && !dgl_be->tex_sub(t, level, x, y, w, h)) {
        dgl_texc.sub_full++;
        t->dirty = 1;
        dgl_gl.dirty |= DGL_DIRTY_TEXTURE;
    } else if (w && h) {
        /* Written in place: the G400's texture cache keeps the texels it
         * read before (engine or CPU writes alike) until the texture
         * registers are written again, so the next draw re-emits them
         * (bench-g400, G4EXP E6; docs/silicon-experiments.md). */
        dgl_gl.dirty |= DGL_DIRTY_TEXTURE;
    }
    FT_END();
}

