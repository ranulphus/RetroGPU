/* virge.c - the S3 ViRGE/DX's Glide back end (V5 of docs/virge.md; the plan of 2026-11-20): Glide 2 through the
 * ViRGE device (hal/src/rg/virge.c: the mode, the buffers) and its layer (hal/src/virge: 2D, the display start, the
 * 3D triangle with the host's set-up).
 *
 * - The window: the BIOS's 15-bit mode at its own pitch (the 3D engine draws 16 bits only as RGB555), double
 *   buffered, a Z16 aux buffer; every LFB lock through the core's converting shadow (mg.screen555: the owner's
 *   choice), the Voodoo's 2 MB of textures in the 2.2 MB left.
 * - Clears by the 2D engine, flips by the display start (written before the retrace wait: VFLIP on bench C).
 * - Triangles clipped to Glide's clip window in software (the engine's coordinates are unsigned), the Voodoo's level
 *   per triangle (split where the vertices want different ones, or table fog's factor differs), the combines on the
 *   engine's DECAL, MODULATE and REFLECTION with the vertices' colours worked out by the core's combine units, the
 *   rest approximated and said once. Blending: SRC_ALPHA / ONE_MINUS_SRC_ALPHA by the texel's or the vertex's
 *   alpha; the others a 50% blend (g100_additive=1 skips them). No alpha test, no chroma key: keyed texture
 *   variants drawn with the texel's alpha, untextured triangles decided whole. */
#include "glide/mg.h"
#include "backend/backend.h"
#include "backend/share.h"
#include "combine/eval.h"
#include "tex/texmgr.h"
#include "tex/texconv.h"
#include "tex/texfmt.h"
#include "dll/config.h"
#include "rg/device.h"
#include "rg/virge_dev.h"
#include "virge/virge.h"
#include "mga/sys.h"
#include "mga/fp.h"
#include <string.h>

static int warned[24];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        mg_log(MG_LOG_WARN, "virge: %s", what);
    }
}

static void log_line(const char *line) { mg_line("%s", line); }

/* ---- The card --------------------------------------------------------------------------------------------- */

static int pitch_bytes;                 /* the colour buffers' (and, at the widths Glide opens, Z's) */
static unsigned sync_timeouts;

static void sync_said(void)
{
    if (virge_sync(500000) != 0 && ++sync_timeouts <= 4)
        mg_line("MGL-W virge sync timed out (frame %u)", mg.frame);
}

static int probe(void)
{
    static int said;
    rg_info i;
    rg_set_format(mg_vsnprintf);
    rg_set_log(log_line);
    if (rg_virge_device.probe() != 0)
        return -1;
    if (!said) {
        said = 1;
        rg_virge_device.info(&i);
        mg_line("MGL-CARD %s id=%04x rev=%02x family=virge emulated=0 fb=%08lx vram=%lu", i.chip_name, i.device_id,
                i.revision, (unsigned long)i.fb_phys, (unsigned long)i.vram_bytes);
    }
    mg_vram = virge.fb;
    return 0;
}

/* A 16-bit surface of the window (a colour buffer, or the aux buffer as one) as the 2D engine's target. */
static void fill16(uint32_t off, int x, int y, int w, int h, uint32_t v)
{
    if (w <= 0 || h <= 0)
        return;
    virge_set_target(off, pitch_bytes, 16);
    virge_set_clip(0, 0, mg.rw, mg.rh);
    virge_fill(x, y, w, h, v);
    virge_sync(500000);                 /* (2D and 3D in turn: a triangle drawn into a fill once survived it, t27) */
}

static FxBool win_open(int w, int h, GrColorFormat_t color_format, GrOriginLocation_t origin_location,
                       int nColBuffers, int nAuxBuffers)
{
    rg_open o;
    rg_opened op;
    uint32_t front, back, z;
    int z_pitch, bpp;
    memset(&o, 0, sizeof o);
    o.width = w, o.height = h, o.color_bits = 15, o.depth_bits = nAuxBuffers > 0 ? 16 : 0, o.double_buffer = 1;
    if (rg_virge_device.open(&o, &op) != 0 || rg_virge_layout(&front, &back, &z, &z_pitch, &pitch_bytes, &bpp) != 0) {
        mg_log(MG_LOG_ERROR, "virge: %dx%d: %s", w, h, rg_error());
        rg_virge_device.close();
        return FXFALSE;
    }
    if (z_pitch && z_pitch != pitch_bytes)
        warn_once(1, "the depth buffer's pitch is not the colour buffers': LFB depth access reads wrongly");
    mg_vram = virge.fb;
    mg.bpp = 16;
    mg.screen555 = 1;
    mg.scaled = 0, mg.sx = mg.sy = 1.0;
    mg.present_scaled = 0;
    mg.width = w, mg.height = h, mg.rw = w, mg.rh = h;
    mg.pitch_px = pitch_bytes / 2;
    if (nColBuffers > 2)
        warn_once(0, "triple buffering drawn as double");
    mg.nbuffers = 2;
    mg.has_aux = nAuxBuffers > 0 && op.depth_bits > 0;
    mg.zbits = 16;
    mg.color_format = color_format;
    mg.st.origin = origin_location == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
    mg.buf_off[0] = front;
    mg.buf_off[1] = back;
    mg.aux_off = mg.has_aux ? z : 0;
    mg.heap_off = op.mem_start;
    mg.heap_end = op.mem_end;
    tex_heap_init(mg.heap_off, mg.heap_end);
    tex_reset();
    mg.front = 0, mg.back = 1;
    mg.render_buffer = GR_BUFFER_BACKBUFFER;
    mg.open = 1;
    mg.dirty = ~0u;
    mg.st.clip_x0 = 0; mg.st.clip_y0 = 0; mg.st.clip_x1 = w; mg.st.clip_y1 = h;
    mg_validate();
    if (mg.has_aux)
        fill16(mg.aux_off, 0, 0, mg.rw, mg.rh, 0);         /* (as the Matrox cards: depth 0) */
    mg.dirty = ~0u;
    sync_said();
    virge_set_start(mg.buf_off[mg.front]);
    mg_gamma_apply();
    mg.fogcol_valid = 0;
    mg_hooks_install();
    mg_line("MGL-WINOPEN %dx%d mode=virge bpp=15 pitch=%d buffers=%d aux=%d z16 vram=%lu heap=%lu", w, h,
            mg.pitch_px, mg.nbuffers, mg.has_aux, (unsigned long)op.vram_bytes,
            (unsigned long)(mg.heap_end - mg.heap_off));
    return FXTRUE;
}

static void win_close(void)
{
    sync_said();
    rg_virge_device.close();
    mg.open = 0;
    mg.screen555 = 0;
    sys_unhook_faults();
    sys_unhook_exit();
    mg_line("MGL-WINCLOSE frames=%u sync_timeouts=%u", mg.frame, sync_timeouts);
}

static void fault(void) { rg_virge_device.close(); }
static void exit_restore(void) { virge_sync(500000); rg_virge_device.close(); }
static void sync(void) { sync_said(); }
static int busy(void) { return !virge_idle(); }
static int in_vblank(void) { return (sys_inb(0x3DA) & 0x08) != 0; }
static unsigned vcount(void) { return 0; }

static void set_ramp(const uint8_t ramp[256])
{
    MGA_UNUSED(ramp);
    warn_once(2, "gamma not applied (the ViRGE/DX's DAC in 15-bit: W1's open point)");
}

/* ---- State ------------------------------------------------------------------------------------------------ */

static void validate(void)
{
    if (mg.dirty & MG_DIRTY_TARGET)
        virge_3d_target(mg_buffer_offset(mg.render_buffer), pitch_bytes, 16, mg.rw, mg.rh, mg.aux_off, pitch_bytes);
    mg.dirty = 0;                       /* (the clip window: in software, at each triangle) */
}

static void clear(int x0, int y0, int x1, int y1, GrColor_t color, FxU16 depth)
{
    sync_said();                        /* (a triangle still drawing when the 2D fill's registers are written) */
    if (mg.st.color_mask_rgb)
        fill16(mg_buffer_offset(mg.render_buffer), x0, y0, x1 - x0, y1 - y0, mg_argb_to_555(mg_color_to_argb(color)));
    if (mg.has_aux && mg.st.depth_mask && mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE)
        fill16(mg.aux_off, x0, y0, x1 - x0, y1 - y0, mg_depth_clear_value(depth));
}

static void swap(int swap_interval)
{
    int i;
    sync_said();
    /* The start first, then the retrace (the TNT2's order: right whether the chip takes the start at the retrace or
     * at the blank before it; VFLIP on bench C checks the ViRGE). */
    virge_set_start(mg.buf_off[mg.back]);
    PROF_BACK(PROF_G_VSYNC);
    for (i = 0; i < swap_interval; i++)
        virge_vsync_wait(50000);
    PROF_BACK(PROF_G_SWAP);
}

static void present_front(void) { }             /* (no scaled fits) */

/* ---- Triangles -------------------------------------------------------------------------------------------- */

/* Glide's compare (NEVER 0 .. ALWAYS 7, the new value against the stored) as the engine's (0 never, 1 >, 2 =, 3 >=,
 * 4 <, 5 !=, 6 <=, 7 always); W-buffering keeps a linear 1/w, nearer larger, so its compares are reversed. */
static int zfunc_of(GrCmpFnc_t f, int reversed)
{
    static const int map[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    if (reversed)
        switch (f) {
        case GR_CMP_LESS: f = GR_CMP_GREATER; break;
        case GR_CMP_GREATER: f = GR_CMP_LESS; break;
        case GR_CMP_LEQUAL: f = GR_CMP_GEQUAL; break;
        case GR_CMP_GEQUAL: f = GR_CMP_LEQUAL; break;
        default: break;
        }
    return map[(int)f & 7];
}

static int cmp_pass(GrCmpFnc_t f, float a, float ref)
{
    switch (f) {
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

static float clamp255(float f) { return f < 0 ? 0 : f > 255.0f ? 255.0f : f; }

/* The colour combine on the engine: which environment, and what the vertices carry. */
enum { K_GOURAUD, K_DECAL, K_MODULATE, K_REFLECT };

/* A factor (Glide's GR_COMBINE_FACTOR_*, with ONE_MINUS at bit 3) that is one, or zero. */
static int factor_one(int f) { return f == (GR_COMBINE_FACTOR_ZERO | 8); }
static int factor_zero(int f) { return f == GR_COMBINE_FACTOR_ZERO; }
static int factor_textured(int f)
{
    f &= 7;
    return f == GR_COMBINE_FACTOR_TEXTURE_ALPHA || f == GR_COMBINE_FACTOR_TEXTURE_RGB;
}

/* The plan for the colour: kind, and the vertex colour as the combine's factor (f), its local (l) or the whole
 * untextured combine (u). Approximations said once each. */
typedef struct { int kind; char vcol; } cplan;

static cplan colour_plan(int textured)
{
    const mg_state *s = &mg.st;
    cplan p;
    int f = s->cc_factor;
    p.kind = K_GOURAUD, p.vcol = 'u';
    if (!textured || s->cc_other != GR_COMBINE_OTHER_TEXTURE)
        return p;                       /* (the texture not in the colour: the core's combine at the vertices) */
    switch (s->cc_func) {
    case GR_COMBINE_FUNCTION_SCALE_OTHER:
        if (factor_one(f)) p.kind = K_DECAL;
        else if (factor_zero(f)) p.kind = K_GOURAUD, p.vcol = 'u';
        else if (!factor_textured(f)) p.kind = K_MODULATE, p.vcol = 'f';
        else { p.kind = K_DECAL; warn_once(3, "a texture factor on the texture: drawn as the texture"); }
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        if (factor_one(f)) p.kind = K_REFLECT, p.vcol = 'l';
        else if (!factor_textured(f)) {
            p.kind = K_MODULATE, p.vcol = 'f';
            warn_once(4, "texture * factor + local: the local dropped");
        } else { p.kind = K_REFLECT, p.vcol = 'l'; warn_once(5, "texture * texture factor + local: as texture + local"); }
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        if (factor_one(f)) p.kind = K_DECAL;
        else if (factor_zero(f)) p.kind = K_GOURAUD, p.vcol = 'l';
        else { p.kind = K_DECAL; warn_once(6, "a blend between the texture and the local: drawn as the texture"); }
        break;
    default:
        p.kind = K_MODULATE, p.vcol = 'f';
        warn_once(7, "a combine with a subtraction or the local's: approximated as texture * factor");
        break;
    }
    return p;
}

/* Where the source alpha comes from for blending (and the alpha test): 't' the texel's, 'v' the vertex's (the core's
 * alpha combine there, or its factor), '1' opaque. */
static char alpha_source(int textured)
{
    const mg_state *s = &mg.st;
    if (textured && s->ac_other == GR_COMBINE_OTHER_TEXTURE && s->ac_func == GR_COMBINE_FUNCTION_SCALE_OTHER) {
        if (!factor_one(s->ac_factor))
            warn_once(8, "the texel's alpha times a factor: the texel's alone");
        return 't';
    }
    if (textured && s->ac_other == GR_COMBINE_OTHER_TEXTURE && s->ac_func != GR_COMBINE_FUNCTION_LOCAL &&
        s->ac_func != GR_COMBINE_FUNCTION_LOCAL_ALPHA && s->ac_func != GR_COMBINE_FUNCTION_ZERO) {
        warn_once(9, "an alpha combine of the texel's and the vertex's: the texel's");
        return 't';
    }
    return 'v';
}

static void tri_one(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod);

/* ---- Clamped textures. The engine always wraps; a triangle reaching past the texture's edge in a clamped direction
 * is cut along s = 0 and s = S (then t likewise), straight lines on the screen since s = (s/w) / (1/w) with both
 * iterated linearly. The pieces keep their coordinates, so the level is the Voodoo's (from the real gradient), and
 * the pieces outside are drawn with s (or t) at the centre of the bound level's edge texel: the Voodoo's clamp.
 * (Bilinear filtering inside, within half a texel of the edge, still wraps.) ---- */

static int hold_s, hold_t;              /* the piece being drawn: 0 inside, 1 below 0, 2 past the edge */

/* The texture's extent in Glide's s and t: 256 on the long side, 256 / aspect on the short one. */
static double edge_s_of(void)
{
    int w, h;
    tex_dims(tmu0.large, tmu0.aspect, &w, &h);
    return 256.0 * (double)w / (double)(w > h ? w : h);
}

static double edge_t_of(void)
{
    int w, h;
    tex_dims(tmu0.large, tmu0.aspect, &w, &h);
    return 256.0 * (double)h / (double)(w > h ? w : h);
}

static double q_of(const GrVertex *v)
{
    double q = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v->tmuvtx[0].oow : v->oow;
    return q != 0 ? q : 1e-9;
}

static double num_of(const GrVertex *v, int axis) { return axis ? v->tmuvtx[0].tow : v->tmuvtx[0].sow; }

static void lerpv(GrVertex *o, const GrVertex *a, const GrVertex *b, float k)
{
    const float *fa = (const float *)a, *fb = (const float *)b;
    float *fo = (float *)o;
    unsigned i;
    for (i = 0; i < sizeof(GrVertex) / sizeof(float); i++)
        fo[i] = fa[i] + (fb[i] - fa[i]) * k;
}

/* The part of a polygon where s (axis 0) or t (1) is at least (keep_greater) or at most bound. */
static int cut_tex(GrVertex *out, const GrVertex *in, int n, int axis, double bound, int keep_greater)
{
    int i, m = 0;
    for (i = 0; i < n && m < 15; i++) {
        const GrVertex *a = &in[i], *b = &in[(i + 1) % n];
        double qa = q_of(a), qb = q_of(b), na = num_of(a, axis), nb = num_of(b, axis), va = na / qa, vb = nb / qb;
        int ina = keep_greater ? va >= bound : va <= bound, inb = keep_greater ? vb >= bound : vb <= bound;
        if (ina)
            out[m++] = *a;
        if (ina != inb) {
            double den = (nb - na) - bound * (qb - qa), k = den != 0 ? (bound * qa - na) / den : 0.5;
            lerpv(&out[m++], a, b, (float)(k < 0 ? 0 : k > 1 ? 1 : k));
        }
    }
    return m;
}

static void draw_poly(const GrVertex *p, int n, int fogged, int mip)
{
    int i;
    for (i = 2; i < n; i++)
        mg_split(&p[0], &p[i - 1], &p[i], fogged, mip, tri_one);
}

static void clamp_axis(const GrVertex *p, int n, int axis, double edge, int fogged, int mip);

/* A piece along s handed on to t's cuts (when t clamps), or drawn. */
static void next_piece(const GrVertex *p, int n, int axis, int fogged, int mip)
{
    if (n < 3)
        return;
    if (axis == 0 && tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP)
        clamp_axis(p, n, 1, edge_t_of(), fogged, mip);
    else
        draw_poly(p, n, fogged, mip);
}

/* The polygon in up to three pieces along one axis (below 0, inside, past the edge), each handed on with its side. */
static void clamp_axis(const GrVertex *p, int n, int axis, double edge, int fogged, int mip)
{
    GrVertex lo[16], mid[16], hi[16], tmp[16];
    int i, nl, nm, nh, outside = 0, *side = axis ? &hold_t : &hold_s, w, h;
    double b0 = 0, b1 = edge;
    if (tmu0.minf == GR_TEXTUREFILTER_BILINEAR || tmu0.magf == GR_TEXTUREFILTER_BILINEAR) {
        /* (bilinear: the strips within half a texel of the edges too, where the Voodoo's clamped filter gives the
         * edge texel and the engine's would wrap into the opposite edge) */
        tex_dims(tmu0.large, tmu0.aspect, &w, &h);
        b0 = 128.0 / (double)(w > h ? w : h), b1 = edge - b0;
    }
    for (i = 0; i < n; i++) {
        double v = num_of(&p[i], axis) / q_of(&p[i]);
        outside |= v < b0 || v > b1;
    }
    if (!outside) {
        *side = 0;
        next_piece(p, n, axis, fogged, mip);
        return;
    }
    nl = cut_tex(lo, p, n, axis, b0, 0);
    i = cut_tex(tmp, p, n, axis, b0, 1);
    nm = cut_tex(mid, tmp, i, axis, b1, 0);
    nh = cut_tex(hi, p, n, axis, b1, 1);
    *side = 1, next_piece(lo, nl, axis, fogged, mip);
    *side = 0, next_piece(mid, nm, axis, fogged, mip);
    *side = 2, next_piece(hi, nh, axis, fogged, mip);
    *side = 0;
}

void virge_be_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    GrVertex poly[16];
    int depth = mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux, n, i, fogged, mip, inside, clamped;
    int32_t ax, ay, bx, by, cx, cy;
    float x0, y0, x1, y1;
    if (!mg.open)
        return;
    mg_stats.tri_processed++;
    if (depth && mg.st.depth_func == GR_CMP_NEVER)
        return;
    if (mg.st.alpha_test_func == GR_CMP_NEVER)
        return;
    ax = mg_snap16(a->x); ay = mg_snap16(a->y);
    bx = mg_snap16(b->x); by = mg_snap16(b->y);
    cx = mg_snap16(c->x); cy = mg_snap16(c->y);
    if (mg.st.cull != GR_CULL_DISABLE) {
        int64_t area = ((int64_t)bx - ax) * ((int64_t)cy - ay) - ((int64_t)cx - ax) * ((int64_t)by - ay);
        int sgn = (area > 0) - (area < 0);
        if (sgn == 0)
            return;
        if (mg.st.cull == GR_CULL_NEGATIVE && sgn < 0)
            return;
        if (mg.st.cull == GR_CULL_POSITIVE && sgn > 0)
            return;
    }
    if (!mg.st.color_mask_rgb && !(depth && mg.st.depth_mask))
        return;
    if (mg.st.delta0) {
        static GrVertex d[3];
        uint32_t k = mg.st.delta0_argb;
        int m;
        d[0] = *a; d[1] = *b; d[2] = *c;
        for (m = 0; m < 3; m++) {
            d[m].r = (float)((k >> 16) & 0xFF); d[m].g = (float)((k >> 8) & 0xFF); d[m].b = (float)(k & 0xFF);
        }
        a = &d[0]; b = &d[1]; c = &d[2];
    }
    if (mg.st.chroma_mode == GR_CHROMAKEY_ENABLE && !mg_uses_texture()) {
        /* (untextured: dropped when every vertex's colour is the key, as the Matrox back end's) */
        uint32_t key = mg_color_to_argb(mg.st.chroma_value) & 0xFFFFFFu, k[3];
        const GrVertex *p[3];
        float rgb[3];
        int m;
        p[0] = a, p[1] = b, p[2] = c;
        for (m = 0; m < 3; m++) {
            mg_cc_vertex(p[m], 0, rgb);
            k[m] = (uint32_t)clamp255(rgb[0]) << 16 | (uint32_t)clamp255(rgb[1]) << 8 | (uint32_t)clamp255(rgb[2]);
        }
        if (k[0] == key && k[1] == key && k[2] == key)
            return;
    }
    mg_validate();
    fogged = mg_fog_source() == GR_FOG_WITH_TABLE;
    mip = mg_uses_texture() && tmu0.has_source && tmu0.mipmap != GR_MIPMAP_DISABLE && tmu0.small > tmu0.large;
    /* (untextured combines are worked out at the vertices: products of iterated values cut finer, see mg_split) */
    if (!mg_uses_texture())
        mip |= MG_SPLIT_COMBINE;
    /* Clipped to the clip window when it reaches past it (the engine's coordinates are unsigned). */
    x0 = (float)mg.st.clip_x0, y0 = (float)mg.st.clip_y0, x1 = (float)mg.st.clip_x1, y1 = (float)mg.st.clip_y1;
    inside = a->x >= x0 && b->x >= x0 && c->x >= x0 && a->x <= x1 && b->x <= x1 && c->x <= x1 &&
             a->y >= y0 && b->y >= y0 && c->y >= y0 && a->y <= y1 && b->y <= y1 && c->y <= y1;
    clamped = mg_uses_texture() && tmu0.has_source && tmu0.combine_set &&
              (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP || tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP);
    if (inside && !clamped) {
        mg_split(a, b, c, fogged, mip, tri_one);
        return;
    }
    poly[0] = *a, poly[1] = *b, poly[2] = *c;
    n = inside ? 3 : mg_clip_to_window(poly, 3);
    if (n < 3)
        return;
    if (clamped) {
        if (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP)
            clamp_axis(poly, n, 0, edge_s_of(), fogged, mip);
        else
            clamp_axis(poly, n, 1, edge_t_of(), fogged, mip);
        return;
    }
    for (i = 2; i < n; i++)
        mg_split(&poly[0], &poly[i - 1], &poly[i], fogged, mip, tri_one);
}

static void tri_one(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod)
{
    const mg_state *s = &mg.st;
    const GrVertex *g[3];
    virge_vertex v[3];
    virge_texture t;
    tex_level_hw hw;
    tex_variant var;
    int wmode = s->depth_mode == GR_DEPTHBUFFER_WBUFFER || s->depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = s->depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux;
    int textured, fog, blend = VIRGE_BLEND_NONE, n, half = 0, persp, mult2;
    char asrc;
    cplan cp;
    double st[3][2], ks = 0, kt = 0;
    textured = mg_uses_texture() && tmu0.has_source;
    fog = mg_fog_source() != GR_FOG_DISABLE;
    g[0] = a, g[1] = b, g[2] = c;

    /* Blending and the source alpha. */
    asrc = alpha_source(textured);
    if (!s->color_mask_rgb) {
        blend = VIRGE_BLEND_SOURCE, half = -1;              /* (depth alone: alpha 0 leaves the colour) */
    } else if (s->blend_src == GR_BLEND_ONE && s->blend_dst == GR_BLEND_ZERO) {
        blend = VIRGE_BLEND_NONE;
    } else if (s->blend_src == GR_BLEND_SRC_ALPHA && s->blend_dst == GR_BLEND_ONE_MINUS_SRC_ALPHA) {
        blend = asrc == 't' ? VIRGE_BLEND_TEXTURE : VIRGE_BLEND_SOURCE;
    } else {
        if (mg_config.g100_additive == 1) {
            warn_once(10, "a blend the chip has not: skipped (g100_additive=1)");
            return;
        }
        warn_once(11, "a blend the chip has not (additive and others): drawn as a 50% blend");
        blend = VIRGE_BLEND_SOURCE, half = 1;
    }

    /* The texture: keyed variants for the chroma key and the texel's alpha test, drawn with the texel's alpha. */
    memset(&var, 0, sizeof var);
    if (textured) {
        if (s->chroma_mode == GR_CHROMAKEY_ENABLE) {
            var.kind = TV_CHROMA;
            var.chroma = mg_color_to_argb(s->chroma_value) & 0xFFFFFFu;
        }
        if (s->alpha_test_func != GR_CMP_ALWAYS && asrc == 't') {
            var.kind |= TV_ATEST;
            var.afunc = s->alpha_test_func;
            var.aref = (int)s->alpha_test_ref;
        }
        if (var.kind && blend == VIRGE_BLEND_NONE)
            blend = VIRGE_BLEND_TEXTURE;
        else if (var.kind && blend == VIRGE_BLEND_SOURCE)
            warn_once(12, "a keyed texture under the vertex's alpha blend: the key's holes lost");
        textured = tex_bind_level(lod, &var, &hw) == 0;
    }
    if (!textured && fog) {
        /* (the engine fogs textured triangles only: the white texel, modulated by the vertex colour) */
        if (tex_white(&hw) != 0)
            fog = 0;
    }
    if (!textured && s->alpha_test_func != GR_CMP_ALWAYS) {
        /* (no alpha test: the triangle kept or dropped whole by its vertices' alpha, as the G100's) */
        int keep = 0;
        for (n = 0; n < 3; n++)
            keep |= cmp_pass(s->alpha_test_func, clamp255(mg_ac_vertex(g[n], 0)), (float)s->alpha_test_ref);
        if (!keep)
            return;
        warn_once(13, "the alpha test on untextured triangles: decided by the whole triangle");
    }
    cp = colour_plan(textured);
    if (fog && blend == VIRGE_BLEND_SOURCE) {
        warn_once(14, "fog with the vertex's alpha blend: drawn without fog");
        fog = 0;
    }
    /* (MULT2: the fog term alone, f * the fog colour: a black triangle under the engine's fog) */
    mult2 = fog && (s->fog_mode & GR_FOG_MULT2);

    /* The texture's words. */
    if (textured || fog) {
        /* One filter a triangle: the Voodoo's choice at the piece's centre (minified past the largest level, or at a
         * smaller one: the minification filter). */
        int minified = lod > tmu0.large, bilinear;
        float qa, qb, qc;
        if (textured && !minified && tmu0.minf != tmu0.magf) {
            mg_lod_planes L;
            mg_lod_setup(&L, a, b, c);
            minified = mg_minified_at(&L, (a->x + b->x + c->x) / 3.0, (a->y + b->y + c->y) / 3.0);
        }
        bilinear = (minified ? tmu0.minf : tmu0.magf) == GR_TEXTUREFILTER_BILINEAR;
        qa = (s->stw_hint & GR_STWHINT_W_DIFF_TMU0) ? a->tmuvtx[0].oow : a->oow;
        qb = (s->stw_hint & GR_STWHINT_W_DIFF_TMU0) ? b->tmuvtx[0].oow : b->oow;
        qc = (s->stw_hint & GR_STWHINT_W_DIFF_TMU0) ? c->tmuvtx[0].oow : c->oow;
        t.offset = hw.org;
        t.fmt = hw.hwfmt == HW_TW12 ? VIRGE_TEX_ARGB4444 : VIRGE_TEX_ARGB1555;
        t.log2size = hw.w_log2;
        t.levels = 1;
        t.linear = textured ? bilinear : 0;
        persp = textured && !(qa == qb && qb == qc);     /* (the texture's own 1/w under the STW hint) */
        t.persp = persp;
        t.env = !textured || mult2 ? VIRGE_TEXENV_MODULATE
              : cp.kind == K_MODULATE ? VIRGE_TEXENV_MODULATE
              : cp.kind == K_REFLECT ? VIRGE_TEXENV_REFLECTION
              : cp.kind == K_GOURAUD ? VIRGE_TEXENV_MODULATE : VIRGE_TEXENV_DECAL;
        if (textured && tmu0.combine_set) {
            ks = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.w_log2));
            kt = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.h_log2));
        }
        if (textured && (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP || tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP))
            warn_once(15, "clamped textures: the engine wraps (the padding's edge on the short side)");
        virge_3d_texture(&t);
    } else
        virge_3d_texture(NULL);

    /* The vertices. */
    for (n = 0; n < 3; n++) {
        const GrVertex *p = g[n];
        int32_t sx = mg_snap16(p->x), sy = mg_snap16(p->y);
        float rgb[3], al;
        double z;
        if (s->origin == GR_ORIGIN_LOWER_LEFT)
            sy = mg.height * 16 - sy;
        /* (x a 64th of a pixel left: a centre exactly on an edge then falls as the Voodoo's does, as on the
         * TNT2: conformance t04, 245 pixels on left and right edges without it) */
        v[n].x = (float)sx * (1.0f / 16.0f) - 1.0f / 64.0f;
        v[n].y = (float)sy * (1.0f / 16.0f);
        if (wmode)
            z = mg_wdepth_from_oow(p->oow, 16);
        else {
            z = p->ooz + s->depth_bias;
            if (z < 0) z = 0;
            if (z > 65535.0) z = 65535.0;
        }
        v[n].z = (float)z;
        if (cp.kind == K_DECAL || (cp.kind == K_MODULATE && cp.vcol == 'f'))
            mg_cc_vertex(p, cp.vcol == 'f', rgb);
        else if (cp.vcol == 'l')
            mg_cc_local_vertex(p, s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, rgb);
        else
            mg_cc_vertex(p, 0, rgb);
        if (cp.kind == K_DECAL)
            rgb[0] = rgb[1] = rgb[2] = 255.0f;              /* (DECAL lit for fog: the texel as it is) */
        if (mult2)
            rgb[0] = rgb[1] = rgb[2] = 0;
        al = asrc == 'v' ? mg_ac_vertex(p, 0) : 255.0f;
        if (half > 0)
            al = 128.0f;
        else if (half < 0)
            al = 0;
        if (fog)
            al = (float)(255.0 - mg_fog_vertex(p));         /* (the engine's fog factor: 1 - a / 255) */
        v[n].r = clamp255(rgb[0]), v[n].g = clamp255(rgb[1]), v[n].b = clamp255(rgb[2]), v[n].a = clamp255(al);
        mg_st_of(p, &st[n][0], &st[n][1]);
        if (textured && (hold_s || hold_t)) {
            /* (a clamped piece outside the texture: the centre of the bound level's edge texel) */
            double half = 128.0 / (double)hw.logical_maxdim;
            if (hold_s) st[n][0] = hold_s == 1 ? half : edge_s_of() - half;
            if (hold_t) st[n][1] = hold_t == 1 ? half : edge_t_of() - half;
        }
        v[n].s = (float)(st[n][0] * ks);
        v[n].t = (float)(st[n][1] * kt);
        {
            float q = (s->stw_hint & GR_STWHINT_W_DIFF_TMU0) ? p->tmuvtx[0].oow : p->oow;
            v[n].oow = q > 0 ? q : 1e-6f;
        }
    }
    virge_3d_state(depth ? zfunc_of(s->depth_func, wmode) : -1, depth && s->depth_mask);
    virge_3d_blend(blend);
    virge_3d_fog(fog, (s->fog_mode & GR_FOG_ADD2) ? 0u : (mg_color_to_argb(s->fog_color) & 0xFFFFFFu));
    virge_tri(&v[0], &v[1], &v[2]);
    mg_stats.tris++;
    mg_stats.tri_drawn++;
}

/* A level the texture manager converted (square, 1555 or 4444) into place: the engine's rows as they are. */
static void tex_write(uint32_t org, const uint16_t *texels, int hw_w, int hw_h)
{
    virge_texture t;
    int l = 0;
    while ((1 << l) < hw_w)
        l++;
    memset(&t, 0, sizeof t);
    t.offset = org, t.fmt = VIRGE_TEX_ARGB1555, t.log2size = l, t.levels = 1;
    if (hw_w != hw_h)
        warn_once(16, "a texture slot not square");
    virge_tex_load(&t, 0, texels);
}

const mg_backend mg_virge_backend = {
    "virge", probe, win_open, win_close, fault, exit_restore, sync, busy, in_vblank, vcount, set_ramp, validate,
    clear, swap, present_front, virge_be_tri, tex_write, 8, 1, 1, 0
};
