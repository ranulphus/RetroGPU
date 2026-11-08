/* attrib.c - the rest of GL 1.1's immediate-mode attribute commands
 * (glColor*, glVertex*, glTexCoord*, glNormal* in every type and vector
 * form), each a thin wrapper over the float forms in vertex.c.
 *
 * Signed integer colours map as GL 1.1 specifies, c -> (2c + 1) / (2^n - 1);
 * unsigned ones as c / (2^n - 1). Texture coordinates keep s and t (DOS-GL
 * textures are 2D); a q other than 1 divides them, the projective form of
 * glTexCoord4. Normals are accepted and ignored (no lighting, PRD §2.2). */
#include "gl_state.h"

void dgl_imm_vertex(float x, float y, float z, float w);     /* vertex.c */

/* ---- Colours --------------------------------------------------------------- */
#define SB(c) ((2.0f * (float)(c) + 1.0f) / 255.0f)
#define SS(c) ((2.0f * (float)(c) + 1.0f) / 65535.0f)
#define SI(c) ((float)((2.0 * (double)(c) + 1.0) / 4294967295.0))
#define UB(c) DGL_UB(c)
#define US(c) ((float)(c) / 65535.0f)
#define UI(c) ((float)((double)(c) / 4294967295.0))

void APIENTRY glColor3b(GLbyte r, GLbyte g, GLbyte b) { glColor4f(SB(r), SB(g), SB(b), 1.0f); }
void APIENTRY glColor3bv(const GLbyte *v) { glColor4f(SB(v[0]), SB(v[1]), SB(v[2]), 1.0f); }
void APIENTRY glColor3d(GLdouble r, GLdouble g, GLdouble b) { glColor4f((float)r, (float)g, (float)b, 1.0f); }
void APIENTRY glColor3dv(const GLdouble *v) { glColor4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void APIENTRY glColor3fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], 1.0f); }
void APIENTRY glColor3i(GLint r, GLint g, GLint b) { glColor4f(SI(r), SI(g), SI(b), 1.0f); }
void APIENTRY glColor3iv(const GLint *v) { glColor4f(SI(v[0]), SI(v[1]), SI(v[2]), 1.0f); }
void APIENTRY glColor3s(GLshort r, GLshort g, GLshort b) { glColor4f(SS(r), SS(g), SS(b), 1.0f); }
void APIENTRY glColor3sv(const GLshort *v) { glColor4f(SS(v[0]), SS(v[1]), SS(v[2]), 1.0f); }
void APIENTRY glColor3ubv(const GLubyte *v) { glColor4f(UB(v[0]), UB(v[1]), UB(v[2]), 1.0f); }
void APIENTRY glColor3ui(GLuint r, GLuint g, GLuint b) { glColor4f(UI(r), UI(g), UI(b), 1.0f); }
void APIENTRY glColor3uiv(const GLuint *v) { glColor4f(UI(v[0]), UI(v[1]), UI(v[2]), 1.0f); }
void APIENTRY glColor3us(GLushort r, GLushort g, GLushort b) { glColor4f(US(r), US(g), US(b), 1.0f); }
void APIENTRY glColor3usv(const GLushort *v) { glColor4f(US(v[0]), US(v[1]), US(v[2]), 1.0f); }

void APIENTRY glColor4b(GLbyte r, GLbyte g, GLbyte b, GLbyte a) { glColor4f(SB(r), SB(g), SB(b), SB(a)); }
void APIENTRY glColor4bv(const GLbyte *v) { glColor4f(SB(v[0]), SB(v[1]), SB(v[2]), SB(v[3])); }
void APIENTRY glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a)
{ glColor4f((float)r, (float)g, (float)b, (float)a); }
void APIENTRY glColor4dv(const GLdouble *v) { glColor4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glColor4fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], v[3]); }
void APIENTRY glColor4i(GLint r, GLint g, GLint b, GLint a) { glColor4f(SI(r), SI(g), SI(b), SI(a)); }
void APIENTRY glColor4iv(const GLint *v) { glColor4f(SI(v[0]), SI(v[1]), SI(v[2]), SI(v[3])); }
void APIENTRY glColor4s(GLshort r, GLshort g, GLshort b, GLshort a) { glColor4f(SS(r), SS(g), SS(b), SS(a)); }
void APIENTRY glColor4sv(const GLshort *v) { glColor4f(SS(v[0]), SS(v[1]), SS(v[2]), SS(v[3])); }
void APIENTRY glColor4ubv(const GLubyte *v) { glColor4f(UB(v[0]), UB(v[1]), UB(v[2]), UB(v[3])); }
void APIENTRY glColor4ui(GLuint r, GLuint g, GLuint b, GLuint a) { glColor4f(UI(r), UI(g), UI(b), UI(a)); }
void APIENTRY glColor4uiv(const GLuint *v) { glColor4f(UI(v[0]), UI(v[1]), UI(v[2]), UI(v[3])); }
void APIENTRY glColor4us(GLushort r, GLushort g, GLushort b, GLushort a) { glColor4f(US(r), US(g), US(b), US(a)); }
void APIENTRY glColor4usv(const GLushort *v) { glColor4f(US(v[0]), US(v[1]), US(v[2]), US(v[3])); }

/* ---- Vertices ------------------------------------------------------------------ */
void APIENTRY glVertex2d(GLdouble x, GLdouble y) { dgl_imm_vertex((float)x, (float)y, 0, 1); }
void APIENTRY glVertex2dv(const GLdouble *v) { dgl_imm_vertex((float)v[0], (float)v[1], 0, 1); }
void APIENTRY glVertex2fv(const GLfloat *v) { dgl_imm_vertex(v[0], v[1], 0, 1); }
void APIENTRY glVertex2i(GLint x, GLint y) { dgl_imm_vertex((float)x, (float)y, 0, 1); }
void APIENTRY glVertex2iv(const GLint *v) { dgl_imm_vertex((float)v[0], (float)v[1], 0, 1); }
void APIENTRY glVertex2s(GLshort x, GLshort y) { dgl_imm_vertex(x, y, 0, 1); }
void APIENTRY glVertex2sv(const GLshort *v) { dgl_imm_vertex(v[0], v[1], 0, 1); }
void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z) { dgl_imm_vertex((float)x, (float)y, (float)z, 1); }
void APIENTRY glVertex3dv(const GLdouble *v) { dgl_imm_vertex((float)v[0], (float)v[1], (float)v[2], 1); }
void APIENTRY glVertex3i(GLint x, GLint y, GLint z) { dgl_imm_vertex((float)x, (float)y, (float)z, 1); }
void APIENTRY glVertex3iv(const GLint *v) { dgl_imm_vertex((float)v[0], (float)v[1], (float)v[2], 1); }
void APIENTRY glVertex3s(GLshort x, GLshort y, GLshort z) { dgl_imm_vertex(x, y, z, 1); }
void APIENTRY glVertex3sv(const GLshort *v) { dgl_imm_vertex(v[0], v[1], v[2], 1); }
void APIENTRY glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{ dgl_imm_vertex((float)x, (float)y, (float)z, (float)w); }
void APIENTRY glVertex4dv(const GLdouble *v) { dgl_imm_vertex((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { dgl_imm_vertex(x, y, z, w); }
void APIENTRY glVertex4fv(const GLfloat *v) { dgl_imm_vertex(v[0], v[1], v[2], v[3]); }
void APIENTRY glVertex4i(GLint x, GLint y, GLint z, GLint w)
{ dgl_imm_vertex((float)x, (float)y, (float)z, (float)w); }
void APIENTRY glVertex4iv(const GLint *v) { dgl_imm_vertex((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w) { dgl_imm_vertex(x, y, z, w); }
void APIENTRY glVertex4sv(const GLshort *v) { dgl_imm_vertex(v[0], v[1], v[2], v[3]); }

/* ---- Texture coordinates ----------------------------------------------------------- */
static void tc(float s, float t, float q)
{
    if (q != 1.0f && q != 0.0f) {
        s /= q;
        t /= q;
    }
    glTexCoord2f(s, t);
}

void APIENTRY glTexCoord1d(GLdouble s) { tc((float)s, 0, 1); }
void APIENTRY glTexCoord1dv(const GLdouble *v) { tc((float)v[0], 0, 1); }
void APIENTRY glTexCoord1f(GLfloat s) { tc(s, 0, 1); }
void APIENTRY glTexCoord1fv(const GLfloat *v) { tc(v[0], 0, 1); }
void APIENTRY glTexCoord1i(GLint s) { tc((float)s, 0, 1); }
void APIENTRY glTexCoord1iv(const GLint *v) { tc((float)v[0], 0, 1); }
void APIENTRY glTexCoord1s(GLshort s) { tc(s, 0, 1); }
void APIENTRY glTexCoord1sv(const GLshort *v) { tc(v[0], 0, 1); }
void APIENTRY glTexCoord2d(GLdouble s, GLdouble t) { tc((float)s, (float)t, 1); }
void APIENTRY glTexCoord2dv(const GLdouble *v) { tc((float)v[0], (float)v[1], 1); }
void APIENTRY glTexCoord2fv(const GLfloat *v) { tc(v[0], v[1], 1); }
void APIENTRY glTexCoord2i(GLint s, GLint t) { tc((float)s, (float)t, 1); }
void APIENTRY glTexCoord2iv(const GLint *v) { tc((float)v[0], (float)v[1], 1); }
void APIENTRY glTexCoord2s(GLshort s, GLshort t) { tc(s, t, 1); }
void APIENTRY glTexCoord2sv(const GLshort *v) { tc(v[0], v[1], 1); }
void APIENTRY glTexCoord3d(GLdouble s, GLdouble t, GLdouble r) { (void)r; tc((float)s, (float)t, 1); }
void APIENTRY glTexCoord3dv(const GLdouble *v) { tc((float)v[0], (float)v[1], 1); }
void APIENTRY glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { (void)r; tc(s, t, 1); }
void APIENTRY glTexCoord3fv(const GLfloat *v) { tc(v[0], v[1], 1); }
void APIENTRY glTexCoord3i(GLint s, GLint t, GLint r) { (void)r; tc((float)s, (float)t, 1); }
void APIENTRY glTexCoord3iv(const GLint *v) { tc((float)v[0], (float)v[1], 1); }
void APIENTRY glTexCoord3s(GLshort s, GLshort t, GLshort r) { (void)r; tc(s, t, 1); }
void APIENTRY glTexCoord3sv(const GLshort *v) { tc(v[0], v[1], 1); }
void APIENTRY glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{ (void)r; tc((float)s, (float)t, (float)q); }
void APIENTRY glTexCoord4dv(const GLdouble *v) { tc((float)v[0], (float)v[1], (float)v[3]); }
void APIENTRY glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) { (void)r; tc(s, t, q); }
void APIENTRY glTexCoord4fv(const GLfloat *v) { tc(v[0], v[1], v[3]); }
void APIENTRY glTexCoord4i(GLint s, GLint t, GLint r, GLint q) { (void)r; tc((float)s, (float)t, (float)q); }
void APIENTRY glTexCoord4iv(const GLint *v) { tc((float)v[0], (float)v[1], (float)v[3]); }
void APIENTRY glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q) { (void)r; tc(s, t, q); }
void APIENTRY glTexCoord4sv(const GLshort *v) { tc(v[0], v[1], v[3]); }

/* ---- Normals (accepted, no lighting) --------------------------------------------------- */
void APIENTRY glNormal3b(GLbyte x, GLbyte y, GLbyte z) { (void)x; (void)y; (void)z; }
void APIENTRY glNormal3bv(const GLbyte *v) { (void)v; }
void APIENTRY glNormal3d(GLdouble x, GLdouble y, GLdouble z) { (void)x; (void)y; (void)z; }
void APIENTRY glNormal3dv(const GLdouble *v) { (void)v; }
void APIENTRY glNormal3fv(const GLfloat *v) { (void)v; }
void APIENTRY glNormal3i(GLint x, GLint y, GLint z) { (void)x; (void)y; (void)z; }
void APIENTRY glNormal3iv(const GLint *v) { (void)v; }
void APIENTRY glNormal3s(GLshort x, GLshort y, GLshort z) { (void)x; (void)y; (void)z; }
void APIENTRY glNormal3sv(const GLshort *v) { (void)v; }
