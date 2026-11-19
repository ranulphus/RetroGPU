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
    tnt2_sync(500000);
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
    tnt2_sync(500000);
    rg_tnt2_device.close();
    mg.open = 0;
    sys_unhook_faults();
    sys_unhook_exit();
    mg_line("MGL-WINCLOSE frames=%u", mg.frame);
}

static void fault(void) { rg_tnt2_device.close(); }
static void exit_restore(void) { tnt2_sync(500000); rg_tnt2_device.close(); }
static void sync(void) { tnt2_sync(500000); }
static int busy(void) { return !tnt2_idle(); }
static int in_vblank(void) { return (sys_inb(0x3DA) & 0x08) != 0; }
static unsigned vcount(void) { return 0; }       /* (the NV5's raster position: not read yet) */

static void set_ramp(const uint8_t ramp[256])
{
    (void)ramp;
    warn_once(1, "gamma not applied (the RAMDAC's palette in direct colour: not done yet)");
}

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
    tnt2_sync(500000);
    PROF_BACK(PROF_G_VSYNC);
    for (i = 0; i < swap_interval; i++)
        tnt2_vsync_wait(50000);
    PROF_BACK(PROF_G_SWAP);
    tnt2_set_start(mg.buf_off[mg.back]);
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

static void vertex(tnt2_vertex *o, const GrVertex *v, int32_t sx, int32_t sy, int wmode, int fog)
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
    o->rhw = v->oow > 0 ? v->oow : 1e-6f;
    mg_cc_vertex(v, 0, rgb);
    a = mg_ac_vertex(v, 0);
    o->color = byte_of(a) << 24 | byte_of(rgb[0]) << 16 | byte_of(rgb[1]) << 8 | byte_of(rgb[2]);
    o->specular = (fog ? byte_of((float)(255.0 - mg_fog_vertex(v))) : 255u) << 24;
    o->tu = o->tv = 0;
    o->tu1 = o->tv1 = 0;
}

void tnt2_be_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
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
    if (uses_texture())
        warn_once(2, "textures not drawn yet (a white texel)");
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
    tnt2_3d_state(tnt2_tex_dummy_off(), TNT2_DUMMY_FORMAT, TNT2_DUMMY_FILTER, blend, control, fogc);
    vertex(&v[0], a, ax, ay, wmode, fog);
    vertex(&v[1], b, bx, by, wmode, fog);
    vertex(&v[2], c, cx, cy, wmode, fog);
    if (fog && (mg.st.fog_mode & GR_FOG_MULT2)) {
        int n;
        for (n = 0; n < 3; n++)
            v[n].color &= 0xFF000000u;          /* (MULT2: the fog term alone, f * the fog colour) */
    }
    tnt2_tri(&v[0], &v[1], &v[2]);
    mg_stats.tris++;
    mg_stats.tri_drawn++;
}

const mg_backend mg_tnt2_backend = {
    "tnt2", probe, win_open, win_close, fault, exit_restore, sync, busy, in_vblank, vcount, set_ramp, validate,
    clear, swap, present_front, tnt2_be_tri
};
