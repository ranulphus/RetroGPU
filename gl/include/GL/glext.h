/* glext.h - extension and GL 1.2 tokens DOS-GL accepts or advertises (PRD §6.1).
 * Values from the extension specifications; each guarded, since the Quake
 * ports define some of them themselves. */
#ifndef __glext_h_
#define __glext_h_

/* GL_EXT_bgra: BGRA transfers for glTexImage2D/glTexSubImage2D/glReadPixels. */
#define GL_EXT_bgra 1
#define GL_BGR_EXT               0x80E0
#define GL_BGRA_EXT              0x80E1

/* GL_EXT_texture: the one internal format GL 1.1 did not keep (programs
 * list it among their texture modes). */
#ifndef GL_RGB2_EXT
#define GL_RGB2_EXT              0x804E
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif
#ifndef GL_ADD
#define GL_ADD 0x0104
#endif
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB 0x84C0
#endif
#ifndef GL_TEXTURE1_ARB
#define GL_TEXTURE1_ARB 0x84C1
#endif
#ifndef GL_ACTIVE_TEXTURE_ARB
#define GL_ACTIVE_TEXTURE_ARB 0x84E0
#endif
#ifndef GL_CLIENT_ACTIVE_TEXTURE_ARB
#define GL_CLIENT_ACTIVE_TEXTURE_ARB 0x84E1
#endif
#ifndef GL_MAX_TEXTURE_UNITS_ARB
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif
#ifndef GL_COLOR_INDEX1_EXT
#define GL_COLOR_INDEX1_EXT 0x80E2
#endif
#ifndef GL_COLOR_INDEX2_EXT
#define GL_COLOR_INDEX2_EXT 0x80E3
#endif
#ifndef GL_COLOR_INDEX4_EXT
#define GL_COLOR_INDEX4_EXT 0x80E4
#endif
#ifndef GL_COLOR_INDEX8_EXT
#define GL_COLOR_INDEX8_EXT 0x80E5
#endif
#ifndef GL_COLOR_INDEX12_EXT
#define GL_COLOR_INDEX12_EXT 0x80E6
#endif
#ifndef GL_COLOR_INDEX16_EXT
#define GL_COLOR_INDEX16_EXT 0x80E7
#endif
#ifndef GL_TEXTURE_INDEX_SIZE_EXT
#define GL_TEXTURE_INDEX_SIZE_EXT 0x80ED
#endif
#ifndef GL_COLOR_TABLE_FORMAT_EXT
#define GL_COLOR_TABLE_FORMAT_EXT 0x80D8
#endif
#ifndef GL_COLOR_TABLE_WIDTH_EXT
#define GL_COLOR_TABLE_WIDTH_EXT 0x80D9
#endif
#ifndef GL_COLOR_TABLE_RED_SIZE_EXT
#define GL_COLOR_TABLE_RED_SIZE_EXT 0x80DA
#define GL_COLOR_TABLE_GREEN_SIZE_EXT 0x80DB
#define GL_COLOR_TABLE_BLUE_SIZE_EXT 0x80DC
#define GL_COLOR_TABLE_ALPHA_SIZE_EXT 0x80DD
#define GL_COLOR_TABLE_LUMINANCE_SIZE_EXT 0x80DE
#define GL_COLOR_TABLE_INTENSITY_SIZE_EXT 0x80DF
#endif
#ifndef GL_SHARED_TEXTURE_PALETTE_EXT
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB
#endif
#ifndef GL_POINT_SIZE_MIN_EXT
#define GL_POINT_SIZE_MIN_EXT 0x8126
#endif
#ifndef GL_POINT_SIZE_MAX_EXT
#define GL_POINT_SIZE_MAX_EXT 0x8127
#endif
#ifndef GL_POINT_FADE_THRESHOLD_SIZE_EXT
#define GL_POINT_FADE_THRESHOLD_SIZE_EXT 0x8128
#endif
#ifndef GL_DISTANCE_ATTENUATION_EXT
#define GL_DISTANCE_ATTENUATION_EXT 0x8129
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RGB_FXT1_3DFX
#define GL_COMPRESSED_RGB_FXT1_3DFX 0x86B0
#endif
#ifndef GL_COMPRESSED_RGBA_FXT1_3DFX
#define GL_COMPRESSED_RGBA_FXT1_3DFX 0x86B1
#endif
#ifndef GL_INVALID_FRAMEBUFFER_OPERATION_EXT
#define GL_INVALID_FRAMEBUFFER_OPERATION_EXT 0x0506
#endif
#ifndef GL_TABLE_TOO_LARGE
#define GL_TABLE_TOO_LARGE 0x8031
#endif
#ifndef GL_TEXTURE_COLOR_TABLE_SGI
#define GL_TEXTURE_COLOR_TABLE_SGI 0x80BC
#endif
#ifndef GL_COLOR_TABLE_FORMAT_EXT
#define GL_COLOR_TABLE_FORMAT_EXT 0x80D8
#endif
#ifndef GL_COLOR_TABLE_WIDTH_EXT
#define GL_COLOR_TABLE_WIDTH_EXT 0x80D9
#endif
/* GL_SGIS_multitexture (period GLQuake builds): texture units by these names. */
#ifndef GL_TEXTURE0_SGIS
#define GL_TEXTURE0_SGIS 0x835E
#endif
#ifndef GL_TEXTURE1_SGIS
#define GL_TEXTURE1_SGIS 0x835F
#endif

/* GL_ARB_vertex_buffer_object (every card: buffers in system memory) */
#ifndef GL_ARB_vertex_buffer_object
#define GL_ARB_vertex_buffer_object 1
#include <stddef.h>
typedef ptrdiff_t GLintptrARB;
typedef ptrdiff_t GLsizeiptrARB;
#define GL_BUFFER_SIZE_ARB                        0x8764
#define GL_BUFFER_USAGE_ARB                       0x8765
#define GL_ARRAY_BUFFER_ARB                       0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ARB               0x8893
#define GL_ARRAY_BUFFER_BINDING_ARB               0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB       0x8895
#define GL_VERTEX_ARRAY_BUFFER_BINDING_ARB        0x8896
#define GL_COLOR_ARRAY_BUFFER_BINDING_ARB         0x8898
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING_ARB 0x889A
#define GL_READ_ONLY_ARB                          0x88B8
#define GL_WRITE_ONLY_ARB                         0x88B9
#define GL_READ_WRITE_ARB                         0x88BA
#define GL_BUFFER_ACCESS_ARB                      0x88BB
#define GL_BUFFER_MAPPED_ARB                      0x88BC
#define GL_BUFFER_MAP_POINTER_ARB                 0x88BD
#define GL_STREAM_DRAW_ARB                        0x88E0
#define GL_STATIC_DRAW_ARB                        0x88E4
#define GL_DYNAMIC_DRAW_ARB                       0x88E8
#endif

/* GL_EXT_draw_range_elements (every card) */
#ifndef GL_MAX_ELEMENTS_VERTICES_EXT
#define GL_MAX_ELEMENTS_VERTICES_EXT 0x80E8
#define GL_MAX_ELEMENTS_INDICES_EXT  0x80E9
#endif

/* GL_ARB_texture_env_combine (G400 and G450: the combinations their
 * combiner stages can do, combine.c) */
#ifndef GL_COMBINE_ARB
#define GL_COMBINE_ARB        0x8570
#define GL_COMBINE_RGB_ARB    0x8571
#define GL_COMBINE_ALPHA_ARB  0x8572
#define GL_RGB_SCALE_ARB      0x8573
#define GL_ADD_SIGNED_ARB     0x8574
#define GL_INTERPOLATE_ARB    0x8575
#define GL_CONSTANT_ARB       0x8576
#define GL_PRIMARY_COLOR_ARB  0x8577
#define GL_PREVIOUS_ARB       0x8578
#define GL_SUBTRACT_ARB       0x84E7
#define GL_SOURCE0_RGB_ARB    0x8580
#define GL_SOURCE1_RGB_ARB    0x8581
#define GL_SOURCE2_RGB_ARB    0x8582
#define GL_SOURCE0_ALPHA_ARB  0x8588
#define GL_SOURCE1_ALPHA_ARB  0x8589
#define GL_SOURCE2_ALPHA_ARB  0x858A
#define GL_OPERAND0_RGB_ARB   0x8590
#define GL_OPERAND1_RGB_ARB   0x8591
#define GL_OPERAND2_RGB_ARB   0x8592
#define GL_OPERAND0_ALPHA_ARB 0x8598
#define GL_OPERAND1_ALPHA_ARB 0x8599
#define GL_OPERAND2_ALPHA_ARB 0x859A
#endif

/* Entry points DOS-GL implements, reached through dglGetProcAddress or
 * declared here when GL_GLEXT_PROTOTYPES is defined (as with Mesa's glext.h;
 * programs that keep their own function pointers never see them). */
#ifdef GL_GLEXT_PROTOTYPES
#ifndef APIENTRY
#define APIENTRY
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* GL_ARB_multitexture (G400 and G450) */
void APIENTRY glActiveTextureARB(GLenum target);
void APIENTRY glClientActiveTextureARB(GLenum target);
void APIENTRY glMultiTexCoord1dARB(GLenum target, GLdouble s);
void APIENTRY glMultiTexCoord1dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord2dARB(GLenum target, GLdouble s, GLdouble t);
void APIENTRY glMultiTexCoord2dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord3dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r);
void APIENTRY glMultiTexCoord3dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord4dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void APIENTRY glMultiTexCoord4dvARB(GLenum target, const GLdouble *v);
void APIENTRY glMultiTexCoord1fARB(GLenum target, GLfloat s);
void APIENTRY glMultiTexCoord1fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);
void APIENTRY glMultiTexCoord2fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord3fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r);
void APIENTRY glMultiTexCoord3fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void APIENTRY glMultiTexCoord4fvARB(GLenum target, const GLfloat *v);
void APIENTRY glMultiTexCoord1iARB(GLenum target, GLint s);
void APIENTRY glMultiTexCoord1ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord2iARB(GLenum target, GLint s, GLint t);
void APIENTRY glMultiTexCoord2ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord3iARB(GLenum target, GLint s, GLint t, GLint r);
void APIENTRY glMultiTexCoord3ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord4iARB(GLenum target, GLint s, GLint t, GLint r, GLint q);
void APIENTRY glMultiTexCoord4ivARB(GLenum target, const GLint *v);
void APIENTRY glMultiTexCoord1sARB(GLenum target, GLshort s);
void APIENTRY glMultiTexCoord1svARB(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord2sARB(GLenum target, GLshort s, GLshort t);
void APIENTRY glMultiTexCoord2svARB(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord3sARB(GLenum target, GLshort s, GLshort t, GLshort r);
void APIENTRY glMultiTexCoord3svARB(GLenum target, const GLshort *v);
void APIENTRY glMultiTexCoord4sARB(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
void APIENTRY glMultiTexCoord4svARB(GLenum target, const GLshort *v);
/* GL_SGIS_multitexture names */
void APIENTRY glSelectTextureSGIS(GLenum target);
void APIENTRY glSelectTextureCoordSetSGIS(GLenum target);
void APIENTRY glMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t);
void APIENTRY glMTexCoord2fvSGIS(GLenum target, const GLfloat *v);
/* GL_ARB_vertex_buffer_object */
void APIENTRY glBindBufferARB(GLenum target, GLuint buffer);
void APIENTRY glDeleteBuffersARB(GLsizei n, const GLuint *buffers);
void APIENTRY glGenBuffersARB(GLsizei n, GLuint *buffers);
GLboolean APIENTRY glIsBufferARB(GLuint buffer);
void APIENTRY glBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage);
void APIENTRY glBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data);
void APIENTRY glGetBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, GLvoid *data);
GLvoid *APIENTRY glMapBufferARB(GLenum target, GLenum access);
GLboolean APIENTRY glUnmapBufferARB(GLenum target);
void APIENTRY glGetBufferParameterivARB(GLenum target, GLenum pname, GLint *params);
void APIENTRY glGetBufferPointervARB(GLenum target, GLenum pname, GLvoid **params);
/* GL_EXT_draw_range_elements */
void APIENTRY glDrawRangeElementsEXT(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                     const GLvoid *indices);
/* GL_EXT_paletted_texture */
void APIENTRY glColorTableEXT(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type,
                              const GLvoid *table);
void APIENTRY glColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type,
                                 const GLvoid *data);
void APIENTRY glGetColorTableEXT(GLenum target, GLenum format, GLenum type, GLvoid *data);
void APIENTRY glGetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params);
void APIENTRY glGetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params);
#ifdef __cplusplus
}
#endif
#endif /* GL_GLEXT_PROTOTYPES */

#endif /* __glext_h_ */
