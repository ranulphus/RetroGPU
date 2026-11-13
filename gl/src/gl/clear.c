/* clear.c - glClear, glFlush and glFinish (PRD §8.4): GL's entry points; the
 * clear itself is the drawing back end's, the scaled present and the sync
 * the device's (docs/b1.md). */
#include "gl_state.h"
#include "../dgl/dgl.h"
#include "../backend/backend.h"

void APIENTRY glClear(GLbitfield mask)
{
    if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | 0x200u /* accum */)) {
        dgl_gl_error(GL_INVALID_VALUE);
        return;
    }
    if (!dgl_ctx.active)
        return;
    dgl_be->clear(mask);
}

/* In a scaled mode a single-buffered program, or one drawing to GL_FRONT,
 * sees its picture once it is scaled onto the display (the device's present
 * does nothing in other modes). */
void APIENTRY glFlush(void)
{
    if (dgl_ctx.active && (!dgl_ctx.double_buffer || dgl_ctx.draw_front))
        dgl_present_front();
}

void APIENTRY glFinish(void)
{
    if (!dgl_ctx.active)
        return;
    if (!((!dgl_ctx.double_buffer || dgl_ctx.draw_front) && dgl_present_front()))
        dgl_sync();
}
