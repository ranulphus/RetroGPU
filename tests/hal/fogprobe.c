/* fogprobe.c - FOGPROBE: which part of fog a real G100 takes (DOS-GL's conformance t10 and t13, both with GL fog,
 * hung bench-g400's PC with a real G100 on 2026-11-18; 86Box's G100 draws them). One step a run, so that a hang
 * names its step; in rising order of risk:
 *
 *   FOGPROBE base | fogcol | fogregs | fogen | fogall | fogtex
 *
 *   base     a Gouraud TRAP triangle (the control: conformance's first tests pass on the real card)
 *   fogcol   FOGCOL written first
 *   fogregs  the triangle with MGA_S_FOG (FOGSTART, FOGXINC, FOGYINC written), MACCESS's fogen off
 *   fogen    MACCESS fogen on, the triangle without MGA_S_FOG
 *   fogall   both, a TRAP triangle
 *   fogtex   both, a TEXTURE_TRAP triangle on an 8x8 white texture (DOS-GL's way: the G100 fogs only textured)
 *   persp    a perspective-correct TEXTURE_TRAP triangle (q 1, 1/2, 1/4), no fog (as t07, which passes)
 *   fogpersp both and perspective-correct (as t10's and t13's draws, which hung)
 *   t10tri   t10's first five triangles as DOS-GL logged them on the real G100 before it hung (DGL_TRILOG):
 *            fogged, white-textured, perspective, reaching off the screen's left edge, s = t = 0
 *   t10on    the same moved 400 pixels right (all on the screen)
 *   t10nofog the same as t10tri without fog (no MGA_S_FOG, fogen off): the real G100's engine stalled (sync
 *            timed out, the HAL reset it; with fog the PC hung)
 *   t10only1 t10's triangle 1 alone, no fog: wholly left of the screen (x -320 to -27)
 *   t10only3 t10's triangle 3 alone, no fog: across the left edge (x -27 to 88)
 *   t10only5 t10's triangle 5 alone, no fog: most rows wholly left of the screen, its tip across the edge
 *
 *   tri X0 Y0 X1 Y1 X2 Y2  any triangle (pixels), no fog, t10's colour and q (perspective)
 *   replay FILE  DOS-GL's triangles from a DGL_TRILOG log (DGL-TRI lines: DWGCTL, flags, the texture's size, X16/Y16,
 *            z, fog, q, s, t), on the 8x8 white texture, without fog unless `fog`; `each` syncs after every triangle
 *            and names any that stalls (HX-STAT fogprobe stall), `every N` after every N; a 16-bit depth buffer
 *            at 1 MB, cleared to the far plane, when a triangle tests or writes depth
 *
 * Options after the step (and tri's six numbers): `notex` draws untextured (TRAP); `affine` maps the texture
 * affinely; `nocull` sends textured trapezoids that draw nothing, as the HAL did before mga.textrap_empty_stall
 * (on a real G100 a stall; on 86Box's, the textrap-empty hazard of local patch 0132). The screen's columns are the
 * engine's clip (0 to 640), which the setup reads.
 *
 * HX-STAT fogprobe lines: STATUS before and after, the engine's sync result, pixels inside and outside. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned long n, dw, fl;
    int tw, th;
    long X[3], Y[3];
    float z[3], f[3], q[3], s[3], t[3];
} rtri;

/* DGL-TRI lines (gl/src/backend/mga/emit.c, DGL_TRILOG=1) from FILE. */
static rtri *read_tris(const char *file, int *count)
{
    FILE *fp = fopen(file, "r");
    char line[512];
    rtri *r = NULL, t;
    int n = 0, cap = 0;
    if (!fp)
        return NULL;
    while (fgets(line, sizeof line, fp)) {
        const char *p = strstr(line, "DGL-TRI ");
        if (!p || sscanf(p, "DGL-TRI %lu dw=%lx fl=%lx tw=%d th=%d %ld,%ld %ld,%ld %ld,%ld z=%f,%f,%f f=%f,%f,%f "
                         "q=%f,%f,%f s=%f,%f,%f t=%f,%f,%f", &t.n, &t.dw, &t.fl, &t.tw, &t.th, &t.X[0], &t.Y[0],
                         &t.X[1], &t.Y[1], &t.X[2], &t.Y[2], &t.z[0], &t.z[1], &t.z[2], &t.f[0], &t.f[1], &t.f[2],
                         &t.q[0], &t.q[1], &t.q[2], &t.s[0], &t.s[1], &t.s[2], &t.t[0], &t.t[1], &t.t[2]) != 26)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 256;
            r = (rtri *)realloc(r, (size_t)cap * sizeof *r);
            if (!r)
                break;
        }
        r[n++] = t;
    }
    fclose(fp);
    *count = n;
    return r;
}

int main(int argc, char **argv)
{
    const char *step = argc > 1 ? argv[1] : "base";
    mga_vbe_mode m;
    mga_target t;
    mga_svtx v[3];
    mga_tri_ctx ctx;
    int pitch = 0, i, ok, sync, fog = 0, fogen = 0, tex = 0, persp = 0, t10 = 0, t10dx = 0, t10first = 0, t10n = 5;
    int custom = 0, opt = 2, notex = 0, affine = 0, replay = 0, nrt = 0, every = 0, stalls = 0, rfog = 0;
    rtri *rt = NULL;
    float cv[6] = { 0, 0, 0, 0, 0, 0 };
    uint16_t inside, outside;
    long drawn = 0;
    int x, y;
    uint32_t st0, st1;
    volatile uint16_t *fb;
    hx_init(1, argv, "fogprobe");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        hx_test("init", 0, "no Matrox card");
        hx_done(HX_INIT_FAILED);
    }
    if (!strcmp(step, "fogregs")) fog = 1;
    else if (!strcmp(step, "fogen")) fogen = 1;
    else if (!strcmp(step, "fogall")) fog = fogen = 1;
    else if (!strcmp(step, "fogtex")) fog = fogen = tex = 1;
    else if (!strcmp(step, "persp")) tex = persp = 1;
    else if (!strcmp(step, "fogpersp")) fog = fogen = tex = persp = 1;
    else if (!strcmp(step, "t10tri")) fog = fogen = tex = persp = t10 = 1;
    else if (!strcmp(step, "t10on")) fog = fogen = tex = persp = t10 = 1, t10dx = 400 * 16;
    else if (!strcmp(step, "t10nofog")) tex = persp = t10 = 1;
    else if (!strcmp(step, "t10only1")) tex = persp = t10 = 1, t10first = 0, t10n = 1;
    else if (!strcmp(step, "t10only3")) tex = persp = t10 = 1, t10first = 2, t10n = 1;
    else if (!strcmp(step, "t10only5")) tex = persp = t10 = 1, t10first = 4, t10n = 1;
    else if (!strcmp(step, "replay") && argc >= 3) {
        rt = read_tris(argv[2], &nrt);
        if (!rt || !nrt) {
            hx_test("replay", 0, "no DGL-TRI lines in %s", argv[2]);
            hx_done(HX_BAD_ARGS);
        }
        tex = persp = replay = 1;
        opt = 3;
    } else if (!strcmp(step, "tri") && argc >= 8) {
        tex = persp = custom = 1;
        for (i = 0; i < 6; i++)
            cv[i] = (float)atof(argv[2 + i]);
        opt = 8;
    }
    else if (strcmp(step, "base") && strcmp(step, "fogcol")) {
        hx_test("args", 0, "unknown step %s", step);
        hx_done(HX_BAD_ARGS);
    }
    for (i = opt; i < argc; i++) {
        if (!strcmp(argv[i], "notex")) notex = 1;
        else if (!strcmp(argv[i], "nocull")) mga.textrap_empty_stall = 0;
        else if (!strcmp(argv[i], "each")) every = 1;
        else if (!strcmp(argv[i], "every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "fog")) rfog = 1;
        else if (!strcmp(argv[i], "affine")) affine = 1;
        else {
            hx_test("args", 0, "unknown option %s", argv[i]);
            hx_done(HX_BAD_ARGS);
        }
    }
    if (notex) tex = persp = 0;
    if (affine) persp = 0;
    hx_log("HX-STAT fogprobe step=%s card=%s fog_textrap_only=%u textrap_empty_stall=%u tex=%d persp=%d", step,
           mga.name, (unsigned)mga.fog_textrap_only, (unsigned)mga.textrap_empty_stall, tex, persp);
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 1024, &pitch) != 0) {
        hx_test("mode", 0, "640x480x16");
        hx_done(1);
    }
    engine_init(pitch, 16);
    memset(&t, 0, sizeof t);
    t.pitch_px = pitch; t.bpp = 16; t.zbits = 0;
    for (i = 0; i < nrt; i++)
        if ((rt[i].dw & 0x70) == DWG_ATYPE_ZI || (rt[i].dw & 0x700))
            t.zbits = 16, t.z_off = 1u << 20;               /* (the colour's 1024 x 480 x 2 below it) */
    engine_set_target(&t);
    engine_set_clip(0, 0, 640, 480);
    engine_fill(0, 0, 640, 480, 0x001F);                     /* blue */
    if (t.zbits)
        engine_fill_depth(0, 0, 640, 480, 0xFFFF);
    if (tex) {                                                /* 8x8 white at 3 MB */
        volatile uint16_t *tx = (volatile uint16_t *)(mga_fb + (3u << 20));
        mga_texstate ts;
        for (i = 0; i < 64; i++)
            tx[i] = 0xFFFF;
        memset(&ts, 0, sizeof ts);
        ts.org = 3u << 20;
        ts.w_log2 = ts.h_log2 = 3;
        ts.pitch = 8;
        ts.hwfmt = TEXCTL_TW16;
        ts.modulate = 1;
        tex_emit(&ts);
    }
    if (strcmp(step, "base")) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_FOGCOL, 0x00FF0000u);                   /* red */
    }
    if (fogen)
        engine_set_maccess_flags(MACCESS_FOGEN);
    memset(v, 0, sizeof v);
    for (i = 0; i < 3; i++) {
        v[i].r = 255; v[i].g = 255; v[i].b = 0; v[i].a = 255;
        v[i].fog = (float)(i * 120);                          /* 0, 120, 240: some fogged */
        v[i].q = 1;
    }
    v[0].X16 = 100 * 16; v[0].Y16 = 100 * 16; v[0].s = 0; v[0].t = 0;
    v[1].X16 = 300 * 16; v[1].Y16 = 120 * 16; v[1].s = 8; v[1].t = 0;
    v[2].X16 = 150 * 16; v[2].Y16 = 300 * 16; v[2].s = 0; v[2].t = 8;
    if (persp) {                                              /* (s/w, t/w, 1/w with w = 1, 2, 4) */
        v[1].q = 0.5f; v[1].s = 8 * 0.5f;
        v[2].q = 0.25f; v[2].t = 8 * 0.25f;
    }
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = (tex ? DWG_OPCOD_TEXTURE_TRAP : DWG_OPCOD_TRAP) | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
    ctx.flags = MGA_S_COLOR | (fog ? MGA_S_FOG : 0) | (tex ? MGA_S_TEX : 0) | (tex && !persp ? MGA_S_AFFINE : 0);
    ctx.tex_tw = ctx.tex_th = 3;
    ctx.clip_y0 = 0; ctx.clip_y1 = 480;
    st0 = MGA_RD32(MGAREG_STATUS);
    if (replay) {
        int k, j;
        if (rfog) {
            fog = fogen = 1;
            engine_set_maccess_flags(MACCESS_FOGEN);
        }
        hx_log("HX-STAT fogprobe replay %d triangles, fog %d, sync every %d", nrt, rfog, every);
        for (k = 0; k < nrt; k++) {
            mga_tri_ctx rc = ctx;
            rc.dwgctl = (uint32_t)rt[k].dw;
            rc.flags = (uint32_t)rt[k].fl & ~(uint32_t)(rfog ? 0 : MGA_S_FOG);
            rc.tex_tw = rt[k].tw; rc.tex_th = rt[k].th;
            for (j = 0; j < 3; j++) {
                memset(&v[j], 0, sizeof v[j]);
                v[j].X16 = (int32_t)rt[k].X[j]; v[j].Y16 = (int32_t)rt[k].Y[j];
                v[j].z = rt[k].z[j]; v[j].fog = rt[k].f[j]; v[j].q = rt[k].q[j];
                v[j].s = rt[k].s[j]; v[j].t = rt[k].t[j];
                v[j].r = 229.5f; v[j].g = 153; v[j].b = 51; v[j].a = 255;
            }
            setup_triangle(&v[0], &v[1], &v[2], &rc);
            if (every && (k + 1) % every == 0 && engine_sync(100000) != 0) {
                stalls++;
                hx_log("HX-STAT fogprobe stall at triangle %lu (%d of %d): %ld,%ld %ld,%ld %ld,%ld q=%g,%g,%g", rt[k].n,
                       k + 1, nrt, rt[k].X[0], rt[k].Y[0], rt[k].X[1], rt[k].Y[1], rt[k].X[2], rt[k].Y[2],
                       (double)rt[k].q[0], (double)rt[k].q[1], (double)rt[k].q[2]);
                if (fogen)
                    engine_set_maccess_flags(MACCESS_FOGEN);
            }
        }
    } else if (custom) {
        for (i = 0; i < 3; i++) {
            memset(&v[i], 0, sizeof v[i]);
            v[i].X16 = (int32_t)(cv[2 * i] * 16.0f + (cv[2 * i] < 0 ? -0.5f : 0.5f));
            v[i].Y16 = (int32_t)(cv[2 * i + 1] * 16.0f + (cv[2 * i + 1] < 0 ? -0.5f : 0.5f));
            v[i].q = i == 2 ? 0.3765f : 0.6667f;
            v[i].r = 229.5f; v[i].g = 153; v[i].b = 51; v[i].a = 255;
        }
        setup_triangle(&v[0], &v[1], &v[2], &ctx);
    } else if (t10) {
        /* DGL-TRI 1-5 of t10 on the real G100: X16,Y16 x3, z x3, fog x3, q x3 (s = t = 0); colour 0.9, 0.6, 0.2 */
        static const float T[5][15] = {
            { -5120, 7253, -4715, 7253, -434, 5768, 22598, 22598, 42272, 255, 255, 242, 0.6667f, 0.6667f, 0.3765f },
            { -4715, 7253, -4309, 7253, -205, 5768, 22598, 22598, 42272, 255, 255, 242, 0.6667f, 0.6667f, 0.3765f },
            { -434, 5768, -205, 5768, 1410, 5183, 42272, 42272, 50013, 242, 242, 219, 0.3765f, 0.3765f, 0.2623f },
            { -4715, 7253, -205, 5768, -434, 5768, 22598, 42272, 42272, 255, 242, 242, 0.6667f, 0.3765f, 0.3765f },
            { -4309, 7253, -3904, 7253, 24, 5768, 22598, 22598, 42272, 255, 255, 242, 0.6667f, 0.6667f, 0.3765f } };
        int k, j;
        for (k = t10first; k < t10first + t10n; k++) {
            for (j = 0; j < 3; j++) {
                memset(&v[j], 0, sizeof v[j]);
                v[j].X16 = (int32_t)T[k][2 * j] + t10dx;
                v[j].Y16 = (int32_t)T[k][2 * j + 1];
                v[j].z = T[k][6 + j];
                v[j].fog = T[k][9 + j];
                v[j].q = T[k][12 + j];
                v[j].r = 229.5f; v[j].g = 153; v[j].b = 51; v[j].a = 255;
            }
            hx_log("HX-STAT fogprobe t10 triangle %d", k + 1);
            setup_triangle(&v[0], &v[1], &v[2], &ctx);
        }
    } else
        setup_triangle(&v[0], &v[1], &v[2], &ctx);
    sync = engine_sync(200000);
    st1 = MGA_RD32(MGAREG_STATUS);
    fb = (volatile uint16_t *)mga_fb;
    outside = fb[10 * pitch + 10];
    inside = fb[150 * pitch + 170];
    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++)
            drawn += fb[y * pitch + x] != 0x001F;
    hx_log("HX-STAT fogprobe step=%s status=%08lx->%08lx sync=%d outside=%04x inside=%04x near0=%04x resets=%lu "
           "traps=%lu culled=%lu drawn=%ld", step, (unsigned long)st0, (unsigned long)st1, sync, outside, inside,
           fb[110 * pitch + 130], (unsigned long)engine_resets, (unsigned long)setup_stats.traps,
           (unsigned long)setup_stats.culled_empty, drawn);
    if (replay)
        hx_log("HX-STAT fogprobe replay stalls=%d", stalls);
    ok = sync == 0 && stalls == 0 && (custom || replay || outside == 0x001F) && (t10 || custom || replay || inside != 0x001F);
    engine_set_maccess_flags(0);
    vbe_set_text_mode();
    hx_test(step, ok, "drew: sync %d, inside %04x", sync, inside);
    hx_done(ok ? 0 : 1);
}
