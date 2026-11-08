/* buffer.c - clears, swaps and render-buffer selection. */
#include "glide/mg.h"
#include "mga/sys.h"
#include "tex/texmgr.h"

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
void mg_present_front(void)
{
    if (!mg.open || !mg.present_scaled)
        return;
    engine_sync(200000);
    present(mg.buf_off[mg.front], 1);
}

GR_ENTRY(void, grRenderBuffer, (GrBuffer_t buffer))
{
    GrBuffer_t b = buffer == GR_BUFFER_FRONTBUFFER ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER;
    if (mg.render_buffer == GR_BUFFER_FRONTBUFFER && b != GR_BUFFER_FRONTBUFFER)
        mg_present_front();
    mg.render_buffer = b;
    mg.dirty |= MG_DIRTY_TARGET;
}

GR_ENTRY(void, grBufferClear, (GrColor_t color, GrAlpha_t alpha, FxU16 depth))
{
    int x0 = mg.st.clip_x0, x1 = mg.st.clip_x1, y0 = mg.st.clip_y0, y1 = mg.st.clip_y1;
    MGA_UNUSED(alpha);
    if (!mg.open)
        return;
    mg_validate();
    if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
        y0 = mg.height - mg.st.clip_y1;
        y1 = mg.height - mg.st.clip_y0;
    }
    x0 = mg_hx(x0); x1 = mg_hx(x1); y0 = mg_hy(y0); y1 = mg_hy(y1);
    if (mg.st.color_mask_rgb)
        engine_fill(x0, y0, x1 - x0, y1 - y0, mg.bpp == 32 ? (mg_color_to_argb(color) & 0xFFFFFFu)
                                                           : mg_argb_to_565(mg_color_to_argb(color)));
    if (mg.has_aux && mg.st.depth_mask && mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE)
        engine_fill_depth(x0, y0, x1 - x0, y1 - y0, mg_depth_clear_value(depth));
}

GR_ENTRY(void, grBufferSwap, (int swap_interval))
{
    int i, t, po;
    if (!mg.open)
        return;
    po = PROF_SWITCH(PROF_G_DRAIN);
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
    t = mg.front;
    mg.front = mg.back;
    mg.back = (mg.nbuffers == 3) ? (3 - mg.front - t) : t;
    if (mg.nbuffers == 3 && mg.back == mg.front)
        mg.back = (mg.front + 1) % 3;
    mg.dirty |= MG_DIRTY_TARGET;
    mg.frame++;
    tex_frame();
    mg_frame_end();
    PROF_BACK(po);
}

GR_ENTRY(int, grBufferNumPending, (void)) { return 0; }

/* Load the palette ramp for mg.gamma. In direct-colour modes each pixel
 * component indexes the palette RAM, and the video BIOS leaves whatever it
 * likes there, so the ramp is loaded at every window open, not only when
 * the game asks for gamma. */
void mg_gamma_apply(void)
{
    uint8_t ramp[256];
    double g = mg_config.gamma_enable ? mg.gamma : 1.0;
    int i;
    for (i = 0; i < 256; i++) {
        double x = i / 255.0, y = i ? mg_exp(mg_ln(x) / g) : 0.0;
        int v = (int)(y * 255.0 + 0.5);
        ramp[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    dac_set_ramp(ramp);
}

GR_ENTRY(void, grGammaCorrectionValue, (float value))
{
    FPU_ENTER();
    if (value <= 0.0f)
        value = 1.0f;
    mg.gamma = value;
    if (mg.open)
        mg_gamma_apply();
    FPU_LEAVE();
}
