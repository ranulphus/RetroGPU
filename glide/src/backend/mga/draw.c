/* draw.c - vertex conversion, culling and the drawing entry points. */
#include "glide/mg.h"
#include "mga/setup.h"
#include "mga/tex.h"
#include "mga/regs_mga.h"
#include "mga/mmio.h"
#include "combine/combine.h"
#include "tex/texmgr.h"
#include "mga/fp.h"
#include "mga/warp.h"
#include <string.h>

/* The Voodoo converts float vertices to 12.4 fixed point by truncation
 * toward zero; games that follow 3dfx's advice pre-snap to 1/16 anyway.
 * v * 16 is exact, so the FISTP truncation (mga/fp.h) gives the cast's
 * value without its control-word changes. */
static int32_t snap16(float v)
{
    int ok;
    int32_t r = mga_trunc_x16(v, &ok);  /* (the same, from the float's bits) */
    double x;
    if (ok)
        return r;
    x = (double)(v * 16.0f);
    if (x > -2147483647.0 && x < 2147483647.0)
        return mga_itrunc(x);
    return (int32_t)(v * 16.0f);
}

/* (int)mga_floor(x), without a cast's control-word changes in range. */
static int32_t floor_i(double x)
{
    if (x > -2147483647.0 && x < 2147483647.0)
        return mga_ifloor(x);
    return (int32_t)mga_floor(x);
}

static uint32_t zmode_for(GrCmpFnc_t f, int reversed)
{
    switch (f) {
    case GR_CMP_LESS: return reversed ? DWG_ZMODE_ZGT : DWG_ZMODE_ZLT;
    case GR_CMP_EQUAL: return DWG_ZMODE_ZE;
    case GR_CMP_LEQUAL: return reversed ? DWG_ZMODE_ZGTE : DWG_ZMODE_ZLTE;
    case GR_CMP_GREATER: return reversed ? DWG_ZMODE_ZLT : DWG_ZMODE_ZGT;
    case GR_CMP_NOTEQUAL: return DWG_ZMODE_ZNE;
    case GR_CMP_GEQUAL: return reversed ? DWG_ZMODE_ZLTE : DWG_ZMODE_ZGTE;
    default: return DWG_ZMODE_NOZCMP;
    }
}

/* Depth value written by grBufferClear for the current mode. */
uint32_t mg_depth_clear_value(FxU16 depth)
{
    if (mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER || mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS)
        return mg_wdepth_from_code(depth, mg.zbits);
    return mg.zbits == 32 ? ((uint32_t)depth << 16) | depth : depth;
}

static uint32_t argb_const(void) { return mg_color_to_argb(mg.st.constant_color); }

/* sx, sy: snap16 of v->x and v->y (the caller has them for culling). */
static void vtx(mga_svtx *o, const GrVertex *v, int32_t sx, int32_t sy, int wmode, int csrc, int asrc, int spec)
{
    uint32_t c;
    o->X16 = sx;
    o->Y16 = sy;
    if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
        o->Y16 = mg.height * 16 - o->Y16;
    if (mg.scaled) {
        /* After the Voodoo's truncation to 1/16 pixel, so coverage scales
         * the same way everywhere. */
        o->X16 = floor_i(o->X16 * mg.sx);
        o->Y16 = floor_i(o->Y16 * mg.sy);
    }
    if (wmode)
        o->z = mg_wdepth_from_oow(v->oow, mg.zbits);
    else {
        double z = v->ooz + mg.st.depth_bias;
        if (z < 0) z = 0;
        if (z > 65535.0) z = 65535.0;
        o->z = mg.zbits == 32 ? z * 65536.0 : z;
    }
    switch (csrc) {
    case CS_CONSTANT:
        c = argb_const();
        o->r = (float)((c >> 16) & 0xFF); o->g = (float)((c >> 8) & 0xFF); o->b = (float)(c & 0xFF);
        break;
    case CS_CONST_ALPHA:
        o->r = o->g = o->b = (float)(argb_const() >> 24);
        break;
    case CS_ITER_ALPHA:
        o->r = o->g = o->b = v->a < 0 ? 0 : (v->a > 255 ? 255 : v->a);
        break;
    case CS_ZERO:
        o->r = o->g = o->b = 0;
        break;
    case CS_ONE:
        o->r = o->g = o->b = 255;
        break;
    case CS_SOFT:
    case CS_SOFT_FACTOR: {
        float rgb[3];
        mg_combine_vertex(v, csrc, rgb);
        o->r = rgb[0]; o->g = rgb[1]; o->b = rgb[2];
        break; }
    default:
        o->r = v->r < 0 ? 0 : (v->r > 255 ? 255 : v->r);
        o->g = v->g < 0 ? 0 : (v->g > 255 ? 255 : v->g);
        o->b = v->b < 0 ? 0 : (v->b > 255 ? 255 : v->b);
        break;
    }
    if (asrc == AS_SOFT || asrc == AS_MODULATED_SOFT)
        o->a = mg_combine_vertex_alpha(v, asrc);
    else
        o->a = v->a < 0 ? 0 : (v->a > 255 ? 255 : v->a);
    o->fog = mg.fog_on ? (float)(255.0 - mg_fog_vertex(v)) : 255.0f;
    if (spec) {
        float srgb[3];
        mg_combine_vertex_spec(v, spec, srgb);
        o->sr = srgb[0]; o->sg = srgb[1]; o->sb = srgb[2];
    } else
        o->sr = o->sg = o->sb = 0;
    o->q = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v->tmuvtx[0].oow : v->oow;
    o->s = v->tmuvtx[0].sow;          /* scaled to the bound level below */
    o->t = v->tmuvtx[0].tow;
}

/* ---- The G400 family's WARP (docs/g400-warp.md) --------------------------
 * A triangle the CPU would set up, set up by the WARP from the same setup
 * vertices: the position from the snapped X16/Y16 with Mesa's subpixel
 * offset, depth over the buffer's range, the colours as bytes, s and t
 * divided by q. Its edges are the WARP's, not exactly the Voodoo's (the
 * replays' tolerances). Bands (a level or a fog fit per run of rows) and two
 * maps stay on the CPU, as do lines and points. warp=0 turns it off. */
static int warp_ok = -1;            /* -1 not tried yet, 0 off, 1 on */
static int warp_since_cpu;          /* WARP triangles since the CPU's: setup_triangle's shadows are stale */
static int warp_tw = -1, warp_th = -1;  /* the map size the WARP's registers hold (-1 unknown) */

/* Before any state of a triangle is written: the WARP started now, as its
 * first start loads the microcode behind a soft reset, which clears
 * registers (the engine keeps the target, clip and MACCESS flags). */
static void warp_prepare(void)
{
    if (warp_ok < 0) {
        warp_ok = mg_config.warp && warp_available();
        warp_use_agp = mg_config.agp;
        if (mg_config.dma_ring_kb >= 32)
            warp_ring_bytes = (uint32_t)mg_config.dma_ring_kb << 10;
        if (mg_config.dma_list_div >= 2)
            warp_list_div = mg_config.dma_list_div;
        if (mg_config.dma_kick_kb >= 1)
            warp_kick_bytes = (uint32_t)mg_config.dma_kick_kb << 10;
    }
    if (warp_ok && warp_select(WARP_S | WARP_A | WARP_F) != 0) {
        warp_ok = 0;
        mg_log(MG_LOG_WARN, "the WARP did not start: triangles are set up by the CPU");
    }
}

static uint32_t cbyte(float f)
{
    return mga_cbyte(f);                /* f <= 0 ? 0 : f >= 255 ? 255 : floor(f + 0.5), from the float's bits */
}

/* A setup vertex as the WARP takes it, in two parts: what the vertex alone
 * gives (wvtx_base), and its map coordinates (wvtx_uv: after the level's
 * scale and the triangle's whole periods). warp_tri and the fast path both
 * convert with these, so their triangles are the same. zs: depth units to
 * 0..1. */
static void wvtx_base(const mga_svtx *v, mga_wvtx *w, double zs)
{
    w->x = (float)v->X16 * (1.0f / 16.0f) + WARP_SUBPIXEL_X;
    w->y = (float)v->Y16 * (1.0f / 16.0f) + WARP_SUBPIXEL_Y;
    w->z = (float)(v->z * zs);
    w->rhw = v->q;
    w->bgra = cbyte(v->b) | (cbyte(v->g) << 8) | (cbyte(v->r) << 16) | (cbyte(v->a) << 24);
    w->spec = cbyte(v->sb) | (cbyte(v->sg) << 8) | (cbyte(v->sr) << 16) | (cbyte(v->fog) << 24);
}

/* 1/q as the WARP vertex uses it, rounded to a float (a store: the same
 * value whether kept or not, so the fast path keeps it with the vertex). */
static float wvtx_rq(float q)
{
    volatile float rq = 1.0f / (q > 0 ? q : 1e-9f);
    return rq;
}

static void wvtx_uv(const mga_svtx *v, mga_wvtx *w)
{
    const float rq = wvtx_rq(v->q);
    w->u0 = v->s * rq;
    w->v0 = v->t * rq;
    w->u1 = w->v1 = 0;
}

static void to_wvtx(const mga_svtx *v, mga_wvtx *w, double zs)
{
    wvtx_base(v, w, zs);
    wvtx_uv(v, w);
}

/* The map size the WARP's registers are to hold (0: untextured). */
static void warp_sizes(int textured, int tw, int th)
{
    if (!textured) {
        if (warp_tw != 0) {
            warp_tex_sizes(0, 0, 0, 0);
            warp_tw = warp_th = 0;
        }
    } else if (warp_tw != tw || warp_th != th) {
        warp_tex(0, tw, th, 0);
        warp_tw = tw;
        warp_th = th;
    }
}

/* 0 when the WARP took the triangle. */
static int warp_tri(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx)
{
    const mga_svtx *v[3];
    mga_wvtx w[3];
    const double zs = mg.zbits == 32 ? 1.0 / 4294967295.0 : 1.0 / 65535.0;
    int i;
    int po;
    if (warp_ok <= 0 || (ctx->flags & MGA_S_TEX2))
        return -1;
    po = PROF_SWITCH(PROF_G_WARP);
    setup_dwgctl(ctx->dwgctl);
    warp_sizes((ctx->flags & MGA_S_TEX) != 0, ctx->tex_tw, ctx->tex_th);
    v[0] = a; v[1] = b; v[2] = c;
    for (i = 0; i < 3; i++)
        to_wvtx(v[i], &w[i], zs);
    if (warp_vertices(w, 3) != 0) {
        PROF_BACK(po);
        warp_ok = 0;
        mg_log(MG_LOG_WARN, "the WARP stopped taking vertices: triangles are set up by the CPU");
        return -1;
    }
    PROF_BACK(po);
    warp_since_cpu = 1;
    return 0;
}

static void cpu_tri(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx)
{
    if (warp_since_cpu) {
        setup_invalidate();             /* the WARP wrote the registers setup_triangle shadows */
        warp_since_cpu = 0;
        warp_tw = warp_th = -1;         /* (and the setup writes TEXWIDTH/TEXHEIGHT its own way) */
    }
    setup_triangle(a, b, c, ctx);
}

/* At the window's close, or before the engine goes to another program. */
void mg_warp_stop(void)
{
    if (warp_tris || warp_failures)
        warp_stop();
    warp_ok = -1;
    warp_tw = warp_th = -1;
    warp_since_cpu = 0;
}

/* log2 without libm: exponent by scaling, mantissa by the atanh series. */
static double log2d(double v)
{
    double z, z2, ln;
    int e = 0;
    if (v <= 0)
        return -1e9;
    while (v >= 2.0) { v *= 0.5; e++; }
    while (v < 1.0) { v *= 2.0; e--; }
    z = (v - 1.0) / (v + 1.0);
    z2 = z * z;
    ln = 2.0 * z * (1.0 + z2 * (1.0 / 3 + z2 * (1.0 / 5 + z2 * (1.0 / 7 + z2 * (1.0 / 9)))));
    return e + ln * 1.4426950408889634;
}

/* Voodoo LOD model: lambda = log2(max(|grad u|_x, |grad u|_y)) + bias,
 * with u, v the perspective-correct texel coordinates at the largest
 * level; the level is floor(lambda), clamped to the chain. */
typedef struct { double s0, sx, sy, t0, tx, ty, q0, qx, qy, x0, y0; int valid; } lod_planes;

/* k: the largest level's texels per 256 (lod_scale(): the state's alone) */
static double lod_scale(void)
{
    return (256 >> tmu0.large) / 256.0;
}

static void lod_setup_k(lod_planes *L, const GrVertex *a, const GrVertex *b, const GrVertex *c, double k)
{
    const GrVertex *v[3];
    double x[3], y[3], S[3], T[3], Q[3], area;
    int i;
    v[0] = a; v[1] = b; v[2] = c;
    for (i = 0; i < 3; i++) {
        x[i] = v[i]->x; y[i] = v[i]->y;
        Q[i] = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v[i]->tmuvtx[0].oow : v[i]->oow;
        S[i] = v[i]->tmuvtx[0].sow * k;
        T[i] = v[i]->tmuvtx[0].tow * k;
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    L->valid = area != 0;
    if (!L->valid)
        return;
#define PL(A, o) do { \
        L->o##x = ((A[1] - A[0]) * (y[2] - y[0]) - (A[2] - A[0]) * (y[1] - y[0])) / area; \
        L->o##y = ((x[1] - x[0]) * (A[2] - A[0]) - (x[2] - x[0]) * (A[1] - A[0])) / area; \
        L->o##0 = A[0]; } while (0)
    PL(S, s); PL(T, t); PL(Q, q);
#undef PL
    L->x0 = x[0]; L->y0 = y[0];
}

static void lod_setup(lod_planes *L, const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    lod_setup_k(L, a, b, c, lod_scale());
}

/* floor(2 * log2 g) for g > 0, exactly, from the double's bits: with
 * g = m * 2^e (1 <= m < 2) it is 2e, plus 1 when m >= sqrt 2 (no double
 * lies between sqrt 2 and its nearest, which is above it). log2d's series
 * for subnormals. */
static int log2x2_floor(double g)
{
    union { double d; uint32_t w[2]; } c;   /* (32-bit halves: Watcom's 64-bit shifts are calls) */
    uint32_t m;
    int e;
    c.d = g;
    e = (int)((c.w[1] >> 20) & 0x7FF);
    if (e == 0)
        return floor_i(2.0 * log2d(g));
    m = c.w[1] & 0xFFFFF;               /* mantissa >= 6A09E667F3BCDh */
    return 2 * (e - 1023) + (m > 0x6A09Eu || (m == 0x6A09Eu && c.w[0] >= 0x667F3BCDu));
}

/* The quarter levels lod_at adds to floor(2 log2 g): the LOD bias, less the
 * finer levels of a scaled resolution (the state's alone: the fast path
 * works them out once a generation). */
static int lod_offset(void)
{
    int off = (int)(tmu0.lod_bias * 4.0f + (tmu0.lod_bias >= 0 ? 0.5f : -0.5f));
    if (mg.scaled)
        off -= floor_i(2.0 * log2d(mg.sx * mg.sy) + 0.5);   /* more pixels per texel: finer levels */
    return off;
}

static int lod_at_off(const lod_planes *L, double px, double py, int off)
{
    double dx = px - L->x0, dy = py - L->y0;
    double S = L->s0 + L->sx * dx + L->sy * dy, T = L->t0 + L->tx * dx + L->ty * dy;
    double Q = L->q0 + L->qx * dx + L->qy * dy, u, v, gx, gy, g;
    int lam;
    if (!L->valid || Q <= 1e-12)
        return 0;
    u = S / Q; v = T / Q;
    /* max(gx, gy) with gx, gy over the same positive Q * Q: the larger
     * numerator over it, one divide (division keeps the order, and a NaN
     * numerator compares false either way). */
    gx = (L->sx - u * L->qx) * (L->sx - u * L->qx) + (L->tx - v * L->qx) * (L->tx - v * L->qx);
    gy = (L->sy - u * L->qy) * (L->sy - u * L->qy) + (L->ty - v * L->qy) * (L->ty - v * L->qy);
    if (Q * Q < 1e300)
        g = (gx > gy ? gx : gy) / (Q * Q);
    else {                              /* (Q * Q infinite: inf / inf as the two divides had it) */
        gx /= Q * Q;
        gy /= Q * Q;
        g = gx > gy ? gx : gy;
    }
    if (g <= 0)
        return -64 * 4;
    /* quarter levels: floor(4 * log2(sqrt g)) = floor(2 * log2 g) */
    lam = log2x2_floor(g);
    return lam + off;
}

static int lod_at(const lod_planes *L, double px, double py)
{
    return lod_at_off(L, px, py, lod_offset());
}

static GrLOD_t level_for(int quarter_lod)
{
    int l = quarter_lod >= 0 ? quarter_lod / 4 : -((-quarter_lod + 3) / 4);   /* floor */
    if (tmu0.mipmap == GR_MIPMAP_DISABLE)
        return tmu0.large;
    l += tmu0.large;
    if (l < tmu0.large) l = tmu0.large;
    if (l > tmu0.small) l = tmu0.small;
    return (GrLOD_t)l;
}

static int bilinear_on(void)
{
    return tmu0.minf == GR_TEXTUREFILTER_BILINEAR || tmu0.magf == GR_TEXTUREFILTER_BILINEAR ||
           mg_config.forced_bilinear;
}

/* The sampler state for a bound level, from the TMU0 state (tex_emit and
 * tex_adjust_coords in the HAL, hal/src/texhw.c). */
static void tex_state(mga_texstate *t, const tex_level_hw *hw)
{
    memset(t, 0, sizeof *t);
    t->org = hw->org;
    t->w_log2 = hw->w_log2;
    t->h_log2 = hw->h_log2;
    t->pitch = hw->pitch;
    t->hwfmt = (uint32_t)hw->hwfmt;
    t->clamp_u = tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP;
    t->clamp_v = tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP;
    t->bilinear = bilinear_on();
}

typedef struct {
    tex_variant var;          /* keyed texture variant (alpha test / chroma key) */
    uint32_t    alphactrl;    /* ALPHACTRL for the draw */
    int         akey0;        /* key texels whose alpha field is 0 (formats that keep alpha) */
    int         vertex_alpha; /* G200: blending or alpha test reads the iterated alpha */
    int         mip_n;        /* G200: levels in the hardware mip window (0/1 = single level) */
    uint32_t    texctl2;      /* G200: TEXCTL2 (specular enable) */
} draw_opts;

/* The texture registers and ALPHACTRL as last written, so a triangle leaves
 * out writes of the values they hold (each write also ends the WARP's
 * vertex run in the DMA stream, and costs the chip a secondary transfer).
 * Forgotten when anything else may have changed them: a level written into
 * VRAM (the G400's texture cache empties on TEXORG), an engine reset, a new
 * frame (the scaled present samples its own texture). */
static struct { int valid, tex; uint32_t frame, resets, vram_gen, alphactrl; mga_texstate t; } scache;
static uint32_t scache_epoch;           /* moves whenever scache does (the fast path's check) */

/* Forgotten by the present (buffer.c), which writes ALPHACTRL and the
 * texture registers for its own scaling. */
void mg_state_forget(void)
{
    scache.valid = 0;
    scache_epoch++;
}

static int scache_fresh(void)
{
    return scache.valid && scache.frame == mg.frame && scache.resets == engine_resets &&
           scache.vram_gen == tex_vram_gen;
}

static void scache_keep(const mga_texstate *t, uint32_t alphactrl)
{
    scache_epoch++;
    scache.valid = 1;
    scache.frame = mg.frame;
    scache.resets = engine_resets;
    scache.vram_gen = tex_vram_gen;
    scache.alphactrl = alphactrl;
    scache.tex = t != NULL;
    if (t)
        scache.t = *t;
}

/* 1 when the texture state and ALPHACTRL already hold these values;
 * otherwise records them (the caller writes them). */
static int state_cached(const mga_texstate *t, uint32_t alphactrl)
{
    if (scache_fresh() && scache.tex && scache.alphactrl == alphactrl && !memcmp(&scache.t, t, sizeof *t))
        return 1;
    scache_keep(t, alphactrl);
    return 0;
}

/* The same for an untextured draw's ALPHACTRL (the texture state is kept). */
static int alphactrl_cached(uint32_t alphactrl)
{
    if (scache_fresh() && scache.alphactrl == alphactrl)
        return 1;
    if (scache_fresh() && scache.tex) {
        scache.alphactrl = alphactrl;
        scache_epoch++;
        return 0;
    }
    scache_keep(NULL, alphactrl);
    return 0;
}

/* The texture registers' state for a bound level (emit_texture_state's). */
static void texstate_for(mga_texstate *t, const tex_level_hw *hw, int modulate, const draw_opts *o,
                         const uint32_t *mip_org, int mip_n)
{
    int k;
    tex_state(t, hw);
    t->modulate = modulate;
    t->trilinear = mg_config.trilinear;
    t->texctl2 = o->texctl2;
    /* A keyed variant, or alpha-0 texels keyed for an alpha test: alpha 0
     * is transparent. Otherwise texel alpha never keys (G100 spec: opaque). */
    t->key_alpha0 = o->var.kind || (o->akey0 && (hw->hwfmt == HW_TW12 || hw->hwfmt == HW_TW15));
    t->mip_n = mip_n;
    for (k = 0; k < mip_n && k < 5; k++)
        t->mip_org[k] = mip_org[k];
}

static void emit_texture_state(const tex_level_hw *hw, int modulate, const draw_opts *o,
                               const uint32_t *mip_org, int mip_n)
{
    mga_texstate t;
    texstate_for(&t, hw, modulate, o, mip_org, mip_n);
    if (state_cached(&t, o->alphactrl))
        return;
    tex_emit(&t);
    fifo_reserve(1);
    MGA_WR32(MGAREG_ALPHACTRL, o->alphactrl);
}

/* Draw a textured triangle. The G100 samples one level per trapezoid, so
 * when the Voodoo would change level across the triangle it is drawn in
 * horizontal bands, each at the level for its centre (PRD §7.4). */
#define BAND_ROWS 8

/* Table fog over a band of rows: a linear fit of the table around the
 * band centre, through 1/w (which is linear in screen space). The fit is
 * pulled towards the centre value where it would leave 0..255 anywhere in
 * the band, because the fog iterator does not saturate. */
typedef struct { int on; double X[3], Y[3], O[3]; } fog_fit;

#define FOG_SPREAD 24           /* table-fog spread (of 255) that triggers banding */

static int clip_rows(const fog_fit *F, double y0, double y1, double *px, double *py)
{
    double ix[8], iy[8], ox[8], oy[8];
    int n = 3, m, i, pass;
    for (i = 0; i < 3; i++) { ix[i] = F->X[i]; iy[i] = F->Y[i]; }
    for (pass = 0; pass < 2; pass++) {
        m = 0;
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double a = pass ? y1 - iy[i] : iy[i] - y0, b = pass ? y1 - iy[j] : iy[j] - y0;
            if (a >= 0) { ox[m] = ix[i]; oy[m] = iy[i]; m++; }
            if ((a >= 0) != (b >= 0) && m < 8) {
                double t = a / (a - b);
                ox[m] = ix[i] + t * (ix[j] - ix[i]); oy[m] = iy[i] + t * (iy[j] - iy[i]); m++;
            }
        }
        for (i = 0; i < m; i++) { ix[i] = ox[i]; iy[i] = oy[i]; }
        n = m;
    }
    for (i = 0; i < n; i++) { px[i] = ix[i]; py[i] = iy[i]; }
    return n;
}

static void fog_band(const fog_fit *F, int row0, int row1, mga_svtx *sa, mga_svtx *sb, mga_svtx *sc)
{
    double area, ox, oy, xc, yc, oc, fc, d, k = 1.0, px[8], py[8];
    mga_svtx *v[3];
    int i, n;
    v[0] = sa; v[1] = sb; v[2] = sc;
    area = (F->X[1] - F->X[0]) * (F->Y[2] - F->Y[0]) - (F->X[2] - F->X[0]) * (F->Y[1] - F->Y[0]);
    if (area == 0)
        return;
    ox = ((F->O[1] - F->O[0]) * (F->Y[2] - F->Y[0]) - (F->O[2] - F->O[0]) * (F->Y[1] - F->Y[0])) / area;
    oy = ((F->X[1] - F->X[0]) * (F->O[2] - F->O[0]) - (F->X[2] - F->X[0]) * (F->O[1] - F->O[0])) / area;
    n = clip_rows(F, row0, row1, px, py);
    if (n < 3)
        return;
    xc = yc = 0;
    for (i = 0; i < n; i++) { xc += px[i]; yc += py[i]; }
    xc /= n; yc /= n;
    oc = F->O[0] + ox * (xc - F->X[0]) + oy * (yc - F->Y[0]);
    if (oc < 1e-9) oc = 1e-9;
    fc = mg_fog_table_at(oc);
    d = (mg_fog_table_at(oc * 1.03) - mg_fog_table_at(oc / 1.03)) / (oc * 1.03 - oc / 1.03);
    for (i = 0; i < n; i++) {
        double fv = fc + d * (ox * (px[i] - xc) + oy * (py[i] - yc));
        if (fv > 255.0 && fv - fc > 0) { double kk = (255.0 - fc) / (fv - fc); if (kk < k) k = kk; }
        if (fv < 0.0 && fv - fc < 0) { double kk = (0.0 - fc) / (fv - fc); if (kk < k) k = kk; }
    }
    for (i = 0; i < 3; i++)
        v[i]->fog = (float)(255.0 - (fc + k * d * (F->O[i] - oc)));
}

static void draw_band(const mga_svtx *a0, const mga_svtx *b0, const mga_svtx *c0, mga_tri_ctx *ctx,
                      const mg_plan *plan, const draw_opts *o, GrLOD_t lod, int row0, int row1,
                      const fog_fit *F)
{
    mga_svtx sa = *a0, sb = *b0, sc = *c0;
    tex_level_hw hw;
    double sig_s, sig_t;
    int po;
    mg_stats.bands++;
    if (row0 > 0 || row1 < mg.rh)
        mg_stats.banded++;
    if (F && F->on)
        fog_band(F, row0, row1, &sa, &sb, &sc);
    po = PROF_SWITCH(PROF_G_TEXBIND);
    if (plan->tex_white || tex_bind_level(lod, o->var.kind ? &o->var : NULL, &hw) < 0) {
        tex_white(&hw);
        sig_s = sig_t = 0;
    } else {
        sig_s = (double)hw.logical_maxdim / (256.0 * (1 << hw.w_log2));
        sig_t = (double)hw.logical_maxdim / (256.0 * (1 << hw.h_log2));
    }
    PROF_BACK(po);
    if (!tmu0.combine_set)
        sig_s = sig_t = 0;               /* retail Glide: no s,t until a texture combine is set */
    sa.s *= (float)sig_s; sa.t *= (float)sig_t;
    sb.s *= (float)sig_s; sb.t *= (float)sig_t;
    sc.s *= (float)sig_s; sc.t *= (float)sig_t;
    {
        mga_texstate ts;
        tex_state(&ts, &hw);
        tex_adjust_coords(&sa, &sb, &sc, &ts);
    }
    {
        uint32_t mip_org[5];
        int mip_n = 0;
        po = PROF_SWITCH(PROF_G_TEXBIND);
        if (o->mip_n > 1 && !plan->tex_white) {
            /* Bind the following levels while each is 8x8 or more (their
             * pitch then halves, as the G200 expects) and converts to the
             * same hardware format. */
            tex_level_hw hk;
            mip_org[0] = hw.org;
            for (mip_n = 1; mip_n < o->mip_n; mip_n++) {
                if (lod + mip_n > tmu0.small ||
                    tex_bind_level((GrLOD_t)(lod + mip_n), o->var.kind ? &o->var : NULL, &hk) < 0 ||
                    hk.hwfmt != hw.hwfmt || hk.w_log2 != hw.w_log2 - mip_n || hk.h_log2 != hw.h_log2 - mip_n ||
                    hk.w_log2 < 3 || hk.h_log2 < 3)
                    break;
                mip_org[mip_n] = hk.org;
            }
        }
        emit_texture_state(&hw, plan->modulate, o, mip_org, mip_n);
        PROF_BACK(po);
    }
    ctx->tex_tw = hw.w_log2;
    ctx->tex_th = hw.h_log2;
    ctx->clip_y0 = row0 < 0 ? 0 : row0;
    ctx->clip_y1 = row1 > mg.rh ? mg.rh : row1;
    if (mg_log_level() >= MG_LOG_TRACE)
        mg_log(MG_LOG_TRACE, "band lod=%d rows=%d..%d org=%x tw=%d th=%d fmt=%d mod=%d", lod, ctx->clip_y0,
               ctx->clip_y1, hw.org, hw.w_log2, hw.h_log2, hw.hwfmt, plan->modulate);
    if (warp_ok > 0 && !(ctx->flags & MGA_S_TEX2) && !F) {
        /* Every band of levels through the WARP, a band as the whole
         * triangle under the clip narrowed to its rows: the same depths and
         * edges in every band and every pass (a surface's passes mixing the
         * WARP and the CPU's setup flickered where their depths differed).
         * A band with its own fog fit stays on the CPU: the fit holds
         * within the band and its vertices' factors leave 0..255, which a
         * WARP vertex's fog byte can't carry (conformance t17). */
        const int banded = row0 > 0 || row1 < mg.rh;
        mga_engine_state es;
        int r;
        if (banded) {
            int y0, y1;
            engine_save(&es);
            y0 = es.clip[1] > row0 ? es.clip[1] : row0;
            y1 = es.clip[3] < row1 ? es.clip[3] : row1;
            if (y1 <= y0)
                return;                 /* (none of its rows visible) */
            engine_set_clip(es.clip[0], y0, es.clip[2], y1);
        }
        r = warp_tri(&sa, &sb, &sc, ctx);
        if (banded)
            engine_set_clip(es.clip[0], es.clip[1], es.clip[2], es.clip[3]);
        if (r == 0)
            return;
    }
    cpu_tri(&sa, &sb, &sc, ctx);
}

static void draw_textured(const GrVertex *a, const GrVertex *b, const GrVertex *c,
                          const mga_svtx *sa, const mga_svtx *sb, const mga_svtx *sc,
                          mga_tri_ctx *ctx, const mg_plan *plan, const draw_opts *o)
{
    lod_planes L;
    fog_fit F;
    int32_t ymin = sa->Y16, ymax = sa->Y16;
    int row, r0, r1, run_start, need_lod;
    double cx;
    GrLOD_t run_lod;
    int lpo;
    need_lod = !(tmu0.mipmap == GR_MIPMAP_DISABLE || tmu0.small == tmu0.large || plan->tex_white);
    if (sb->Y16 < ymin) ymin = sb->Y16;
    if (sc->Y16 < ymin) ymin = sc->Y16;
    if (sb->Y16 > ymax) ymax = sb->Y16;
    if (sc->Y16 > ymax) ymax = sc->Y16;
    r0 = (ymin + 7) >> 4;
    r1 = (ymax + 7) >> 4;
    if (r1 <= r0)
        return;
    lpo = PROF_SWITCH(PROF_G_LOD);
    F.on = 0;
    if (mg.fog_on && mg_fog_source() == GR_FOG_WITH_TABLE && r1 - r0 > BAND_ROWS) {
        double fmin = sa->fog, fmax = sa->fog;
        if (sb->fog < fmin) fmin = sb->fog;
        if (sc->fog < fmin) fmin = sc->fog;
        if (sb->fog > fmax) fmax = sb->fog;
        if (sc->fog > fmax) fmax = sc->fog;
        if (fmax - fmin > FOG_SPREAD) {
            F.on = 1;
            F.X[0] = sa->X16 / 16.0; F.Y[0] = sa->Y16 / 16.0; F.O[0] = a->oow;
            F.X[1] = sb->X16 / 16.0; F.Y[1] = sb->Y16 / 16.0; F.O[1] = b->oow;
            F.X[2] = sc->X16 / 16.0; F.Y[2] = sc->Y16 / 16.0; F.O[2] = c->oow;
        }
    }
    if (!need_lod && !F.on) {
        PROF_BACK(lpo);
        draw_band(sa, sb, sc, ctx, plan, o, tmu0.large, 0, mg.rh, NULL);
        return;
    }
    if (need_lod)
        lod_setup(&L, a, b, c);
    if (need_lod && mga.max_mip_levels > 1 && (mg_config.hwmip || mg_config.trilinear) &&
        tmu0.lod_bias == 0.0f && !F.on) {
        /* Enhancement (hwmip=1): one draw with a hardware mip window starting
         * at the finest level any part of the triangle needs; the chip picks
         * the level per pixel. It rounds the level where the Voodoo floors
         * it (measured on a G200eR2), so the default keeps the exact banded
         * path. (No LOD bias register: a biased chain keeps it too.) */
        draw_opts mo = *o;
        GrLOD_t l0 = level_for(lod_at(&L, a->x, a->y)), l;
        double cxm = (a->x + b->x + c->x) / 3.0, cym = (a->y + b->y + c->y) / 3.0;
        l = level_for(lod_at(&L, b->x, b->y)); if (l < l0) l0 = l;
        l = level_for(lod_at(&L, c->x, c->y)); if (l < l0) l0 = l;
        l = level_for(lod_at(&L, cxm, cym)); if (l < l0) l0 = l;
        mo.mip_n = mga.max_mip_levels > 5 ? 5 : mga.max_mip_levels;
        PROF_BACK(lpo);
        draw_band(sa, sb, sc, ctx, plan, &mo, l0, 0, mg.rh, NULL);
        return;
    }
    cx = (a->x + b->x + c->x) / 3.0;
    run_start = r0;
    run_lod = (GrLOD_t)-1;
    for (row = r0; row < r1; row += BAND_ROWS) {
        int mid = row + BAND_ROWS / 2, end = row + BAND_ROWS > r1 ? r1 : row + BAND_ROWS;
        double gy;
        GrLOD_t l = tmu0.large;
        if (mid >= r1) mid = (row + r1) / 2;
        gy = mg.st.origin == GR_ORIGIN_LOWER_LEFT ? mg.height - (mid + 0.5) : mid + 0.5;
        if (need_lod)
            l = level_for(lod_at(&L, cx, gy));
        if (F.on) {
            /* Every band gets its own fog fit (and its own level). */
            PROF_BACK(lpo);
            draw_band(sa, sb, sc, ctx, plan, o, l, row, end, &F);
            lpo = PROF_SWITCH(PROF_G_LOD);
            continue;
        }
        if (run_lod == (GrLOD_t)-1)
            run_lod = l;
        else if (l != run_lod) {
            PROF_BACK(lpo);
            draw_band(sa, sb, sc, ctx, plan, o, run_lod, run_start, row, NULL);
            lpo = PROF_SWITCH(PROF_G_LOD);
            run_start = row;
            run_lod = l;
        }
    }
    PROF_BACK(lpo);
    if (!F.on) {
        if (run_start == r0)            /* one level for the whole triangle: its rows need no clip */
            draw_band(sa, sb, sc, ctx, plan, o, run_lod, 0, mg.rh, NULL);
        else
            draw_band(sa, sb, sc, ctx, plan, o, run_lod, run_start, r1, NULL);
    }
}

static int atest_pass(int func, int a, int ref)
{
    switch (func) {
    case GR_CMP_NEVER: return 0;
    case GR_CMP_LESS: return a < ref;
    case GR_CMP_EQUAL: return a == ref;
    case GR_CMP_LEQUAL: return a <= ref;
    case GR_CMP_GREATER: return a > ref;
    case GR_CMP_NOTEQUAL: return a != ref;
    case GR_CMP_GEQUAL: return a >= ref;
    default: return 1;
    }
}

/* Apply the alpha plan: alpha test, chroma key and blending. Returns 0
 * when the triangle draws nothing. */
/* Alpha below which stipple coverage rounds to zero (half of 1/16). */
#define STIPPLE_MIN_ALPHA 8

static int alpha_setup(mg_plan *plan, const mg_aplan *ap, draw_opts *o, mga_svtx *sa, mga_svtx *sb, mga_svtx *sc)
{
    const mg_state *s = &mg.st;
    uint32_t at_bits = 0;
    mga_svtx *v[3];
    int i, tex_alpha = ap->alpha_src == AS_TEXTURE || ap->alpha_src == AS_MODULATED_ITER ||
                       ap->alpha_src == AS_MODULATED_CONST || ap->alpha_src == AS_MODULATED_SOFT;
    int const_a = (int)(mg_color_to_argb(s->constant_color) >> 24);
    int binary = tmu0.fmt == GR_TEXFMT_ARGB_1555;
    v[0] = sa; v[1] = sb; v[2] = sc;
    /* Per-vertex alpha for the diffuse plane. */
    for (i = 0; i < 3; i++) {
        if (ap->alpha_src == AS_CONSTANT || ap->alpha_src == AS_MODULATED_CONST)
            v[i]->a = (float)const_a;
        else if (ap->alpha_src == AS_ONE || ap->alpha_src == AS_TEXTURE)
            v[i]->a = 255;
        if (ap->fixed_alpha >= 0)
            v[i]->a = (float)ap->fixed_alpha;
        if (ap->invert)
            v[i]->a = 255 - v[i]->a;
    }
    /* Alpha test: native on the G200 family, otherwise keyed texture
     * variants (texture alpha) or a per-triangle decision (vertex alpha). */
    if (ap->atest != GR_CMP_ALWAYS && mga.has_alpha_test) {
        static const uint8_t atmode[8] = { 0, 4, 2, 5, 6, 3, 7, 0 };   /* by GR_CMP_* */
        at_bits = ALPHACTRL_ATEN | ALPHACTRL_ATMODE(atmode[ap->atest & 7]) | ALPHACTRL_ATREF(s->alpha_test_ref);
    } else if (ap->atest != GR_CMP_ALWAYS) {
        if (tex_alpha && plan->textured) {
            o->var.kind |= TV_ATEST;
            o->var.afunc = ap->atest;
            o->var.aref = s->alpha_test_ref;
        } else {
            int pass = 0;
            for (i = 0; i < 3; i++)
                pass += atest_pass(ap->atest, (int)v[i]->a, s->alpha_test_ref);
            if (!pass)
                return 0;
        }
    }
    /* Chroma key: exact texel matches become transparent. */
    if (s->chroma_mode == GR_CHROMAKEY_ENABLE) {
        uint32_t key = mg_color_to_argb(s->chroma_value) & 0xFFFFFF;
        if (plan->textured && !plan->tex_white) {
            o->var.kind |= TV_CHROMA;
            o->var.chroma = key;
        } else if (!plan->textured) {
            /* Untextured: keyed out only when the whole triangle is the key colour. */
            int hit = 0;
            for (i = 0; i < 3; i++) {
                uint32_t c = ((uint32_t)(v[i]->r + 0.5f) << 16) | ((uint32_t)(v[i]->g + 0.5f) << 8) |
                             (uint32_t)(v[i]->b + 0.5f);
                hit += c == key;
            }
            if (hit == 3)
                return 0;
        }
    }
    /* Blending. */
    if (mga.has_alpha_blend) {
        uint32_t f = ap->native ? ap->factors : (ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO));
        int sel = !(tex_alpha && plan->textured) ? ALPHASEL_DIFFUSE
                : ap->alpha_src == AS_TEXTURE ? ALPHASEL_TEXTURE : ALPHASEL_MODULATED;
        o->alphactrl = f | at_bits | ALPHACTRL_ALPHASEL(sel);
        /* Any alpha but the texel's own comes from the alpha plane. */
        o->vertex_alpha = (ap->native || at_bits) && sel != ALPHASEL_TEXTURE;
        return 1;
    }
    if (ap->stipple) {
        if (ap->alpha_src == AS_TEXTURE && binary && plan->textured && ap->fixed_alpha < 0) {
            /* 1-bit alpha: blending with alpha 0 or 1 is exactly a key. */
            o->var.kind |= TV_ATEST;
            o->var.afunc = ap->invert ? GR_CMP_LESS : GR_CMP_GEQUAL;
            o->var.aref = 128;
            return 1;
        }
        /* The stipple threshold matrix includes 0, so any alpha draws at
         * least one pixel in 16, where a blend with alpha ~0 draws nothing.
         * Drop what would round to zero coverage: whole triangles when the
         * alpha comes from the vertices, texels when it comes from the
         * texture (hardware alpha key, which keeps the texel alpha). */
        {
            int src_vtx = ap->fixed_alpha >= 0 || ap->alpha_src != AS_TEXTURE;
            int src_tex = ap->fixed_alpha < 0 && tex_alpha && plan->textured && !ap->invert;
            if (src_vtx && ap->alpha_src != AS_TEXTURE) {
                int max_a = 0;
                for (i = 0; i < 3; i++)
                    if ((int)v[i]->a > max_a)
                        max_a = (int)v[i]->a;
                if (max_a < STIPPLE_MIN_ALPHA)
                    return 0;
            }
            if (src_tex)
                o->akey0 = 1;   /* hardware key on alpha 0; no conversion, alpha kept */
        }
        if (!plan->textured) {
            /* G100 applies alpha only in textured trapezoids. */
            plan->textured = 1;
            plan->tex_white = 1;
            plan->modulate = 1;
        }
        o->alphactrl = ALPHACTRL_G100_FIXED | ALPHACTRL_ASTIPPLE |
                       ALPHACTRL_ALPHASEL(ap->alpha_src == AS_TEXTURE && ap->fixed_alpha < 0 ? ALPHASEL_TEXTURE :
                                          (ap->alpha_src == AS_MODULATED_ITER || ap->alpha_src == AS_MODULATED_CONST ||
                                           ap->alpha_src == AS_MODULATED_SOFT) &&
                                          ap->fixed_alpha < 0 ? ALPHASEL_MODULATED : ALPHASEL_DIFFUSE);
    }
    return 1;
}

/* ---- Subdivision -----------------------------------------------------------
 * A combine that multiplies two iterated terms is not linear across the
 * triangle, so per-vertex evaluation drifts in the interior. Large such
 * triangles are split into SUBDIV x SUBDIV pieces (error falls with the
 * square of the size). Attributes are interpolated linearly in screen
 * space, as the hardware iterates them; points on an original edge are
 * computed from the edge's endpoints in a canonical order, so a
 * neighbour sharing the edge gets bit-identical vertices. */
#define SUBDIV 4
#define SUBDIV_MIN_AREA 256.0f          /* pixels^2 */

static int subdividing;

static void vlerp(GrVertex *o, const GrVertex *p, const GrVertex *q, float t)
{
    const float *fp = (const float *)p, *fq = (const float *)q;
    float *fo = (float *)o;
    unsigned i;
    for (i = 0; i < sizeof(GrVertex) / sizeof(float); i++)
        fo[i] = fp[i] + (fq[i] - fp[i]) * t;
}

static void edge_point(GrVertex *o, const GrVertex *p, const GrVertex *q, int k, int n)
{
    if (p->x < q->x || (p->x == q->x && p->y < q->y))
        vlerp(o, p, q, (float)k / n);
    else
        vlerp(o, q, p, (float)(n - k) / n);
}

static void subdivide(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    static GrVertex P[(SUBDIV + 1) * (SUBDIV + 1)];
    int i, j;
#define PT(i, j) P[(j) * (SUBDIV + 1) + (i)]
    for (j = 0; j <= SUBDIV; j++)
        for (i = 0; i + j <= SUBDIV; i++) {
            GrVertex *o = &PT(i, j);
            if (j == 0) edge_point(o, a, b, i, SUBDIV);
            else if (i == 0) edge_point(o, a, c, j, SUBDIV);
            else if (i + j == SUBDIV) edge_point(o, b, c, j, SUBDIV);
            else {
                GrVertex ab, ac;
                vlerp(&ab, a, b, (float)i / SUBDIV);
                vlerp(&ac, a, c, (float)j / SUBDIV);
                {   /* o = a + (b - a) i/n + (c - a) j/n */
                    const float *fa = (const float *)a, *fb = (const float *)&ab, *fc = (const float *)&ac;
                    float *fo = (float *)o;
                    unsigned k;
                    for (k = 0; k < sizeof(GrVertex) / sizeof(float); k++)
                        fo[k] = fb[k] + fc[k] - fa[k];
                }
            }
        }
    for (j = 0; j < SUBDIV; j++)
        for (i = 0; i + j < SUBDIV; i++) {
            mg_draw_tri(&PT(i, j), &PT(i + 1, j), &PT(i, j + 1));
            if (i + j < SUBDIV - 1)
                mg_draw_tri(&PT(i + 1, j), &PT(i + 1, j + 1), &PT(i, j + 1));
        }
#undef PT
}

/* ---- The fast path (G400 family) ------------------------------------------
 * What mg_draw_tri works out from the state alone - the plans, alpha_setup's
 * ALPHACTRL, validation, DWGCTL, the level bound and its texture registers -
 * is worked out once a state generation (mg_state_gen moves with every call
 * but the drawing ones). A triangle the WARP sets up whole at one level then
 * costs its vertices: made by vtx() and alpha_setup's rules, scaled and
 * adjusted as draw_band does, converted by to_wvtx and written straight into
 * the DMA stream: the triangle the general path would send. Bands (of levels
 * or of fog), subdivided or soft combines, specular, keyed textures, the
 * hardware mip window and chips without the WARP go the general way, as do
 * triangles whose state the fast path can't take. fast=0 turns it off. */
static struct {
    FxU32 gen;                  /* the generation it was made for */
    int made;                   /* (gen is valid) */
    int ok;                     /* the state takes the fast path */
    int wmode, csrc, asrc, textured, tex_white, modulate, need_lod, table_fog;
    int a_set, a_invert;        /* alpha_setup: the vertices' alpha set to a_k, then 255 - it */
    float a_k;
    draw_opts opts;
    uint32_t dwgctl;
    struct fp_level {           /* each level as bound in this generation */
        int valid;
        tex_level_hw hw;
        float sig_s, sig_t;
        mga_texstate ts;        /* (tex_adjust_coords') */
        mga_texstate regs;      /* the texture registers' (emit_texture_state's) */
    } lv[GR_LOD_1 + 1];
    int lod_off;                /* lod_offset() */
    double lod_k;               /* lod_scale() */
    int emitted_lod;            /* the level whose registers were last written, */
    uint32_t emitted;           /* ... at this scache_epoch (0: not yet) */
} fp;

/* The fast path's vertices by address, as the state of one generation made
 * them (fp_epoch moves with each): vtx() and alpha_setup's rules, then
 * wvtx_base. A game's triangles share vertices; one changed in place since
 * misses (its contents are compared). */
#define FP_SLOTS_LOG2 8
typedef struct {
    const GrVertex *key;
    uint32_t epoch;
    GrVertex in;
    mga_svtx sv;
    mga_wvtx w;                 /* (its x, y, z, rhw, bgra, spec) */
    float rq;                   /* wvtx_rq(sv.q) */
    int lvl;                    /* the level ls..fv are for (-1 none) */
    int norm;                   /* ls, lt finite, q positive and finite: fp_adjust may use fu, fv */
    float ls, lt;               /* s, t scaled to the level (draw_band's) */
    double fu, fv;              /* mga_floor of ls / q, lt / q (tex_adjust_coords' terms) */
} fp_vtx;
static fp_vtx fpv[1 << FP_SLOTS_LOG2];
static uint32_t fp_epoch = 1;

static void fp_fill(fp_vtx *e, const GrVertex *v, int32_t sx, int32_t sy, double zs)
{
    e->key = v;
    vtx(&e->sv, v, sx, sy, fp.wmode, fp.csrc, fp.asrc, 0);
    if (fp.a_set)
        e->sv.a = fp.a_k;
    if (fp.a_invert)
        e->sv.a = 255 - e->sv.a;
    wvtx_base(&e->sv, &e->w, zs);
    e->rq = wvtx_rq(e->sv.q);
    e->lvl = -1;
}

/* *a == *b, bit for bit: its 15 words (include/glide/layout.h; memcmp is a
 * call and a byte loop) */
static int same_vtx(const GrVertex *a, const GrVertex *b)
{
    const uint32_t *p = (const uint32_t *)a, *q = (const uint32_t *)b;
    return !((p[0] ^ q[0]) | (p[1] ^ q[1]) | (p[2] ^ q[2]) | (p[3] ^ q[3]) | (p[4] ^ q[4]) | (p[5] ^ q[5]) |
             (p[6] ^ q[6]) | (p[7] ^ q[7]) | (p[8] ^ q[8]) | (p[9] ^ q[9]) | (p[10] ^ q[10]) | (p[11] ^ q[11]) |
             (p[12] ^ q[12]) | (p[13] ^ q[13]) | (p[14] ^ q[14]));
}

static fp_vtx *fp_vert(const GrVertex *v, int32_t sx, int32_t sy, double zs)
{
    fp_vtx *e = &fpv[((uint32_t)(size_t)v * 2654435761u) >> (32 - FP_SLOTS_LOG2)];
    if (e->key == v && e->epoch == fp_epoch && same_vtx(&e->in, v))
        return e;
    e->epoch = fp_epoch;
    e->in = *v;
    fp_fill(e, v, sx, sy, zs);
    return e;
}

static void fp_prepare(const mg_plan *plan_in, const mg_aplan *ap, int depth, int wmode)
{
    mg_plan plan = *plan_in;
    mga_svtx d[3];
    fp.gen = mg_state_gen;
    fp.made = 1;
    fp.ok = 0;
    memset(fp.lv, 0, sizeof fp.lv);
    fp.emitted_lod = -1;
    fp.emitted = 0;
    if (++fp_epoch == 0) {              /* (every slot stale) */
        memset(fpv, 0, sizeof fpv);
        fp_epoch = 1;
    }
    if (!mg_config.fast || warp_ok <= 0 || !mga.has_alpha_blend || mg.st.delta0 || plan.quadratic ||
        ap->quadratic || plan.approx || ap->approx || mg_config.census || ap->skip || (ap->color_off && !depth) ||
        plan.spec || (ap->atest != GR_CMP_ALWAYS && !mga.has_alpha_test) ||
        mg.st.chroma_mode == GR_CHROMAKEY_ENABLE || mg_config.hwmip || mg_config.trilinear ||
        mg_log_level() >= MG_LOG_TRACE)
        return;
    /* mg_draw_tri's fog rules */
    mg.fog_on = mg_fog_source() != GR_FOG_DISABLE;
    if (mg.fog_on) {
        if (mg.st.fog_mode & GR_FOG_MULT2) {
            plan.color_src = CS_ZERO;
            plan.modulate = 1;
        }
        if (!plan.textured) {
            plan.textured = 1;
            plan.tex_white = 1;
            plan.modulate = 1;
        }
    }
    memset(&fp.opts, 0, sizeof fp.opts);
    fp.opts.alphactrl = ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE);
    memset(d, 0, sizeof d);
    if (!alpha_setup(&plan, ap, &fp.opts, &d[0], &d[1], &d[2]) || fp.opts.var.kind || fp.opts.akey0)
        return;                         /* (the state's ALPHACTRL; with these states it draws every triangle) */                         /* (the state's ALPHACTRL; with these states it draws every triangle) */
    fp.a_set = ap->alpha_src == AS_CONSTANT || ap->alpha_src == AS_MODULATED_CONST || ap->alpha_src == AS_ONE ||
               ap->alpha_src == AS_TEXTURE || ap->fixed_alpha >= 0;
    fp.a_k = ap->fixed_alpha >= 0 ? (float)ap->fixed_alpha
           : ap->alpha_src == AS_CONSTANT || ap->alpha_src == AS_MODULATED_CONST
           ? (float)(int)(mg_color_to_argb(mg.st.constant_color) >> 24) : 255.0f;
    fp.a_invert = ap->invert;
    fp.wmode = wmode;
    fp.csrc = plan.color_src;
    fp.asrc = ap->alpha_src;
    /* Soft combines whose per-vertex value is the clamped iterated colour or
     * alpha (mg_combine_vertex: a LOCAL factor or function of an iterated
     * local, uninverted) are vtx()'s iterated rule: the same values. */
    if (mg.st.cc_local == GR_COMBINE_LOCAL_ITERATED &&
        ((fp.csrc == CS_SOFT_FACTOR && mg.st.cc_factor == GR_COMBINE_FACTOR_LOCAL) ||
         (fp.csrc == CS_SOFT && mg.st.cc_func == GR_COMBINE_FUNCTION_LOCAL && !mg.st.cc_invert)))
        fp.csrc = CS_ITERATED;
    if (mg.st.ac_local == GR_COMBINE_LOCAL_ITERATED &&
        ((fp.asrc == AS_MODULATED_SOFT &&
          (mg.st.ac_factor == GR_COMBINE_FACTOR_LOCAL || mg.st.ac_factor == GR_COMBINE_FACTOR_LOCAL_ALPHA)) ||
         (fp.asrc == AS_SOFT && !mg.st.ac_invert &&
          (mg.st.ac_func == GR_COMBINE_FUNCTION_LOCAL || mg.st.ac_func == GR_COMBINE_FUNCTION_LOCAL_ALPHA))))
        fp.asrc = AS_ITERATED;
    fp.textured = plan.textured;
    fp.tex_white = plan.tex_white;
    fp.modulate = plan.modulate;
    fp.need_lod = !(tmu0.mipmap == GR_MIPMAP_DISABLE || tmu0.small == tmu0.large || plan.tex_white);
    fp.lod_off = lod_offset();
    fp.lod_k = lod_scale();
    fp.table_fog = mg.fog_on && mg_fog_source() == GR_FOG_WITH_TABLE;
    fp.dwgctl = DWG_BOP_COPY | (plan.textured ? DWG_OPCOD_TEXTURE_TRAP : DWG_OPCOD_TRAP) |
                (depth ? (mg.st.depth_mask ? DWG_ATYPE_ZI : DWG_ATYPE_I) | zmode_for(mg.st.depth_func, wmode)
                       : DWG_ATYPE_I | DWG_ZMODE_NOZCMP);
    mg_validate();
    if (mg.fog_on)
        mg_fog_emit_color();
    fp.ok = 1;
}

/* The triangle's three vertices into the DMA stream: the cached part and
 * the map coordinates st (s, t a vertex: scaled and moved by whole periods,
 * or as they are for an untextured draw), times the vertex's 1/q (wvtx_uv).
 * 0, or -1 when the WARP stopped taking them. */
static int fp_send(fp_vtx *const *P, const float *st, int tw, int th)
{
    uint32_t *d;
    int i, dw;
    setup_dwgctl(fp.dwgctl);
    warp_sizes(fp.textured, tw, th);
    d = warp_reserve(3, &dw);
    if (d) {                            /* straight into the DMA stream */
        for (i = 0; i < 3; i++, d += dw) {
            float u = st[2 * i] * P[i]->rq, v = st[2 * i + 1] * P[i]->rq;
            memcpy(d, &P[i]->w, 24);    /* x, y, z, rhw, bgra, spec */
            memcpy(d + 6, &u, 4);
            memcpy(d + 7, &v, 4);
            if (dw == 10)
                d[8] = d[9] = 0;
        }
        warp_commit(3);
    } else {
        mga_wvtx w[3];
        for (i = 0; i < 3; i++) {
            w[i] = P[i]->w;
            w[i].u0 = st[2 * i] * P[i]->rq;
            w[i].v0 = st[2 * i + 1] * P[i]->rq;
            w[i].u1 = w[i].v1 = 0;
        }
        if (warp_vertices(w, 3) != 0) {
            warp_ok = 0;
            mg_log(MG_LOG_WARN, "the WARP stopped taking vertices: triangles are set up by the CPU");
            return -1;
        }
    }
    warp_since_cpu = 1;
    return 0;
}

/* A float's exponent is not all ones (not an infinity or NaN). */
static int finite_f(float f)
{
    union { float f; uint32_t u; } c;
    c.f = f;
    return (c.u & 0x7F800000u) != 0x7F800000u;
}

/* The vertex's coordinates at level lv (draw_band's scale), and the terms
 * tex_adjust_coords takes the smallest of. */
static void fp_at_level(fp_vtx *e, GrLOD_t lod, const struct fp_level *lv)
{
    e->lvl = lod;
    e->ls = e->sv.s * lv->sig_s;
    e->lt = e->sv.t * lv->sig_t;
    e->norm = finite_f(e->ls) && finite_f(e->lt) && finite_f(e->sv.q) && e->sv.q > 0;
    if (e->norm) {
        e->fu = mga_floor((double)e->ls / (double)e->sv.q);
        e->fv = mga_floor((double)e->lt / (double)e->sv.q);
    }
}

/* draw_band's for the fast path: the triangle at level lod, its vertices'
 * coordinates scaled to the level and moved by whole periods; under the
 * clip narrowed to rows [row0, row1) when es holds the clip to narrow.
 * 0, 1 when none of its rows is visible, -1 when the WARP failed.
 *
 * The whole periods are tex_adjust_coords' (map 0, no half-texel shift):
 * floor of the smallest s / q, as a double, which is the smallest of the
 * vertices' floors (rounding and floor keep the order), kept with each
 * vertex for its level. Anything else (an infinity, a NaN, q <= 0, the
 * half-texel shift) goes through tex_adjust_coords itself. */
static int fp_level(fp_vtx *const *P, GrLOD_t lod, int row0, int row1, const mga_engine_state *es)
{
    struct fp_level *lv;
    float st[6];
    int i, r, po = PROF_SWITCH(PROF_G_TEXBIND);
    lv = &fp.lv[lod];
    if (!lv->valid) {                   /* draw_band's binding */
        double sig_s, sig_t;
        if (fp.tex_white || tex_bind_level(lod, NULL, &lv->hw) < 0) {
            tex_white(&lv->hw);
            sig_s = sig_t = 0;
        } else {
            sig_s = (double)lv->hw.logical_maxdim / (256.0 * (1 << lv->hw.w_log2));
            sig_t = (double)lv->hw.logical_maxdim / (256.0 * (1 << lv->hw.h_log2));
        }
        if (!tmu0.combine_set)
            sig_s = sig_t = 0;
        lv->sig_s = (float)sig_s;
        lv->sig_t = (float)sig_t;
        tex_state(&lv->ts, &lv->hw);
        texstate_for(&lv->regs, &lv->hw, fp.modulate, &fp.opts, NULL, 0);
        lv->valid = 1;
    }
    mg_stats.bands++;
    for (i = 0; i < 3; i++)
        if (P[i]->lvl != lod)
            fp_at_level(P[i], lod, lv);
    if (P[0]->norm && P[1]->norm && P[2]->norm && !(lv->ts.bilinear && !mga.texel_centre)) {
        double su = 0, sv = 0;
        if (!lv->ts.clamp_u) {
            double m = P[0]->fu < 1e30 ? P[0]->fu : 1e30;   /* (tex_adjust_coords' mn starts at 1e30) */
            if (P[1]->fu < m) m = P[1]->fu;
            if (P[2]->fu < m) m = P[2]->fu;
            su -= m;
        }
        if (!lv->ts.clamp_v) {
            double m = P[0]->fv < 1e30 ? P[0]->fv : 1e30;
            if (P[1]->fv < m) m = P[1]->fv;
            if (P[2]->fv < m) m = P[2]->fv;
            sv -= m;
        }
        for (i = 0; i < 3; i++) {
            st[2 * i] = P[i]->ls + (float)(su * P[i]->sv.q);
            st[2 * i + 1] = P[i]->lt + (float)(sv * P[i]->sv.q);
        }
    } else {
        mga_svtx tq[3];
        for (i = 0; i < 3; i++) {
            tq[i].s = P[i]->ls;
            tq[i].t = P[i]->lt;
            tq[i].q = P[i]->sv.q;
        }
        tex_adjust_coords(&tq[0], &tq[1], &tq[2], &lv->ts);
        for (i = 0; i < 3; i++) {
            st[2 * i] = tq[i].s;
            st[2 * i + 1] = tq[i].t;
        }
    }
    if (lod != fp.emitted_lod || fp.emitted == 0 || fp.emitted != scache_epoch || !scache_fresh()) {
        if (fp.emitted && fp.emitted == scache_epoch && scache_fresh() && scache.tex &&
            scache.alphactrl == fp.opts.alphactrl) {
            /* another level of this generation's: the registers that differ */
            tex_emit_change(&lv->regs, &scache.t);
            scache_keep(&lv->regs, fp.opts.alphactrl);
        } else
            emit_texture_state(&lv->hw, fp.modulate, &fp.opts, NULL, 0);
        fp.emitted_lod = lod;
        fp.emitted = scache_epoch;
    }
    PROF_BACK(po);
    if (es) {
        int y0 = es->clip[1] > row0 ? es->clip[1] : row0, y1 = es->clip[3] < row1 ? es->clip[3] : row1;
        mg_stats.banded++;
        if (y1 <= y0)
            return 1;                   /* (none of its rows visible) */
        engine_set_clip(es->clip[0], y0, es->clip[2], y1);
    }
    po = PROF_SWITCH(PROF_G_WARP);
    r = fp_send(P, st, lv->hw.w_log2, lv->hw.h_log2);
    PROF_BACK(po);
    return r;
}

/* 1 when the triangle was drawn (or has nothing to draw), 0: the general path. */
#define FP_RUNS 16                      /* (runs of one level a triangle; more: the general path) */
static int fp_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c, int32_t ax, int32_t ay, int32_t bx,
                  int32_t by, int32_t cx, int32_t cy)
{
    const double zs = mg.zbits == 32 ? 1.0 / 4294967295.0 : 1.0 / 65535.0;
    fp_vtx *P[3];
    fp_vtx spare[3];
    int i, po;
    if (mg.dirty)
        mg_validate();
    po = PROF_SWITCH(PROF_G_VERT);
    P[0] = fp_vert(a, ax, ay, zs);
    P[1] = fp_vert(b, bx, by, zs);
    P[2] = fp_vert(c, cx, cy, zs);
    if (P[0]->key != a) {               /* (b or c took a's slot; b's c's: made again, uncached) */
        fp_fill(&spare[0], a, ax, ay, zs);
        P[0] = &spare[0];
    }
    if (P[1]->key != b) {
        fp_fill(&spare[1], b, bx, by, zs);
        P[1] = &spare[1];
    }
    PROF_BACK(po);
    if (!fp.textured) {
        float st[6];
        int r;
        if (!alphactrl_cached(fp.opts.alphactrl)) {
            fifo_reserve(1);
            MGA_WR32(MGAREG_ALPHACTRL, fp.opts.alphactrl);
        }
        for (i = 0; i < 3; i++) {       /* (the coordinates as they are: wvtx_uv's) */
            st[2 * i] = P[i]->sv.s;
            st[2 * i + 1] = P[i]->sv.t;
        }
        po = PROF_SWITCH(PROF_G_WARP);
        r = fp_send(P, st, 0, 0);
        PROF_BACK(po);
        if (r != 0)
            return 0;
    } else {
        int32_t ymin = P[0]->sv.Y16, ymax = P[0]->sv.Y16;
        GrLOD_t rl[FP_RUNS];            /* each run's level and first row */
        int rs[FP_RUNS + 1], nr = 1, r0, r1;
        if (P[1]->sv.Y16 < ymin) ymin = P[1]->sv.Y16;
        if (P[2]->sv.Y16 < ymin) ymin = P[2]->sv.Y16;
        if (P[1]->sv.Y16 > ymax) ymax = P[1]->sv.Y16;
        if (P[2]->sv.Y16 > ymax) ymax = P[2]->sv.Y16;
        r0 = (ymin + 7) >> 4;
        r1 = (ymax + 7) >> 4;
        if (r1 <= r0)
            return 1;                   /* (no rows: draw_textured draws nothing) */
        if (fp.table_fog && r1 - r0 > BAND_ROWS) {
            float fmin = P[0]->sv.fog, fmax = P[0]->sv.fog;
            for (i = 1; i < 3; i++) {
                if (P[i]->sv.fog < fmin) fmin = P[i]->sv.fog;
                if (P[i]->sv.fog > fmax) fmax = P[i]->sv.fog;
            }
            if ((double)fmax - (double)fmin > FOG_SPREAD)
                return 0;               /* (bands with their own fog fits) */
        }
        rl[0] = tmu0.large;
        rs[0] = r0;
        if (fp.need_lod) {
            /* draw_textured's bands, each at the level for its centre, in
             * runs of one level */
            lod_planes L;
            double cxm;
            int row, lpo = PROF_SWITCH(PROF_G_LOD);
            lod_setup_k(&L, a, b, c, fp.lod_k);
            cxm = (a->x + b->x + c->x) / 3.0;
            for (row = r0; row < r1; row += BAND_ROWS) {
                int mid = row + BAND_ROWS / 2;
                double gy;
                GrLOD_t l;
                if (mid >= r1) mid = (row + r1) / 2;
                gy = mg.st.origin == GR_ORIGIN_LOWER_LEFT ? mg.height - (mid + 0.5) : mid + 0.5;
                l = level_for(lod_at_off(&L, cxm, gy, fp.lod_off));
                if (row == r0)
                    rl[0] = l;
                else if (l != rl[nr - 1]) {
                    if (nr == FP_RUNS) {
                        PROF_BACK(lpo);
                        return 0;
                    }
                    rl[nr] = l;
                    rs[nr++] = row;
                }
            }
            PROF_BACK(lpo);
        }
        for (i = 0; i < nr; i++)
            if ((unsigned)rl[i] > GR_LOD_1)
                return 0;
        rs[nr] = r1;
        if (nr == 1) {                  /* the whole triangle, its rows unclipped */
            if (fp_level(P, rl[0], 0, mg.rh, NULL) < 0)
                return 0;
        } else {
            /* the whole triangle at each run's level, under the clip
             * narrowed to the run's rows (draw_band) */
            mga_engine_state es;
            int k, r = 0;
            engine_save(&es);
            for (k = 0; k < nr && r >= 0; k++)
                r = fp_level(P, rl[k], rs[k], rs[k + 1], &es);
            engine_set_clip(es.clip[0], es.clip[1], es.clip[2], es.clip[3]);
            if (r < 0 && k == 1)
                return 0;               /* (the WARP failed at once: the general path, by the CPU) */
            mg_stats.fast_bands++;
        }
    }
    mg_stats.fast++;
    return 1;
}

void mg_draw_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    mga_svtx sa, sb, sc;
    mga_tri_ctx ctx;
    mg_plan plan;
    mg_aplan ap;
    draw_opts opts;
    int vpo;
    int wmode = mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER || mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux;
    int64_t area;
    int32_t ax, ay, bx, by, cx, cy;
    if (!mg.open)
        return;
    mg_stats.tri_processed++;
    if (depth && mg.st.depth_func == GR_CMP_NEVER)
        return;
    ax = snap16(a->x); ay = snap16(a->y);
    bx = snap16(b->x); by = snap16(b->y);
    cx = snap16(c->x); cy = snap16(c->y);
    /* Culling uses the game's orientation (before any origin flip). */
    if (mg.st.cull != GR_CULL_DISABLE) {
        int sgn;
        if ((((uint32_t)ax + 16384u) | ((uint32_t)ay + 16384u) | ((uint32_t)bx + 16384u) | ((uint32_t)by + 16384u) |
             ((uint32_t)cx + 16384u) | ((uint32_t)cy + 16384u)) < 32768u) {
            /* every position within 1024 pixels of the origin: the
             * products fit 32 bits (Watcom's 64-bit multiplies are calls) */
            int32_t a32 = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            sgn = (a32 > 0) - (a32 < 0);
        } else {
            area = ((int64_t)bx - ax) * ((int64_t)cy - ay) - ((int64_t)cx - ax) * ((int64_t)by - ay);
            sgn = (area > 0) - (area < 0);
        }
        if (sgn == 0)
            return;
        if (mg.st.cull == GR_CULL_NEGATIVE && sgn < 0)
            return;
        if (mg.st.cull == GR_CULL_POSITIVE && sgn > 0)
            return;
    }
    if (!mg.st.color_mask_rgb && !(depth && mg.st.depth_mask))
        return;
    warp_prepare();
    if (mg.st.delta0) {
        /* DELTA0: every vertex carries the grConstantColorValue4 colour. */
        static GrVertex d[3];
        uint32_t k = mg.st.delta0_argb;
        int n;
        d[0] = *a; d[1] = *b; d[2] = *c;
        for (n = 0; n < 3; n++) {
            d[n].r = (float)((k >> 16) & 0xFF); d[n].g = (float)((k >> 8) & 0xFF); d[n].b = (float)(k & 0xFF);
        }
        a = &d[0]; b = &d[1]; c = &d[2];
    }
    {
        /* Plans depend only on state: recompute them when it changed. */
        static mg_plan c_plan;
        static mg_aplan c_ap;
        static uint32_t c_gen;
        if (c_gen != mg_state_gen || !fp.made) {
            mg_combine_plan(&c_plan);
            mg_alpha_plan(&c_ap);
            c_gen = mg_state_gen;
            fp_prepare(&c_plan, &c_ap, depth, wmode);
        }
        if (fp.ok && warp_ok > 0) {
            int r, fpo = PROF_SWITCH(PROF_G_FAST);
            r = fp_tri(a, b, c, ax, ay, bx, by, cx, cy);
            PROF_BACK(fpo);
            if (r) {
                mg_stats.tris++;
                mg_stats.tri_drawn++;
                return;
            }
        }
        plan = c_plan;
        ap = c_ap;
    }
    if ((plan.quadratic || ap.quadratic) && !subdividing) {
        float ar = ((b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y)) * 0.5f;
        if (ar < 0) ar = -ar;
        if (ar > SUBDIV_MIN_AREA) {
            subdividing = 1;
            subdivide(a, b, c);
            subdividing = 0;
            return;
        }
    }
    if (plan.approx || ap.approx) {
        mg_note_approx();
        if (mg_config.combine_strict)
            return;             /* strict=1: approximated states are not drawn, to find them */
    }
    if (mg_config.census)
        mg_census();
    if (ap.skip || (ap.color_off && !depth))
        return;
    /* Fog: the G100 fogs only in textured trapezoids (white texture for
     * untextured draws); MULT2 keeps only the fog term (f * fog colour),
     * so the colour into the fog unit is forced to zero. */
    mg.fog_on = mg_fog_source() != GR_FOG_DISABLE;
    if (mg.fog_on) {
        if (mg.st.fog_mode & GR_FOG_MULT2) {
            plan.color_src = CS_ZERO;
            plan.modulate = 1;
        }
        if (!plan.textured) {
            plan.textured = 1;
            plan.tex_white = 1;
            plan.modulate = 1;
        }
    }
    vpo = PROF_SWITCH(PROF_G_VERT);
    vtx(&sa, a, ax, ay, wmode, plan.color_src, ap.alpha_src, plan.spec);
    vtx(&sb, b, bx, by, wmode, plan.color_src, ap.alpha_src, plan.spec);
    vtx(&sc, c, cx, cy, wmode, plan.color_src, ap.alpha_src, plan.spec);
    memset(&opts, 0, sizeof opts);
    opts.alphactrl = mga.has_alpha_blend ? ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) |
                                           ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE)
                                         : ALPHACTRL_G100_FIXED | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE);
    if (!alpha_setup(&plan, &ap, &opts, &sa, &sb, &sc)) {
        PROF_BACK(vpo);
        return;
    }
    mg_validate();
    ctx.flags = MGA_S_VOODOO_EDGES;
    if (mg.fog_on) {
        mg_fog_emit_color();
        ctx.flags |= MGA_S_FOG;
    }
    ctx.dwgctl = DWG_BOP_COPY;
    ctx.tex_tw = ctx.tex_th = 3;
    if (depth) {
        ctx.dwgctl |= (mg.st.depth_mask ? DWG_ATYPE_ZI : DWG_ATYPE_I) | zmode_for(mg.st.depth_func, wmode);
        ctx.flags |= MGA_S_Z | (mg.zbits == 32 ? MGA_S_Z32 : 0);
    } else {
        ctx.dwgctl |= DWG_ATYPE_I | DWG_ZMODE_NOZCMP;
    }
    ctx.clip_y0 = 0;
    ctx.clip_y1 = mg.rh;
    if (opts.vertex_alpha)
        ctx.flags |= MGA_S_ALPHA;
    if (plan.spec) {
        ctx.flags |= MGA_S_SPEC;
        opts.texctl2 |= TEXCTL2_SPECEN;
    }
    PROF_BACK(vpo);
    if (!plan.textured) {
        ctx.dwgctl |= DWG_OPCOD_TRAP;
        if (plan.color_src == CS_ITERATED || plan.color_src == CS_ITER_ALPHA || plan.color_src == CS_SOFT)
            ctx.flags |= MGA_S_COLOR;
        if (mga.has_alpha_blend && !alphactrl_cached(opts.alphactrl)) {
            /* The G200 blends and alpha-tests plain trapezoids too. */
            fifo_reserve(1);
            MGA_WR32(MGAREG_ALPHACTRL, opts.alphactrl);
        }
        if (warp_tri(&sa, &sb, &sc, &ctx) != 0)
            cpu_tri(&sa, &sb, &sc, &ctx);
    } else {
        ctx.dwgctl |= DWG_OPCOD_TEXTURE_TRAP;
        ctx.flags |= MGA_S_TEX;
        if (plan.modulate && (plan.color_src == CS_ITERATED || plan.color_src == CS_ITER_ALPHA ||
                              plan.color_src == CS_SOFT || plan.color_src == CS_SOFT_FACTOR))
            ctx.flags |= MGA_S_COLOR;
        if (ap.stipple && ap.alpha_src != AS_TEXTURE)
            ctx.flags |= MGA_S_ALPHA;
        draw_textured(a, b, c, &sa, &sb, &sc, &ctx, &plan, &opts);
    }
    mg_stats.tris++;
    mg_stats.tri_drawn++;
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
