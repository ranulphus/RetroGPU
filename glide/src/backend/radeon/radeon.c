/* radeon.c - the ATI Radeon's Glide back end (RD6 of docs/radeon.md): Glide 2 through the Radeon device
 * (hal/src/rg/radeon.c: the mode, the buffers, the command processor) and its layer (hal/src/radeon: 2D, the 3D
 * state, primitives). Made from the Rage 128's back end (backend/r128).
 *
 * - The window: the BIOS's 16-bit mode at a 1024-pixel pitch (the Voodoo's 2048-byte stride Glide games assume),
 *   double buffered, a Z16 aux buffer at the same pitch.
 * - Clears by the 2D engine, flips in the command stream, the clip window as the chip's scissor.
 * - Triangles: the Voodoo's level per triangle and table fog's factor per vertex (share.c's split). With a texture
 *   the colour and alpha combines run on the chip's combiner units (each A x B + C, A x B - C or a blend, over the
 *   texel, the iterated colour as the vertex's, the constant colour as the units' factor colour and the unit
 *   before): every Glide function has its form, two units for the ones with a subtraction; without a texture the
 *   combine is worked out at the vertices (combine/eval.c), as on the other cards. Depth as the Matrox cards keep
 *   it (Z: ooz; W: a linear 1/w, compares reversed), the alpha test, blending, dithering, fog by the specular's
 *   alpha. Chroma key by the core's keyed texture variants and the alpha test. One texture level a triangle. */
#include "glide/mg.h"
#include "backend/backend.h"
#include "backend/share.h"
#include "combine/eval.h"
#include "tex/texmgr.h"
#include "tex/texconv.h"
#include "rg/device.h"
#include "rg/radeon_dev.h"
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "radeon/regs3d_radeon.h"
#include "dll/config.h"
#include "mga/sys.h"
#include "mga/fp.h"
#include <string.h>

static int warned[24];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        mg_log(MG_LOG_WARN, "radeon: %s", what);
    }
}

static void log_line(const char *line) { mg_line("%s", line); }

/* ---- The card --------------------------------------------------------------------------------------------- */

static unsigned sync_timeouts;
static unsigned long vs_waits, vs_fails, flips_queued;
static uint32_t flip_seq;               /* the last flip queued in the command stream (radeon_flip_queue) */
static int line_flips;                  /* (swaps without the vertical sync: a start taken at the next scan line) */
/* (no bias: the chip rounds vertices to sixteenths of a pixel, Glide's own grid, so the Rage 128's and the TNT2's
 * 64th of a pixel never reaches it: t04 on the P3 is 0.08% off the Voodoo's edges whatever is set here. MGAGLIDE
 * radeon_bias=X,Y, in 256ths, stays as a test aid.) */
static float bias_x, bias_y;

static void sync_said(const char *where)
{
    if (radeon_sync(500000) != 0 && ++sync_timeouts <= 8)
        mg_line("MGL-W radeon sync timed out (%s, frame %u): RBBM_STATUS=%08lx CP_RB_RPTR=%08lx", where, mg.frame,
                (unsigned long)radeon_rd(RADEON_RBBM_STATUS), (unsigned long)radeon_rd(RADEON_CP_RB_RPTR));
}

static int probe(void)
{
    static int said;
    rg_info i;
    rg_set_format(mg_vsnprintf);
    rg_set_log(log_line);
    if (mg_config.radeon_bias_set)
        bias_x = (float)mg_config.radeon_bias[0] / 256.0f, bias_y = (float)mg_config.radeon_bias[1] / 256.0f;
    if (mg_config.radeon_vram[1] > mg_config.radeon_vram[0])    /* (MGAGLIDE radeon_vram=A-B: megabytes of the card) */
        rg_radeon_vram_mb[0] = mg_config.radeon_vram[0], rg_radeon_vram_mb[1] = mg_config.radeon_vram[1];
    if (rg_radeon_device.probe() != 0)
        return -1;
    if (!said) {
        said = 1;
        rg_radeon_device.info(&i);
        mg_line("MGL-CARD %s id=%04x rev=%02x family=radeon emulated=0 fb=%08lx mmio=%08lx vram=%lu", i.chip_name,
                i.device_id, i.revision, (unsigned long)i.fb_phys, (unsigned long)i.mmio_phys,
                (unsigned long)i.vram_bytes);
    }
    mg_vram = radeon.fb;
    return 0;
}

/* A 16-bit surface of the window (a colour buffer, or the aux buffer as one) as the 2D engine's target. */
static void fill16(uint32_t off, int x, int y, int w, int h, uint32_t v)
{
    if (w <= 0 || h <= 0)
        return;
    radeon_set_target(off, mg.pitch_px * 2, 16);
    radeon_set_clip(0, 0, mg.rw, mg.rh);
    radeon_fill(x, y, w, h, v);
}

static FxBool win_open(int w, int h, GrColorFormat_t color_format, GrOriginLocation_t origin_location,
                       int nColBuffers, int nAuxBuffers)
{
    rg_open o;
    rg_opened op;
    memset(&o, 0, sizeof o);
    o.width = w, o.height = h, o.color_bits = 16, o.depth_bits = nAuxBuffers > 0 ? 16 : 0, o.double_buffer = 1;
    o.pitch_px = w <= 1024 ? 1024 : 0;
    if (rg_radeon_device.open(&o, &op) != 0) {
        mg_log(MG_LOG_ERROR, "radeon: %dx%d: %s", w, h, rg_error());
        rg_radeon_device.close();
        return FXFALSE;
    }
    mg_vram = radeon.fb;
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
    mg.buf_off[0] = rg_radeon.front_off;
    mg.buf_off[1] = rg_radeon.back_off;
    mg.aux_off = mg.has_aux ? rg_radeon.z_off : 0;
    mg.heap_off = op.mem_start;
    mg.heap_end = op.mem_end;
    tex_heap_init(mg.heap_off, mg.heap_end);
    tex_reset();
    /* (the 3D state for this window: Mesa's values; Z16 at the colour's pitch) */
    radeon_3d_init(mg.buf_off[1], mg.pitch_px * 2, 16, mg.has_aux ? mg.aux_off : 0, mg.pitch_px * 2,
                   mg.has_aux ? 16 : 0, w, h);
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
    radeon_set_start(mg.buf_off[mg.front]);
    flip_seq = radeon_cp_running() ? radeon_flip_seen() : 0, flips_queued = 0, line_flips = 0;
    mg_gamma_apply();
    mg.fogcol_valid = 0;
    mg_hooks_install();
    mg_line("MGL-WINOPEN %dx%d mode=radeon bpp=16 pitch=%d buffers=%d aux=%d z16 vram=%lu heap=%lu", w, h,
            mg.pitch_px, mg.nbuffers, mg.has_aux, (unsigned long)op.vram_bytes,
            (unsigned long)(mg.heap_end - mg.heap_off));
    return FXTRUE;
}

static void win_close(void)
{
    sync_said("close");
    radeon_ramp_off();
    rg_radeon_device.close();
    mg.open = 0;
    sys_unhook_faults();
    sys_unhook_exit();
    mg_line("MGL-WINCLOSE frames=%u sync_timeouts=%u vsync_waits=%lu failed=%lu flips_queued=%lu", mg.frame,
            sync_timeouts, vs_waits, vs_fails, flips_queued);
}

static void fault(void) { radeon_ramp_off(); rg_radeon_device.close(); }
static void exit_restore(void) { radeon_sync(500000); radeon_ramp_off(); rg_radeon_device.close(); }
static void sync(void) { sync_said("sync"); }
static int busy(void) { return !radeon_idle(); }
static int in_vblank(void) { return (sys_inb(0x3DA) & 0x08) != 0; }
static unsigned vcount(void) { return 0; }

static void set_ramp(const uint8_t ramp[256]) { radeon_set_ramp(ramp); }

/* ---- State ------------------------------------------------------------------------------------------------ */

static void validate(void)
{
    radeon_ctx3d *c = &radeon_3d;
    /* (called before every triangle: the state marked for sending only when the target or the clip changed) */
    if (mg.dirty & MG_DIRTY_TARGET) {
        c->rb3d_coloroffset = (radeon.fb_location + mg_buffer_offset(mg.render_buffer)) & RADEON_COLOROFFSET_MASK;
        radeon_3d_dirty = 1;
    }
    if (mg.dirty & (MG_DIRTY_TARGET | MG_DIRTY_CLIP)) {
        int y0 = mg.st.clip_y0, y1 = mg.st.clip_y1, x0 = mg.st.clip_x0, x1 = mg.st.clip_x1;
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
            y0 = mg.height - mg.st.clip_y1;
            y1 = mg.height - mg.st.clip_y0;
        }
        if (x1 <= x0 || y1 <= y0)
            x0 = y0 = 2047, x1 = y1 = 2047;     /* (nothing: a pixel off the screen) */
        else
            x1--, y1--;                         /* (the scissor's bottom right inclusive) */
        c->re_top_left = (uint32_t)y0 << 16 | (uint32_t)x0;
        c->re_width_height = (uint32_t)x1 << RADEON_RE_WIDTH_SHIFT | (uint32_t)y1 << RADEON_RE_HEIGHT_SHIFT;
        radeon_3d_dirty = 1;
    }
    mg.dirty = 0;
}

static void clear(int x0, int y0, int x1, int y1, GrColor_t color, FxU16 depth)
{
    if (mg.st.color_mask_rgb)
        fill16(mg_buffer_offset(mg.render_buffer), x0, y0, x1 - x0, y1 - y0, mg_argb_to_565(mg_color_to_argb(color)));
    if (mg.has_aux && mg.st.depth_mask && mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE)
        fill16(mg.aux_off, x0, y0, x1 - x0, y1 - y0, mg_depth_clear_value(depth));
    radeon_cp_flush();
}

static void swap(int swap_interval)
{
    int i;
    /* Without the vertical sync a start is taken at the next scan line (FLIP_CNTL: docs/radeon.md, DFLIP): changed
     * with the engine idle. */
    if (line_flips != (swap_interval == 0)) {
        sync_said("swap mode");
        line_flips = swap_interval == 0;
        radeon_flip_at_line(line_flips);
    }
    /* With the command processor: the flip in the command stream (radeon_flip_queue), the stream held until the new
     * buffer is shown and the next frame's drawing, queued after it, reaching the buffer that was on screen only
     * then; the CPU goes on with the game. One flip in flight: the one before shown first. */
    if (swap_interval <= 1 && radeon_cp_running()) {
        uint32_t t0 = sys_time_us();
        PROF_BACK(PROF_G_VSYNC);
        while ((int32_t)(flip_seq - radeon_flip_seen()) > 0)
            if (sys_time_us() - t0 > 500000u) {
                sync_said("flip");
                break;
            }
        if (radeon_flip_queue(mg.buf_off[mg.back], flip_seq + 1u, swap_interval == 1) == 0) {
            flip_seq++, flips_queued++, vs_waits++;
            PROF_BACK(PROF_G_SWAP);
            return;
        }
    }
    sync_said("swap");
    /* (the start first, then the retrace: the chip takes it at the end of the vertical blank) */
    radeon_set_start(mg.buf_off[mg.back]);
    PROF_BACK(PROF_G_VSYNC);
    for (i = 0; i < swap_interval; i++) {
        if (radeon_vsync_wait(50000) != 0)
            vs_fails++;
        vs_waits++;
    }
    PROF_BACK(PROF_G_SWAP);
}

static void present_front(void) { }             /* (no scaled fits) */

/* ---- The Voodoo's combines on the chip's combiner units ----------------------------------------------------
 * A unit computes A x B + C, A x B - C, or the blend A x (1 - C) + B x C, each argument one of: nothing, the
 * texel (its colour or its alpha), the vertex's colour or alpha (Glide's iterated ones), the unit's factor colour
 * or alpha (Glide's constant), the unit before's result; or the complement of one. */
typedef struct { uint8_t c, a, inv; } opnd;     /* its code as a colour argument and as an alpha argument */
static const opnd O_ZERO = { 0, 0, 0 }, O_ONE = { 0, 0, 1 }, O_PREV = { 2, 1, 0 }, O_T = { 10, 5, 0 }, O_TA = { 11, 5, 0 },
                  O_I = { 4, 2, 0 }, O_IA = { 5, 2, 0 }, O_K = { 8, 4, 0 }, O_KA = { 9, 4, 0 };
enum { U_MULADD, U_SUB, U_BLEND };

static opnd inv(opnd o) { o.inv ^= 1; return o; }

static uint32_t unit_word(int op, opnd A, opnd B, opnd C, int alpha)
{
    uint32_t w = RADEON_CLAMP_TX | RADEON_SCALE_1X |
                 (op == U_SUB ? RADEON_BLEND_CTL_SUBTRACT : op == U_BLEND ? RADEON_BLEND_CTL_BLEND : RADEON_BLEND_CTL_ADD);
    if (alpha)
        w |= (uint32_t)A.a | (uint32_t)B.a << 4 | (uint32_t)C.a << 8;
    else
        w |= (uint32_t)A.c | (uint32_t)B.c << 5 | (uint32_t)C.c << 10;
    return w | (uint32_t)A.inv << 15 | (uint32_t)B.inv << 16 | (uint32_t)C.inv << 17;
}

/* One of Glide's combines (the colour's, or the alpha's) as up to three units' words: how many. local, other,
 * alocal: its inputs; factor: its factor, complemented already. */
static int plan_units(int func, int invert, opnd local, opnd other, opnd alocal, opnd factor, int alpha, uint32_t *w)
{
#define U(op, A, B, C) unit_word(op, A, B, C, alpha)
    int n = 1;
    switch (func) {
    case GR_COMBINE_FUNCTION_ZERO:
        w[0] = U(U_MULADD, O_ZERO, O_ZERO, O_ZERO);
        break;
    case GR_COMBINE_FUNCTION_LOCAL:
        w[0] = U(U_MULADD, O_ZERO, O_ZERO, local);
        break;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        w[0] = U(U_MULADD, O_ZERO, O_ZERO, alocal);
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER:                               /* f x other */
        w[0] = U(U_MULADD, other, factor, O_ZERO);
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:                     /* f x other + local */
        w[0] = U(U_MULADD, other, factor, local);
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        w[0] = U(U_MULADD, other, factor, alocal);
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:                   /* f x (other - local): nothing below 0 either way */
        w[0] = U(U_SUB, other, O_ONE, local);
        w[1] = U(U_MULADD, O_PREV, factor, O_ZERO);
        n = 2;
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:         /* f x other + (1 - f) x local */
        w[0] = U(U_BLEND, local, other, factor);
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:   /* f x (other - local) + local's alpha */
        w[0] = U(U_SUB, other, O_ONE, local);
        w[1] = U(U_MULADD, O_PREV, factor, alocal);
        n = 2;
        warn_once(3, "f x (other - local) + local alpha: the difference never below 0 on this chip");
        break;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:               /* (1 - f) x local */
        w[0] = U(U_MULADD, local, inv(factor), O_ZERO);
        break;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:         /* local's alpha - f x local */
        w[0] = U(U_MULADD, local, factor, O_ZERO);
        w[1] = U(U_SUB, alocal, O_ONE, O_PREV);
        n = 2;
        break;
    default:
        w[0] = U(U_MULADD, other, factor, O_ZERO);
        warn_once(4, "an unknown combine function: drawn as f x other");
        break;
    }
    if (invert)
        w[n++] = U(U_MULADD, O_ZERO, O_ZERO, inv(O_PREV));
#undef U
    return n;
}

/* The units for the state: n of them, each a colour word and an alpha word. cpu: no texture, the whole combine
 * worked out at the vertices and passed through (the vertices then carry its result, not the iterated colour). */
typedef struct { int n, cpu; uint32_t c[4], a[4]; } rplan;

static rplan plan_of(int textured, int chroma, int black)
{
    const mg_state *s = &mg.st;
    rplan p;
    int nc, na, i;
    memset(&p, 0, sizeof p);
    if (!textured) {
        p.n = 1, p.cpu = 1;
        p.c[0] = unit_word(U_MULADD, O_ZERO, O_ZERO, O_I, 0);
        p.a[0] = unit_word(U_MULADD, O_ZERO, O_ZERO, O_IA, 1);
        if (black)
            p.c[0] = unit_word(U_MULADD, O_ZERO, O_ZERO, O_ZERO, 0);
        return p;
    }
    {
        opnd cl = s->cc_local == GR_COMBINE_LOCAL_CONSTANT ? O_K : O_I;
        opnd co = s->cc_other == GR_COMBINE_OTHER_TEXTURE ? O_T : s->cc_other == GR_COMBINE_OTHER_CONSTANT ? O_K : O_I;
        opnd al = s->ac_local == GR_COMBINE_LOCAL_CONSTANT ? O_KA : O_IA;
        opnd ao = s->ac_other == GR_COMBINE_OTHER_TEXTURE ? O_TA : s->ac_other == GR_COMBINE_OTHER_CONSTANT ? O_KA : O_IA;
        opnd f, af;
        if (s->ac_local == GR_COMBINE_LOCAL_DEPTH)
            warn_once(5, "the depth as the alpha combine's local: the iterated alpha used");
        switch (s->cc_factor & 7) {
        case GR_COMBINE_FACTOR_LOCAL: f = cl; break;
        case GR_COMBINE_FACTOR_OTHER_ALPHA: f = ao; break;
        case GR_COMBINE_FACTOR_LOCAL_ALPHA: f = al; break;
        case GR_COMBINE_FACTOR_TEXTURE_ALPHA: f = O_TA; break;
        case GR_COMBINE_FACTOR_TEXTURE_RGB: f = O_T; break;
        default: f = O_ZERO; break;
        }
        if (s->cc_factor & 8)
            f = inv(f);
        switch (s->ac_factor & 7) {
        case GR_COMBINE_FACTOR_LOCAL: case GR_COMBINE_FACTOR_LOCAL_ALPHA: af = al; break;
        case GR_COMBINE_FACTOR_OTHER_ALPHA: af = ao; break;
        case GR_COMBINE_FACTOR_TEXTURE_ALPHA: case GR_COMBINE_FACTOR_TEXTURE_RGB: af = O_TA; break;
        default: af = O_ZERO; break;
        }
        if (s->ac_factor & 8)
            af = inv(af);
        nc = plan_units(s->cc_func, s->cc_invert, cl, co, al, f, 0, p.c);
        na = plan_units(s->ac_func, s->ac_invert, al, ao, al, af, 1, p.a);
        if (black)                      /* (fog's MULT2: the colour black, the fog colour blended over it) */
            nc = 1, p.c[0] = unit_word(U_MULADD, O_ZERO, O_ZERO, O_ZERO, 0);
        /* (keyed texels' alpha 0 into an alpha that doesn't take the texel's: for the alpha test that drops them) */
        if (chroma && (s->ac_other != GR_COMBINE_OTHER_TEXTURE || s->ac_func == GR_COMBINE_FUNCTION_ZERO ||
                       s->ac_func == GR_COMBINE_FUNCTION_LOCAL || s->ac_func == GR_COMBINE_FUNCTION_LOCAL_ALPHA) && na < 3)
            p.a[na++] = unit_word(U_MULADD, O_PREV, O_TA, O_ZERO, 1);
    }
    p.n = nc > na ? nc : na;
    for (i = nc; i < p.n; i++)
        p.c[i] = unit_word(U_MULADD, O_ZERO, O_ZERO, O_PREV, 0);
    for (i = na; i < p.n; i++)
        p.a[i] = unit_word(U_MULADD, O_ZERO, O_ZERO, O_PREV, 1);
    return p;
}

/* ---- Triangles -------------------------------------------------------------------------------------------- */

/* A blend factor's code (RB3D_BLENDCNTL's GL_ numbering: 32 zero, 33 one, 34 and 35 the source's colour and its
 * complement, 36 and 37 the destination's, 38 and 39 the source's alpha, 42 saturate). */
static uint32_t bfactor(GrAlphaBlendFnc_t f, int dst)
{
    switch (f) {
    case GR_BLEND_ZERO: return 32;
    case GR_BLEND_SRC_ALPHA: return 38;
    case GR_BLEND_SRC_COLOR: return dst ? 34 : 36;             /* (DST_COLOR as a source) */
    case GR_BLEND_DST_ALPHA: return 33;                        /* (no destination alpha in 565: reads as 1) */
    case GR_BLEND_ONE: return 33;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return 39;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return dst ? 35 : 37;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return 32;
    case GR_BLEND_ALPHA_SATURATE: return dst ? 32 : 42;
    default: return dst ? 32 : 33;
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

void radeon_be_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
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

/* A word of the 3D state set, the state marked for sending only when it changed. */
#define SETW(f, v) do { uint32_t v_ = (v); if (x->f != v_) x->f = v_, radeon_3d_dirty = 1; } while (0)

static void tri_one(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod)
{
    const mg_state *s = &mg.st;
    const GrVertex *g[3];
    radeon_ctx3d *x = &radeon_3d;
    radeon_vertex v[3];
    tex_level_hw hw;
    tex_variant var;
    rplan p;
    int wmode = s->depth_mode == GR_DEPTHBUFFER_WBUFFER || s->depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = s->depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux;
    int textured, fog, mult2, chroma, n, i;
    double st[3][2], ks = 0, kt = 0, ms = 0, mt = 0;
    uint32_t pp, rb, zs, misc, kcol;
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
    p = plan_of(textured, chroma && textured, mult2);

    /* The texture's words: the bound level alone (the Voodoo's level for this piece), its filter and wrap; the
     * same on every unit the combine uses, each reading the vertex's one pair of coordinates. */
    pp = RADEON_SCISSOR_ENABLE | RADEON_ANTI_ALIAS_NONE;
    if (textured) {
        int dt = hw.hwfmt == HW_TW15 ? 3 : hw.hwfmt == HW_TW12 ? 5 : 4;         /* (ARGB1555, ARGB4444, RGB565) */
        int minified = lod > tmu0.large;
        uint32_t filt, fw, tw;
        if (!minified && tmu0.minf != tmu0.magf) {
            mg_lod_planes L;
            mg_lod_setup(&L, a, b, c);
            minified = mg_minified_at(&L, (a->x + b->x + c->x) / 3.0, (a->y + b->y + c->y) / 3.0);
        }
        filt = (minified ? tmu0.minf : tmu0.magf) == GR_TEXTUREFILTER_BILINEAR;
        fw = (filt ? RADEON_MAG_FILTER_LINEAR | RADEON_MIN_FILTER_LINEAR : RADEON_MAG_FILTER_NEAREST | RADEON_MIN_FILTER_NEAREST) |
             (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP ? RADEON_CLAMP_S_CLAMP_LAST : RADEON_CLAMP_S_WRAP) |
             (tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP ? RADEON_CLAMP_T_CLAMP_LAST : RADEON_CLAMP_T_WRAP);
        tw = (uint32_t)dt | (dt == 4 ? 0u : RADEON_TXFORMAT_ALPHA_IN_MAP) | (uint32_t)hw.w_log2 << RADEON_TXFORMAT_WIDTH_SHIFT |
             (uint32_t)hw.h_log2 << RADEON_TXFORMAT_HEIGHT_SHIFT | RADEON_TXFORMAT_ST_ROUTE_STQ0 |
             RADEON_TXFORMAT_PERSPECTIVE_ENABLE;
        for (i = 0; i < p.n; i++) {
            SETW(tex[i].txfilter, fw);
            SETW(tex[i].txformat, tw);
            SETW(tex[i].txoffset, radeon.fb_location + hw.org);
            pp |= RADEON_TEX_0_ENABLE << i;
        }
        if (tmu0.combine_set) {
            ks = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.w_log2));
            kt = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.h_log2));
        }
        if (tmu0.combine_set && (tmu0.rgb_func != GR_COMBINE_FUNCTION_LOCAL || tmu0.alpha_func != GR_COMBINE_FUNCTION_LOCAL ||
                                 tmu0.rgb_invert || tmu0.alpha_invert))
            warn_once(13, "a texture combine other than the texel: drawn as the texel");
    }
    kcol = mg_color_to_argb(s->constant_color);
    for (i = 0; i < 3; i++) {
        if (i < p.n) {
            SETW(tex[i].txcblend, p.c[i]);
            SETW(tex[i].txablend, p.a[i]);
            pp |= RADEON_TEX_BLEND_0_ENABLE << i;
        }
        SETW(tex[i].tfactor, kcol);
    }

    /* The enables, tests and blend. */
    rb = RADEON_PLANE_MASK_ENABLE | RADEON_COLOR_FORMAT_RGB565 | RADEON_ZBLOCK16;
    if (fog)
        pp |= RADEON_FOG_ENABLE;
    if (s->dither != GR_DITHER_DISABLE)
        rb |= RADEON_DITHER_ENABLE;
    if (s->color_mask_rgb && !(s->blend_src == GR_BLEND_ONE && s->blend_dst == GR_BLEND_ZERO)) {
        rb |= RADEON_ALPHA_BLEND_ENABLE;
        SETW(rb3d_blendcntl, RADEON_COMB_FCN_ADD_CLAMP | bfactor(s->blend_src, 0) << 16 | bfactor(s->blend_dst, 1) << 24);
    } else
        SETW(rb3d_blendcntl, RADEON_COMB_FCN_ADD_CLAMP | RADEON_SRC_BLEND_GL_ONE | RADEON_DST_BLEND_GL_ZERO);
    misc = x->pp_misc & ~(RADEON_ALPHA_TEST_OP_MASK | RADEON_REF_ALPHA_MASK);
    if (s->alpha_test_func != GR_CMP_ALWAYS) {
        pp |= RADEON_ALPHA_TEST_ENABLE;
        misc |= cmp_of(s->alpha_test_func, 0) << 8 | ((uint32_t)s->alpha_test_ref & 255u);
    } else if (chroma && textured) {
        pp |= RADEON_ALPHA_TEST_ENABLE;     /* (keyed texels away: alpha above 0) */
        misc |= RADEON_ALPHA_TEST_GREATER;
    } else
        misc |= RADEON_ALPHA_TEST_PASS;
    zs = (x->rb3d_zstencilcntl & 0xFu) | RADEON_STENCIL_TEST_ALWAYS;
    if (depth) {
        rb |= RADEON_Z_ENABLE;
        zs |= cmp_of(s->depth_func, wmode) << 4 | (s->depth_mask ? RADEON_Z_WRITE_ENABLE : 0u);
    } else
        zs |= RADEON_Z_TEST_ALWAYS;
    SETW(pp_cntl, pp);
    SETW(pp_misc, misc);
    SETW(rb3d_cntl, rb);
    SETW(rb3d_zstencilcntl, zs);
    SETW(rb3d_planemask, s->color_mask_rgb ? 0xFFFFFFFFu : 0);
    SETW(pp_fog_color, ((s->fog_mode & GR_FOG_ADD2) ? 0u : (mg_color_to_argb(s->fog_color) & 0xFFFFFFu)) |
                       RADEON_FOG_VERTEX | RADEON_FOG_USE_SPEC_ALPHA);
    radeon_3d_format(RADEON_VTX_RHW | RADEON_VTX_ARGB | (fog ? RADEON_VTX_SPEC : 0) | (textured ? RADEON_VTX_ST : 0));

    /* The vertices. */
    for (n = 0; n < 3; n++) {
        const GrVertex *q = g[n];
        int32_t sx = mg_snap16(q->x), sy = mg_snap16(q->y);
        float rgb[3], al;
        double z;
        if (s->origin == GR_ORIGIN_LOWER_LEFT)
            sy = mg.height * 16 - sy;
        /* (a bias so that a centre exactly on an edge falls as the Voodoo's does: bias_x, bias_y) */
        v[n].x = (float)sx * (1.0f / 16.0f) + bias_x;
        v[n].y = (float)sy * (1.0f / 16.0f) + bias_y;
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
        if (p.cpu) {                    /* (no texture: the combines' results) */
            mg_cc_vertex(q, 0, rgb);
            al = mg_ac_vertex(q, 0);
        } else {                        /* (the iterated colour and alpha: the units combine them) */
            rgb[0] = clamp255(q->r), rgb[1] = clamp255(q->g), rgb[2] = clamp255(q->b);
            al = clamp255(q->a);
        }
        v[n].argb = byte_of(al) << 24 | byte_of(rgb[0]) << 16 | byte_of(rgb[1]) << 8 | byte_of(rgb[2]);
        v[n].spec = (fog ? byte_of((float)(255.0 - mg_fog_vertex(q))) : 255u) << 24;
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
    radeon_tri(&v[0], &v[1], &v[2]);
    mg_stats.tris++;
    mg_stats.tri_drawn++;
}

/* A level the texture manager converted (565, 1555 or 4444, rows hw_w apart, at least 16 texels: 32 bytes, the
 * chip's shortest row) into place, the rows as they are: through the command stream (radeon_upload: ordered with
 * the queued triangles, no sync; the bytes as they are whatever the format), else by the CPU after a sync. */
static void tex_write(uint32_t org, const uint16_t *texels, int hw_w, int hw_h)
{
    uint32_t bytes = (uint32_t)hw_w * (uint32_t)hw_h * 2u;
    if (radeon_upload(org, (const uint32_t *)texels, bytes) != 0) {
        volatile uint16_t *d = (volatile uint16_t *)(radeon.fb + org);
        long i, n = (long)hw_w * hw_h;
        sync_said("texture");
        for (i = 0; i < n; i++)
            d[i] = texels[i];
        radeon_vram_written(org + bytes - 2u);
    }
    radeon_3d_tex_written();
}

const mg_backend mg_radeon_backend = {
    "radeon", probe, win_open, win_close, fault, exit_restore, sync, busy, in_vblank, vcount, set_ramp, validate,
    clear, swap, present_front, radeon_be_tri, tex_write, 32, 0, 0, 1, 16
};
