/* tnt2.c - the Riva TNT2's Glide back end (T5 of docs/tnt2.md; the plan of 2026-11-19): Glide 2 on the NV5 through
 * the TNT2 device (hal/src/rg/tnt2.c: the mode, the buffers) and its layer (hal/src/tnt2: the command ring, 2D,
 * the textured triangle object 0x54).
 *
 * - The window: the BIOS's mode at a 1024-pixel pitch, the Voodoo's 2048-byte stride that Glide games assume
 *   (VBE 4F06, else the CRTC's offset), double buffered (triple drawn as double), a Z16 aux buffer.
 * - Clears by the 2D engine, flips by the display start, the 3D clip from Glide's clip window.
 * - Triangles: Glide's snapped window coordinates as the chip's screen-space vertices; the colour and alpha
 *   combines evaluated at the vertices (combine/eval.c) when no texture term is in them; depth as the Matrox
 *   cards keep it (Z: ooz; W: a linear 1/w, compares reversed) in Z16; the alpha test, blending, dithering, and
 *   table fog per vertex into the specular's fog byte. Textures: G5 (until then a white texel, said once). */
#include "glide/mg.h"
#include "backend/backend.h"
#include "combine/eval.h"
#include "tex/texmgr.h"
#include "tex/texconv.h"
#include "rg/device.h"
#include "rg/tnt2_dev.h"
#include "tnt2/tnt2.h"
#include "tnt2/words.h"
#include "mga/sys.h"
#include "mga/fp.h"
#include <string.h>

static int warned[16];

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        mg_log(MG_LOG_WARN, "tnt2: %s", what);
    }
}

static void log_line(const char *line) { mg_line("%s", line); }

/* ---- The card --------------------------------------------------------------------------------------------- */

/* Every sync the back end waits on: a time-out logged with what PGRAPH says (its interrupt, the method it trapped
 * on and its data, NSOURCE and NSTATUS), the first few times, and counted for MGL-WINCLOSE. */
static unsigned sync_timeouts;
static unsigned long vs_waits, vs_cyc, vs_fails, vs_short;  /* the swaps' retrace waits (MGL-WINCLOSE) */

/* (the time-stamp counter's low half: the BIOS tick under Watcom is too coarse to time a retrace wait) */
#if defined(__WATCOMC__)
uint32_t tsc_lo(void);
#pragma aux tsc_lo = 0x0F 0x31 value [eax] modify exact [eax edx];   /* (rdtsc: the code is built for a 386) */
#else
static uint32_t tsc_lo(void) { uint32_t lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return lo; }
#endif

static void sync_said(const char *where)
{
    if (tnt2_sync(500000) != 0) {
        uint32_t addr, data, intr = tnt2_3d_trap(&addr, &data);
        if (++sync_timeouts <= 8)
            mg_line("MGL-W tnt2 sync timed out (%s, frame %u): intr=%08lx trapped=%08lx data=%08lx nsource=%08lx "
                    "nstatus=%08lx", where, mg.frame, (unsigned long)intr, (unsigned long)addr, (unsigned long)data,
                    (unsigned long)tnt2_regs[0x400108 / 4], (unsigned long)tnt2_regs[0x400104 / 4]);
    }
}

static int probe(void)
{
    static int said;
    rg_info i;
    rg_set_format(mg_vsnprintf);
    rg_set_log(log_line);
    if (rg_tnt2_device.probe() != 0)
        return -1;
    if (!said) {
        said = 1;
        rg_tnt2_device.info(&i);
        mg_line("MGL-CARD %s id=%04x rev=%02x family=tnt2 emulated=0 fb=%08lx mmio=%08lx", i.chip_name,
                i.device_id, i.revision, (unsigned long)i.fb_phys, (unsigned long)i.mmio_phys);
    }
    mg_vram = tnt2_fb;
    return 0;
}

/* A 16-bit surface of the window (a colour buffer, or the aux buffer as one) as the 2D engine's target. */
static void fill16(uint32_t off, int x, int y, int w, int h, uint32_t v)
{
    if (w <= 0 || h <= 0)
        return;
    tnt2_set_target(off, mg.pitch_px * 2, 16);
    tnt2_set_clip(0, 0, mg.rw, mg.rh);
    tnt2_fill(x, y, w, h, v);
}

static FxBool win_open(int w, int h, GrColorFormat_t color_format, GrOriginLocation_t origin_location,
                       int nColBuffers, int nAuxBuffers)
{
    rg_open o;
    rg_opened op;
    memset(&o, 0, sizeof o);
    o.width = w, o.height = h, o.color_bits = 16, o.depth_bits = nAuxBuffers > 0 ? 16 : 0, o.double_buffer = 1;
    o.pitch_px = w <= 1024 ? 1024 : 0;
    if (rg_tnt2_device.open(&o, &op) != 0) {
        mg_log(MG_LOG_ERROR, "tnt2: %dx%d: %s", w, h, rg_error());
        rg_tnt2_device.close();
        return FXFALSE;
    }
    mg_vram = tnt2_fb;
    tnt2_set_diag(log_line);                    /* (a trap's report: the registers, the state words, the ring) */
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
    mg.buf_off[0] = rg_tnt2.front_off;
    mg.buf_off[1] = rg_tnt2.back_off;
    mg.aux_off = mg.has_aux ? rg_tnt2.z_off : 0;
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
    sync_said("open");
    tnt2_set_start(mg.buf_off[mg.front]);
    mg_gamma_apply();
    mg.fogcol_valid = 0;
    mg_hooks_install();
    mg_line("MGL-WINOPEN %dx%d mode=tnt2 bpp=16 pitch=%d buffers=%d aux=%d z16 vram=%lu heap=%lu", w, h,
            mg.pitch_px, mg.nbuffers, mg.has_aux, (unsigned long)op.vram_bytes,
            (unsigned long)(mg.heap_end - mg.heap_off));
    return FXTRUE;
}

static void win_close(void)
{
    sync_said("close");
    tnt2_ramp_off();
    rg_tnt2_device.close();
    mg.open = 0;
    sys_unhook_faults();
    sys_unhook_exit();
    mg_line("MGL-WINCLOSE frames=%u sync_timeouts=%u traps=%u vsync_waits=%lu avg_kcycles=%lu failed=%lu under_100k=%lu",
            mg.frame, sync_timeouts, tnt2_traps(), vs_waits, vs_waits ? vs_cyc / vs_waits / 1000ul : 0ul, vs_fails, vs_short);
}

static void fault(void) { tnt2_ramp_off(); rg_tnt2_device.close(); }
static void exit_restore(void) { tnt2_sync(500000); tnt2_ramp_off(); rg_tnt2_device.close(); }
static void sync(void) { sync_said("sync"); }
static int busy(void) { return !tnt2_idle(); }
static int in_vblank(void) { return (sys_inb(0x3DA) & 0x08) != 0; }
static unsigned vcount(void) { return 0; }       /* (the NV5's raster position: not read yet) */

static void set_ramp(const uint8_t ramp[256]) { tnt2_set_ramp(ramp); }

/* ---- State ------------------------------------------------------------------------------------------------ */

static void validate(void)
{
    if (mg.dirty & MG_DIRTY_TARGET)
        tnt2_3d_target(mg_buffer_offset(mg.render_buffer), mg.aux_off, mg.pitch_px * 2, 16, mg.rw, mg.rh);
    if (mg.dirty & (MG_DIRTY_TARGET | MG_DIRTY_CLIP)) {
        int y0 = mg.st.clip_y0, y1 = mg.st.clip_y1;
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
            y0 = mg.height - mg.st.clip_y1;
            y1 = mg.height - mg.st.clip_y0;
        }
        tnt2_3d_clip(mg.st.clip_x0, y0, mg.st.clip_x1, y1);
    }
    mg.dirty = 0;
}

static void clear(int x0, int y0, int x1, int y1, GrColor_t color, FxU16 depth)
{
    if (mg.st.color_mask_rgb)
        fill16(mg_buffer_offset(mg.render_buffer), x0, y0, x1 - x0, y1 - y0, mg_argb_to_565(mg_color_to_argb(color)));
    if (mg.has_aux && mg.st.depth_mask && mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE)
        fill16(mg.aux_off, x0, y0, x1 - x0, y1 - y0, mg_depth_clear_value(depth));
}

static void swap(int swap_interval)
{
    int i;
    sync_said("swap");
    /* The start first, then the retrace: the TNT2 takes its display start at the retrace's start, so a start
     * written after the wait (the Matrox order) showed one frame late, and the game drew a whole frame into the
     * buffer on screen (Screamer Rally blanking before each frame, GTA's tearing on the P3). */
    tnt2_set_start(mg.buf_off[mg.back]);
    PROF_BACK(PROF_G_VSYNC);
    for (i = 0; i < swap_interval; i++) {
        uint32_t t0 = tsc_lo(), dt;
        if (tnt2_vsync_wait(50000) != 0)
            vs_fails++;
        dt = tsc_lo() - t0;
        vs_waits++, vs_cyc += dt;
        if (dt < 100000u)
            vs_short++;
    }
    PROF_BACK(PROF_G_SWAP);
}

static void present_front(void) { }             /* (no scaled fits) */

/* ---- Triangles -------------------------------------------------------------------------------------------- */

/* The Matrox back end's snap16: the Voodoo's truncation to 1/16 pixel toward zero (v * 16 is exact). */
static int32_t snap16(float v)
{
    int ok;
    int32_t r = mga_trunc_x16(v, &ok);
    double x;
    if (ok)
        return r;
    x = (double)(v * 16.0f);
    if (x > -2147483647.0 && x < 2147483647.0)
        return mga_itrunc(x);
    return (int32_t)(v * 16.0f);
}

static uint32_t blend_src(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return TNT2_BF_ZERO;
    case GR_BLEND_SRC_ALPHA: return TNT2_BF_SRC_ALPHA;
    case GR_BLEND_DST_COLOR: return TNT2_BF_DST_COLOR;
    case GR_BLEND_DST_ALPHA: return TNT2_BF_ONE;             /* no destination alpha: reads as 1 (as the Matrox) */
    case GR_BLEND_ONE: return TNT2_BF_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return TNT2_BF_INV_SRC_ALPHA;
    case GR_BLEND_ONE_MINUS_DST_COLOR: return TNT2_BF_INV_DST_COLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return TNT2_BF_ZERO;
    case GR_BLEND_ALPHA_SATURATE: return TNT2_BF_SRC_ALPHA_SAT;
    default: return TNT2_BF_ONE;
    }
}

static uint32_t blend_dst(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return TNT2_BF_ZERO;
    case GR_BLEND_SRC_ALPHA: return TNT2_BF_SRC_ALPHA;
    case GR_BLEND_SRC_COLOR: return TNT2_BF_SRC_COLOR;
    case GR_BLEND_DST_ALPHA: return TNT2_BF_ONE;
    case GR_BLEND_ONE: return TNT2_BF_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return TNT2_BF_INV_SRC_ALPHA;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return TNT2_BF_INV_SRC_COLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return TNT2_BF_ZERO;
    default: return TNT2_BF_ZERO;
    }
}

/* Glide's compare (GR_CMP_NEVER 0 .. ALWAYS 7) as Direct3D's (1 .. 8); W-buffering keeps a linear 1/w, nearer
 * larger, so its depth compares are reversed (as the Matrox back end's zmode_for). */
static uint32_t cmp_of(GrCmpFnc_t f, int reversed)
{
    if (reversed)
        switch (f) {
        case GR_CMP_LESS: f = GR_CMP_GREATER; break;
        case GR_CMP_GREATER: f = GR_CMP_LESS; break;
        case GR_CMP_LEQUAL: f = GR_CMP_GEQUAL; break;
        case GR_CMP_GEQUAL: f = GR_CMP_LEQUAL; break;
        default: break;
        }
    return (uint32_t)f + 1u;
}

static int uses_texture(void)
{
    const mg_state *s = &mg.st;
    int cf = s->cc_factor & 7, af = s->ac_factor & 7;
    return s->cc_other == GR_COMBINE_OTHER_TEXTURE || s->ac_other == GR_COMBINE_OTHER_TEXTURE ||
           cf == GR_COMBINE_FACTOR_TEXTURE_ALPHA || cf == GR_COMBINE_FACTOR_TEXTURE_RGB ||
           af == GR_COMBINE_FACTOR_TEXTURE_ALPHA || af == GR_COMBINE_FACTOR_TEXTURE_RGB;
}

static uint32_t byte_of(float f)
{
    int32_t i = f <= 0.0f ? 0 : f >= 255.0f ? 255 : mga_irint(f);
    return (uint32_t)i;
}

static float clamp255(float f) { return f < 0 ? 0 : f > 255.0f ? 255.0f : f; }

static void vertex(tnt2_vertex *o, const GrVertex *v, int32_t sx, int32_t sy, int wmode, int fog, int textured)
{
    double z;
    float rgb[3], a;
    if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
        sy = mg.height * 16 - sy;
    /* x a 64th of a pixel left: an edge exactly on a pixel's centre then leaves it as the Voodoo does (conformance
     * t04 on the P3: 237 pixels of its fill rule differed, then one; any shift of x up to a 16th does, y's none). */
    o->sx = (float)sx * (1.0f / 16.0f) - 1.0f / 64.0f;
    o->sy = (float)sy * (1.0f / 16.0f);
    if (wmode)
        z = mg_wdepth_from_oow(v->oow, 16);
    else {
        z = v->ooz + mg.st.depth_bias;
        if (z < 0) z = 0;
        if (z > 65535.0) z = 65535.0;
    }
    z = (z + 0.5) / 65535.0;                    /* (the chip's Z16 is SZ * 65535 truncated: z again) */
    o->sz = (float)(z > 1.0 ? 1.0 : z);
    {
        /* (the chip's 1/w is the texture's perspective alone: the TMU's when it has its own, the STW hint) */
        float q = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v->tmuvtx[0].oow : v->oow;
        o->rhw = q > 0 ? q : 1e-6f;
    }
    if (textured) {
        rgb[0] = clamp255(v->r), rgb[1] = clamp255(v->g), rgb[2] = clamp255(v->b), a = clamp255(v->a);
    } else {
        mg_cc_vertex(v, 0, rgb);
        a = mg_ac_vertex(v, 0);
    }
    o->color = byte_of(a) << 24 | byte_of(rgb[0]) << 16 | byte_of(rgb[1]) << 8 | byte_of(rgb[2]);
    o->specular = (fog ? byte_of((float)(255.0 - mg_fog_vertex(v))) : 255u) << 24;
    o->tu = o->tv = 0;
    o->tu1 = o->tv1 = 0;
}

/* ---- Textured triangles: the multitexture object's two stages (G5) ----------------------------------------
 * Stage 0 is TMU0's texture combine (its other, a second TMU's output, is zero); stage 1 the colour and alpha
 * combines, out = in0*in1 + in2*in3 over inputs (source << 2 | alpha << 1 | inverse): the iterated values in the
 * vertex colour (PRIMARY), the constant colour in COMBINE_FACTOR (CONSTANT), the texture through stage 0
 * (PREVIOUS) or itself (TEXTURE0) for its alpha. The forms with a subtraction have no exact form (the combiner
 * adds): drawn as the nearest that adds, said once. */
enum { CS_ZERO = 1, CS_CONST = 2, CS_PRIM = 3, CS_PREV = 4, CS_TEX0 = 5 };
#define CIN(src, alpha, inv) ((uint32_t)(src) << 2 | ((alpha) ? 2u : 0u) | ((inv) ? 1u : 0u))
#define C_ONE  CIN(CS_ZERO, 0, 1)
#define C_NONE CIN(CS_ZERO, 0, 0)

static uint32_t cw(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return a | b << 8 | c << 16 | d << 24 | 1u << 29; }

static int approx_said;
static void approx(const char *what)
{
    if (!approx_said) {
        approx_said = 1;
        mg_log(MG_LOG_WARN, "tnt2: combine approximated (%s)", what);
    }
}

static int src_local(void)
{
    if (mg.st.cc_local == GR_COMBINE_LOCAL_DEPTH)
        approx("depth as the local colour");
    return mg.st.cc_local == GR_COMBINE_LOCAL_CONSTANT ? CS_CONST : CS_PRIM;
}
static int src_other(void)
{
    return mg.st.cc_other == GR_COMBINE_OTHER_TEXTURE ? CS_PREV
         : mg.st.cc_other == GR_COMBINE_OTHER_CONSTANT ? CS_CONST : CS_PRIM;
}
static int src_alocal(void)
{
    if (mg.st.ac_local == GR_COMBINE_LOCAL_DEPTH)
        approx("depth as the local alpha");
    return mg.st.ac_local == GR_COMBINE_LOCAL_CONSTANT ? CS_CONST : CS_PRIM;
}
static int src_aother(void)
{
    return mg.st.ac_other == GR_COMBINE_OTHER_TEXTURE ? CS_PREV
         : mg.st.ac_other == GR_COMBINE_OTHER_CONSTANT ? CS_CONST : CS_PRIM;
}

/* A combine factor as an input; alpha: the alpha combine's (its inputs are alphas already). */
static uint32_t factor_in(GrCombineFactor_t f, int alpha)
{
    int inv = (f & 8) != 0;
    switch (f & 7) {
    case GR_COMBINE_FACTOR_LOCAL: return alpha ? CIN(src_alocal(), 0, inv) : CIN(src_local(), 0, inv);
    case GR_COMBINE_FACTOR_OTHER_ALPHA: return CIN(src_aother(), !alpha, inv);
    case GR_COMBINE_FACTOR_LOCAL_ALPHA: return CIN(src_alocal(), !alpha, inv);
    case GR_COMBINE_FACTOR_TEXTURE_ALPHA: return CIN(CS_TEX0, !alpha, inv);
    case GR_COMBINE_FACTOR_TEXTURE_RGB: return CIN(CS_TEX0, 0, inv);
    default: return CIN(CS_ZERO, 0, inv);               /* (ZERO; ONE as its inverse) */
    }
}

/* A combine function over other O, local L and local alpha aL with factor F, as one stage's word. Inverted (1 - the
 * result), the forms that can be are rewritten over inverted inputs: 1 - F*O = F*(1-O) + (1-F); the lerp's
 * F*(1-O) + (1-F)*(1-L); 1 - (1-F)*L = (1-L)*(1-F) + F; 1 - L; zero's one. (An input's inverse is its byte ^ 1.) */
static uint32_t fn_word(GrCombineFunction_t fn, uint32_t O, uint32_t L, uint32_t aL, uint32_t F, FxBool invert)
{
    if (invert)
        switch (fn) {
        case GR_COMBINE_FUNCTION_ZERO: return cw(C_ONE, C_ONE, C_NONE, C_NONE);
        case GR_COMBINE_FUNCTION_LOCAL: return cw(L ^ 1u, C_ONE, C_NONE, C_NONE);
        case GR_COMBINE_FUNCTION_LOCAL_ALPHA: return cw(aL ^ 1u, C_ONE, C_NONE, C_NONE);
        case GR_COMBINE_FUNCTION_SCALE_OTHER: return cw(O ^ 1u, F, F ^ 1u, C_ONE);
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: return cw(O ^ 1u, F, L ^ 1u, F ^ 1u);
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL: return cw(L ^ 1u, F ^ 1u, F, C_ONE);
        default:
            approx("an inverted combine that adds");
            break;
        }
    switch (fn) {
    case GR_COMBINE_FUNCTION_ZERO: return cw(C_NONE, C_ONE, C_NONE, C_NONE);
    case GR_COMBINE_FUNCTION_LOCAL: return cw(L, C_ONE, C_NONE, C_NONE);
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA: return cw(aL, C_ONE, C_NONE, C_NONE);
    case GR_COMBINE_FUNCTION_SCALE_OTHER: return cw(O, F, C_NONE, C_NONE);
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: return cw(O, F, L, C_ONE);
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA: return cw(O, F, aL, C_ONE);
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: return cw(O, F, L, F ^ 1u);   /* lerp: exact */
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL: return cw(L, F ^ 1u, C_NONE, C_NONE);
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
        approx("f * (other - local)");
        return cw(O, F, C_NONE, C_NONE);
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        approx("f * (other - local) + local alpha");
        return cw(O, F, aL, F ^ 1u);
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        approx("local alpha - f * local");
        return cw(aL, F ^ 1u, C_NONE, C_NONE);
    default: return cw(L, C_ONE, C_NONE, C_NONE);
    }
}

/* TMU0's texture combine as stage 0 (T the texel; a second TMU's output, its other, is zero). */
static void stage0_words(uint32_t *aw, uint32_t *cwd)
{
    const tex_unit *u = &tmu0;
    GrCombineFunction_t rf = u->combine_set ? u->rgb_func : GR_COMBINE_FUNCTION_LOCAL;
    GrCombineFunction_t af = u->combine_set ? u->alpha_func : GR_COMBINE_FUNCTION_LOCAL;
    uint32_t T = CIN(CS_TEX0, 0, 0), Ta = CIN(CS_TEX0, 1, 0);
    uint32_t rF = (u->rgb_factor & 7) == GR_COMBINE_FACTOR_LOCAL ? CIN(CS_TEX0, 0, (u->rgb_factor & 8) != 0)
                : (u->rgb_factor & 7) == GR_COMBINE_FACTOR_LOCAL_ALPHA ? CIN(CS_TEX0, 1, (u->rgb_factor & 8) != 0)
                : CIN(CS_ZERO, 0, (u->rgb_factor & 8) != 0);
    uint32_t aF = ((u->alpha_factor & 7) == GR_COMBINE_FACTOR_LOCAL || (u->alpha_factor & 7) == GR_COMBINE_FACTOR_LOCAL_ALPHA)
                ? CIN(CS_TEX0, 0, (u->alpha_factor & 8) != 0) : CIN(CS_ZERO, 0, (u->alpha_factor & 8) != 0);
    /* (other zero: the forms reduce to the texel, its alpha, a fraction of either, or zero) */
    switch (rf) {
    case GR_COMBINE_FUNCTION_LOCAL: case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: *cwd = cw(T, C_ONE, C_NONE, C_NONE); break;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA: case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        *cwd = cw(Ta, C_ONE, C_NONE, C_NONE); break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
        *cwd = cw(T, rF ^ 1u, C_NONE, C_NONE); break;
    default: *cwd = cw(C_NONE, C_ONE, C_NONE, C_NONE); break;
    }
    switch (af) {
    case GR_COMBINE_FUNCTION_LOCAL: case GR_COMBINE_FUNCTION_LOCAL_ALPHA: case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        *aw = cw(T, C_ONE, C_NONE, C_NONE); break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
        *aw = cw(T, aF ^ 1u, C_NONE, C_NONE); break;
    default: *aw = cw(C_NONE, C_ONE, C_NONE, C_NONE); break;
    }
    if (u->combine_set && (u->rgb_invert || u->alpha_invert))
        approx("an inverted texture combine");
}

static int log2i(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

/* A texture without alpha (565) is opaque to the Voodoo; the combiner's reading of its alpha isn't (conformance t13's
 * blend of the constant and iterated colours by texture alpha drew the iterated alone): every input that reads the
 * texture's alpha becomes one (an alpha stage's every TEXTURE0 input; a colour stage's with the alpha bit). */
static uint32_t opaque_alpha(uint32_t w, int alpha_stage)
{
    int k;
    for (k = 0; k < 4; k++) {
        uint32_t in = (w >> (8 * k)) & (k == 3 ? 0x1Fu : 0xFFu);
        if ((in >> 2) == CS_TEX0 && (alpha_stage || (in & 2u))) {
            uint32_t one = CIN(CS_ZERO, 0, !(in & 1u));
            w = (w & ~((k == 3 ? 0x1Fu : 0xFFu) << (8 * k))) | one << (8 * k);
        }
    }
    return w;
}

static uint32_t tnt2_fmt(int hwfmt)
{
    return hwfmt == HW_TW15 ? TNT2_F_A1R5G5B5 : hwfmt == HW_TW12 ? TNT2_F_A4R4G4B4 : TNT2_F_R5G6B5;
}

/* The texel coordinates of a vertex (s, t over the TMU's q). */
static void st_of(const GrVertex *v, double *s, double *t)
{
    double q = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v->tmuvtx[0].oow : v->oow;
    if (q == 0)
        q = 1e-9;
    *s = v->tmuvtx[0].sow / q;
    *t = v->tmuvtx[0].tow / q;
}

/* The Voodoo's level (the Matrox back end's model): lambda = log2 of the larger of the texel coordinates' gradients
 * along x and along y, at the largest level, in quarter levels with the LOD bias, the level floor(lambda) clamped to
 * the chain (the large one with mipmapping off). Evaluated at a point of the triangle's planes. */
typedef struct { double s0, sx, sy, t0, tx, ty, q0, qx, qy, x0, y0; int valid; } lod_planes;

static void lod_setup(lod_planes *L, const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    const GrVertex *v[3];
    double x[3], y[3], S[3], T[3], Q[3], area, k = (double)(256 >> tmu0.large) / 256.0;
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

static GrLOD_t level_at(const lod_planes *L, double px, double py)
{
    double dx = px - L->x0, dy = py - L->y0, S, T, Q, u, v, gx, gy, g;
    int q, l;
    if (tmu0.mipmap == GR_MIPMAP_DISABLE || !L->valid)
        return tmu0.large;
    S = L->s0 + L->sx * dx + L->sy * dy, T = L->t0 + L->tx * dx + L->ty * dy, Q = L->q0 + L->qx * dx + L->qy * dy;
    if (Q <= 1e-12)
        return tmu0.large;
    u = S / Q, v = T / Q;
    gx = (L->sx - u * L->qx) * (L->sx - u * L->qx) + (L->tx - v * L->qx) * (L->tx - v * L->qx);
    gy = (L->sy - u * L->qy) * (L->sy - u * L->qy) + (L->ty - v * L->qy) * (L->ty - v * L->qy);
    g = (gx > gy ? gx : gy) / (Q * Q);
    if (!(g > 0))
        return tmu0.large;
    /* quarter levels: floor(4 * log2(sqrt g)) = floor(2 * log2 g), and the bias's */
    q = mga_ifloor(2.0 * mg_ln(g) / 0.69314718055994531) +
        (int)(tmu0.lod_bias * 4.0f + (tmu0.lod_bias >= 0 ? 0.5f : -0.5f));
    l = (q >= 0 ? q / 4 : -((-q + 3) / 4)) + tmu0.large;
    return (GrLOD_t)(l < tmu0.large ? tmu0.large : l > tmu0.small ? tmu0.small : l);
}

static void draw_textured(const GrVertex *a, const GrVertex *b, const GrVertex *c, tnt2_vertex *v, uint32_t blend,
                          uint32_t control, uint32_t fogc, GrLOD_t lod)
{
    uint32_t w[16], off, format, filter;
    tex_level_hw hw;
    tex_variant var;
    int n, bound, chroma = mg.st.chroma_mode == GR_CHROMAKEY_ENABLE;
    double s[3], t[3], ks = 0, kt = 0, ms = 0, mt = 0;
    memset(&var, 0, sizeof var);
    if (chroma) {
        var.kind = TV_CHROMA;
        var.chroma = mg_color_to_argb(mg.st.chroma_value) & 0xFFFFFFu;
    }
    bound = uses_texture() && tmu0.has_source && tex_bind_level(lod, &var, &hw) == 0;
    if (bound) {
        uint32_t wrap_s = tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP ? 3u : 1u, wrap_t = tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP ? 3u : 1u;
        uint32_t mag = tmu0.magf == GR_TEXTUREFILTER_BILINEAR ? 2u : 1u, min = tmu0.minf == GR_TEXTUREFILTER_BILINEAR ? 2u : 1u;
        off = hw.org;
        format = TNT2_F_DMA_A | TNT2_F_ORIGIN | tnt2_fmt(hw.hwfmt) | 1u << 12 | (uint32_t)hw.w_log2 << 16 |
                 (uint32_t)hw.h_log2 << 20 | wrap_s << 24 | wrap_t << 28;
        filter = mag << 28 | min << 24 | 0x1010u;
        ks = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.w_log2));
        kt = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.h_log2));
    } else {
        off = tnt2_tex_dummy_off(), format = TNT2_DUMMY_FORMAT, filter = TNT2_DUMMY_FILTER;
    }
    w[0] = w[1] = off, w[2] = w[3] = format, w[4] = w[5] = filter;
    stage0_words(&w[6], &w[7]);
    {
        uint32_t L = CIN(src_local(), 0, 0), O = CIN(src_other(), 0, 0), aLc = CIN(src_alocal(), 1, 0);
        uint32_t La = CIN(src_alocal(), 0, 0), Oa = CIN(src_aother(), 0, 0);
        w[9] = fn_word(mg.st.cc_func, O, L, aLc, factor_in(mg.st.cc_factor, 0), mg.st.cc_invert);
        w[8] = fn_word(mg.st.ac_func, Oa, La, La, factor_in(mg.st.ac_factor, 1), mg.st.ac_invert);
        if (chroma && ((w[8] >> 8) & 0xFF) == C_ONE && ((w[8] >> 16) & 0xFF) == C_NONE)
            w[8] = (w[8] & ~0xFF00u) | CIN(CS_TEX0, 0, 0) << 8;   /* (keyed texels' alpha 0 into the alpha) */
    }
    if (mg_fog_source() != GR_FOG_DISABLE && (mg.st.fog_mode & GR_FOG_MULT2))
        w[9] = cw(C_NONE, C_ONE, C_NONE, C_NONE);       /* (MULT2: the fog term alone, f * the fog colour) */
    if (bound && hw.hwfmt == HW_TW16) {
        w[6] = opaque_alpha(w[6], 1), w[7] = opaque_alpha(w[7], 0);
        w[8] = opaque_alpha(w[8], 1), w[9] = opaque_alpha(w[9], 0);
    }
    w[10] = mg_color_to_argb(mg.st.constant_color);
    w[11] = blend & ~0xFu;                              /* (0x55's BLEND has no texture map field) */
    w[12] = control | 0x3C000000u;                      /* (every channel written) */
    if (chroma && !(control & TNT2_C_ALPHA_ON))
        w[12] = (w[12] & ~0xFFFu) | TNT2_C_ALPHA_ON | TNT2_C_ALPHA_FUNC(TNT2_CMP_GREATER) | TNT2_C_ALPHA_REF(0);
    w[13] = 0, w[14] = 0x111, w[15] = fogc;
    tnt2_3d_mt_state(w);
    /* Coordinates: s and t (0..256 over the larger side) in the bound level's texture, whole periods taken off
     * when it repeats (the chip interpolates in floats). */
    st_of(a, &s[0], &t[0]); st_of(b, &s[1], &t[1]); st_of(c, &s[2], &t[2]);
    if (!tmu0.combine_set)
        ks = kt = 0;                    /* (retail Glide: no s, t until a texture combine is set, as the Matrox's) */
    for (n = 0; n < 3; n++)
        s[n] *= ks, t[n] *= kt;
    if (bound && tmu0.clamp_s != GR_TEXTURECLAMP_CLAMP) {
        ms = s[0] < s[1] ? s[0] : s[1];
        ms = s[2] < ms ? s[2] : ms;
        ms = ms > -1e9 && ms < 1e9 ? mga_floor(ms) : 0;
    }
    if (bound && tmu0.clamp_t != GR_TEXTURECLAMP_CLAMP) {
        mt = t[0] < t[1] ? t[0] : t[1];
        mt = t[2] < mt ? t[2] : mt;
        mt = mt > -1e9 && mt < 1e9 ? mga_floor(mt) : 0;
    }
    for (n = 0; n < 3; n++) {
        v[n].tu = v[n].tu1 = (float)(s[n] - ms);
        v[n].tv = v[n].tv1 = (float)(t[n] - mt);
    }
    tnt2_tri_mt(&v[0], &v[1], &v[2]);
}

/* What the Voodoo works out per pixel and the chip per vertex or per triangle: table fog (from 1/w) and the texture's
 * level. A triangle whose vertices' fog factors differ by more than a sixteenth, or whose vertices want different
 * levels, is split in four in screen space, up to three times: everything else the Voodoo iterates linearly in
 * screen space (colours, z, 1/w, s/w, t/w) is exact at the midpoints, where the factor and the level are worked out
 * again (as the Matrox back end's 8-row bands; DOS-GL's fog_split). Each piece then takes its centroid's level. */
static void mid(GrVertex *o, const GrVertex *a, const GrVertex *b)
{
    const float *p = (const float *)a, *q = (const float *)b;
    float *r = (float *)o;
    unsigned i;
    for (i = 0; i < sizeof *o / sizeof(float); i++)
        r[i] = (p[i] + q[i]) * 0.5f;
}

static void tri_one(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod);

static void split(const GrVertex *a, const GrVertex *b, const GrVertex *c, int depth, int fogged, int mip)
{
    GrVertex ab, bc, ca;
    lod_planes L;
    GrLOD_t la = tmu0.large;
    int cut = 0;
    if (mip) {
        lod_setup(&L, a, b, c);
        la = level_at(&L, a->x, a->y);
        cut = level_at(&L, b->x, b->y) != la || level_at(&L, c->x, c->y) != la;
    }
    if (fogged && !cut) {
        double fa = mg_fog_vertex(a), fb = mg_fog_vertex(b), fc = mg_fog_vertex(c), lo, hi;
        lo = fa < fb ? fa : fb, lo = fc < lo ? fc : lo;
        hi = fa > fb ? fa : fb, hi = fc > hi ? fc : hi;
        cut = hi - lo > 16.0;
    }
    if (!cut || depth >= 5) {
        if (mip && cut)
            la = level_at(&L, (a->x + b->x + c->x) / 3.0, (a->y + b->y + c->y) / 3.0);
        tri_one(a, b, c, la);
        return;
    }
    mid(&ab, a, b), mid(&bc, b, c), mid(&ca, c, a);
    split(a, &ab, &ca, depth + 1, fogged, mip);
    split(&ab, b, &bc, depth + 1, fogged, mip);
    split(&ca, &bc, c, depth + 1, fogged, mip);
    split(&ab, &bc, &ca, depth + 1, fogged, mip);
}

void tnt2_be_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    int fogged, mip;
    if (!mg.open)
        return;
    fogged = mg_fog_source() == GR_FOG_WITH_TABLE;
    mip = uses_texture() && tmu0.has_source && tmu0.mipmap != GR_MIPMAP_DISABLE && tmu0.small > tmu0.large;
    split(a, b, c, 0, fogged, mip);
}

static void tri_one(const GrVertex *a, const GrVertex *b, const GrVertex *c, GrLOD_t lod)
{
    int wmode = mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER || mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux, fog;
    int32_t ax, ay, bx, by, cx, cy;
    uint32_t blend, control, fogc;
    tnt2_vertex v[3];
    if (!mg.open)
        return;
    mg_stats.tri_processed++;
    if (depth && mg.st.depth_func == GR_CMP_NEVER)
        return;
    if (mg.st.alpha_test_func == GR_CMP_NEVER)
        return;
    ax = snap16(a->x); ay = snap16(a->y);
    bx = snap16(b->x); by = snap16(b->y);
    cx = snap16(c->x); cy = snap16(c->y);
    if (mg.st.cull != GR_CULL_DISABLE) {
        /* (the Matrox back end's: the game's orientation, before any origin flip) */
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
    if (mg.st.chroma_mode == GR_CHROMAKEY_ENABLE && !uses_texture()) {
        /* (untextured: dropped when every vertex's colour is the key, as the Matrox back end's alpha_setup) */
        uint32_t key = mg_color_to_argb(mg.st.chroma_value) & 0xFFFFFFu, k[3];
        const GrVertex *p[3];
        float rgb[3];
        int n;
        p[0] = a, p[1] = b, p[2] = c;
        for (n = 0; n < 3; n++) {
            mg_cc_vertex(p[n], 0, rgb);
            k[n] = byte_of(rgb[0]) << 16 | byte_of(rgb[1]) << 8 | byte_of(rgb[2]);
        }
        if (k[0] == key && k[1] == key && k[2] == key)
            return;
    }
    mg_validate();
    fog = mg_fog_source() != GR_FOG_DISABLE;
    blend = TNT2_B_MSB | TNT2_B_PERSP | TNT2_B_GOURAUD | TNT2_TB_MODULATEALPHA;
    if (fog)
        blend |= TNT2_B_FOG;
    if (!mg.st.color_mask_rgb)
        blend |= TNT2_B_BLEND | TNT2_B_SRC(TNT2_BF_ZERO) | TNT2_B_DST(TNT2_BF_ONE);   /* (depth alone) */
    else if (mg.st.blend_src == GR_BLEND_ONE && mg.st.blend_dst == GR_BLEND_ZERO)
        blend |= TNT2_B_SRC(TNT2_BF_ONE) | TNT2_B_DST(TNT2_BF_ZERO);
    else
        blend |= TNT2_B_BLEND | TNT2_B_SRC(blend_src(mg.st.blend_src)) | TNT2_B_DST(blend_dst(mg.st.blend_dst));
    /* (W-buffering: the linear 1/w the Matrox cards keep, in Z16: far away coarser than the Voodoo's float code,
     * conformance t07 6.9% off. The chip's float Z format, alone or with Z_PERSPECTIVE, was worse: 21-56%.) */
    control = TNT2_C_Z_FIXED | TNT2_C_CORNER | TNT2_C_CULL_NONE;
    if (mg.st.alpha_test_func != GR_CMP_ALWAYS)
        control |= TNT2_C_ALPHA_ON | TNT2_C_ALPHA_FUNC(cmp_of(mg.st.alpha_test_func, 0)) |
                   TNT2_C_ALPHA_REF(mg.st.alpha_test_ref);
    else
        control |= TNT2_C_ALPHA_FUNC(TNT2_CMP_ALWAYS);
    if (depth) {
        control |= TNT2_C_Z_ON | TNT2_C_Z_FUNC(cmp_of(mg.st.depth_func, wmode));
        if (mg.st.depth_mask)
            control |= TNT2_C_Z_WRITE;
    } else
        control |= TNT2_C_Z_FUNC(TNT2_CMP_ALWAYS);
    if (mg.st.dither != GR_DITHER_DISABLE)
        control |= TNT2_C_DITHER;
    fogc = 0xFF000000u | ((mg.st.fog_mode & GR_FOG_ADD2) ? 0u : (mg_color_to_argb(mg.st.fog_color) & 0xFFFFFFu));
    vertex(&v[0], a, ax, ay, wmode, fog, 1);
    vertex(&v[1], b, bx, by, wmode, fog, 1);
    vertex(&v[2], c, cx, cy, wmode, fog, 1);
    /* (every triangle through the combiner: per pixel, so a product of two iterated values (a lerp by iterated
     * alpha) is exact too, where evaluating at the vertices is not; untextured, the white texel) */
    draw_textured(a, b, c, v, blend, control, fogc, lod);
    mg_stats.tris++;
    mg_stats.tri_drawn++;
}

/* A level the texture manager converted: swizzled into place (the chip's layout), then the triangle state sent
 * again (the texture cache keeps texels it read: docs/tnt2.md). */
static void tex_write(uint32_t org, const uint16_t *texels, int hw_w, int hw_h)
{
    const void *lv[1];
    lv[0] = texels;
    tnt2_tex_load(org, 2, log2i(hw_w), log2i(hw_h), 1, lv);
    tnt2_3d_tex_written();
}

const mg_backend mg_tnt2_backend = {
    "tnt2", probe, win_open, win_close, fault, exit_restore, sync, busy, in_vblank, vcount, set_ramp, validate,
    clear, swap, present_front, tnt2_be_tri, tex_write, 128, 0, 0, 0, 0
};
