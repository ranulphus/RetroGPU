/* emit.c - from the vertex stream to the drawing engine (FR-TS-1..3).
 *
 * begin() validates dirty state into registers: DWGCTL (depth), ALPHACTRL
 * (blend, alpha test), MACCESS fog enable, FOGCOL, PLNWT (colour mask) and
 * the scissor. Each triangle is clipped (near/far and a guard band),
 * projected to 1/16-pixel screen coordinates, culled, and handed to the
 * HAL's setup_triangle. Lines and points become screen-space quads. */
#if defined(__GNUC__) && defined(__i386__)
#  include <cpuid.h>
#endif
#include "../../gl/gl_state.h"
#include "../../gl/gl_draw.h"
#include "../../gl/gl_tex.h"
#include "../../dgl/dgl.h"
#include "mga_be.h"
#include "mga/fp.h"
#include "mga/tex.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/warp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


static mga_tri_ctx tctx;
static mga_texstate tstate, tstate1;  /* hardware maps 0 and 1 */
static int textured;                  /* 0 none, 1 the bound texture, 2 the white texture (fog) */
static int dual;                      /* G400: both maps in use (MGA_S_TEX2) */
static int map0_unit;                 /* the GL unit whose coordinates feed map 0 */
static int map1_copy;                 /* map 1 repeats map 0 (a one-texture combiner mode needing dualtex) */
static float tex_scale_s = 1, tex_scale_t = 1;   /* logical / stored size (small textures are widened to 8) */
static float tex_scale_s1 = 1, tex_scale_t1 = 1;
/* Silicon experiments (docs/silicon-experiments.md): DGL_COMBINER=1 draws
 * every environment with the G400's combiner, the legacy modulate off (as
 * Mesa does); DGL_TC2_EXTRA=hex ORs bits into every TEXCTL2 (bit 15, which
 * the Linux and X.org drivers always set on the G400). */
static int force_combiner;
static uint32_t tc2_extra;
/* DGL_GUARD_PX: how far from the viewport's centre screen coordinates may
 * reach before triangles are clipped (default 2000, the setup's range);
 * smaller clips more but keeps edge and texture gradients shorter. Edges
 * stay within 4000 pixels, which the HAL fits into the G200's 18-bit AR
 * fields (up to 8191; RetroGPU's docs/loop-c-results.md). */
static float guard_px = 2000.0f;
static int skip_all;                  /* depth or alpha function NEVER, or no context */
static float tri_offset;              /* glPolygonOffset for the triangle being drawn (depth steps) */
static float guard_x, guard_y;
/* The G200 and the G400 family: triangles set up by the chip's WARP engine
 * from the projected vertices instead of by setup_triangle (RetroGPU's
 * docs/g400-warp.md, docs/g200-warp.md), unless DGL_WARP=0. Lines and
 * points, and any triangle after the WARP fails, go through setup_triangle. */
static int warp_on;                   /* asked for, on a chip with a WARP */
static int warp_now;                  /* the current state draws through the WARP */
static int warp_since_cpu;            /* WARP triangles since setup_triangle's last: its shadows are stale */

/* ---- State validation --------------------------------------------------- */
static uint32_t zmode_for(GLenum f)
{
    switch (f) {
    case GL_LESS:     return DWG_ZMODE_ZLT;
    case GL_EQUAL:    return DWG_ZMODE_ZE;
    case GL_LEQUAL:   return DWG_ZMODE_ZLTE;
    case GL_GREATER:  return DWG_ZMODE_ZGT;
    case GL_NOTEQUAL: return DWG_ZMODE_ZNE;
    case GL_GEQUAL:   return DWG_ZMODE_ZGTE;
    default:          return DWG_ZMODE_NOZCMP;       /* ALWAYS */
    }
}

static uint32_t src_factor(GLenum f)
{
    switch (f) {
    case GL_ZERO: return BLEND_ZERO;
    case GL_DST_COLOR: return BLEND_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return BLEND_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA: return BLEND_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return BLEND_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return BLEND_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return BLEND_ONE_MINUS_DST_ALPHA;
    case GL_SRC_ALPHA_SATURATE: return BLEND_SRC_ALPHA_SATURATE;
    default: return BLEND_ONE;
    }
}

static uint32_t dst_factor(GLenum f)
{
    switch (f) {
    case GL_ONE: return BLEND_ONE;
    case GL_SRC_COLOR: return BLEND_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return BLEND_ONE_MINUS_SRC_COLOR;
    case GL_SRC_ALPHA: return BLEND_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return BLEND_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return BLEND_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return BLEND_ONE_MINUS_DST_ALPHA;
    default: return BLEND_ZERO;
    }
}

static uint32_t atmode_for(GLenum f)
{
    static const uint8_t m[8] = { 0, 4, 2, 5, 6, 3, 7, 0 };   /* NEVER..ALWAYS -> ATMODE codes */
    return m[(f - GL_NEVER) & 7];
}

static uint32_t plnwt_for(void)
{
    uint32_t m = (dgl_gl.color_mask[0] ? 0xF800u : 0) | (dgl_gl.color_mask[1] ? 0x07E0u : 0) |
                 (dgl_gl.color_mask[2] ? 0x001Fu : 0);
    return m | (m << 16);
}

static void scissor_rows(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = 0; *y0 = 0; *x1 = dgl_ctx.width; *y1 = dgl_ctx.height;
    if (dgl_gl.scissor_test) {
        int sx0 = dgl_gl.scissor[0], sx1 = dgl_gl.scissor[0] + dgl_gl.scissor[2];
        int sy0 = dgl_ctx.height - (dgl_gl.scissor[1] + dgl_gl.scissor[3]);     /* GL y is bottom-up */
        int sy1 = dgl_ctx.height - dgl_gl.scissor[1];
        if (sx0 > *x0) *x0 = sx0;
        if (sy0 > *y0) *y0 = sy0;
        if (sx1 < *x1) *x1 = sx1;
        if (sy1 < *y1) *y1 = sy1;
        if (*x1 < *x0) *x1 = *x0;
        if (*y1 < *y0) *y1 = *y0;
    }
}

/* What a primitive may draw: the scissor's rectangle cut to the viewport's.
 * GL clips primitives to the view volume, which the viewport maps onto its
 * rectangle; the clipper here keeps a guard band instead, so the cut is the
 * engine's. Clears take the scissor alone (GL: the viewport doesn't apply).
 * A wide point or line straddling the viewport's edge is cut there too, as
 * on most hardware drivers. */
static void draw_rows(int *x0, int *y0, int *x1, int *y1)
{
    int vx0 = dgl_gl.viewport[0], vx1 = dgl_gl.viewport[0] + dgl_gl.viewport[2];
    int vy0 = dgl_ctx.height - (dgl_gl.viewport[1] + dgl_gl.viewport[3]);    /* GL y is bottom-up */
    int vy1 = dgl_ctx.height - dgl_gl.viewport[1];
    scissor_rows(x0, y0, x1, y1);
    if (vx0 > *x0) *x0 = vx0;
    if (vy0 > *y0) *y0 = vy0;
    if (vx1 < *x1) *x1 = vx1;
    if (vy1 < *y1) *y1 = vy1;
    if (*x1 < *x0) *x1 = *x0;
    if (*y1 < *y0) *y1 = *y0;
}

/* A map's sampler from a texture object. */
static void sampler(mga_texstate *ts, const dgl_texture *tex, float *scale_s, float *scale_t)
{
    int k;
    memset(ts, 0, sizeof *ts);
    ts->org = MT(tex)->level_off[0];
    ts->mip_n = MT(tex)->hw_levels > 1 ? MT(tex)->hw_levels : 0;
    for (k = 0; k < MT(tex)->hw_levels && k < 5; k++)
        ts->mip_org[k] = MT(tex)->level_off[k];
    ts->w_log2 = MT(tex)->hw_w_log2;
    ts->h_log2 = MT(tex)->hw_h_log2;
    *scale_s = (float)tex->level[0].w / (float)(1 << MT(tex)->hw_w_log2);
    *scale_t = (float)tex->level[0].h / (float)(1 << MT(tex)->hw_h_log2);
    ts->pitch = 1 << MT(tex)->hw_w_log2;
    ts->hwfmt = (uint32_t)MT(tex)->hwfmt;
    ts->clamp_u = tex->wrap_s != GL_REPEAT;      /* CLAMP behaves as CLAMP_TO_EDGE */
    ts->clamp_v = tex->wrap_t != GL_REPEAT;
    ts->bilinear = tex->mag_filter == GL_LINEAR || tex->min_filter == GL_LINEAR ||
                   tex->min_filter == GL_LINEAR_MIPMAP_NEAREST || tex->min_filter == GL_LINEAR_MIPMAP_LINEAR;
    ts->trilinear = ts->mip_n > 1 && (tex->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                                      tex->min_filter == GL_LINEAR_MIPMAP_LINEAR);
}

/* A stage's combiner word for a GL unit's environment; one the combiner
 * cannot do in one stage (GL_BLEND with a colour, some GL_COMBINE_ARB
 * functions, combine.c) is drawn as GL_MODULATE, logged once. */
static uint32_t stage_word(int stage, int unit, const dgl_texture *t)
{
    static int warned;
    const dgl_texenv *e = dgl_tex_env(unit);
    dgl_texenv modulate = *e;
    uint32_t w = 0;
    if (!dgl_stage_word(stage, e, t, &w)) {
        if (!warned++)
            DGL_WARN("DGL-WARN env 0x%x (combine 0x%x/0x%x) is drawn as GL_MODULATE", (unsigned)e->mode,
                     (unsigned)e->combine_rgb, (unsigned)e->combine_alpha);
        modulate.mode = GL_MODULATE;
        dgl_stage_word(stage, &modulate, t, &w);
    }
    return w;
}

/* What validation last wrote, so that a change writes only what differs
 * (fe.md F1): a texture or blend change used to rewrite every register of
 * the block, about 40 writes and 3,000-4,800 cycles on a Pentium II. Valid
 * while ok is set and the HAL's register epoch hasn't moved (resets, resyncs,
 * VRAM copies, presents and WARP starts write some of these registers
 * themselves); anything that marks the target dirty (swaps, clears,
 * read-backs, the scissor), and any texel written (dgl_tex_wgen: the G400's
 * texel cache), has every group written again too. */
static struct {
    int ok;
    uint32_t epoch, wgen;
    int textured, dual;                 /* the samplers' last emission */
    mga_texstate t0, t1;
    uint32_t w0, w1;                    /* the combiner's words */
    int wt_set, wt_textured, wt_dual;   /* the WARPs' sizes, last warp_tex */
    int wt_w0, wt_h0, wt_w1, wt_h1;
    uint32_t wt_ctl2;
    uint32_t alphactrl, plnwt, fogcol;
} regs;

static void regs_forget(void) { regs.ok = 0; }

static uint32_t fogcol_for(void)
{
    return ((uint32_t)lrintf(dgl_gl.fog_color[0] * 255.0f) << 16) | ((uint32_t)lrintf(dgl_gl.fog_color[1] * 255.0f) << 8) |
           (uint32_t)lrintf(dgl_gl.fog_color[2] * 255.0f);
}

/* The last full validation's textures (after its residency checks), for a
 * draw that changes nothing (fe.md F2): every change to what it depends on
 * marks a register group or the clip dirty (binds, deletes, images and
 * sub-images, palettes, glEnable, the viewport). ok only when that
 * validation drew (a texture that could not be made resident tries again). */
#define DGL_DIRTY_REGS (DGL_DIRTY_RASTER | DGL_DIRTY_FOG | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE)
static struct { int ok; dgl_texture *tex, *tex1; } idle;
static float guard_vp[4] = { -1, -1, -1, -1 };     /* the viewport the guard band is for */

static void validate_state(void)
{
    int depth, unit0 = 0;
    uint32_t alphactrl, zmode, asel;
    dgl_texture *tex, *tex1;
    if (idle.ok && dgl_ctx.active && !(dgl_gl.dirty & (DGL_DIRTY_REGS | DGL_DIRTY_CLIP))) {
        if (idle.tex)
            dgl_texture_drawn(idle.tex);    /* (busy until the next completed sync) */
        if (idle.tex1)
            dgl_texture_drawn(idle.tex1);
        return;
    }
    depth = dgl_gl.depth_test && rg_mga.z_off;
    zmode = depth ? zmode_for(dgl_gl.depth_func) : DWG_ZMODE_NOZCMP;
    tex = dgl_gl.texture_2d ? dgl_unit_texture(0) : NULL;
    tex1 = dgl_gl.texture_2d1 && dgl_texture_units() > 1 ? dgl_unit_texture(1) : NULL;
    if (!tex && tex1) {
        tex = tex1;                     /* GL unit 1 alone drives map 0 */
        tex1 = NULL;
        unit0 = 1;
    }
    skip_all = !dgl_ctx.active || (depth && dgl_gl.depth_func == GL_NEVER) ||
               (dgl_gl.alpha_test && dgl_gl.alpha_func == GL_NEVER);
    if (dgl_gl.dirty & (DGL_DIRTY_TARGET | DGL_DIRTY_CLIP)) {
        /* Anything else that sets the engine's clip (a clear, a swap, a reset)
         * marks the target dirty; a new viewport alone that leaves the
         * rectangle as it was writes nothing. */
        static int last[4];
        int x0, y0, x1, y1;
        draw_rows(&x0, &y0, &x1, &y1);
        if ((dgl_gl.dirty & DGL_DIRTY_TARGET) || x0 != last[0] || y0 != last[1] || x1 != last[2] || y1 != last[3]) {
            engine_set_clip(x0, y0, x1, y1);
            last[0] = x0, last[1] = y0, last[2] = x1, last[3] = y1;
        }
        tctx.clip_y0 = y0;
        tctx.clip_y1 = y1;
    }
    /* An incomplete texture draws untextured (GL); one that cannot be made
     * resident skips the draw (out of memory, PRD §8.3). */
    if (tex && dgl_texture_ready(tex) != 0) {
        skip_all = 1;
        tex = NULL;
    } else if (tex) {
        /* The LUT first: loading it may sync, which ends every texture's
         * busy mark, and this draw's must outlast it (a glTexSubImage2D
         * after the draw would otherwise write in place under it: t19 with
         * the G200's WARP, whose triangles wait in the DMA stream). */
        dgl_texture_lut(tex);          /* TW8: its palette in the LUT (queued before this draw) */
        dgl_texture_drawn(tex);        /* busy until the next completed sync */
    }
    if (tex && tex1) {
        /* Busy, map 0's texture cannot be evicted to make room for map 1's
         * unless a sync frees everything; then make it resident again. */
        if (dgl_texture_ready(tex1) != 0 || dgl_texture_ready(tex) != 0) {
            skip_all = 1;
            tex = tex1 = NULL;
        } else {
            dgl_texture_lut(tex1);     /* both maps share one LUT (the shared palette); first, as above */
            dgl_texture_drawn(tex1);
            dgl_texture_drawn(tex);
        }
    }
    if ((dgl_gl.dirty & DGL_DIRTY_TEXTURE) || !tex != (textured != 1) || (tex1 != NULL) != (dual && !map1_copy) ||
        unit0 != map0_unit)
        dgl_gl.dirty |= DGL_DIRTY_RASTER | DGL_DIRTY_TEXTURE;
    if (dgl_gl.dirty & (DGL_DIRTY_RASTER | DGL_DIRTY_FOG | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE)) {
        uint32_t white, w0 = 0, w1 = 0, plnwt, fogcol;
        const int known = regs.ok && regs.epoch == engine_regs_epoch && regs.wgen == dgl_tex_wgen &&
                          !(dgl_gl.dirty & DGL_DIRTY_TARGET);
        memset(&tstate, 0, sizeof tstate);
        textured = 0;
        dual = map1_copy = 0;
        map0_unit = unit0;
        asel = ALPHASEL_DIFFUSE;
        if (tex) {
            GLenum env = dgl_tex_env_mode(unit0);
            textured = 1;
            sampler(&tstate, tex, &tex_scale_s, &tex_scale_t);
            if (mga.has_dual_tex && (tex1 || force_combiner || dgl_env_needs_combiner(env, tex))) {
                /* G400: the combiner does the environments (Mesa's words);
                 * the legacy modulate stays off. */
                w0 = stage_word(0, unit0, tex);
                if (tex1) {
                    sampler(&tstate1, tex1, &tex_scale_s1, &tex_scale_t1);
                    w1 = stage_word(1, 1, tex1);
                    dual = 1;
                } else if (w0 & (1u << 20)) {
                    /* The blend mode (GL_DECAL on alpha) needs dualtex: map 1
                     * repeats map 0 and stage 1 passes stage 0 through. */
                    tstate1 = tstate;
                    tex_scale_s1 = tex_scale_s;
                    tex_scale_t1 = tex_scale_t;
                    w1 = 0x43200003u;
                    dual = map1_copy = 1;
                } else
                    w1 = w0;                /* single texturing: stage 1 as stage 0 */
                asel = ALPHASEL_TEXTURE;    /* the combiner's alpha */
            } else {
                static int warned;
                if (env == GL_BLEND && !warned++)
                    DGL_WARN("DGL-WARN GL_BLEND is drawn as GL_MODULATE on this card");
                tstate.modulate = env == GL_MODULATE || env == GL_BLEND;
                if (env == GL_DECAL && dgl_env_needs_combiner(env, tex) && mga.has_decalblend)
                    tstate.texctl2 |= TEXCTL2_DECALBLEND;   /* G200: blend by texel alpha */
                asel = tstate.modulate ? ALPHASEL_MODULATED : ALPHASEL_TEXTURE;
            }
        } else if (dgl_gl.fog && dgl_white_texture(&white) == 0) {
            /* The engine fogs only textured trapezoids (in 86Box, and on the
             * G100): untextured fogged draws sample a white texel. */
            textured = 2;
            tex_scale_s = tex_scale_t = 1;
            tstate.org = white;
            tstate.w_log2 = tstate.h_log2 = 3;
            tstate.pitch = 8;
            tstate.hwfmt = DGL_TW16;
            tstate.modulate = 1;
        }
        tctx.dwgctl = (textured ? DWG_OPCOD_TEXTURE_TRAP : DWG_OPCOD_TRAP) | zmode | DWG_BOP_COPY |
                      ((depth && dgl_gl.depth_mask) ? DWG_ATYPE_ZI : DWG_ATYPE_I);
        tctx.flags = MGA_S_COLOR | (depth ? MGA_S_Z : 0) | (textured ? MGA_S_TEX : 0) | (dual ? MGA_S_TEX2 : 0);
        tctx.tex_tw = tstate.w_log2;
        tctx.tex_th = tstate.h_log2;
        tctx.tex_tw1 = tstate1.w_log2;
        tctx.tex_th1 = tstate1.h_log2;
        tstate.texctl2 |= tc2_extra;
        tstate1.texctl2 |= tc2_extra;
        tctx.texctl2_1 = tstate1.texctl2 | TEXCTL2_DUALTEX;
        if (textured) {
            if (!known || regs.textured != textured || regs.dual != dual) {
                if (dual)
                    tex_emit_dual(&tstate, &tstate1);
                else
                    tex_emit(&tstate);
                if (mga.has_dual_tex)
                    tex_emit_combiner(w0, w1);  /* zero words pass the legacy result through */
            } else {
                /* The maps hold regs.t0 and regs.t1: only what differs.
                 * In dual mode map 0's sequence reaches both maps, so a
                 * change there writes both again. */
                if (dual) {
                    if (memcmp(&regs.t0, &tstate, sizeof tstate))
                        tex_emit_dual(&tstate, &tstate1);
                    else if (memcmp(&regs.t1, &tstate1, sizeof tstate1))
                        tex_emit_map1(&tstate1);
                } else if (memcmp(&regs.t0, &tstate, sizeof tstate))
                    tex_emit_change(&tstate, &regs.t0);
                if (mga.has_dual_tex && (w0 != regs.w0 || w1 != regs.w1))
                    tex_emit_combiner(w0, w1);
            }
            regs.t0 = tstate;
            regs.t1 = tstate1;
            regs.w0 = w0;
            regs.w1 = w1;
        }
        regs.textured = textured;
        regs.dual = dual;
        /* The WARP: Mesa's two pipes (everything iterated; DWGCTL, ALPHACTRL,
         * MACCESS and TEXCTL2 decide what is used), DWGCTL written here (the
         * setup writes it otherwise) through setup_dwgctl, so that the HAL's
         * clip barrier draws with it, the maps' sizes as the microcode wants
         * them. */
        warp_now = 0;
        if (warp_on) {
            const uint32_t e0 = engine_regs_epoch;
            if (warp_select(WARP_S | WARP_A | WARP_F | (dual ? WARP_T2 : 0)) == 0) {
                /* (a start moves the epoch: the sizes go again) */
                const int same = known && e0 == engine_regs_epoch && regs.wt_set && regs.wt_textured == !!textured &&
                                 regs.wt_dual == dual &&
                                 (!textured || (regs.wt_w0 == tstate.w_log2 && regs.wt_h0 == tstate.h_log2)) &&
                                 (!dual || (regs.wt_w1 == tstate1.w_log2 && regs.wt_h1 == tstate1.h_log2 &&
                                            regs.wt_ctl2 == tctx.texctl2_1));
                warp_now = 1;
                setup_dwgctl(tctx.dwgctl);
                if (!same) {
                    if (textured) {
                        warp_tex(0, tstate.w_log2, tstate.h_log2, 0);
                        if (dual)
                            warp_tex(1, tstate1.w_log2, tstate1.h_log2, tctx.texctl2_1);
                    } else
                        warp_tex_sizes(0, 0, 0, 0);
                    regs.wt_set = 1;
                    regs.wt_textured = !!textured;
                    regs.wt_dual = dual;
                    regs.wt_w0 = tstate.w_log2;
                    regs.wt_h0 = tstate.h_log2;
                    regs.wt_w1 = tstate1.w_log2;
                    regs.wt_h1 = tstate1.h_log2;
                    regs.wt_ctl2 = tctx.texctl2_1;
                }
            } else {
                warp_on = 0;
                DGL_WARN("DGL-WARN the WARP did not start: triangles are set up by the CPU");
            }
        }
        if (mga.has_alpha_blend) {
            alphactrl = ALPHACTRL_ALPHASEL(asel);
            if (dgl_gl.blend)
                alphactrl |= ALPHACTRL_SRC(src_factor(dgl_gl.blend_src)) | ALPHACTRL_DST(dst_factor(dgl_gl.blend_dst));
            else
                alphactrl |= ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO);
            if (dgl_gl.alpha_test && dgl_gl.alpha_func != GL_ALWAYS && mga.has_alpha_test)
                alphactrl |= ALPHACTRL_ATEN | ALPHACTRL_ATMODE(atmode_for(dgl_gl.alpha_func)) |
                             ALPHACTRL_ATREF((uint32_t)lrintf(dgl_gl.alpha_ref * 255.0f));
        } else {
            /* G100 (development only): stipple is its only blend (PRD D13). */
            alphactrl = ALPHACTRL_G100_FIXED | ALPHACTRL_ALPHASEL(asel) |
                        (dgl_gl.blend ? ALPHACTRL_ASTIPPLE : 0);
        }
        if (dgl_gl.blend || dgl_gl.alpha_test)
            tctx.flags |= MGA_S_ALPHA;
        if (dgl_gl.fog && textured)
            tctx.flags |= MGA_S_FOG;
        engine_set_maccess_flags(((tctx.flags & MGA_S_FOG) ? MACCESS_FOGEN : 0) |
                                 (dgl_gl.dither ? 0 : MACCESS_NODITHER));
        plnwt = plnwt_for();
        fogcol = (!known || (dgl_gl.dirty & DGL_DIRTY_FOG)) ? fogcol_for() : regs.fogcol;
        fifo_reserve(3);
        if (!known || alphactrl != regs.alphactrl)
            MGA_WR32(MGAREG_ALPHACTRL, alphactrl);
        if (!known || plnwt != regs.plnwt)
            MGA_WR32(MGAREG_PLNWT, plnwt);
        if (!known || fogcol != regs.fogcol)
            MGA_WR32(MGAREG_FOGCOL, fogcol);
        regs.alphactrl = alphactrl;
        regs.plnwt = plnwt;
        regs.fogcol = fogcol;
        regs.epoch = engine_regs_epoch;
        regs.wgen = dgl_tex_wgen;
        regs.ok = 1;
    }
    dgl_gl.dirty &= ~(DGL_DIRTY_RASTER | DGL_DIRTY_FOG | DGL_DIRTY_TARGET | DGL_DIRTY_TEXTURE | DGL_DIRTY_CLIP);
    /* Guard band: keep screen coordinates inside +-2048 (the setup's range);
     * the same viewport, the same band. */
    if (guard_vp[0] != dgl_gl.viewport[0] || guard_vp[1] != dgl_gl.viewport[1] ||
        guard_vp[2] != dgl_gl.viewport[2] || guard_vp[3] != dgl_gl.viewport[3]) {
        float hw = dgl_gl.viewport[2] * 0.5f, hh = dgl_gl.viewport[3] * 0.5f;
        guard_x = hw > 0 ? (guard_px - fabsf(dgl_gl.viewport[0] + hw)) / hw : 1.0f;
        guard_y = hh > 0 ? (guard_px - fabsf(dgl_gl.viewport[1] + hh)) / hh : 1.0f;
        if (guard_x < 1.0f) guard_x = 1.0f;
        if (guard_y < 1.0f) guard_y = 1.0f;
        guard_vp[0] = dgl_gl.viewport[0], guard_vp[1] = dgl_gl.viewport[1];
        guard_vp[2] = dgl_gl.viewport[2], guard_vp[3] = dgl_gl.viewport[3];
    }
    idle.ok = !skip_all;
    idle.tex = tex;
    idle.tex1 = tex1;
}


/* A soft reset while validating (the WARP's microcode loads take one, and
 * so does a WARP that fails) clears the drawing registers: everything is
 * written again. The second pass finds the pipe running. */
static void validate(void)
{
    uint32_t resets = engine_resets;
    validate_state();
    if (engine_resets != resets) {
        dgl_gl.dirty |= DGL_DIRTY_REGS;
        regs_forget();
        validate_state();
    }
}

static int emit_begin(void)
{
    const int idle = !(dgl_gl.dirty & DGL_DIRTY_REGS);    /* (PROF=2: no register group to write) */
    int prof_o = idle ? DGL_FINE_SWITCH(PROF_D_VIDLE) : 0;
    DGL_GPU_ENTER();
    validate();
    if (idle)
        DGL_FINE_BACK(prof_o);
    return !skip_all;
}

/* ---- Projection -------------------------------------------------------- */
static float fog_factor(float d)
{
    float f;
    switch (dgl_gl.fog_mode) {
    case GL_LINEAR:
        f = dgl_gl.fog_end != dgl_gl.fog_start ? (dgl_gl.fog_end - d) / (dgl_gl.fog_end - dgl_gl.fog_start) : 1.0f;
        break;
    case GL_EXP:
        f = expf(-dgl_gl.fog_density * d);
        break;
    default:
        f = expf(-(dgl_gl.fog_density * d) * (dgl_gl.fog_density * d));
        break;
    }
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

typedef struct { double x, y; mga_svtx v; } proj;

/* Out of line, as it has always been compiled: its float temporaries stay
 * where they are. */
static __attribute__((noinline)) void project(const dgl_cvtx *c, proj *p)
{
    double iw = 1.0 / c->w;
    double xw = dgl_gl.viewport[0] + (c->x * iw + 1.0) * 0.5 * dgl_gl.viewport[2];
    double yw = dgl_gl.viewport[1] + (c->y * iw + 1.0) * 0.5 * dgl_gl.viewport[3];
    double zw = dgl_gl.depth_near + (c->z * iw + 1.0) * 0.5 * (dgl_gl.depth_far - dgl_gl.depth_near);
    p->x = xw;
    p->y = dgl_ctx.height - yw;                 /* screen rows go down */
    memset(&p->v, 0, sizeof p->v);
    p->v.X16 = mga_irint_nearest(p->x * 16.0);     /* lrint() inside DGL_FPU_ENTER, without the call */
    p->v.Y16 = mga_irint_nearest(p->y * 16.0);
    p->v.z = zw < 0 ? 0 : zw > 1 ? 65535.0 : zw * 65535.0;
    p->v.r = c->r * 255.0f; p->v.g = c->g * 255.0f; p->v.b = c->b * 255.0f; p->v.a = c->a * 255.0f;
    p->v.fog = dgl_gl.fog ? 255.0f * fog_factor(c->eye_d) : 255.0f;
    /* Texture coordinates for the setup: normalised s, t times q = 1/w. */
    p->v.q = (float)iw;
    if (map0_unit) {                            /* GL unit 1 alone feeds map 0 */
        p->v.s = (float)(c->s1 * tex_scale_s * iw);
        p->v.t = (float)(c->t1 * tex_scale_t * iw);
    } else {
        p->v.s = (float)(c->s * tex_scale_s * iw);
        p->v.t = (float)(c->t * tex_scale_t * iw);
    }
    if (dual) {
        p->v.s1 = (float)((map1_copy ? p->v.s / tex_scale_s * tex_scale_s1 : c->s1 * tex_scale_s1 * iw));
        p->v.t1 = (float)((map1_copy ? p->v.t / tex_scale_t * tex_scale_t1 : c->t1 * tex_scale_t1 * iw));
    }
}

static void clamp_colour(mga_svtx *v)
{
#define C(f) if (v->f < 0) v->f = 0; else if (v->f > 255) v->f = 255
    C(r); C(g); C(b); C(a);
#undef C
}

/* A WARP vertex from a projected one and its setup values (colours clamped,
 * depth offset, texture coordinates adjusted): Mesa's mga vertex, with its
 * subpixel offset from the window coordinates the CPU setup rounds. */
/* (int)(f + 0.5) for f in 0..255, without a cast: the cast wraps its FISTP
 * in two FLDCWs, which cost 59 cycles a conversion on the bench's Pentium
 * II (DGL-MICRO), 24 a triangle here. */
static uint32_t byte_of(float f)
{
    return f <= 0 ? 0 : f >= 255.0f ? 255 : (uint32_t)mga_ifloor((double)f + 0.5);
}

static void to_wvtx(const proj *p, const mga_svtx *v, mga_wvtx *w)
{
    const float rq = 1.0f / (v->q > 0 ? v->q : 1e-9f);   /* one divide, not four */
    w->x = (float)p->x + WARP_SUBPIXEL_X;
    w->y = (float)p->y + WARP_SUBPIXEL_Y;
    w->z = (float)(v->z * (1.0 / 65535.0));
    w->rhw = v->q;
    w->bgra = byte_of(v->b) | (byte_of(v->g) << 8) | (byte_of(v->r) << 16) | (byte_of(v->a) << 24);
    w->spec = byte_of(v->sb) | (byte_of(v->sg) << 8) | (byte_of(v->sr) << 16) | (byte_of(v->fog) << 24);
    w->u0 = v->s * rq;
    w->v0 = v->t * rq;
    w->u1 = v->s1 * rq;
    w->v1 = v->t1 * rq;
}

/* tex_adjust_coords for the WARP, in its divided coordinates: whole periods
 * off each repeating axis so the smallest lies in [0, 1), without the six
 * divides (the WARP's chips sample at texel centres: no half-texel shift). */
static void warp_wrap(mga_wvtx *w, int map1, const mga_texstate *t)
{
    int axis, i;
    for (axis = 0; axis < 2; axis++) {
        float mn;
        if (axis ? t->clamp_v : t->clamp_u)
            continue;
#define U(k) (*(axis ? (map1 ? &w[k].v1 : &w[k].v0) : (map1 ? &w[k].u1 : &w[k].u0)))
        mn = U(0);
        if (U(1) < mn) mn = U(1);
        if (U(2) < mn) mn = U(2);
        if (!(mn > -1e9f && mn < 1e9f))
            continue;                   /* (mga_ifloor's range; NaN too) */
        mn = (float)mga_ifloor(mn);
        for (i = 0; i < 3; i++)
            U(i) -= mn;
#undef U
    }
}

/* flat: the provoking vertex when its colour is to replace the projected
 * ones (GL_FLAT on the fast path: the same product project() makes). */
static void draw_projected(const proj *p0, const proj *p1, const proj *p2, const dgl_cvtx *flat)
{
    mga_svtx a = p0->v, b = p1->v, c = p2->v;
    if (flat) {
        a.r = flat->r * 255.0f; a.g = flat->g * 255.0f; a.b = flat->b * 255.0f; a.a = flat->a * 255.0f;
        b.r = a.r; b.g = a.g; b.b = a.b; b.a = a.a;
        c.r = a.r; c.g = a.g; c.b = a.b; c.a = a.a;
    }
    clamp_colour(&a); clamp_colour(&b); clamp_colour(&c);
    if (tri_offset != 0.0f) {
#define OFS(v) v.z += tri_offset; if (v.z < 0) v.z = 0; else if (v.z > 65535.0) v.z = 65535.0
        OFS(a); OFS(b); OFS(c);
#undef OFS
    }
    if (warp_now) {
        mga_wvtx w[3];
        to_wvtx(p0, &a, &w[0]);
        to_wvtx(p1, &b, &w[1]);
        to_wvtx(p2, &c, &w[2]);
        if (textured)
            warp_wrap(w, 0, &tstate);
        if (dual)
            warp_wrap(w, 1, &tstate1);
        if (warp_vertices(w, 3) == 0) {
            warp_since_cpu = 1;
            return;
        }
        warp_on = warp_now = 0;                 /* the FIFO stopped draining: the CPU from now on */
        DGL_WARN("DGL-WARN the WARP stopped taking vertices: triangles are set up by the CPU");
        dgl_gl.dirty |= DGL_DIRTY_REGS;         /* (the engine was reset) */
    }
    if (textured)
        tex_adjust_coords(&a, &b, &c, &tstate);
    if (dual)
        tex_adjust_coords1(&a, &b, &c, &tstate1);
    if (warp_since_cpu) {
        setup_invalidate();                     /* the WARP wrote the registers setup_triangle shadows */
        warp_since_cpu = 0;
    }
    setup_triangle(&a, &b, &c, &tctx);
}

/* ---- Fog subdivision -------------------------------------------------------
 * The fog factor is iterated linearly across the screen, while GL's varies
 * with eye distance (perspective-correct, and exponentially for EXP/EXP2).
 * Where it changes by more than FOG_SPLIT levels over a sizeable triangle,
 * the triangle is split at its clip-space edge midpoints, which puts exact
 * per-vertex fog at the new corners. */
#define FOG_SPLIT 12.0f
#define FOG_SPLIT_DEPTH 4

static void mid(dgl_cvtx *o, const dgl_cvtx *a, const dgl_cvtx *b)
{
#define M(f) o->f = 0.5f * (a->f + b->f)
    M(x); M(y); M(z); M(w); M(r); M(g); M(b); M(a); M(s); M(t); M(s1); M(t1); M(eye_d);
#undef M
}

static void fog_split(const dgl_cvtx *c0, const dgl_cvtx *c1, const dgl_cvtx *c2,
                      const proj *p0, const proj *p1, const proj *p2, int depth, const dgl_cvtx *flat)
{
    float lo = p0->v.fog, hi = p0->v.fog;
    double area;
    dgl_cvtx m01, m12, m20;
    proj q01, q12, q20;
    if (p1->v.fog < lo) lo = p1->v.fog;
    if (p1->v.fog > hi) hi = p1->v.fog;
    if (p2->v.fog < lo) lo = p2->v.fog;
    if (p2->v.fog > hi) hi = p2->v.fog;
    area = fabs((p1->x - p0->x) * (p2->y - p0->y) - (p2->x - p0->x) * (p1->y - p0->y)) * 0.5;
    if (depth >= FOG_SPLIT_DEPTH || hi - lo <= FOG_SPLIT || area < 32.0) {
        draw_projected(p0, p1, p2, flat);
        return;
    }
    mid(&m01, c0, c1); mid(&m12, c1, c2); mid(&m20, c2, c0);
    project(&m01, &q01); project(&m12, &q12); project(&m20, &q20);
    fog_split(c0, &m01, &m20, p0, &q01, &q20, depth + 1, flat);
    fog_split(&m01, c1, &m12, &q01, p1, &q12, depth + 1, flat);
    fog_split(&m20, &m12, c2, &q20, &q12, p2, depth + 1, flat);
    fog_split(&m01, &m12, &m20, &q01, &q12, &q20, depth + 1, flat);
}

/* ---- Triangles ---------------------------------------------------------- */
/* Per-slot outcodes and projections of vertex.c's cache (gl_draw.h): a
 * vertex shared by several triangles of one dgl_assemble is projected
 * once. project() is out of line and deterministic, so a cached result is
 * the one a fresh call would give. */
enum { VS_OC = 1, VS_PROJ = 2, VS_WARP = 4 };
static struct { proj p; unsigned oc; mga_wvtx w; } vs[DGL_VSLOTS];

static int slot_of(const dgl_cvtx *v)
{
    uintptr_t d = (uintptr_t)v - (uintptr_t)dgl_vslot;
    return d < sizeof dgl_vslot ? (int)(d / sizeof dgl_vslot[0]) : -1;
}

static unsigned slot_oc(int s, const dgl_cvtx *v)
{
    if (!(dgl_vslot_sink[s] & VS_OC)) {
        vs[s].oc = dgl_outcode(v, guard_x, guard_y);
        dgl_vslot_sink[s] |= VS_OC;
    }
    return vs[s].oc;
}

static const proj *slot_proj(int s, const dgl_cvtx *v)
{
    if (!(dgl_vslot_sink[s] & VS_PROJ)) {
        project(v, &vs[s].p);
        dgl_vslot_sink[s] |= VS_PROJ;
    }
    return &vs[s].p;
}

/* The WARP's vertex for a projected one, made once a vertex (the slots keep
 * it): as to_wvtx makes it from the setup vertex, but with the texture
 * coordinates undivided from the GL vertex (the same values but for
 * rounding), and before anything a triangle changes (flat colour, polygon
 * offset, the shift by whole periods: draw_warp). */
static void warp_form(const dgl_cvtx *c, const proj *p, mga_wvtx *w)
{
    const mga_svtx *v = &p->v;
    w->x = (float)p->x + WARP_SUBPIXEL_X;
    w->y = (float)p->y + WARP_SUBPIXEL_Y;
    w->z = (float)(v->z * (1.0 / 65535.0));
    w->rhw = v->q;
    w->bgra = byte_of(v->b) | (byte_of(v->g) << 8) | (byte_of(v->r) << 16) | (byte_of(v->a) << 24);
    w->spec = byte_of(v->sb) | (byte_of(v->sg) << 8) | (byte_of(v->sr) << 16) | (byte_of(v->fog) << 24);
    if (map0_unit) {
        w->u0 = c->s1 * tex_scale_s;
        w->v0 = c->t1 * tex_scale_t;
    } else {
        w->u0 = c->s * tex_scale_s;
        w->v0 = c->t * tex_scale_t;
    }
    if (dual) {
        w->u1 = map1_copy ? w->u0 / tex_scale_s * tex_scale_s1 : c->s1 * tex_scale_s1;
        w->v1 = map1_copy ? w->v0 / tex_scale_t * tex_scale_t1 : c->t1 * tex_scale_t1;
    } else
        w->u1 = w->v1 = 0;
}

static const mga_wvtx *slot_warp(int s, const dgl_cvtx *v)
{
    if (!(dgl_vslot_sink[s] & VS_WARP)) {
        warp_form(v, slot_proj(s, v), &vs[s].w);
        dgl_vslot_sink[s] |= VS_WARP;
    }
    return &vs[s].w;
}

/* One triangle through the WARP from its vertices' WARP forms: 0, or -1
 * when the WARP failed (the CPU sets up this triangle and the rest). */
static int draw_warp(const mga_wvtx *a, const mga_wvtx *b, const mga_wvtx *c, const dgl_cvtx *flat)
{
    mga_wvtx w[3];
    int i;
    w[0] = *a; w[1] = *b; w[2] = *c;
    if (flat) {
        const uint32_t k = byte_of(flat->b * 255.0f) | (byte_of(flat->g * 255.0f) << 8) |
                           (byte_of(flat->r * 255.0f) << 16) | (byte_of(flat->a * 255.0f) << 24);
        w[0].bgra = w[1].bgra = w[2].bgra = k;
    }
    if (tri_offset != 0.0f)
        for (i = 0; i < 3; i++) {
            double z = w[i].z * 65535.0 + tri_offset;
            w[i].z = (float)((z < 0 ? 0 : z > 65535.0 ? 65535.0 : z) * (1.0 / 65535.0));
        }
    if (textured)
        warp_wrap(w, 0, &tstate);
    if (dual)
        warp_wrap(w, 1, &tstate1);
    if (warp_vertices(w, 3) == 0) {
        warp_since_cpu = 1;
        return 0;
    }
    warp_on = warp_now = 0;                     /* the FIFO stopped draining: the CPU from now on */
    DGL_WARN("DGL-WARN the WARP stopped taking vertices: triangles are set up by the CPU");
    dgl_gl.dirty |= DGL_DIRTY_REGS;             /* (the engine was reset) */
    return -1;
}

/* glPolygonOffset for the triangle (P0, P1, P2): GL's o = factor * max
 * |dz/dx|, |dz/dy| + units * r, with z in depth-buffer steps (r = one step). */
static float offset_for(const proj *p0, const proj *p1, const proj *p2)
{
    if (dgl_gl.offset_fill && (dgl_gl.offset_factor != 0.0f || dgl_gl.offset_units != 0.0f)) {
        double x1 = p1->x - p0->x, y1 = p1->y - p0->y, z1 = p1->v.z - p0->v.z;
        double x2 = p2->x - p0->x, y2 = p2->y - p0->y, z2 = p2->v.z - p0->v.z;
        double det = x1 * y2 - x2 * y1, m = 0;
        if (det != 0) {
            double dzdx = fabs((z1 * y2 - z2 * y1) / det), dzdy = fabs((x1 * z2 - x2 * z1) / det);
            m = dzdx > dzdy ? dzdx : dzdy;
        }
        return (float)(dgl_gl.offset_factor * m + dgl_gl.offset_units);
    }
    return 0.0f;
}

static void emit_triangle(const dgl_cvtx *a, const dgl_cvtx *b, const dgl_cvtx *c, const dgl_cvtx *prov)
{
    dgl_cvtx in[3], out[9];
    proj p[9];
    const proj *P[9];
    const dgl_cvtx *C[9], *flat = NULL;
    int n, i, sa = slot_of(a), sb = slot_of(b), sc = slot_of(c), fast = 0;
    int64_t area2;
    PROF_SCOPE(PROF_D_CLIP);
    dgl_prims.tris_in++;
    if (sa >= 0 && sb >= 0 && sc >= 0 && !(slot_oc(sa, a) | slot_oc(sb, b) | slot_oc(sc, c))) {
        /* Nothing to clip: the vertices as they are, projected once each;
         * flat colour replaced after projection instead of before. */
        PROF_SWITCH(PROF_D_PROJ);
        P[0] = slot_proj(sa, a); P[1] = slot_proj(sb, b); P[2] = slot_proj(sc, c);
        C[0] = a; C[1] = b; C[2] = c;
        n = 3;
        fast = 1;
        if (dgl_gl.shade_model == GL_FLAT)
            flat = prov;
    } else {
        in[0] = *a; in[1] = *b; in[2] = *c;
        if (dgl_gl.shade_model == GL_FLAT)
            for (i = 0; i < 3; i++) {
                in[i].r = prov->r; in[i].g = prov->g; in[i].b = prov->b; in[i].a = prov->a;
            }
        n = dgl_clip_polygon(in, 3, out, guard_x, guard_y);
        if (n < 3) {
            dgl_prims.clipped++;
            return;
        }
        PROF_SWITCH(PROF_D_PROJ);
        for (i = 0; i < n; i++) {
            project(&out[i], &p[i]);
            P[i] = &p[i];
            C[i] = &out[i];
        }
    }
    /* Cull on the (unclipped-equivalent) winding of the first three screen
     * vertices; y runs down on screen, so GL's counter-clockwise is area < 0. */
    area2 = (int64_t)(P[1]->v.X16 - P[0]->v.X16) * (P[2]->v.Y16 - P[0]->v.Y16) -
            (int64_t)(P[2]->v.X16 - P[0]->v.X16) * (P[1]->v.Y16 - P[0]->v.Y16);
    if (area2 == 0) {
        dgl_prims.zero_area++;
        return;
    }
    if (dgl_gl.cull_face) {
        int front = (dgl_gl.front_face == GL_CCW) ? area2 < 0 : area2 > 0;
        if (dgl_gl.cull_mode == GL_FRONT_AND_BACK || (dgl_gl.cull_mode == GL_BACK) != front) {
            dgl_prims.culled++;
            return;
        }
    }
    PROF_SWITCH(PROF_D_SETUP);
    if (warp_now && !dgl_gl.fog) {
        /* The WARP from the vertices' WARP forms (made once each); fog's
         * subdivision goes the setup vertices' way below. */
        const mga_wvtx *W[9];
        mga_wvtx wl[9];
        for (i = 0; i < n; i++) {
            if (fast)
                W[i] = slot_warp(i == 0 ? sa : i == 1 ? sb : sc, C[i]);
            else {
                warp_form(C[i], P[i], &wl[i]);
                W[i] = &wl[i];
            }
        }
        for (i = 1; i + 1 < n; i++) {
            tri_offset = offset_for(P[0], P[i], P[i + 1]);
            if (!warp_now || draw_warp(W[0], W[i], W[i + 1], flat) != 0)
                draw_projected(P[0], P[i], P[i + 1], flat);
        }
        return;
    }
    for (i = 1; i + 1 < n; i++) {
        tri_offset = offset_for(P[0], P[i], P[i + 1]);
        if (dgl_gl.fog)
            fog_split(C[0], C[i], C[i + 1], P[0], P[i], P[i + 1], 0, flat);
        else
            draw_projected(P[0], P[i], P[i + 1], flat);
    }
}

/* ---- The fast path: vertex arrays straight to the WARP (S1) ---------------
 * A draw of triangles from vertex arrays (or immediate mode's vertices,
 * planned as arrays at glEnd: vertex.c), while the WARP draws the state and
 * there is no fog, texture matrix, polygon offset, repeated map 1 or
 * remapped unit (G400 dual texturing is drawn, map 1 from unit 1): each vertex is fetched, transformed, given its outcode and projected
 * straight into its WARP form once (a direct-mapped cache by index), and a
 * triangle is culled and sent as three copies. A triangle with a vertex
 * outside the guard band goes the general way (dgl_tri_general: clipping).
 * The arithmetic is project()'s; the CPU setup's path is not used here, so
 * its frames are unchanged. */
#define FAST_SLOTS 256
typedef struct { GLint key; unsigned gen, oc; int32_t X16, Y16, fu, fv, fu1, fv1; mga_wvtx w; } fast_vtx;
#define NO_FLOOR INT32_MIN              /* (a coordinate wrap_dw leaves alone, or a reason to ask it) */
static fast_vtx fv[FAST_SLOTS];
static unsigned fgen;
static const float *fm;                 /* the MVP matrix */
static const float *fmv;                /* the modelview matrix, for fog's eye distance */
static uint32_t fbgra;                  /* the current colour, without a colour array */
static float fs0, ft0;                  /* the current texture coordinates, without an array */
static float fs1, ft1;                  /* unit 1's */

#define SEQ_MAX 64
static int seq_kinds = 7;               /* DGL_SEQ: 1 fans and polygons, 2 strips, 4 flat polygons (0: none) */

/* The fast path, twice (fast.inc): for the Pentium (-march=i586, as the
 * rest), and for the Pentium II and later, chosen at start by CPUID (fe.md
 * F3; DGL_P6=0 keeps the Pentium's). Only in DOS builds: elsewhere (the
 * Linux rig) both copies are the same code, and glibc's fortified memcpy, an
 * always_inline function, won't inline into another target's. */
#define FAST(n) n##_p5
#define FAST_ATTR
#include "fast.inc"
#undef FAST
#undef FAST_ATTR
#define FAST(n) n##_p6
#if defined(__GNUC__) && defined(__i386__) && defined(__DJGPP__)
#  define FAST_ATTR __attribute__((target("arch=pentium2")))
#else
#  define FAST_ATTR
#endif
#include "fast.inc"
#undef FAST
#undef FAST_ATTR

/* FCOMI, FCMOV and CMOV: CPUID's CMOV bit with an FPU (a Pentium Pro or later). */
static int cpu_p6(void)
{
#if defined(__GNUC__) && defined(__i386__)
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(1, &a, &b, &c, &d))    /* (no CPUID: a 486 before the DX4's) */
        return 0;
    return (d & 1) && ((d >> 15) & 1);
#else
    return 0;
#endif
}

/* ---- Lines and points: one-pixel screen-space quads (PRD §7) ------------ */
static void quad(proj *a, proj *b, double ox, double oy)
{
    proj q[4];
    q[0] = *a; q[1] = *b; q[2] = *b; q[3] = *a;
    q[0].x -= ox; q[0].y -= oy; q[1].x -= ox; q[1].y -= oy;
    q[2].x += ox; q[2].y += oy; q[3].x += ox; q[3].y += oy;
    {
        int i;
        for (i = 0; i < 4; i++) {
            q[i].v.X16 = mga_irint_nearest(q[i].x * 16.0);
            q[i].v.Y16 = mga_irint_nearest(q[i].y * 16.0);
        }
    }
    draw_projected(&q[0], &q[1], &q[2], NULL);
    draw_projected(&q[0], &q[2], &q[3], NULL);
}

static void emit_line(const dgl_cvtx *a, const dgl_cvtx *b)
{
    dgl_cvtx in[3], out[9];
    proj p0, p1;
    int n;
    double dx, dy, hw;
    PROF_SCOPE(PROF_D_SETUP);
    /* Clip the segment as a degenerate triangle, keep the first two outputs. */
    in[0] = *a; in[1] = *b; in[2] = *b;
    if (dgl_gl.shade_model == GL_FLAT) {
        in[0].r = b->r; in[0].g = b->g; in[0].b = b->b; in[0].a = b->a;
    }
    n = dgl_clip_polygon(in, 3, out, guard_x, guard_y);
    if (n < 2)
        return;
    project(&out[0], &p0);
    project(&out[1], &p1);
    tri_offset = 0.0f;
    dx = p1.x - p0.x; dy = p1.y - p0.y;
    hw = 0.5 * (dgl_gl.line_width < 1.0f ? 1.0 : dgl_gl.line_width);
    if (fabs(dx) >= fabs(dy))
        quad(&p0, &p1, 0.0, hw);              /* x-major: widen vertically */
    else
        quad(&p0, &p1, hw, 0.0);
}

static void emit_point(const dgl_cvtx *a)
{
    proj p, q;
    double s = dgl_gl.point_size < 1.0f ? 1.0 : dgl_gl.point_size;   /* a square s pixels wide */
    PROF_SCOPE(PROF_D_SETUP);
    if (dgl_outcode(a, guard_x, guard_y))
        return;
    project(a, &p);
    tri_offset = 0.0f;
    p.x -= 0.5 * s;
    q = p;
    q.x += s;
    quad(&p, &q, 0.0, 0.5 * s);
}

static void emit_end(void) { }

/* The CPU's setup from now on (GLOS windows). */
static void dgl_emit_no_warp(void)
{
    if (warp_tris || warp_failures)
        warp_stop();
    warp_on = warp_now = 0;
    dgl_gl.dirty |= DGL_DIRTY_REGS;
}

void dgl_emit_install(void)
{
    const char *e = getenv("DGL_WARP");
    if (rg_lock.on)
        dgl_emit_no_warp();                     /* (a GLOS window: the CPU's setup; hal/src/rg/mga_glos.c) */
    warp_on = !(e && *e == '0') && warp_available() && !rg_lock.on;  /* (not in a GLOS window) */
    e = getenv("DGL_WARPDMA");
    warp_use_dma = !(e && *e == '0');          /* DGL_WARPDMA=0: vertices by the CPU, no DMA stream */
    e = getenv("DGL_AGP");
    warp_use_agp = !(e && *e == '0');          /* DGL_AGP=0: cached rings fetched by PCI, under GLOS too */
    warp_agp_rate = e && (*e == '1' || *e == '2') ? *e - '0' : 0;   /* DGL_AGP=1 or 2: at most 1x or 2x */
    if ((e = getenv("DGL_SEQ")) != NULL)
        seq_kinds = atoi(e) & 7;
    if ((e = getenv("DGL_KICK")) != NULL && atoi(e) >= 512)
        warp_kick_bytes = (uint32_t)atoi(e);    /* (tuning: vertex bytes between kicks) */
    if ((e = getenv("DGL_RING")) != NULL && atoi(e) >= 32768)
        warp_ring_bytes = (uint32_t)atoi(e);    /* (tuning: the rings' size at most) */
    warp_now = warp_since_cpu = 0;
    e = getenv("DGL_COMBINER");
    force_combiner = e && *e && *e != '0';
    e = getenv("DGL_TC2_EXTRA");
    tc2_extra = e && *e && mga.has_dual_tex ? (uint32_t)strtoul(e, NULL, 16) : 0;
    e = getenv("DGL_GUARD_PX");
    guard_px = e && atoi(e) > 0 && atoi(e) <= 2000 ? (float)atoi(e) : 2000.0f;
    dgl_sink.begin = emit_begin;
    dgl_sink.triangle = emit_triangle;
    e = getenv("DGL_FAST");
    dgl_sink.fast = e && *e == '0' ? NULL : emit_fast_p5;  /* DGL_FAST=0: the general path only */
    e = getenv("DGL_P6");
    if (dgl_sink.fast && cpu_p6() && !(e && *e == '0'))
        dgl_sink.fast = emit_fast_p6;
    dgl_sink.line = emit_line;
    dgl_sink.point = emit_point;
    dgl_sink.end = emit_end;
}

/* ---- Clears -------------------------------------------------------------- */
void dgl_mga_clear(GLbitfield mask)
{
    int x0, y0, x1, y1;
    PROF_SCOPE(PROF_D_CLEAR);
    DGL_FPU_ENTER();
    DGL_GPU_ENTER();
    scissor_rows(&x0, &y0, &x1, &y1);
    engine_set_clip(x0, y0, x1, y1);
    /* Clears are engine fills: no blending or fog; the colour mask applies. */
    regs_forget();                      /* (ALPHACTRL and PLNWT below) */
    engine_set_maccess_flags(0);
    fifo_reserve(2);
    MGA_WR32(MGAREG_ALPHACTRL, mga.has_alpha_blend ? ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO)
                                                   : ALPHACTRL_G100_FIXED);
    MGA_WR32(MGAREG_PLNWT, plnwt_for());
    if (mask & GL_COLOR_BUFFER_BIT) {
        uint32_t r = (uint32_t)lrintf(dgl_gl.clear_color[0] * 31.0f);
        uint32_t g = (uint32_t)lrintf(dgl_gl.clear_color[1] * 63.0f);
        uint32_t b = (uint32_t)lrintf(dgl_gl.clear_color[2] * 31.0f);
        engine_fill(x0, y0, x1 - x0, y1 - y0, (r << 11) | (g << 5) | b);
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) && rg_mga.z_off && dgl_gl.depth_mask) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
        engine_fill_depth(x0, y0, x1 - x0, y1 - y0, (uint32_t)lrint(dgl_gl.clear_depth * 65535.0));
    }
    dgl_gl.dirty |= DGL_DIRTY_RASTER | DGL_DIRTY_TARGET;
    DGL_FPU_LEAVE();
}

