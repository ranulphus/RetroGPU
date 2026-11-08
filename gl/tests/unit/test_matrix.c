/* test_matrix.c - matrix stacks and the helpers against hand-derived results. */
#include "unit.h"
#include "../../src/gl/gl_state.h"

static void xform(const dgl_mat4 *m, const float in[4], float out[4])
{
    int i;
    for (i = 0; i < 4; i++)
        out[i] = m->m[i] * in[0] + m->m[4 + i] * in[1] + m->m[8 + i] * in[2] + m->m[12 + i] * in[3];
}

void unit_run(void)
{
    float p[4] = { 1, 2, 3, 1 }, q[4];
    int i;
    dgl_gl_reset();
    /* Translate then rotate 90 degrees about z: GL applies the last call first. */
    glMatrixMode(GL_MODELVIEW);
    glTranslatef(10, 0, 0);
    glRotatef(90, 0, 0, 1);
    xform(&dgl_gl.mv.stack[0], p, q);
    CHECK_NEAR(q[0], 10 - 2, 1e-5);
    CHECK_NEAR(q[1], 1, 1e-5);
    CHECK_NEAR(q[2], 3, 1e-5);
    /* Push/pop keeps the saved matrix. */
    glPushMatrix();
    glScalef(2, 2, 2);
    glPopMatrix();
    xform(&dgl_gl.mv.stack[0], p, q);
    CHECK_NEAR(q[0], 8, 1e-5);
    CHECK(glGetError() == GL_NO_ERROR);
    /* Projection stack is 2 deep. */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glPushMatrix();
    CHECK(glGetError() == GL_STACK_OVERFLOW);
    glPopMatrix();
    glPopMatrix();
    CHECK(glGetError() == GL_STACK_UNDERFLOW);
    /* Ortho maps the box to [-1, 1]. */
    glLoadIdentity();
    glOrtho(0, 640, 480, 0, -1, 1);
    {
        float c[4] = { 640, 0, 0, 1 };
        xform(&dgl_gl.proj.stack[0], c, q);
        CHECK_NEAR(q[0], 1, 1e-6);
        CHECK_NEAR(q[1], 1, 1e-6);
    }
    /* Frustum: a point on the near plane's corner lands on the clip corner. */
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 1, 100);
    {
        float c[4] = { 1, 1, -1, 1 };
        xform(&dgl_gl.proj.stack[0], c, q);
        CHECK_NEAR(q[0] / q[3], 1, 1e-6);
        CHECK_NEAR(q[1] / q[3], 1, 1e-6);
        CHECK_NEAR(q[2] / q[3], -1, 1e-5);
    }
    /* The cached MVP follows both stacks. */
    {
        const dgl_mat4 *mvp = dgl_mvp();
        dgl_mat4 ref;
        dgl_mat_mul(&ref, &dgl_gl.proj.stack[0], &dgl_gl.mv.stack[0]);
        for (i = 0; i < 16; i++)
            CHECK_NEAR(mvp->m[i], ref.m[i], 1e-6);
    }
    /* Texture matrix identity tracking. */
    glMatrixMode(GL_TEXTURE);
    glScalef(2, 1, 1);
    CHECK(!dgl_gl.tex_identity);
    glLoadIdentity();
    CHECK(dgl_gl.tex_identity);
    glMatrixMode(0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM);
}
