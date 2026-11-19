/* buffer.c - clears, swaps and render-buffer selection. */
#include "glide/mg.h"
#include "backend/backend.h"
#include "mga/sys.h"
#include "tex/texmgr.h"

/* Show what the game drew into the front buffer (scaled fits only: there
 * the front buffer is a render buffer, not the one on screen). */
void mg_present_front(void)
{
    if (!mg.open)
        return;
    mg_be->present_front();
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
    mg_be->clear(x0, y0, x1, y1, color, depth);
}

GR_ENTRY(void, grBufferSwap, (int swap_interval))
{
    int t, po;
    if (!mg.open)
        return;
    po = PROF_SWITCH(PROF_G_DRAIN);
    mg_be->swap(swap_interval);
    t = mg.front;
    mg.front = mg.back;
    mg.back = (mg.nbuffers == 3) ? (3 - mg.front - t) : t;
    if (mg.nbuffers == 3 && mg.back == mg.front)
        mg.back = (mg.front + 1) % 3;
    mg.dirty |= MG_DIRTY_TARGET;
    mg.frame++;
    tex_frame();
    mg_frame_end();
#ifdef MGA_REGCRC
    mg_regcrc("swap");
#endif
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
    mg_be->set_ramp(ramp);
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
