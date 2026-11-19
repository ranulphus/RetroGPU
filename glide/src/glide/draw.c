/* draw.c - the drawing entry points: triangles, polygons, points and lines, each to the back end's triangle
 * (glide/src/backend; the Matrox one: backend/mga/draw.c). */
#include "glide/mg.h"
#include "backend/backend.h"

void mg_draw_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    mg_be->tri(a, b, c);
}

GR_ENTRY(void, grDrawTriangle, (const GrVertex *a, const GrVertex *b, const GrVertex *c))
{
    int po;
    FPU_ENTER();
    po = PROF_SWITCH(PROF_G_VTX);
    mg_draw_tri(a, b, c);
    PROF_BACK(po);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawPolygonVertexList, (int nverts, const GrVertex vlist[]))
{
    int i, po;
    FPU_ENTER();
    po = PROF_SWITCH(PROF_G_VTX);
    for (i = 2; i < nverts; i++)
        mg_draw_tri(&vlist[0], &vlist[i - 1], &vlist[i]);
    PROF_BACK(po);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawPolygon, (int nverts, const int ilist[], const GrVertex vlist[]))
{
    int i, po;
    FPU_ENTER();
    po = PROF_SWITCH(PROF_G_VTX);
    for (i = 2; i < nverts; i++)
        mg_draw_tri(&vlist[ilist[0]], &vlist[ilist[i - 1]], &vlist[ilist[i]]);
    PROF_BACK(po);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawPlanarPolygonVertexList, (int nverts, const GrVertex vlist[]))
{
    grDrawPolygonVertexList(nverts, vlist);
}

GR_ENTRY(void, grDrawPlanarPolygon, (int nverts, const int ilist[], const GrVertex vlist[]))
{
    grDrawPolygon(nverts, ilist, vlist);
}

/* Points and lines as small quads (PRD §7). */
static void quad(const GrVertex *p, float x0, float y0, float x1, float y1)
{
    GrVertex q[4];
    int i;
    for (i = 0; i < 4; i++) q[i] = *p;
    q[0].x = x0; q[0].y = y0; q[1].x = x1; q[1].y = y0;
    q[2].x = x1; q[2].y = y1; q[3].x = x0; q[3].y = y1;
    mg_draw_tri(&q[0], &q[1], &q[2]);
    mg_draw_tri(&q[0], &q[2], &q[3]);
}

GR_ENTRY(void, grDrawPoint, (const GrVertex *pt))
{
    float x, y;
    int po;
    FPU_ENTER();
    po = PROF_SWITCH(PROF_G_VTX);
    x = (float)mga_floor(pt->x);
    y = (float)mga_floor(pt->y);
    quad(pt, x, y, x + 1.0f, y + 1.0f);
    PROF_BACK(po);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawLine, (const GrVertex *v1, const GrVertex *v2))
{
    GrVertex q[4];
    float dx, dy;
    int i, po;
    FPU_ENTER();
    po = PROF_SWITCH(PROF_G_VTX);
    dx = v2->x - v1->x; dy = v2->y - v1->y;
    q[0] = *v1; q[1] = *v2; q[2] = *v2; q[3] = *v1;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        q[0].y -= 0.5f; q[1].y -= 0.5f; q[2].y += 0.5f; q[3].y += 0.5f;
    } else {
        q[0].x -= 0.5f; q[1].x -= 0.5f; q[2].x += 0.5f; q[3].x += 0.5f;
    }
    for (i = 0; i < 1; i++) {
        mg_draw_tri(&q[0], &q[1], &q[2]);
        mg_draw_tri(&q[0], &q[2], &q[3]);
    }
    PROF_BACK(po);
    FPU_LEAVE();
}

/* Antialiased primitives degrade to their plain forms (logged once). */
GR_ENTRY(void, grAADrawPoint, (const GrVertex *pt)) { mg_stub_hit(MGA_API_grAADrawPoint); grDrawPoint(pt); }
GR_ENTRY(void, grAADrawLine, (const GrVertex *v1, const GrVertex *v2)) { mg_stub_hit(MGA_API_grAADrawLine); grDrawLine(v1, v2); }
GR_ENTRY(void, grAADrawTriangle, (const GrVertex *a, const GrVertex *b, const GrVertex *c,
                                  FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias))
{
    MGA_UNUSED(ab_antialias); MGA_UNUSED(bc_antialias); MGA_UNUSED(ca_antialias);
    mg_stub_hit(MGA_API_grAADrawTriangle);
    grDrawTriangle(a, b, c);
}
GR_ENTRY(void, grAADrawPolygon, (int nverts, const int ilist[], const GrVertex vlist[]))
{
    mg_stub_hit(MGA_API_grAADrawPolygon);
    grDrawPolygon(nverts, ilist, vlist);
}
GR_ENTRY(void, grAADrawPolygonVertexList, (int nverts, const GrVertex vlist[]))
{
    mg_stub_hit(MGA_API_grAADrawPolygonVertexList);
    grDrawPolygonVertexList(nverts, vlist);
}
