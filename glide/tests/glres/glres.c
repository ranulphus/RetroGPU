/* glres.c - every Glide resolution through a Glide runtime (MGA-Glide's by
 * default): open the window, draw triangles, write a pattern through the
 * LFB and read the frame back (the pattern must come back exactly), swap,
 * and compare the unit tester's picture of the monitor with the frame as
 * the mode planner says it is shown (native, zoomed, scaled or top-left;
 * hal.h vbe_plan_mode). Then a write to the front buffer must appear on
 * screen too.
 *
 *   GLRES [--glide=PATH] [codes...]     codes: Glide resolution numbers
 *
 * The planner flags follow MGAGLIDE's zoom=1, scale=0 and scale=force, the
 * model's filter its scale_filter=bilinear. Run with MGAGLIDE gamma=0 so
 * the display ramp is linear. A resolution no mode can show is skipped. */
#include "hx.h"
#include "glbind.h"
#include "mga/hal.h"
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { int code, w, h; } res[] = {
    { GR_RESOLUTION_320x200, 320, 200 }, { GR_RESOLUTION_320x240, 320, 240 },
    { GR_RESOLUTION_400x256, 400, 256 }, { GR_RESOLUTION_512x384, 512, 384 },
    { GR_RESOLUTION_640x200, 640, 200 }, { GR_RESOLUTION_640x350, 640, 350 },
    { GR_RESOLUTION_640x400, 640, 400 }, { GR_RESOLUTION_640x480, 640, 480 },
    { GR_RESOLUTION_800x600, 800, 600 }, { GR_RESOLUTION_960x720, 960, 720 },
    { GR_RESOLUTION_856x480, 856, 480 }, { GR_RESOLUTION_512x256, 512, 256 },
    { GR_RESOLUTION_1024x768, 1024, 768 }, { GR_RESOLUTION_1280x1024, 1280, 1024 },
    { GR_RESOLUTION_1600x1200, 1600, 1200 }, { GR_RESOLUTION_400x300, 400, 300 },
};
#define NRES ((int)(sizeof res / sizeof res[0]))

static unsigned flags;
static int bilinear;

static void vtx(GrVertex *v, float x, float y, float r, float g, float b)
{
    memset(v, 0, sizeof *v);
    v->x = x; v->y = y; v->r = r; v->g = g; v->b = b; v->a = 255;
    v->ooz = 65535.0f; v->oow = 1.0f;
    v->tmuvtx[0].oow = 1.0f;
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, int rgb)
{
    GrVertex a, b, c;
    vtx(&a, x0, y0, (float)((rgb >> 16) & 255), (float)((rgb >> 8) & 255), (float)(rgb & 255));
    b = a; b.x = x1; b.y = y1;
    c = a; c.x = x2; c.y = y2;
    gl.grDrawTriangle(&a, &b, &c);
}

static void wait_frames(int n)
{
    long spin;
    while (n-- > 0) {
        for (spin = 0; spin < 2000000L && (inp(0x3DA) & 8); spin++)
            ;
        for (spin = 0; spin < 2000000L && !(inp(0x3DA) & 8); spin++)
            ;
    }
}

static int close565(uint16_t a, uint16_t b, int tol)
{
    int dr = ((a >> 11) & 31) - ((b >> 11) & 31), dg = ((a >> 5) & 63) - ((b >> 5) & 63), db = (a & 31) - (b & 31);
    return abs(dr) <= tol && abs(dg) <= 2 * tol && abs(db) <= tol;
}

/* The display pixel (X, Y) the planner predicts, from the w x h frame. */
static int model(const mga_mode_plan *p, const uint16_t *f, int X, int Y, uint16_t *out)
{
    int i = X - p->dx, j = Y - p->dy;
    if (p->fit == MGA_FIT_ZOOM) {
        *out = f[(Y / 2) * p->w + X / 2];
        return 1;
    }
    if (i < 0 || j < 0 || i >= p->dw || j >= p->dh) {
        *out = 0;
        return 1;
    }
    if (p->fit == MGA_FIT_NATIVE || p->fit == MGA_FIT_TOPLEFT) {
        *out = f[j * p->w + i];
        return 1;
    }
    if (bilinear) {
        /* Only interior pixels whose four taps are equal are exact. */
        int x = (int)((double)i * (p->w - 1) / (p->dw > 1 ? p->dw - 1 : 1)), y = (int)((double)j * (p->h - 1) / (p->dh > 1 ? p->dh - 1 : 1));
        int x1 = x + 1 < p->w ? x + 1 : x, y1 = y + 1 < p->h ? y + 1 : y;
        uint16_t a = f[y * p->w + x];
        if (f[y * p->w + x1] != a || f[y1 * p->w + x] != a || f[y1 * p->w + x1] != a)
            return 0;
        *out = a;
        return 1;
    }
    /* floor((pixel centre) x source/destination + 1/64), as engine_present samples */
    *out = f[(int)((((long)j * 2 + 1) * p->h * 32 + p->dh) / (64L * p->dh)) * p->w +
             (int)((((long)i * 2 + 1) * p->w * 32 + p->dw) / (64L * p->dw))];
    return 1;
}

static uint16_t screen565(const uint8_t *bgrx)
{
    return (uint16_t)(((bgrx[2] >> 3) << 11) | ((bgrx[1] >> 2) << 5) | (bgrx[0] >> 3));
}

static void one(int code, int gw, int gh)
{
    static uint16_t pat[32 * 16];
    char name[24];
    mga_mode_plan p;
    uint16_t *frame = NULL, sw = 0, sh = 0, want;
    uint8_t *bgrx = NULL;
    int w, h, x, y, lfb_bad = 0, shot_bad = 0, front_ok = -1, W, H;
    snprintf(name, sizeof name, "%dx%d", gw, gh);
    if (!gl.grSstWinOpen(0, (GrScreenResolution_t)code, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT,
                         2, 1)) {
        hx_log("HX-STAT glres %s skip the runtime cannot open it", name);
        return;
    }
    w = (int)gl.grSstScreenWidth();
    h = (int)gl.grSstScreenHeight();
    if (w != gw || h != gh || vbe_plan_mode(w, h, 16, flags, &p) != 0) {
        gl.grSstWinClose();
        hx_test(name, 0, "screen %dx%d, or no plan", w, h);
        return;
    }
    W = p.disp.width; H = p.disp.height;
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grBufferClear(0x00404040, 0, 0);
    tri(w * 0.1f, h * 0.1f, w * 0.9f, h * 0.2f, w * 0.3f, h * 0.9f, 0xFF4000);
    tri(w * 0.6f, h * 0.5f, w * 0.95f, h * 0.95f, w * 0.5f, h * 0.9f, 0x20C0FF);
    tri(0, 0, (float)w, 0, 0, 2, 0xFFFFFF);         /* the top rows and the last column: edges */
    tri((float)w - 1, 0, (float)w, (float)h, (float)w - 1, (float)h, 0x00FF00);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 32; x++)
            pat[y * 32 + x] = (uint16_t)(((x * 5 + y) & 31) << 11 | ((x ^ (y * 3)) & 63) << 5 | ((y * 7 + x) & 31));
    gl.grLfbWriteRegion(GR_BUFFER_BACKBUFFER, (FxU32)(w / 8), (FxU32)(h / 8), GR_LFB_SRC_FMT_565, 32, 16, 64, pat);
    gl.grSstIdle();
    frame = (uint16_t *)malloc((size_t)w * h * 2);
    bgrx = (uint8_t *)malloc((size_t)W * 4);
    if (!frame || !bgrx || !gl.grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, (FxU32)w, (FxU32)h, (FxU32)w * 2, frame)) {
        free(frame); free(bgrx);
        gl.grSstWinClose();
        hx_test(name, 0, "cannot read the frame back");
        return;
    }
    for (y = 0; y < 16; y++)
        for (x = 0; x < 32; x++)
            lfb_bad += frame[(h / 8 + y) * w + w / 8 + x] != pat[y * 32 + x];
    gl.grBufferSwap(1);
    wait_frames(3);
    if (hx_ut_present() && hx_ut_capture(&sw, &sh) == 0) {
        if (sw != W || sh != H)
            shot_bad = -1;
        else
            for (y = 0; y < H && shot_bad >= 0; y++) {
                if (hx_ut_read(0, y, W, 1, bgrx) != 0) { shot_bad = -2; break; }
                for (x = 0; x < W; x++)
                    if (model(&p, frame, x, y, &want) && !close565(screen565(bgrx + x * 4), want, bilinear ? 1 : 0) &&
                        shot_bad++ < 4)
                        hx_log("HX-STAT glres %s screen (%d,%d) got %04x want %04x", name, x, y,
                               screen565(bgrx + x * 4), want);
            }
        /* A write to the front buffer is shown too. */
        {
            static uint16_t blk[8 * 8];
            int cx = w / 2, cy = h / 2, X, Y;
            for (x = 0; x < 64; x++)
                blk[x] = 0xF81F;
            gl.grLfbWriteRegion(GR_BUFFER_FRONTBUFFER, (FxU32)cx, (FxU32)cy, GR_LFB_SRC_FMT_565, 8, 8, 16, blk);
            gl.grSstIdle();
            wait_frames(3);
            /* Where the block's centre lands on the monitor. */
            if (p.fit == MGA_FIT_ZOOM) { X = (cx + 4) * 2; Y = (cy + 4) * 2; }
            else if (p.fit == MGA_FIT_NATIVE || p.fit == MGA_FIT_TOPLEFT) { X = cx + 4; Y = cy + 4; }
            else { X = p.dx + (int)((cx + 4.5) * p.dw / w); Y = p.dy + (int)((cy + 4.5) * p.dh / h); }
            front_ok = hx_ut_capture(&sw, &sh) == 0 && hx_ut_read(X, Y, 1, 1, bgrx) == 0 &&
                       screen565(bgrx) == 0xF81F;
        }
    }
    hx_log("HX-STAT glres %s fit=%s display=%dx%d rect=%d,%d,%dx%d lfb_bad=%d screen_bad=%d front=%d", name,
           mga_fit_name(p.fit), W, H, p.dx, p.dy, p.dw, p.dh, lfb_bad, shot_bad, front_ok);
    hx_test(name, lfb_bad == 0 && shot_bad == 0 && front_ok != 0, "%s in %dx%d: %d LFB, %d screen mismatches%s",
            mga_fit_name(p.fit), W, H, lfb_bad, shot_bad, front_ok == 0 ? ", front write not shown" : "");
    free(frame); free(bgrx);
    gl.grSstWinClose();
}

int main(int argc, char **argv)
{
    char err[128] = "";
    int missing, i, j, n = 0, codes[NRES];
    const char *path, *e;
    le_module *m;
    for (i = 1; i < argc; i++)
        if (argv[i][0] != '-') {
            if (n < NRES)
                codes[n++] = atoi(argv[i]);
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof argv[0]);
            argc--; i--;
        }
    hx_init(argc, argv, "glres");
    e = getenv("MGAGLIDE");
    if (e) {
        if (strstr(e, "zoom=1")) flags |= MGA_PLAN_ZOOM;
        if (strstr(e, "scale=0")) flags |= MGA_PLAN_TOPLEFT;
        if (strstr(e, "scale=force")) flags |= MGA_PLAN_FORCE;
        bilinear = strstr(e, "scale_filter=bilinear") != NULL;
    }
    path = hx_args.glide ? hx_args.glide : "C:\\TEST\\GLIDE2X.OVL";
    m = glbind_load(path, &missing, err, sizeof err);
    if (!m) {
        hx_test("load", 0, "%s: %s", path, err);
        hx_done(HX_INIT_FAILED);
    }
    gl.grGlideInit();
    {
        GrHwConfiguration hw;
        if (!gl.grSstQueryHardware(&hw) || hw.num_sst < 1) {
            hx_test("open", 0, "no hardware");
            hx_done(HX_INIT_FAILED);
        }
        gl.grSstSelect(0);
    }
    for (i = 0; i < NRES; i++) {
        int want = !n;
        for (j = 0; j < n; j++)
            want |= codes[j] == res[i].code;
        if (want)
            one(res[i].code, res[i].w, res[i].h);
    }
    gl.grGlideShutdown();
    hx_done(0);
    return 0;
}
