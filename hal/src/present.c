/* present.c - show a render surface scaled onto a display surface: the
 * scaled fits of the mode planner (hal.h, vbe_plan_mode). The render
 * surface is one TW16 texture (its pitch the texture width, a power of two)
 * drawn as two triangles through the ordinary setup path, so the scaling
 * uses the same TEXTURE_TRAP code every game draw does. */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/tex.h"
#include <string.h>

static int log2_ceil(int v)
{
    int l = 0;
    while ((1 << l) < v)
        l++;
    return l;
}

int mga_present_ok(const mga_surface *s)
{
    return s->pitch_px >= 32 && s->pitch_px <= 2048 && (s->pitch_px & (s->pitch_px - 1)) == 0 &&
           s->w > 0 && s->w <= s->pitch_px && s->h > 0 && s->h <= 2048 && (s->off & 63) == 0;
}

/* One destination vertex: texture coordinates in texels of the surface,
 * normalised to the texture size (tex.h). Nearest: pixel centres map
 * linearly across the whole surface, so 2x repeats every texel exactly;
 * a 1/64-texel nudge settles centres that fall exactly on a texel boundary
 * (256 rows stretched to 480) on the later texel whatever the engine's
 * rounding, far below the smallest real gap at the scales used.
 * Bilinear: the first and last pixel centres land on the first and last
 * texel centres (tex_adjust_coords then moves them by the half texel the
 * filter wants), so the edges never blend with memory beyond the picture. */
static void corner(mga_svtx *v, int x, int y, const mga_surface *src, int dx, int dy, int dw, int dh,
                   int filter, int tw_log2, int th_log2)
{
    double u, t;
    memset(v, 0, sizeof *v);
    v->X16 = x * 16;
    v->Y16 = y * 16;
    v->r = v->g = v->b = v->a = 255.0f;
    v->fog = 255.0f;
    v->q = 1.0f;
    if (filter == MGA_PRESENT_BILINEAR) {
        double sx = dw > 1 ? (double)(src->w - 1) / (dw - 1) : 0.0, sy = dh > 1 ? (double)(src->h - 1) / (dh - 1) : 0.0;
        u = 0.5 + ((x - dx) - 0.5) * sx;
        t = 0.5 + ((y - dy) - 0.5) * sy;
    } else {
        u = (double)(x - dx) * src->w / dw + 1.0 / 64;
        t = (double)(y - dy) * src->h / dh + 1.0 / 64;
    }
    v->s = (float)(u / (1 << tw_log2));
    v->t = (float)(t / (1 << th_log2));
}

void engine_present(const mga_surface *src, const mga_surface *dst, int dx, int dy, int dw, int dh, int filter)
{
    mga_engine_state saved;
    mga_setup_stats stats = setup_stats;
    mga_target t;
    mga_texstate ts;
    mga_tri_ctx ctx;
    mga_svtx c[4], a[3], b[3];
    int tw = log2_ceil(src->pitch_px), th = log2_ceil(src->h), i;
    if (!mga_present_ok(src) || dw <= 0 || dh <= 0)
        return;
    if (th < 3)
        th = 3;
    engine_regs_epoch++;                    /* (PLNWT, ALPHACTRL, the sampler) */
    engine_save(&saved);
    memset(&t, 0, sizeof t);
    t.color_off = dst->off;
    t.pitch_px = dst->pitch_px;
    t.bpp = 16;
    t.zbits = 16;
    engine_set_maccess_flags(MACCESS_NODITHER);     /* an exact copy: no dither, no fog */
    engine_set_target(&t);
    engine_set_clip(dx, dy, dx + dw, dy + dh);
    fifo_reserve(2);
    MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
    MGA_WR32(MGAREG_ALPHACTRL, mga.has_alpha_blend
                                   ? ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) |
                                         ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE)
                                   : ALPHACTRL_G100_FIXED | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
    if (mga.has_dual_tex)
        tex_emit_combiner(0, 0);                    /* texel passes through both stages */
    memset(&ts, 0, sizeof ts);
    ts.org = ts.mip_org[0] = src->off;
    ts.mip_n = 1;
    ts.w_log2 = tw;
    ts.h_log2 = th;
    ts.pitch = src->pitch_px;
    ts.hwfmt = TEXCTL_TW16;
    ts.clamp_u = ts.clamp_v = 1;
    ts.bilinear = filter == MGA_PRESENT_BILINEAR;
    tex_emit(&ts);                                  /* TEXCTL2 0: single texturing, no specular */
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = DWG_OPCOD_TEXTURE_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
    ctx.flags = MGA_S_TEX | MGA_S_AFFINE;
    ctx.clip_y0 = dy;
    ctx.clip_y1 = dy + dh;
    ctx.tex_tw = tw;
    ctx.tex_th = th;
    corner(&c[0], dx, dy, src, dx, dy, dw, dh, filter, tw, th);
    corner(&c[1], dx + dw, dy, src, dx, dy, dw, dh, filter, tw, th);
    corner(&c[2], dx + dw, dy + dh, src, dx, dy, dw, dh, filter, tw, th);
    corner(&c[3], dx, dy + dh, src, dx, dy, dw, dh, filter, tw, th);
    /* Two triangles sharing the diagonal; the top-left rule draws each
     * pixel once. Each gets its own copies for the coordinate adjustment. */
    for (i = 0; i < 3; i++) {
        a[i] = c[i];
        b[i] = c[i == 0 ? 0 : i + 1];
    }
    tex_adjust_coords(&a[0], &a[1], &a[2], &ts);
    tex_adjust_coords(&b[0], &b[1], &b[2], &ts);
    setup_triangle(&a[0], &a[1], &a[2], &ctx);
    setup_triangle(&b[0], &b[1], &b[2], &ctx);
    setup_stats = stats;
    engine_restore(&saved);
}
