/* mga_dev.h - the Matrox device's own side (hal/src/rg/mga.c), for the
 * Matrox drawing back ends only (GL's: gl/src/backend/mga): the buffers it
 * made, where drawing goes, how the picture is shown. */
#ifndef RG_MGA_DEV_H
#define RG_MGA_DEV_H
#include "rg/device.h"
#include "mga/hal.h"

typedef struct {
    int      active;
    int      width, height, pitch_px;
    int      double_buffer, depth_bits, vsync;
    uint32_t front_off, back_off, z_off, heap_off, heap_end, vram_bytes;
    int      front_is_a;                  /* which of the two colour buffers is shown */
    int      draw_front;                  /* drawing goes into the shown buffer (glDrawBuffer(GL_FRONT)) */
    /* Scaled modes: drawing goes into one render buffer (front_off, at
     * pitch_px) and each swap scales it into the hidden display buffer,
     * then flips the display. Zoomed modes draw into the display buffers
     * and the chip doubles lines and pixels. */
    int      scaled, zoomed, filter;
    uint32_t disp_off[2];
    int      disp_front, disp_pitch_px;
    mga_mode_plan plan;
    rg_open  hooks;                       /* the user's hooks, from open */
} rg_mga_state;
extern rg_mga_state rg_mga;

uint32_t rg_mga_color_off(int front);    /* VRAM offset of the shown (1) or hidden (0) colour buffer */
int      rg_mga_pitch_for(int width);    /* pixels, the engine's rules: a multiple of 32, at most 4096; 0 too wide */

#endif
