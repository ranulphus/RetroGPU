/* tri - M2 rung 2: triangles through the HAL's setup_triangle, below the
 * GL layer.
 *   (default)  clear + one stationary flat-shaded triangle per frame, flipped
 *              for --frames N (60 s at 60 Hz = 3600 is the M2 silicon exit)
 *   --xor      a 64-triangle fan drawn with the XOR raster op onto black:
 *              a pixel hit twice or missed reads back 0, so every sampled
 *              point inside the fan must hold the colour (watertight edges) */
#include "hx.h"
#include "../../src/dgl/dgl.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include <math.h>
#include <string.h>

static void vtx(mga_svtx *v, double x, double y)
{
    memset(v, 0, sizeof *v);
    v->X16 = (int32_t)lrint(x * 16.0);
    v->Y16 = (int32_t)lrint(y * 16.0);
    v->r = 255; v->g = 200; v->b = 0; v->a = 255; v->fog = 255;
}

static int xor_fan(void)
{
    mga_tri_ctx ctx;
    mga_svtx c, a, b;
    const double cx = 320.37, cy = 240.61, r = 200.0;
    const int n = 64;
    volatile uint16_t *fb;
    int i, bad = 0, samples = 0, x, y;
    engine_fill(0, 0, 640, 480, 0);
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_RSTR | DWG_SOLID | DWG_ZMODE_NOZCMP | DWG_BOP(0x6);
    ctx.clip_y0 = 0; ctx.clip_y1 = 480;
    fifo_reserve(1);
    MGA_WR32(MGAREG_FCOL, 0xFFE0FFE0u);
    vtx(&c, cx, cy);
    for (i = 0; i < n; i++) {
        double a0 = 2 * M_PI * i / n, a1 = 2 * M_PI * (i + 1) / n;
        vtx(&a, cx + r * cos(a0), cy + r * sin(a0));
        vtx(&b, cx + r * cos(a1), cy + r * sin(a1));
        setup_triangle(&c, &a, &b, &ctx);
    }
    engine_sync(500000);
    fb = (volatile uint16_t *)(mga_fb + (dgl_ctx.front_is_a ? dgl_ctx.back_off : dgl_ctx.front_off));
    for (y = 60; y < 420; y++)
        for (x = 140; x < 500; x++) {
            double d = hypot(x + 0.5 - cx, y + 0.5 - cy);
            if (d > r * cos(M_PI / n) - 2.0)
                continue;                /* stay inside the polygon */
            samples++;
            bad += fb[y * dgl_ctx.pitch_px + x] != 0xFFE0;
        }
    hx_test("xor-fan", bad == 0, "interior pixels=%d wrong=%d (double hits or holes)", samples, bad);
    return bad;
}

int main(int argc, char **argv)
{
    mga_tri_ctx ctx;
    mga_svtx v[3];
    int f, xor_mode = 0, i;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--xor")) {
            xor_mode = 1;
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof *argv);
            argc--; i--;
        }
    hx_init(argc, argv, "tri");
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    if (xor_mode) {
        xor_fan();
        dglSwapBuffers();
        hx_snap_screen("tri_xor");
        dglShutdown();
        hx_done(0);
    }
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
    ctx.flags = MGA_S_COLOR;
    ctx.clip_y0 = 0; ctx.clip_y1 = 480;
    vtx(&v[0], 120, 80); vtx(&v[1], 520, 140); vtx(&v[2], 260, 420);
    for (f = 0; f < hx_args.frames; f++) {
        engine_fill(0, 0, 640, 480, 0x001F);
        setup_triangle(&v[0], &v[1], &v[2], &ctx);
        dglSwapBuffers();
    }
    hx_test("engine", engine_resets == 0 && engine_timeouts == 0, "frames=%d resets=%lu timeouts=%lu", f,
            (unsigned long)engine_resets, (unsigned long)engine_timeouts);
    hx_snap_screen("tri");
    dglShutdown();
    hx_done(0);
    return 0;
}
