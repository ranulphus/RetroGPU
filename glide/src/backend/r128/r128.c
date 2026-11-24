/* r128.c - the ATI Rage 128's Glide back end (R5 of docs/rage128.md; the plan of 2026-11-21): Glide 2 through the
 * Rage 128 device (hal/src/rg/r128.c: the mode, the buffers, the CCE) and its layer (hal/src/r128: 2D, the 3D
 * context and 3D_RNDR_GEN_PRIM's triangles).
 *
 * - The window: the BIOS's 16-bit mode at a 1024-pixel pitch (the Voodoo's 2048-byte stride Glide games assume),
 *   double buffered, a linear Z16 aux buffer at the same pitch.
 * - Clears by the 2D engine, flips by the display start (written before the retrace wait, as the TNT2's), the
 *   clip window as the context's scissor.
 * - Triangles: the Voodoo's level per triangle and table fog's factor per vertex (share.c's split); the colour and
 *   alpha combines on the chip's combiner (one function a unit over the vertex colour and the texel): a combine
 *   without the texture worked out at the vertices (combine/eval.c), with it the Glide forms the chip has (the
 *   texture, modulated, added, a blend by the texel's alpha or by an iterated factor), the rest approximated and
 *   said once. Depth as the Matrox cards keep it (Z: ooz; W: a linear 1/w, compares reversed), the alpha test,
 *   blending, dithering, fog by the specular's alpha. Chroma key by the core's keyed texture variants and the
 *   alpha test. Textures in the chip's own linear layout, one level a triangle. */
#include "glide/mg.h"
#include "backend/backend.h"
#include "backend/share.h"
#include "combine/eval.h"
#include "tex/texmgr.h"
#include "tex/texconv.h"
#include "rg/device.h"
#include "rg/r128_dev.h"
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include "mga/sys.h"
#include "mga/fp.h"
#include <string.h>

static int warned[24];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        mg_log(MG_LOG_WARN, "r128: %s", what);
    }
}

static void log_line(const char *line) { mg_line("%s", line); }

/* ---- The card --------------------------------------------------------------------------------------------- */

static unsigned sync_timeouts;
static unsigned long vs_waits, vs_fails, flips_queued;
static uint32_t flip_seq;               /* the last flip queued in the command stream (r128_flip_queue) */

static void sync_said(const char *where)
{
    if (r128_sync(500000) != 0 && ++sync_timeouts <= 8)
        mg_line("MGL-W r128 sync timed out (%s, frame %u): GUI_STAT=%08lx PM4_STAT=%08lx", where, mg.frame,
                (unsigned long)r128_rd(R128_GUI_STAT), (unsigned long)r128_rd(R128_PM4_STAT));
}

static int probe(void)
{
    static int said;
    rg_info i;
    rg_set_format(mg_vsnprintf);
    rg_set_log(log_line);
    if (rg_r128_device.probe() != 0)
        return -1;
    if (!said) {
        said = 1;
        rg_r128_device.info(&i);
        mg_line("MGL-CARD %s id=%04x rev=%02x family=r128 emulated=0 fb=%08lx mmio=%08lx vram=%lu", i.chip_name,
                i.device_id, i.revision, (unsigned long)i.fb_phys, (unsigned long)i.mmio_phys,
                (unsigned long)i.vram_bytes);
    }
    mg_vram = r128.fb;
    return 0;
}

/* A 16-bit surface of the window (a colour buffer, or the aux buffer as one) as the 2D engine's target. */
static void fill16(uint32_t off, int x, int y, int w, int h, uint32_t v)
{
    if (w <= 0 || h <= 0)
        return;
    r128_set_target(off, mg.pitch_px * 2, 16);
    r128_set_clip(0, 0, mg.rw, mg.rh);
    r128_fill(x, y, w, h, v);
}

static FxBool win_open(int w, int h, GrColorFormat_t color_format, GrOriginLocation_t origin_location,
                       int nColBuffers, int nAuxBuffers)
{
    rg_open o;
    rg_opened op;
    memset(&o, 0, sizeof o);
    o.width = w, o.height = h, o.color_bits = 16, o.depth_bits = nAuxBuffers > 0 ? 16 : 0, o.double_buffer = 1;
    o.pitch_px = w <= 1024 ? 1024 : 0;
    if (rg_r128_device.open(&o, &op) != 0) {
        mg_log(MG_LOG_ERROR, "r128: %dx%d: %s", w, h, rg_error());
        rg_r128_device.close();
        return FXFALSE;
    }
    mg_vram = r128.fb;
    mg.bpp = 16;
    mg.scaled = 0, mg.sx = mg.sy = 1.0;
    mg.present_scaled = 0;
    mg.width = w, mg.height = h, mg.rw = w, mg.rh = h;
    mg.pitch_px = op.pitch_px;
    if (nColBuffers > 2)
        warn_once(0, "triple buffering drawn as double");
    mg.nbuffers = 2;
    mg.has_aux = nAuxBuffers > 0 && op.depth_bits > 0;
    mg.zbits = 16;
    mg.color_format = color_format;
    mg.st.origin = origin_location == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
    mg.buf_off[0] = rg_r128.front_off;
    mg.buf_off[1] = rg_r128.back_off;
    mg.aux_off = mg.has_aux ? rg_r128.z_off : 0;
    mg.heap_off = op.mem_start;
    mg.heap_end = op.mem_end;
    tex_heap_init(mg.heap_off, mg.heap_end);
    tex_reset();
    /* (the 3D context for this window: Mesa's values; Z linear at the colour's pitch, 16 bits) */
    r128_3d_init(mg.buf_off[1], mg.pitch_px * 2, 16, mg.has_aux ? mg.aux_off : 0, mg.pitch_px * 2, 16);
    mg.front = 0, mg.back = 1;
    mg.render_buffer = GR_BUFFER_BACKBUFFER;
    mg.open = 1;
    mg.dirty = ~0u;
    mg.st.clip_x0 = 0; mg.st.clip_y0 = 0; mg.st.clip_x1 = w; mg.st.clip_y1 = h;
    mg_validate();
    if (mg.has_aux)
        fill16(mg.aux_off, 0, 0, mg.rw, mg.rh, 0);         /* (as the Matrox cards: depth 0) */
    mg.dirty = ~0u;
    sync_said("open");
    r128_set_start(mg.buf_off[mg.front]);
    flip_seq = r128_flip_seen(), flips_queued = 0;
    mg_gamma_apply();
    mg.fogcol_valid = 0;
    mg_hooks_install();
    mg_line("MGL-WINOPEN %dx%d mode=r128 bpp=16 pitch=%d buffers=%d aux=%d z16 vram=%lu heap=%lu", w, h,
            mg.pitch_px, mg.nbuffers, mg.has_aux, (unsigned long)op.vram_bytes,
            (unsigned long)(mg.heap_end - mg.heap_off));
    return FXTRUE;
}

static void win_close(void)
{
    sync_said("close");
    r128_ramp_off();
    rg_r128_device.close();
    mg.open = 0;
    sys_unhook_faults();
    sys_unhook_exit();
    mg_line("MGL-WINCLOSE frames=%u sync_timeouts=%u vsync_waits=%lu failed=%lu flips_queued=%lu", mg.frame,
            sync_timeouts, vs_waits, vs_fails, flips_queued);
}

static void fault(void) { r128_ramp_off(); rg_r128_device.close(); }
static void exit_restore(void) { r128_sync(500000); r128_ramp_off(); rg_r128_device.close(); }
static void sync(void) { sync_said("sync"); }
static int busy(void) { return !r128_idle(); }
static int in_vblank(void) { return (sys_inb(0x3DA) & 0x08) != 0; }
static unsigned vcount(void) { return 0; }

static void set_ramp(const uint8_t ramp[256]) { r128_set_ramp(ramp); }

/* ---- State ------------------------------------------------------------------------------------------------ */

static void validate(void)
{
    r128_ctx3d *c = &r128_3d;
    /* (called before every triangle: the state marked for sending only when the target or the clip changed) */
    if (mg.dirty & MG_DIRTY_TARGET) {
        c->dst_pitch_offset_c = (uint32_t)(mg.pitch_px / 8) << 21 | mg_buffer_offset(mg.render_buffer) >> 5;
        r128_3d_dirty = 1;
    }
    if (mg.dirty & (MG_DIRTY_TARGET | MG_DIRTY_CLIP)) {
        int y0 = mg.st.clip_y0, y1 = mg.st.clip_y1, x0 = mg.st.clip_x0, x1 = mg.st.clip_x1;
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
            y0 = mg.height - mg.st.clip_y1;
            y1 = mg.height - mg.st.clip_y0;
        }
        if (x1 <= x0 || y1 <= y0)
            x0 = x1 = y0 = y1 = 0x3FFF;     /* (nothing: a pixel off the screen) */
        else
            x1--, y1--;                     /* (the scissor's bottom right inclusive) */
        c->sc_top_left_c = (uint32_t)y0 << 16 | (uint32_t)x0;
        c->sc_bottom_right_c = (uint32_t)y1 << 16 | (uint32_t)x1;
        r128_3d_dirty = 1;
    }
    mg.dirty = 0;
}

static void clear(int x0, int y0, int x1, int y1, GrColor_t color, FxU16 depth)
{
    if (mg.st.color_mask_rgb)
        fill16(mg_buffer_offset(mg.render_buffer), x0, y0, x1 - x0, y1 - y0, mg_argb_to_565(mg_color_to_argb(color)));
    if (mg.has_aux && mg.st.depth_mask && mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE)
        fill16(mg.aux_off, x0, y0, x1 - x0, y1 - y0, mg_depth_clear_value(depth));
    r128_cce_flush();
}

static void swap(int swap_interval)
{
    int i;
    /* One retrace a swap with the CCE running: the flip in the command stream (r128_flip_queue), the engine stalled
     * until the new buffer is shown and the next frame's drawing, queued after it, reaching the buffer that was on
     * screen only then; the CPU goes on with the game. One flip in flight: the one before shown first (as the DRM's
     * clients wait for its frame counter). Before, the CPU synced, wrote the start and waited for the retrace
     * itself: ~8 ms a frame of Screamer Rally's on the P3. */
    if (swap_interval == 1 && r128_cce_running()) {
        uint32_t t0 = sys_time_us();
        PROF_BACK(PROF_G_VSYNC);
        while ((int32_t)(flip_seq - r128_flip_seen()) > 0)
            if (sys_time_us() - t0 > 500000u) {
                sync_said("flip");
                break;
            }
        if (r128_flip_queue(mg.buf_off[mg.back], flip_seq + 1u, 1) == 0) {
            flip_seq++, flips_queued++, vs_waits++;
            PROF_BACK(PROF_G_SWAP);
            return;
        }
    }
    sync_said("swap");
    /* (the start first, then the retrace: the TNT2's order, the device's) */
    r128_set_start(mg.buf_off[mg.back]);
    PROF_BACK(PROF_G_VSYNC);
    for (i = 0; i < swap_interval; i++) {
        if (r128_vsync_wait(50000) != 0)
            vs_fails++;
        vs_waits++;
    }
    PROF_BACK(PROF_G_SWAP);
}

static void present_front(void) { }             /* (no scaled fits) */

/* ---- The combine on the chip's combiner -------------------------------------------------------------------
 * One unit: out = a function over the vertex colour (its input) and the texel: R128_COMB_DIS the texel,
 * COPY_INP the input, MODULATE input * factor (the texel, its inverse, its alpha), ADD input + texel,
 * BLEND_TEXTURE input * (1 - At) + texel * At, BLEND_VERTEX the same by the vertex's alpha; alpha alike. */
#define CC(fn, f) ((uint32_t)(fn) | (uint32_t)(f) << R128_COLOR_FACTOR_SHIFT)
#define AC(fn) ((uint32_t)(fn) << R128_COMB_ALPHA_SHIFT | 6u << R128_ALPHA_FACTOR_SHIFT)
enum { F_TEX = 4, F_NTEX = 5, F_ALPHA = 6, F_NALPHA = 7 };
#define INPUT_VERTEX (4u << R128_INPUT_FACTOR_SHIFT | 2u << R128_INP_FACTOR_A_SHIFT)

/* What the vertices carry for the colour ('u' the whole colour combine, 'f' its factor, 'l' its local, 'a' its
 * local alpha as grey, 'o' its other, '0' black, '1' white) and for alpha ('u' the alpha combine, 'f' its factor,
 * 'l' its local alpha, 'F' the colour combine's factor, as BLEND_VERTEX's weight), the combiner's word, and what
 * the specular colour adds after it (SPEC_LIGHT: 'l' the local, 'a' the local alpha as grey, 0 nothing). */
typedef struct { uint32_t comb; char vc, va, spec; int textured; } rplan;

static int factor_one(int f) { return f == (GR_COMBINE_FACTOR_ZERO | 8); }
static int factor_zero(int f) { return f == GR_COMBINE_FACTOR_ZERO; }
static int factor_tex(int f) { f &= 7; return f == GR_COMBINE_FACTOR_TEXTURE_ALPHA || f == GR_COMBINE_FACTOR_TEXTURE_RGB; }

static rplan plan_of(int textured)
{
    const mg_state *s = &mg.st;
    rplan p;
    int f = s->cc_factor, af = s->ac_factor;
    uint32_t cc = CC(R128_COMB_COPY_INP, F_TEX), ac = AC(R128_COMB_COPY_INP);
    p.vc = 'u', p.va = 'u', p.spec = 0, p.textured = textured;
    /* The colour. */
    if (textured && s->cc_other == GR_COMBINE_OTHER_TEXTURE) {
        switch (s->cc_func) {
        case GR_COMBINE_FUNCTION_SCALE_OTHER:
            if (factor_one(f)) cc = CC(R128_COMB_DIS, F_TEX), p.vc = '1';
            else if (factor_zero(f)) p.vc = 'u';
            else if (!factor_tex(f)) cc = CC(R128_COMB_MODULATE, F_TEX), p.vc = 'f';
            else {
                cc = CC(R128_COMB_DIS, F_TEX), p.vc = '1';
                warn_once(3, "texture * a texture factor: drawn as the texture");
            }
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            p.vc = s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL ? 'l' : 'a';
            if (factor_one(f)) cc = CC(R128_COMB_ADD, F_TEX);
            else if (factor_zero(f)) p.vc = 'u';
            else if (!factor_tex(f)) {
                /* (texture * factor + local: the factor modulated at the vertices, the local added by the
                 * specular colour after the combine) */
                p.spec = p.vc, p.vc = 'f';
                cc = CC(R128_COMB_MODULATE, F_TEX);
            } else {
                cc = CC(R128_COMB_ADD, F_TEX);
                warn_once(4, "texture * a texture factor + local: as texture + local");
            }
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
            p.vc = 'l';
            if (factor_one(f)) cc = CC(R128_COMB_DIS, F_TEX), p.vc = '1';
            else if (factor_zero(f)) p.vc = 'u';
            else if ((f & 7) == GR_COMBINE_FACTOR_TEXTURE_ALPHA && !(f & 8))
                cc = CC(R128_COMB_BLEND_TEXTURE, F_TEX);
            else if ((f & 7) == GR_COMBINE_FACTOR_OTHER_ALPHA && !(f & 8) && s->ac_other == GR_COMBINE_OTHER_TEXTURE)
                cc = CC(R128_COMB_BLEND_TEXTURE, F_TEX);
            else if (!factor_tex(f) && !(f & 8))
                cc = CC(R128_COMB_BLEND_VERTEX, F_TEX), p.va = 'F';
            else {
                cc = CC(R128_COMB_BLEND_TEXTURE, F_TEX);
                warn_once(5, "a blend between the texture and the local by an inverted factor: by the texel's alpha");
            }
            break;
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
            p.vc = 'l';
            if ((f & 7) == GR_COMBINE_FACTOR_TEXTURE_RGB) cc = CC(R128_COMB_MODULATE, (f & 8) ? F_TEX : F_NTEX);
            else if ((f & 7) == GR_COMBINE_FACTOR_TEXTURE_ALPHA) cc = CC(R128_COMB_MODULATE, (f & 8) ? F_ALPHA : F_NALPHA);
            else p.vc = 'u';
            break;
        case GR_COMBINE_FUNCTION_ZERO:
        case GR_COMBINE_FUNCTION_LOCAL:
        case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            break;
        default:
            cc = CC(R128_COMB_MODULATE, F_TEX), p.vc = 'f';
            warn_once(6, "a combine with a subtraction: approximated as texture * factor");
            break;
        }
    } else if (textured && factor_tex(f) && s->cc_func == GR_COMBINE_FUNCTION_SCALE_OTHER) {
        /* (the iterated or constant colour scaled by the texel or its alpha) */
        p.vc = 'o';
        cc = CC(R128_COMB_MODULATE, (f & 7) == GR_COMBINE_FACTOR_TEXTURE_RGB ? ((f & 8) ? F_NTEX : F_TEX)
                                                                              : ((f & 8) ? F_NALPHA : F_ALPHA));
    } else if (textured && factor_tex(f) && s->cc_func != GR_COMBINE_FUNCTION_LOCAL &&
               s->cc_func != GR_COMBINE_FUNCTION_LOCAL_ALPHA && s->cc_func != GR_COMBINE_FUNCTION_ZERO) {
        warn_once(7, "a texture factor on an untextured colour: evaluated without it");
    }
    if (s->cc_invert && cc != CC(R128_COMB_COPY_INP, F_TEX))
        warn_once(8, "an inverted textured colour combine: not inverted");
    /* The alpha. */
    if (textured && s->ac_other == GR_COMBINE_OTHER_TEXTURE) {
        switch (s->ac_func) {
        case GR_COMBINE_FUNCTION_SCALE_OTHER:
            if (factor_one(af)) ac = AC(R128_COMB_DIS), p.va = p.va == 'F' ? 'F' : '1';
            else if (factor_zero(af)) p.va = p.va == 'F' ? 'F' : 'u';
            else if (!factor_tex(af)) ac = AC(R128_COMB_MODULATE), p.va = p.va == 'F' ? 'F' : 'f';
            else ac = AC(R128_COMB_DIS), warn_once(9, "the texel's alpha times a texture factor: the texel's");
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            ac = AC(R128_COMB_ADD), p.va = p.va == 'F' ? 'F' : 'l';
            if (!factor_one(af))
                warn_once(10, "the texel's alpha * factor + local: as the texel's + local");
            break;
        case GR_COMBINE_FUNCTION_ZERO:
        case GR_COMBINE_FUNCTION_LOCAL:
        case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            break;
        default:
            ac = AC(R128_COMB_MODULATE), p.va = p.va == 'F' ? 'F' : 'f';
            warn_once(11, "an alpha combine of the texel's: approximated as its alpha * factor");
            break;
        }
    }
    if (p.va == 'F' && ac == AC(R128_COMB_COPY_INP))
        warn_once(12, "a blend by an iterated factor with the vertex's own alpha: the alpha is the factor");
    p.comb = INPUT_VERTEX | cc | ac;
    return p;
}

/* ---- Triangles -------------------------------------------------------------------------------------------- */

static uint32_t bfactor(GrAlphaBlendFnc_t f, int dst)
{
    switch (f) {
    case GR_BLEND_ZERO: return R128_BLEND_ZERO;
    case GR_BLEND_SRC_ALPHA: return R128_BLEND_SRCALPHA;
    case GR_BLEND_SRC_COLOR: return dst ? R128_BLEND_SRCCOLOR : R128_BLEND_DSTCOLOR;   /* (DST_COLOR as a source) */
    case GR_BLEND_DST_ALPHA: return R128_BLEND_ONE;            /* (no destination alpha in 565: reads as 1) */
    case GR_BLEND_ONE: return R128_BLEND_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return R128_BLEND_INVSRCALPHA;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return dst ? R128_BLEND_INVSRCCOLOR : R128_BLEND_INVDSTCOLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return R128_BLEND_ZERO;
    case GR_BLEND_ALPHA_SATURATE: return dst ? R128_BLEND_ZERO : R128_BLEND_SAT;
    default: return dst ? R128_BLEND_ZERO : R128_BLEND_ONE;
    }
}

/* Glide's compare (GR_CMP_NEVER .. ALWAYS, OpenGL's order) as the chip's (never, <, <=, =, >=, >, !=, always); W
 * buffering keeps a linear 1/w, nearer larger: its depth compares reversed (as the Matrox back end's). */
static uint32_t cmp_of(GrCmpFnc_t f, int reversed)
{
    static const uint8_t m[8] = { 0, 1, 3, 2, 5, 6, 4, 7 };
    if (reversed)
        switch (f) {
        case GR_CMP_LESS: f = GR_CMP_GREATER; break;
        case GR_CMP_GREATER: f = GR_CMP_LESS; break;
        case GR_CMP_LEQUAL: f = GR_CMP_GEQUAL; break;
        case GR_CMP_GEQUAL: f = GR_CMP_LEQUAL; break;
        default: break;
        }
    return (unsigned)f < 8 ? m[f] : 7u;
}

static uint32_t byte_of(float f)
{
    int32_t i = f <= 0.0f ? 0 : f >= 255.0f ? 255 : mga_irint(f);
    return (uint32_t)i;
}

static float clamp255(float f) { return f < 0 ? 0 : f > 255.0f ? 255.0f : f; }

static void tri_one(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod);

void r128_be_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    GrVertex poly[16];
    int depth = mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux, n, i, fogged, mip;
    int32_t ax, ay, bx, by, cx, cy;
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
            k[m] = byte_of(rgb[0]) << 16 | byte_of(rgb[1]) << 8 | byte_of(rgb[2]);
        }
        if (k[0] == key && k[1] == key && k[2] == key)
            return;
    }
    mg_validate();
    fogged = mg_fog_source() == GR_FOG_WITH_TABLE;
    mip = mg_uses_texture() && tmu0.has_source && tmu0.mipmap != GR_MIPMAP_DISABLE && tmu0.small > tmu0.large;
    if (!mg_uses_texture())
        mip |= MG_SPLIT_COMBINE;        /* (untextured combines at the vertices: products of iterated values cut finer) */
    /* (vertices far outside: clipped to the clip window first; the chip's set-up is a float's, but its range isn't
     * known past DOS-GL's guard band of two screens) */
    if (a->x < -1024.0f || b->x < -1024.0f || c->x < -1024.0f || a->x > 2047.0f || b->x > 2047.0f || c->x > 2047.0f ||
        a->y < -1024.0f || b->y < -1024.0f || c->y < -1024.0f || a->y > 2047.0f || b->y > 2047.0f || c->y > 2047.0f) {
        poly[0] = *a, poly[1] = *b, poly[2] = *c;
        n = mg_clip_to_window(poly, 3);
        for (i = 2; i < n; i++)
            mg_split(&poly[0], &poly[i - 1], &poly[i], fogged, mip, tri_one);
        return;
    }
    mg_split(a, b, c, fogged, mip, tri_one);
}

/* A word of the 3D state set, the state marked for sending only when it changed: r128_3d_emit compared every group
 * at every triangle otherwise (~1,000 cycles a triangle on the P3, most of Screamer Rally's sending nothing). */
#define SETW(f, v) do { uint32_t v_ = (v); if (x->f != v_) x->f = v_, r128_3d_dirty = 1; } while (0)

static void tri_one(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod)
{
    const mg_state *s = &mg.st;
    const GrVertex *g[3];
    r128_ctx3d *x = &r128_3d;
    r128_vertex v[3];
    tex_level_hw hw;
    tex_variant var;
    rplan p;
    int wmode = s->depth_mode == GR_DEPTHBUFFER_WBUFFER || s->depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = s->depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux;
    int textured, fog, mult2, chroma, n, i;
    double st[3][2], ks = 0, kt = 0, ms = 0, mt = 0;
    uint32_t tc, misc;
    g[0] = a, g[1] = b, g[2] = c;
    textured = mg_uses_texture() && tmu0.has_source;
    fog = mg_fog_source() != GR_FOG_DISABLE;
    mult2 = fog && (s->fog_mode & GR_FOG_MULT2);
    chroma = textured && s->chroma_mode == GR_CHROMAKEY_ENABLE;
    memset(&var, 0, sizeof var);
    if (chroma) {
        var.kind = TV_CHROMA;
        var.chroma = mg_color_to_argb(s->chroma_value) & 0xFFFFFFu;
    }
    if (textured)
        textured = tex_bind_level(lod, &var, &hw) == 0;
    p = plan_of(textured);
    if (chroma && textured && ((p.comb >> R128_COMB_ALPHA_SHIFT) & 15u) == R128_COMB_COPY_INP) {
        /* (keyed texels' alpha 0 into the alpha: the vertex's times the texel's, for the alpha test below) */
        p.comb = (p.comb & ~(15u << R128_COMB_ALPHA_SHIFT)) | (uint32_t)R128_COMB_MODULATE << R128_COMB_ALPHA_SHIFT;
    }
    if (mult2)
        p.comb = (p.comb & ~(15u | 15u << R128_COLOR_FACTOR_SHIFT)) | CC(R128_COMB_COPY_INP, F_TEX), p.vc = '0';

    /* The texture's words: the bound level alone (the Voodoo's level for this piece), its filter and wrap. */
    if (textured) {
        int dt = hw.hwfmt == HW_TW15 ? R128_TEX_ARGB1555 : hw.hwfmt == HW_TW12 ? R128_TEX_ARGB4444 : R128_TEX_RGB565;
        int big = hw.w_log2 > hw.h_log2 ? hw.w_log2 : hw.h_log2, minified = lod > tmu0.large;
        uint32_t filt;
        if (!minified && tmu0.minf != tmu0.magf) {
            mg_lod_planes L;
            mg_lod_setup(&L, a, b, c);
            minified = mg_minified_at(&L, (a->x + b->x + c->x) / 3.0, (a->y + b->y + c->y) / 3.0);
        }
        filt = (minified ? tmu0.minf : tmu0.magf) == GR_TEXTUREFILTER_BILINEAR ? 1u : 0u;
        SETW(prim_tex_cntl_c, (uint32_t)dt << R128_TEX_DATATYPE_SHIFT | filt << R128_MIN_BLEND_SHIFT |
                              filt << R128_MAG_BLEND_SHIFT | R128_MIP_MAP_DISABLE |
                              (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP ? 2u : 0u) << R128_TEX_CLAMP_S_SHIFT |
                              (tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP ? 2u : 0u) << R128_TEX_CLAMP_T_SHIFT);
        SETW(tex_size_pitch_c, (uint32_t)hw.w_log2 | (uint32_t)big << 4 | (uint32_t)hw.h_log2 << 8 | (uint32_t)big << 12);
        if (x->prim_tex_offset_c[0] != hw.org) {
            for (i = 0; i < 11; i++)
                x->prim_tex_offset_c[i] = hw.org;
            r128_3d_dirty = 1;
        }
        if (tmu0.combine_set) {
            ks = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.w_log2));
            kt = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.h_log2));
        }
        if (tmu0.combine_set && (tmu0.rgb_func != GR_COMBINE_FUNCTION_LOCAL || tmu0.alpha_func != GR_COMBINE_FUNCTION_LOCAL ||
                                 tmu0.rgb_invert || tmu0.alpha_invert))
            warn_once(13, "a texture combine other than the texel: drawn as the texel");
    }
    SETW(prim_texture_combine_cntl_c, p.comb);

    /* The enables, tests and blend. */
    tc = R128_TEX_CACHE_FLUSH | 0x3Fu << R128_LOD_BIAS_SHIFT;
    if (textured)
        tc |= R128_TEXMAP_ENABLE;
    if (fog)
        tc |= R128_FOG_ENABLE;
    if (p.spec && !mult2)
        tc |= R128_SPEC_LIGHT_ENABLE;
    if (s->dither != GR_DITHER_DISABLE)
        tc |= R128_DITHER_ENABLE;
    misc = R128_MISC_SCALE_3D_TEXMAP_SHADE | R128_MISC_SCALE_PIX_REPLICATE;
    if (s->color_mask_rgb && !(s->blend_src == GR_BLEND_ONE && s->blend_dst == GR_BLEND_ZERO)) {
        tc |= R128_ALPHA_ENABLE;
        misc |= bfactor(s->blend_src, 0) << R128_ALPHA_BLEND_SRC_SHIFT | bfactor(s->blend_dst, 1) << R128_ALPHA_BLEND_DST_SHIFT;
    } else
        misc |= (uint32_t)R128_BLEND_ONE << R128_ALPHA_BLEND_SRC_SHIFT | (uint32_t)R128_BLEND_ZERO << R128_ALPHA_BLEND_DST_SHIFT;
    if (s->alpha_test_func != GR_CMP_ALWAYS) {
        tc |= R128_ALPHA_TEST_ENABLE;
        misc |= cmp_of(s->alpha_test_func, 0) << R128_ALPHA_TEST_SHIFT | ((uint32_t)s->alpha_test_ref & 255u);
    } else if (chroma && textured) {
        tc |= R128_ALPHA_TEST_ENABLE;       /* (keyed texels away: alpha above 0) */
        misc |= 5u << R128_ALPHA_TEST_SHIFT;
    } else
        misc |= 7u << R128_ALPHA_TEST_SHIFT;
    SETW(z_sten_cntl_c, (x->z_sten_cntl_c & ~(7u << R128_Z_TEST_SHIFT)) |
                        (depth ? cmp_of(s->depth_func, wmode) : 7u) << R128_Z_TEST_SHIFT);
    if (depth)
        tc |= R128_Z_ENABLE | (s->depth_mask ? R128_Z_WRITE_ENABLE : 0u);
    SETW(tex_cntl_c, tc);
    SETW(misc_3d_state_cntl_reg, misc);
    SETW(plane_3d_mask_c, s->color_mask_rgb ? 0xFFFFFFFFu : 0);
    SETW(fog_color_c, (s->fog_mode & GR_FOG_ADD2) ? 0u : (mg_color_to_argb(s->fog_color) & 0xFFFFFFu));
    SETW(constant_color_c, mg_color_to_argb(s->constant_color));
    r128_3d_format(R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB | (fog || p.spec ? R128_VC_FRMT_SPEC_FRGB : 0) |
                   (textured ? R128_VC_FRMT_S_T : 0));

    /* The vertices. */
    for (n = 0; n < 3; n++) {
        const GrVertex *q = g[n];
        int32_t sx = mg_snap16(q->x), sy = mg_snap16(q->y);
        float rgb[3], al;
        double z;
        if (s->origin == GR_ORIGIN_LOWER_LEFT)
            sy = mg.height * 16 - sy;
        /* (x a 64th of a pixel left: a centre exactly on an edge then falls as the Voodoo's does, the TNT2's) */
        v[n].x = (float)sx * (1.0f / 16.0f) - 1.0f / 64.0f;
        v[n].y = (float)sy * (1.0f / 16.0f);
        if (wmode)
            z = mg_wdepth_from_oow(q->oow, 16);
        else {
            z = q->ooz + s->depth_bias;
            if (z < 0) z = 0;
            if (z > 65535.0) z = 65535.0;
        }
        z = (z + 0.5) / 65535.0;
        v[n].z = (float)(z > 1.0 ? 1.0 : z);
        {
            float w = (s->stw_hint & GR_STWHINT_W_DIFF_TMU0) ? q->tmuvtx[0].oow : q->oow;
            v[n].rhw = w > 0 ? w : 1e-6f;
        }
        switch (p.vc) {
        case 'f': mg_cc_vertex(q, 1, rgb); break;
        case 'l': mg_cc_local_vertex(q, 0, rgb); break;
        case 'a': mg_cc_local_vertex(q, 1, rgb); break;
        case 'o':
            if (s->cc_other == GR_COMBINE_OTHER_CONSTANT) {
                uint32_t k = mg_color_to_argb(s->constant_color);
                rgb[0] = (float)((k >> 16) & 0xFF), rgb[1] = (float)((k >> 8) & 0xFF), rgb[2] = (float)(k & 0xFF);
            } else
                rgb[0] = clamp255(q->r), rgb[1] = clamp255(q->g), rgb[2] = clamp255(q->b);
            break;
        case '0': rgb[0] = rgb[1] = rgb[2] = 0; break;
        case '1': rgb[0] = rgb[1] = rgb[2] = 255.0f; break;
        default: mg_cc_vertex(q, 0, rgb); break;
        }
        switch (p.va) {
        case 'f': al = mg_ac_vertex(q, 1); break;
        case 'F': { float f3[3]; mg_cc_vertex(q, 1, f3); al = f3[0]; } break;
        case 'l': {
            uint32_t k = mg_color_to_argb(s->constant_color);
            al = s->ac_local == GR_COMBINE_LOCAL_CONSTANT ? (float)(k >> 24) : clamp255(q->a);
        } break;
        case '1': al = 255.0f; break;
        default: al = mg_ac_vertex(q, 0); break;
        }
        v[n].argb = byte_of(al) << 24 | byte_of(rgb[0]) << 16 | byte_of(rgb[1]) << 8 | byte_of(rgb[2]);
        v[n].spec = (fog ? byte_of((float)(255.0 - mg_fog_vertex(q))) : 255u) << 24;
        if (p.spec && !mult2) {
            float sl[3];
            mg_cc_local_vertex(q, p.spec == 'a', sl);
            v[n].spec |= byte_of(sl[0]) << 16 | byte_of(sl[1]) << 8 | byte_of(sl[2]);
        }
        v[n].s = v[n].t = v[n].s2 = v[n].t2 = 0;
        if (textured)
            mg_st_of(q, &st[n][0], &st[n][1]);
    }
    if (textured) {
        /* s and t over the bound level's texture (0..1 a period), whole periods taken off when it repeats (the
         * chip interpolates in floats). */
        for (n = 0; n < 3; n++)
            st[n][0] *= ks, st[n][1] *= kt;
        if (tmu0.clamp_s != GR_TEXTURECLAMP_CLAMP) {
            ms = st[0][0] < st[1][0] ? st[0][0] : st[1][0];
            ms = st[2][0] < ms ? st[2][0] : ms;
            ms = ms > -1e9 && ms < 1e9 ? mga_floor(ms) : 0;
        }
        if (tmu0.clamp_t != GR_TEXTURECLAMP_CLAMP) {
            mt = st[0][1] < st[1][1] ? st[0][1] : st[1][1];
            mt = st[2][1] < mt ? st[2][1] : mt;
            mt = mt > -1e9 && mt < 1e9 ? mga_floor(mt) : 0;
        }
        for (n = 0; n < 3; n++)
            v[n].s = (float)(st[n][0] - ms), v[n].t = (float)(st[n][1] - mt);
    }
    r128_tri(&v[0], &v[1], &v[2]);
    mg_stats.tris++;
    mg_stats.tri_drawn++;
}

/* A level the texture manager converted (565, 1555 or 4444, rows hw_w apart) into place: the chip's own linear
 * layout, the rows as they are. Through the engine in the command stream (r128_upload): ordered with the queued
 * triangles, so no sync, and the CPU writes cached ring memory rather than uncached video memory (16-bit stores at
 * ~140 cycles each on the P3: a 256x256 level took ~20 ms, Screamer Rally converts one or two a frame). The copy
 * is the bits as they are, whatever the 16-bit format. Rows under 8 texels (the engine's pitch) by the CPU after a
 * sync: the Rage 128 takes CPU writes to video memory only with the engine idle (t14). */
static void tex_write(uint32_t org, const uint16_t *texels, int hw_w, int hw_h)
{
    volatile uint16_t *d = (volatile uint16_t *)(r128.fb + org);
    long i, n = (long)hw_w * hw_h;
    if (hw_w % 8 != 0 || r128_upload(org, hw_w, R128_TEX_RGB565, 0, 0, hw_w, hw_h, (const uint32_t *)texels) != 0) {
        sync_said("texture");
        for (i = 0; i < n; i++)
            d[i] = texels[i];
        r128_vram_written(org + (uint32_t)n * 2u - 2u);
    }
    r128_3d_tex_written();
}

const mg_backend mg_r128_backend = {
    "r128", probe, win_open, win_close, fault, exit_restore, sync, busy, in_vblank, vcount, set_ramp, validate,
    clear, swap, present_front, r128_be_tri, tex_write, 32, 0, 0, 1, 0
};
