/* test_buffer.c - GL_ARB_vertex_buffer_object in system memory (buffer.c):
 * arrays and indices as offsets into bound buffers, reallocation seen at the
 * next draw, map/unmap, deletion; and the G400 combiner words combine.c
 * makes for GL_COMBINE_ARB, checked against Mesa's words for the classic
 * environments they are equivalent to (docs/g400-dual-texture.md §3). */
#include "unit.h"
#include "../../src/gl/gl_state.h"
#include "../../src/gl/gl_draw.h"
#include "../../src/gl/gl_tex.h"
#include <string.h>

int dgl_stage_word(int stage, const dgl_texenv *e, const dgl_texture *t, uint32_t *w);
int dgl_texture_units(void) { return 2; }       /* the combiner's card (ext.c's, host build) */

static int ntri;
static float tri_x[16][3], tri_s1[16];

static void on_tri(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *p)
{
    (void)p;
    if (ntri < 16) {
        tri_x[ntri][0] = a->x; tri_x[ntri][1] = b->x; tri_x[ntri][2] = c->x;
        tri_s1[ntri] = a->s1;
    }
    ntri++;
}

/* GL's defaults for one unit, as texture.c sets them. */
static dgl_texenv env_default(void)
{
    dgl_texenv e;
    memset(&e, 0, sizeof e);
    e.mode = GL_COMBINE_ARB;
    e.combine_rgb = e.combine_alpha = GL_MODULATE;
    e.src_rgb[0] = e.src_alpha[0] = GL_TEXTURE;
    e.src_rgb[1] = e.src_alpha[1] = GL_PREVIOUS_ARB;
    e.src_rgb[2] = e.src_alpha[2] = GL_CONSTANT_ARB;
    e.op_rgb[0] = e.op_rgb[1] = GL_SRC_COLOR;
    e.op_rgb[2] = GL_SRC_ALPHA;
    e.op_alpha[0] = e.op_alpha[1] = e.op_alpha[2] = GL_SRC_ALPHA;
    e.rgb_scale = e.alpha_scale = 1.0f;
    return e;
}

static void combine_words(void)
{
    dgl_texture rgba, rgb;
    dgl_texenv e;
    uint32_t w;
    memset(&rgba, 0, sizeof rgba);
    memset(&rgb, 0, sizeof rgb);
    rgba.level[0].ifc = DGL_IF_RGBA;
    rgb.level[0].ifc = DGL_IF_RGB;

    /* The defaults are MODULATE: Mesa's MODULATE RGBA words on both stages. */
    e = env_default();
    CHECK(dgl_stage_word(0, &e, &rgba, &w) && w == 0xC0600000u);
    CHECK(dgl_stage_word(1, &e, &rgba, &w) && w == 0xC3600003u);
    /* REPLACE of the texture: Mesa's REPLACE RGBA (0). */
    e.combine_rgb = e.combine_alpha = GL_REPLACE;
    CHECK(dgl_stage_word(0, &e, &rgba, &w) && w == 0x00000000u);
    /* Xash3D's overbright lightmap on unit 1: PREVIOUS x TEXTURE x2, alpha
     * from PREVIOUS: Mesa's MODULATE RGB (0x43600003) with modbright x2. */
    e = env_default();
    e.src_rgb[0] = GL_PREVIOUS_ARB;
    e.src_rgb[1] = GL_TEXTURE;
    e.rgb_scale = 2.0f;
    e.combine_alpha = GL_REPLACE;
    e.src_alpha[0] = GL_PREVIOUS_ARB;
    CHECK(dgl_stage_word(1, &e, &rgb, &w) && w == 0x43608003u);
    /* x4 is modbright 2. */
    e.rgb_scale = 4.0f;
    CHECK(dgl_stage_word(1, &e, &rgb, &w) && (w & 0x00018000u) == 0x00010000u);
    /* ADD texture + primary colour on stage 0: Mesa's GL_ADD RGBA word. */
    e = env_default();
    e.combine_rgb = GL_ADD;
    e.src_rgb[1] = GL_PRIMARY_COLOR_ARB;
    e.src_alpha[1] = GL_PRIMARY_COLOR_ARB;
    CHECK(dgl_stage_word(0, &e, &rgba, &w) && w == 0xC0420000u);
    /* SUBTRACT previous - texture: both inputs inverted (Mesa's order trick). */
    e = env_default();
    e.combine_rgb = GL_SUBTRACT_ARB;
    e.src_rgb[0] = GL_PREVIOUS_ARB;
    e.src_rgb[1] = GL_TEXTURE;
    CHECK(dgl_stage_word(1, &e, &rgba, &w) && (w & 0x007FFFFFu) == (0x00400000u | 0x40u | 0x100u | 3u));
    /* What the combiner cannot do is refused (emit.c draws MODULATE). */
    e = env_default();
    e.combine_rgb = GL_INTERPOLATE_ARB;
    CHECK(!dgl_stage_word(0, &e, &rgba, &w));
    e = env_default();
    e.src_rgb[1] = GL_CONSTANT_ARB;                /* FCOL is not programmed */
    CHECK(!dgl_stage_word(0, &e, &rgba, &w));
    e = env_default();
    e.src_rgb[1] = GL_TEXTURE;                     /* texture x texture: one ARG1 */
    CHECK(!dgl_stage_word(0, &e, &rgba, &w));
    e = env_default();
    e.combine_rgb = GL_ADD;
    e.rgb_scale = 4.0f;                            /* the adder doubles at most */
    CHECK(!dgl_stage_word(0, &e, &rgba, &w));
}

void unit_run(void)
{
    /* Vertex i at x = i, unit 1's s = 10 + i, interleaved as a game would. */
    struct { float pos[3], s1[2]; } vtx[8];
    unsigned short idx[6] = { 4, 5, 6, 6, 5, 7 };
    GLuint b[2], vb, ib;
    GLint v = 0;
    double q;                                      /* glGetIntegerv's values (get.c is card-side) */
    void *map;
    int i;
    for (i = 0; i < 8; i++) {
        vtx[i].pos[0] = (float)i; vtx[i].pos[1] = vtx[i].pos[2] = 0;
        vtx[i].s1[0] = 10.0f + i; vtx[i].s1[1] = 0;
    }
    dgl_gl_reset();
    dgl_buffers_reset();
    memset(&dgl_sink, 0, sizeof dgl_sink);
    dgl_sink.triangle = on_tri;

    glGenBuffersARB(2, b);
    vb = b[0];
    ib = b[1];
    CHECK(vb && ib && vb != ib && glIsBufferARB(vb) && !glIsBufferARB(99));
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vb);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof vtx, vtx, GL_STATIC_DRAW_ARB);
    glGetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_SIZE_ARB, &v);
    CHECK(v == (GLint)sizeof vtx);
    CHECK(dgl_buffer_query(GL_ARRAY_BUFFER_BINDING_ARB, &q) == 1 && q == vb);

    /* The pointers are offsets into the bound buffer. */
    glVertexPointer(3, GL_FLOAT, sizeof vtx[0], (const GLvoid *)0);
    glEnableClientState(GL_VERTEX_ARRAY);
    dgl_gl.client_unit = 1;                        /* glClientActiveTextureARB(GL_TEXTURE1_ARB) */
    glTexCoordPointer(2, GL_FLOAT, sizeof vtx[0], (const GLvoid *)(sizeof vtx[0].pos));
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    dgl_gl.client_unit = 0;
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);       /* the arrays keep their buffer */
    CHECK(dgl_buffer_query(GL_VERTEX_ARRAY_BUFFER_BINDING_ARB, &q) == 1 && q == vb);
    ntri = 0;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK(ntri == 1 && tri_x[0][0] == 0 && tri_x[0][2] == 2 && tri_s1[0] == 10.0f);

    /* Indices as an offset into the element buffer (the second triple). */
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, ib);
    glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof idx, idx, GL_STATIC_DRAW_ARB);
    ntri = 0;
    glDrawRangeElementsEXT(GL_TRIANGLES, 5, 7, 3, GL_UNSIGNED_SHORT, (const GLvoid *)(3 * sizeof idx[0]));
    CHECK(ntri == 1 && tri_x[0][0] == 6 && tri_x[0][1] == 5 && tri_x[0][2] == 7 && tri_s1[0] == 16.0f);
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);

    /* New data (new memory) is what the next draw reads. */
    for (i = 0; i < 8; i++) vtx[i].pos[0] = 100.0f + i;
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vb);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof vtx, vtx, GL_DYNAMIC_DRAW_ARB);
    ntri = 0;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK(ntri == 1 && tri_x[0][0] == 100.0f);

    /* Mapping hands out the memory; writes through it are drawn. */
    map = glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
    CHECK(map != NULL);
    glGetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_MAPPED_ARB, &v);
    CHECK(v == 1);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, 4, NULL, GL_STATIC_DRAW_ARB);     /* not while mapped */
    CHECK(glGetError() == GL_INVALID_OPERATION);
    ((float *)map)[0] = 42.0f;
    CHECK(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB) == GL_TRUE);
    ntri = 0;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK(ntri == 1 && tri_x[0][0] == 42.0f);

    /* Deleting a buffer unbinds it from the arrays too. */
    glDeleteBuffersARB(1, &vb);
    CHECK(!glIsBufferARB(vb));
    CHECK(dgl_buffer_query(GL_VERTEX_ARRAY_BUFFER_BINDING_ARB, &q) == 1 && q == 0);
    CHECK(dgl_buffer_query(GL_ARRAY_BUFFER_BINDING_ARB, &q) == 1 && q == 0);

    /* Binding a name nobody generated creates it (as the extension allows). */
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 77);
    CHECK(glIsBufferARB(77) && glGetError() == GL_NO_ERROR);
    glBindBufferARB(0x1234, 1);
    CHECK(glGetError() == GL_INVALID_ENUM);

    combine_words();
}
