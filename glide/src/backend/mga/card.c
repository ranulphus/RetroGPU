/* card.c - the Matrox back end's card: finding it, the window (mode, buffers, heap), clears, swaps, the scaled
 * present, gamma, the raster state and the fog colour register. Moved from glide/src/glide/{sst,buffer,state,fog}.c
 * behind backend.h without changing a register write (glide/tools/conform/regcrc.py). */
#include "glide/mg.h"
#include "backend/backend.h"
#include "mga/warp.h"
#include "mga/sys.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "tex/texmgr.h"
#include <string.h>

uint32_t mga_probe_vram(void);
void mga_be_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c);   /* (draw.c) */

static int probe(void)
{
    if (mga.family != MGA_FAMILY_NONE) {
        mg_vram = mga_fb;
        return 0;
    }
    if (mga_find(&mga) < 0) {
        mg_log(MG_LOG_ERROR, "no supported Matrox card found");
        return -1;
    }
    mga.emulated = mga_detect_emulator();
    mg_line("MGL-CARD %s id=%04x rev=%02x family=%s emulated=%d fb=%08x mmio=%08x",
            mga.name, mga.device_id, mga.revision, mga_family_name(mga.family), mga.emulated,
            mga.fb_phys, mga.mmio_phys);
    if (mga.family == MGA_FAMILY_G200E) {
        mg_log(MG_LOG_ERROR, "server G200e variants are not supported");
        return -1;
    }
    if (mga_map(&mga) < 0) {
        mg_log(MG_LOG_ERROR, "cannot map apertures");
        return -1;
    }
    mg_vram = mga_fb;
    return 0;
}

/* Clear the rw x rh drawable area of a buffer at pitch (and, for the
 * display buffers, the whole mode). */
static void clear_buffer(uint32_t off, int pitch, int w, int h)
{
    mga_target t;
    t.color_off = off; t.z_off = mg.aux_off; t.pitch_px = pitch; t.bpp = mg.bpp; t.zbits = mg.zbits;
    engine_set_target(&t);
    engine_set_clip(0, 0, w, h);
    engine_fill(0, 0, w, h, 0);
}

static FxBool win_open(int w, int h, GrColorFormat_t color_format, GrOriginLocation_t origin_location,
                       int nColBuffers, int nAuxBuffers)
{
    int pitch = 0, want, i, direct, bpp_bytes;
    unsigned flags;
    uint32_t off, bytes, zbytes32;
    mg.bpp = mg_config.bpp == 32 ? 32 : 16;
    /* How the game's size is shown (hal.h, vbe_plan_mode): natively,
     * zoomed, scaled by the engine into a BIOS mode, or top-left. */
    flags = (mg_config.zoom ? MGA_PLAN_ZOOM : 0) | (mg_config.scale == 0 ? MGA_PLAN_TOPLEFT : 0) |
            (mg_config.scale == 2 ? MGA_PLAN_FORCE : 0);
    {
        /* Resolution override: the game keeps its size, the card renders
         * at res_w x res_h and everything in between is scaled. */
        int hw = w, hh = h, ok;
        mg.scaled = 0;
        mg.sx = mg.sy = 1.0;
        if (mg_config.res_w > 0 && mg_config.res_h > 0 && (mg_config.res_w != w || mg_config.res_h != h)) {
            hw = mg_config.res_w;
            hh = mg_config.res_h;
        }
        ok = vbe_plan_mode(hw, hh, mg.bpp, flags, &mg.plan) == 0;
        if (mg.bpp == 32 && (!ok || (mg.plan.fit != MGA_FIT_NATIVE && mg.plan.fit != MGA_FIT_ZOOM &&
                                     mg.plan.fit != MGA_FIT_TOPLEFT))) {
            /* The engine scales 16-bit pictures only. */
            mg_log(MG_LOG_WARN, "no 32-bit mode shows %dx%d unscaled: using 16-bit", hw, hh);
            mg.bpp = 16;
            ok = vbe_plan_mode(hw, hh, 16, flags, &mg.plan) == 0;
        }
        if (!ok) {
            mg_log(MG_LOG_ERROR, "no VBE mode can show %dx%d", hw, hh);
            return FXFALSE;
        }
        if (hw != w || hh != h) {
            mg.scaled = 1;
            mg.sx = (double)hw / w;
            mg.sy = (double)hh / h;
        }
    }
    mg.mode = mg.plan.disp;
    direct = mg.plan.fit == MGA_FIT_NATIVE || mg.plan.fit == MGA_FIT_ZOOM || mg.plan.fit == MGA_FIT_TOPLEFT;
    mg.present_scaled = !direct;
    /* Display pitch 1024 while the mode fits it (the Voodoo's 2048-byte LFB
     * stride), else the width rounded up to 32 pixels. */
    want = mg.mode.width <= 1024 ? 1024 : (mg.mode.width + 31) & ~31;
    if (vbe_set_mode(&mg.mode, want, &pitch) < 0 || pitch < mg.mode.width || (pitch & 31)) {
        mg_log(MG_LOG_ERROR, "VBE mode %03x failed (pitch %d)", mg.mode.mode, pitch);
        return FXFALSE;
    }
    vbe_set_zoom(mg.plan.zoom);
    if (!mga.vram_bytes)
        mga.vram_bytes = mga_probe_vram();
    mg.width = w;
    mg.height = h;
    mg.nbuffers = nColBuffers < 2 ? 2 : (nColBuffers > MG_MAX_BUFFERS ? MG_MAX_BUFFERS : nColBuffers);
    mg.has_aux = nAuxBuffers > 0;
    mg.color_format = color_format;
    mg.st.origin = origin_location == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
    bpp_bytes = mg.bpp / 8;
    off = 0;
    if (direct) {
        /* The game draws into the display buffers: whole screens of the
         * mode so page flips land on line boundaries (a zoomed mode shows
         * half as many rows, doubled). */
        mg.pitch_px = pitch;
        mg.rw = mg.plan.fit == MGA_FIT_ZOOM ? mg.plan.w : mg.mode.width;
        mg.rh = mg.plan.fit == MGA_FIT_ZOOM ? mg.plan.h : mg.mode.height;
    } else {
        /* Two display buffers, then render buffers at a power-of-two pitch
         * (at least 1024, the Voodoo's stride) that engine_present samples. */
        bytes = (uint32_t)pitch * (uint32_t)bpp_bytes * mg.mode.height;
        mg.disp_pitch_px = pitch;
        mg.disp_off[0] = 0;
        mg.disp_off[1] = (bytes + 0xFFF) & ~0xFFFu;
        off = (mg.disp_off[1] + bytes + 0xFFF) & ~0xFFFu;
        mg.pitch_px = mga_pow2_pitch(mg.plan.w) < 1024 ? 1024 : mga_pow2_pitch(mg.plan.w);
        mg.rw = mg.plan.w;
        mg.rh = mg.plan.h;
    }
    mg.disp_front = 0;
    bytes = (uint32_t)mg.pitch_px * (uint32_t)bpp_bytes * (uint32_t)mg.rh;
    for (i = 0; i < mg.nbuffers; i++) {
        mg.buf_off[i] = off;
        off = (off + bytes + 0xFFF) & ~0xFFFu;
    }
    /* 32-bit depth keeps W-buffering precise; use it whenever the buffers,
     * a 32-bit aux buffer and a 2 MB texture heap fit. */
    zbytes32 = (uint32_t)mg.pitch_px * 4u * (uint32_t)mg.rh;
    if (mg_config.force_z32 >= 0)
        mg.zbits = mg_config.force_z32 ? 32 : 16;
    else
        mg.zbits = mga.vram_bytes >= off + zbytes32 + (2u << 20) ? 32 : 16;
    if (mg.has_aux) {
        mg.aux_off = off;
        off = (off + (uint32_t)mg.pitch_px * (mg.zbits / 8u) * (uint32_t)mg.rh + 0xFFF) & ~0xFFFu;
    }
    mg.heap_off = off;
    mg.heap_end = mga.vram_bytes;
    if (mg.heap_off >= mg.heap_end) {
        mg_log(MG_LOG_ERROR, "not enough VRAM for %d buffers at %dx%d", mg.nbuffers, w, h);
        return FXFALSE;
    }
    tex_heap_init(mg.heap_off, mg.heap_end);
    tex_reset();
    mg.front = 0;
    mg.back = 1;
    mg.render_buffer = GR_BUFFER_BACKBUFFER;
    engine_init(mg.pitch_px, mg.bpp);
    mg.open = 1;
    mg.dirty = ~0u;
    mg.st.clip_x0 = 0; mg.st.clip_y0 = 0; mg.st.clip_x1 = w; mg.st.clip_y1 = h;
    mg_validate();
    if (mg.present_scaled)
        for (i = 0; i < 2; i++)
            clear_buffer(mg.disp_off[i], mg.disp_pitch_px, mg.mode.width, mg.mode.height);
    for (i = 0; i < mg.nbuffers; i++)
        clear_buffer(mg.buf_off[i], mg.pitch_px, mg.rw, mg.rh);
    if (mg.has_aux)
        engine_fill_depth(0, 0, mg.rw, mg.rh, 0);
    mg.dirty = ~0u;
    engine_sync(200000);
    if (mg.present_scaled)
        vbe_set_display_start(mg.disp_off[0], mg.disp_pitch_px * bpp_bytes, mg.bpp);
    else
        vbe_set_display_start(mg.buf_off[mg.front], mg.pitch_px * bpp_bytes, mg.bpp);
    mg_gamma_apply();
    mg.fogcol_valid = 0;
    mg_hooks_install();
    mg_line("MGL-WINOPEN %dx%d mode=%03x bpp=%d scale=%dx%d pitch=%d buffers=%d aux=%d z%d vram=%u heap=%u "
            "display=%dx%d fit=%s dpitch=%d filter=%s",
            w, h, mg.mode.mode, mg.bpp, mg_hx(w), mg_hy(h), mg.pitch_px, mg.nbuffers, mg.has_aux, mg.zbits,
            mga.vram_bytes, mg.heap_end - mg.heap_off, mg.mode.width, mg.mode.height, mga_fit_name(mg.plan.fit),
            mg.present_scaled ? mg.disp_pitch_px : pitch, mg_config.scale_filter ? "bilinear" : "nearest");
    return FXTRUE;
}

static void win_close(void)
{
    engine_sync(200000);
    mg_warp_stop();
    vbe_set_text_mode();
    mg.open = 0;
    sys_unhook_faults();
    sys_unhook_exit();
    mg_line("MGL-WINCLOSE frames=%u engine_resets=%u timeouts=%u warp_tris=%lu warp_failures=%lu kicks=%lu ring=%d/%luK "
            "wc=%d/%d fence_stuck=%lu", mg.frame, engine_resets, engine_timeouts, (unsigned long)warp_tris,
            (unsigned long)warp_failures, (unsigned long)warp_dma_kicks, warp_ring_mode, (unsigned long)warp_ring_kb,
            warp_wc_status, sys_cache_wc_err, (unsigned long)engine_fence_stuck);
    if (engine_timeouts)
        mg_line("MGL-TIMEOUT status=%08lx fifo=%08lx dwgsync=%08lx tag=%08lx primaddress=%08lx primend=%08lx "
                "warp0=%08lx warp1=%08lx", (unsigned long)engine_timeout_diag[0], (unsigned long)engine_timeout_diag[1],
                (unsigned long)engine_timeout_diag[2], (unsigned long)engine_timeout_diag[3],
                (unsigned long)engine_timeout_diag[4], (unsigned long)engine_timeout_diag[5],
                (unsigned long)engine_timeout_diag[6], (unsigned long)engine_timeout_diag[7]);
}

static void fault(void)
{
    warp_abort();                   /* (no waiting for a chip that may be stuck) */
    engine_reset();
    vbe_set_text_mode();
}

static void exit_restore(void)
{
    engine_sync(200000);
    mg_warp_stop();
    vbe_set_text_mode();
}

static void sync(void) { engine_sync(200000); }
static int busy(void) { return (MGA_RD32(0x1E14) & (1u << 16)) != 0; }
static int in_vblank(void) { return engine_in_vblank(); }
static unsigned vcount(void) { return engine_vcount(); }
static void set_ramp(const uint8_t ramp[256]) { dac_set_ramp(ramp); }

static void target_for(GrBuffer_t b)
{
    mga_target t;
    t.color_off = mg_buffer_offset(b);
    t.z_off = mg.aux_off;
    t.pitch_px = mg.pitch_px;
    t.bpp = mg.bpp;
    t.zbits = mg.zbits;
    engine_set_target(&t);
}

static void validate(void)
{
    if (mg.dirty & MG_DIRTY_TARGET)
        target_for(mg.render_buffer);
    if (mg.dirty & (MG_DIRTY_TARGET | MG_DIRTY_CLIP)) {
        int y0 = mg.st.clip_y0, y1 = mg.st.clip_y1;
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
            y0 = mg.height - mg.st.clip_y1;
            y1 = mg.height - mg.st.clip_y0;
        }
        engine_set_clip(mg_hx(mg.st.clip_x0), mg_hy(y0), mg_hx(mg.st.clip_x1), mg_hy(y1));
    }
    engine_set_maccess_flags((mg.st.dither == GR_DITHER_DISABLE ? MACCESS_NODITHER : 0) |
                             (mg.fog_on ? MACCESS_FOGEN : 0));
    mg.dirty = 0;
}

static void clear(int x0, int y0, int x1, int y1, GrColor_t color, FxU16 depth)
{
    if (mg.st.color_mask_rgb)
        engine_fill(x0, y0, x1 - x0, y1 - y0, mg.bpp == 32 ? (mg_color_to_argb(color) & 0xFFFFFFu)
                                                           : mg_argb_to_565(mg_color_to_argb(color)));
    if (mg.has_aux && mg.st.depth_mask && mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE)
        engine_fill_depth(x0, y0, x1 - x0, y1 - y0, mg_depth_clear_value(depth));
}

/* Scaled fits: draw render buffer src onto a display buffer (the hidden
 * one, or with to_front the one on screen). The engine's texture and blend
 * registers are overwritten; every draw sets them again. */
static void present(uint32_t src, int to_front)
{
    mga_surface rs, ds;
    rs.off = src; rs.w = mg.rw; rs.h = mg.rh; rs.pitch_px = mg.pitch_px;
    ds.off = mg.disp_off[to_front ? mg.disp_front : 1 - mg.disp_front];
    ds.w = mg.mode.width; ds.h = mg.mode.height; ds.pitch_px = mg.disp_pitch_px;
    FPU_ENTER();
    engine_present(&rs, &ds, mg.plan.dx, mg.plan.dy, mg.plan.dw, mg.plan.dh,
                   mg_config.scale_filter ? MGA_PRESENT_BILINEAR : MGA_PRESENT_NEAREST);
    FPU_LEAVE();
    mg_state_forget();
    mg.dirty |= MG_DIRTY_TARGET | MG_DIRTY_CLIP | MG_DIRTY_RASTER;
}

/* Show what the game drew into the front buffer (scaled fits only: there
 * the front buffer is a render buffer, not the one on screen). */
static void present_front(void)
{
    if (!mg.present_scaled)
        return;
    engine_sync(200000);
    present(mg.buf_off[mg.front], 1);
}

static void swap(int swap_interval)
{
    int i;
    engine_sync(200000);
    PROF_BACK(PROF_G_SWAP);
    if (mg.present_scaled) {
        /* Scale the finished frame into the hidden display buffer; flip
         * that once the engine is done with it. */
        present(mg.buf_off[mg.back], 0);
        PROF_BACK(PROF_G_DRAIN);
        engine_sync(200000);
        PROF_BACK(PROF_G_VSYNC);
        for (i = 0; i < swap_interval; i++)
            engine_vsync_wait(100000);
        PROF_BACK(PROF_G_SWAP);
        mg.disp_front ^= 1;
        vbe_set_display_start(mg.disp_off[mg.disp_front], mg.disp_pitch_px * (mg.bpp / 8), mg.bpp);
    } else {
        PROF_BACK(PROF_G_VSYNC);
        for (i = 0; i < swap_interval; i++)
            engine_vsync_wait(100000);
        PROF_BACK(PROF_G_SWAP);
        vbe_set_display_start(mg.buf_off[mg.back], mg.pitch_px * (mg.bpp / 8), mg.bpp);
    }
}

/* FOGCOL for the current mode: ADD2 drops the fog colour term. */
void mg_fog_emit_color(void)
{
    uint32_t c = (mg.st.fog_mode & GR_FOG_ADD2) ? 0 : (mg_color_to_argb(mg.st.fog_color) & 0xFFFFFF);
    if (mg.fogcol_valid && c == mg.fogcol_hw)
        return;
    fifo_reserve(1);
    MGA_WR32(MGAREG_FOGCOL, c);
    mg.fogcol_hw = c;
    mg.fogcol_valid = 1;
}

/* (texmgr.c's copy, moved: the rows as they are) */
static void tex_write(uint32_t org, const uint16_t *texels, int hw_w, int hw_h)
{
    int y;
    for (y = 0; y < hw_h; y++)
        memcpy((void *)(mg_vram + org + (uint32_t)(y * hw_w * 2)), texels + y * hw_w, (size_t)hw_w * 2);
}

const mg_backend mg_mga_backend = {
    "mga", probe, win_open, win_close, fault, exit_restore, sync, busy, in_vblank, vcount, set_ramp, validate,
    clear, swap, present_front, mga_be_tri, tex_write, 0, 0, 0, 0, 0
};
