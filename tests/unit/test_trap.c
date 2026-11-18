/* Triangle setup against the reference rasteriser: coverage follows the
 * top-left rule exactly, shared edges are watertight, and Gouraud colours
 * and Z follow the plane at pixel centres. */
#include "unit.h"
#include "refrast.h"
#include "mga/hal.h"
#include "mga/setup.h"
#include "mga/regs_mga.h"
#include "mga/mmio.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 256
#define H 256
#define PITCH 256

mga_chip mga;
volatile uint8_t *mga_mmio, *mga_fb;
int mga_fifo_free;                  /* stays 0: every reservation reaches the stub */
void fifo_reserve(int n) { (void)n; }
unsigned setup_valid;               /* (engine.c's: no shadow holds at the start) */
int setup_stale, setup_wrote_texsize;   /* (engine.c's) */
uint32_t mga_warp_dwgctl;          /* (engine.c's) */
static int stub_cx0 = 0, stub_cx1 = W;
void engine_get_clip(int *x0, int *y0, int *x1, int *y1)  /* (engine.c's: the clip test_empty_textrap sets) */
{
    *x0 = stub_cx0; *y0 = 0; *x1 = stub_cx1; *y1 = H;
}

/* Exact coverage test: centre (16x+8, 16y+8) against edge functions with
 * the top-left rule (edges are half-open towards bottom-right). */
static int inside(const mga_svtx *v[3], int x, int y)
{
    int64_t px = 16 * x + 8, py = 16 * y + 8;
    int i;
    int64_t area = (int64_t)(v[1]->X16 - v[0]->X16) * (v[2]->Y16 - v[0]->Y16) -
                   (int64_t)(v[2]->X16 - v[0]->X16) * (v[1]->Y16 - v[0]->Y16);
    for (i = 0; i < 3; i++) {
        const mga_svtx *a = v[i], *b = v[(i + 1) % 3];
        int64_t ex = b->X16 - a->X16, ey = b->Y16 - a->Y16;
        int64_t e = ex * (py - a->Y16) - ey * (px - a->X16);
        if (area < 0) { e = -e; ex = -ex; ey = -ey; }
        /* Inside when e > 0; on the edge include only top or left edges.
         * With y down and positive orientation, a "top" edge has ey == 0
         * and ex > 0, a "left" edge has ey < 0. */
        if (e < 0) return 0;
        if (e == 0 && !((ey == 0 && ex > 0) || ey < 0)) return 0;
    }
    return 1;
}

static void fill_bg(void) { memset(rr->vram, 0, (size_t)PITCH * H * 2 * 2); }

static mga_tri_ctx ctx_flat(void)
{
    mga_tri_ctx c;
    memset(&c, 0, sizeof c);
    c.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_I;
    c.clip_y0 = 0; c.clip_y1 = H;
    return c;
}

static void setup_regs(void)
{
    mga_host_wr32(MGAREG_PITCH, PITCH);
    mga_host_wr32(MGAREG_YDSTORG, 0);
    mga_host_wr32(MGAREG_MACCESS, MACCESS_PW16 | MACCESS_NODITHER);
    mga_host_wr32(MGAREG_CXBNDRY, ((uint32_t)(W - 1) << 16));
    mga_host_wr32(MGAREG_YTOP, 0);
    mga_host_wr32(MGAREG_YBOT, (H - 1) * PITCH);
}

static void rnd_vtx(mga_svtx *v)
{
    memset(v, 0, sizeof *v);
    v->X16 = rand() % (W * 16);
    v->Y16 = rand() % (H * 16);
    v->r = 255; v->g = 255; v->b = 255;
}

static void test_coverage(void)
{
    int n, x, y, bad = 0, drawn = 0;
    mga_tri_ctx c = ctx_flat();
    for (n = 0; n < 400; n++) {
        mga_svtx a, b, cc;
        const mga_svtx *v[3];
        rnd_vtx(&a); rnd_vtx(&b); rnd_vtx(&cc);
        if (n % 4 == 0) { b.Y16 = a.Y16; }                     /* flat top/bottom */
        if (n % 7 == 0) { a.X16 &= ~15; a.Y16 = (a.Y16 & ~15) | 8; }   /* on centres */
        v[0] = &a; v[1] = &b; v[2] = &cc;
        fill_bg();
        setup_triangle(&a, &b, &cc, &c);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int want = inside(v, x, y);
                int got = refrast_px16(x, y, PITCH, 0) != 0;
                drawn += got;
                if (want != got && bad++ < 5)
                    fprintf(stderr, "tri %d: pixel %d,%d want %d got %d  (%d,%d)(%d,%d)(%d,%d)\n", n, x, y,
                            want, got, a.X16, a.Y16, b.X16, b.Y16, cc.X16, cc.Y16);
            }
    }
    CHECK_EQ(bad, 0);
    CHECK(drawn > 1000);
}

static void test_watertight(void)
{
    /* A fan of triangles around a centre covers every pixel of the
     * enclosing polygon exactly once (counted via additive colour). */
    int n, x, y, twice = 0, i;
    static uint8_t count[W * H];
    mga_tri_ctx c = ctx_flat();
    for (n = 0; n < 50; n++) {
        mga_svtx ctr, ring[9];
        memset(count, 0, sizeof count);
        memset(&ctr, 0, sizeof ctr);
        ctr.X16 = 128 * 16 + rand() % 64; ctr.Y16 = 128 * 16 + rand() % 64;
        ctr.r = ctr.g = ctr.b = 255;
        for (i = 0; i < 8; i++) {
            double ang = i * 3.14159265 / 4 + (rand() % 100) / 400.0;
            memset(&ring[i], 0, sizeof ring[i]);
            ring[i].X16 = (int32_t)(ctr.X16 + 1500 * cos(ang));
            ring[i].Y16 = (int32_t)(ctr.Y16 + 1500 * sin(ang));
            ring[i].r = ring[i].g = ring[i].b = 255;
        }
        ring[8] = ring[0];
        for (i = 0; i < 8; i++) {
            fill_bg();
            setup_triangle(&ctr, &ring[i], &ring[i + 1], &c);
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (refrast_px16(x, y, PITCH, 0))
                        count[y * W + x]++;
        }
        for (i = 0; i < W * H; i++)
            twice += count[i] > 1;
    }
    CHECK_EQ(twice, 0);
}

static void test_gouraud(void)
{
    /* Colours at pixel centres match the plane within one 8-bit step. */
    mga_tri_ctx c = ctx_flat();
    mga_svtx a, b, d;
    int x, y, worst = 0;
    const mga_svtx *v[3];
    c.flags = MGA_S_COLOR;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&d, 0, sizeof d);
    a.X16 = 10 * 16; a.Y16 = 10 * 16; a.r = 255; a.g = 0; a.b = 0;
    b.X16 = 240 * 16; b.Y16 = 30 * 16; b.r = 0; b.g = 255; b.b = 0;
    d.X16 = 60 * 16; d.Y16 = 230 * 16; d.r = 0; d.g = 0; d.b = 255;
    v[0] = &a; v[1] = &b; v[2] = &d;
    fill_bg();
    setup_triangle(&a, &b, &d, &c);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint16_t p;
            double l0, l1, l2, det, px = x + 0.5, py = y + 0.5, r;
            if (!inside(v, x, y)) continue;
            p = refrast_px16(x, y, PITCH, 0);
            det = (b.Y16 / 16.0 - d.Y16 / 16.0) * (a.X16 / 16.0 - d.X16 / 16.0) +
                  (d.X16 / 16.0 - b.X16 / 16.0) * (a.Y16 / 16.0 - d.Y16 / 16.0);
            l0 = ((b.Y16 / 16.0 - d.Y16 / 16.0) * (px - d.X16 / 16.0) + (d.X16 / 16.0 - b.X16 / 16.0) * (py - d.Y16 / 16.0)) / det;
            l1 = ((d.Y16 / 16.0 - a.Y16 / 16.0) * (px - d.X16 / 16.0) + (a.X16 / 16.0 - d.X16 / 16.0) * (py - d.Y16 / 16.0)) / det;
            l2 = 1 - l0 - l1;
            (void)l2;
            r = 255 * l0;
            {
                int got = (p >> 11) << 3, want = (int)r & ~7;
                int diff = abs(got - want);
                if (diff > worst) worst = diff;
            }
        }
    CHECK(worst <= 8);
}

static void test_z(void)
{
    /* Z interpolates across a triangle and the Z buffer receives it. */
    mga_tri_ctx c = ctx_flat();
    mga_svtx a, b, d;
    uint16_t *zb;
    c.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_ZI | DWG_ZMODE_NOZCMP;
    c.flags = MGA_S_Z;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&d, 0, sizeof d);
    a.X16 = 0; a.Y16 = 0; a.z = 0;
    b.X16 = 256 * 16; b.Y16 = 0; b.z = 65535;
    d.X16 = 0; d.Y16 = 256 * 16; d.z = 0;
    mga_host_wr32(MGAREG_ZORG, 1u << 20);
    fill_bg();
    memset(rr->vram + (1u << 20), 0xFF, PITCH * H * 2);
    setup_triangle(&a, &b, &d, &c);
    zb = (uint16_t *)(rr->vram + (1u << 20));
    /* z(x) = 65535 * (x + 0.5) / 256 at row 0 */
    CHECK(abs((int)zb[100] - (int)(65535.0 * 100.5 / 256)) <= 1);
    CHECK(abs((int)zb[10 * PITCH + 50] - (int)(65535.0 * 50.5 / 256)) <= 1);
}

/* Voodoo-mode columns on each row equal the Voodoo's own evaluation. */
static int64_t fl16(int64_t v) { return v >= 0 ? v >> 16 : -((-v + 65535) >> 16); }
static void voodoo_span(const mga_svtx *v[3], int y, int *xl, int *xr)
{
    int32_t ry = 16 * y + 8;
    int64_t dAC = (((int64_t)v[2]->X16 - v[0]->X16) << 16) / (v[2]->Y16 - v[0]->Y16);
    int64_t x = ((int64_t)v[0]->X16 << 12) + ((dAC * (ry - v[0]->Y16)) >> 4), x2;
    if (ry < v[1]->Y16) {
        int64_t d = (((int64_t)v[1]->X16 - v[0]->X16) << 16) / (v[1]->Y16 - v[0]->Y16);
        x2 = ((int64_t)v[0]->X16 << 12) + ((d * (ry - v[0]->Y16)) >> 4);
    } else {
        int64_t d = (((int64_t)v[2]->X16 - v[1]->X16) << 16) / (v[2]->Y16 - v[1]->Y16);
        x2 = ((int64_t)v[1]->X16 << 12) + ((d * (ry - v[1]->Y16)) >> 4);
    }
    if (x > x2) { int64_t t = x; x = x2; x2 = t; }
    *xl = (int)fl16(x + 0x7000);
    *xr = (int)fl16(x2 + 0x7000);
}

static void test_voodoo_edges(void)
{
    int n, bad = 0;
    mga_tri_ctx c = ctx_flat();
    c.flags = MGA_S_VOODOO_EDGES;
    for (n = 0; n < 300; n++) {
        mga_svtx a, b, cc, *t;
        const mga_svtx *v[3];
        int x, y;
        rnd_vtx(&a); rnd_vtx(&b); rnd_vtx(&cc);
        /* sort by y for the reference formula */
        if (b.Y16 < a.Y16) { mga_svtx tt = a; a = b; b = tt; }
        if (cc.Y16 < b.Y16) { mga_svtx tt = b; b = cc; cc = tt; }
        if (b.Y16 < a.Y16) { mga_svtx tt = a; a = b; b = tt; }
        if (a.Y16 == b.Y16 || b.Y16 == cc.Y16) continue;
        (void)t;
        v[0] = &a; v[1] = &b; v[2] = &cc;
        fill_bg();
        setup_triangle(&a, &b, &cc, &c);
        for (y = (a.Y16 + 7) >> 4; y < (cc.Y16 + 7) >> 4 && y < H; y++) {
            int xl, xr;
            voodoo_span(v, y, &xl, &xr);
            for (x = 0; x < W; x++) {
                int want = x >= xl && x < xr;
                int got = refrast_px16(x, y, PITCH, 0) != 0;
                if (want != got && bad++ < 5)
                    fprintf(stderr, "voodoo tri %d: %d,%d want %d got %d\n", n, x, y, want, got);
            }
        }
    }
    CHECK_EQ(bad, 0);
}

/* mga.textrap_empty_stall (the real G100 stalls on a textured trapezoid
 * that draws nothing, clipped on the left): a TEXTURE_TRAP piece is left
 * out exactly when it covers no pixel inside the engine's clip columns.
 * Triangles across and beyond the clip's left and right edges, slivers
 * between pixel centres, both edge rules. */
static void test_empty_textrap(void)
{
    int n, bad = 0, kept = 0, culled = 0, cases = 0;
    mga_tri_ctx c = ctx_flat();
    c.dwgctl = DWG_OPCOD_TEXTURE_TRAP | DWG_ATYPE_I;
    c.tex_tw = c.tex_th = 3;
    mga.textrap_empty_stall = 1;
    stub_cx0 = 40; stub_cx1 = 200;
    for (n = 0; n < 4000; n++) {
        mga_svtx p[3];
        const mga_svtx *v[3], *t;
        int i, part, y, x, want[2] = { 0, 0 }, rows[3], voodoo = n % 5 == 4;
        for (i = 0; i < 3; i++) {
            rnd_vtx(&p[i]);
            p[i].q = 1;
            p[i].X16 = rand() % (3 * W * 16) - W * 16;              /* -W to 2W */
            if (n % 3 == 2)
                p[i].X16 = stub_cx0 * 16 + rand() % (48 * 16) - 40 * 16;   /* about the left edge */
        }
        if (n % 4 == 1) {                                         /* a sliver: x within 3/4 pixel */
            p[1].X16 = p[0].X16 + rand() % 12;
            p[2].X16 = p[0].X16 + rand() % 12;
        }
        if ((int64_t)(p[1].X16 - p[0].X16) * (p[2].Y16 - p[0].Y16) ==
            (int64_t)(p[2].X16 - p[0].X16) * (p[1].Y16 - p[0].Y16))
            continue;                                             /* (no area: the setup drops it first) */
        v[0] = &p[0]; v[1] = &p[1]; v[2] = &p[2];
#define LESS(a, b) ((a)->Y16 < (b)->Y16 || ((a)->Y16 == (b)->Y16 && (a)->X16 < (b)->X16))
        if (LESS(v[1], v[0])) { t = v[0]; v[0] = v[1]; v[1] = t; }
        if (LESS(v[2], v[1])) { t = v[1]; v[1] = v[2]; v[2] = t; }
        if (LESS(v[1], v[0])) { t = v[0]; v[0] = v[1]; v[1] = t; }
#undef LESS
        if (voodoo && (v[0]->Y16 == v[1]->Y16 || v[1]->Y16 == v[2]->Y16))
            continue;                                             /* (voodoo_span's formula wants both) */
        c.flags = MGA_S_TEX | MGA_S_AFFINE | (voodoo ? MGA_S_VOODOO_EDGES : 0);
        for (i = 0; i < 3; i++)
            rows[i] = (v[i]->Y16 + 7) >> 4;
        for (part = 0; part < 2; part++)
            for (y = rows[part] < 0 ? 0 : rows[part]; y < rows[part + 1] && y < H && !want[part]; y++) {
                int xl = 0, xr = 0;
                if (voodoo)
                    voodoo_span(v, y, &xl, &xr);
                for (x = stub_cx0; x < stub_cx1 && !want[part]; x++)
                    want[part] = voodoo ? x >= xl && x < xr : inside(v, x, y);
            }
        memset(&setup_stats, 0, sizeof setup_stats);
        setup_triangle(&p[0], &p[1], &p[2], &c);
        cases++;
        kept += (int)setup_stats.traps;
        culled += (int)setup_stats.culled_empty;
        if ((int)setup_stats.traps != want[0] + want[1] && bad++ < 5)
            fprintf(stderr, "textrap %d%s: %u pieces drawn, want %d+%d  (%d,%d)(%d,%d)(%d,%d)\n", n,
                    voodoo ? " voodoo" : "", setup_stats.traps, want[0], want[1], p[0].X16, p[0].Y16, p[1].X16,
                    p[1].Y16, p[2].X16, p[2].Y16);
    }
    mga.textrap_empty_stall = 0;
    stub_cx0 = 0; stub_cx1 = W;
    printf("empty textrap: %d triangles, %d pieces drawn, %d left out\n", cases, kept, culled);
    CHECK_EQ(bad, 0);
    CHECK(kept > 1000);
    CHECK(culled > 1000);
}

int unit_main(void)
{
    srand(1234);
    refrast_init();
    setup_regs();
    test_coverage();
    test_watertight();
    test_gouraud();
    test_z();
    test_voodoo_edges();
    test_empty_textrap();
    printf("setup: tris=%u traps=%u empty=%u\n", setup_stats.tris, setup_stats.traps, setup_stats.culled_empty);
    return 0;
}
