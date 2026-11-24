/* radeon_dev.h - the Radeon device's open context (hal/src/rg/radeon.c), for the drawing back ends (DOS-GL's
 * gl/src/backend/radeon, Glide's glide/src/backend/radeon), as rg/radeon_dev.h is the Rage 128's. Offsets are from
 * the start of the card's memory (inside DGL_RADEON_VRAM's megabytes when that is set). */
#ifndef RG_RADEON_DEV_H
#define RG_RADEON_DEV_H
#include <stdint.h>

typedef struct {
    int      active;
    int      width, height, pitch_bytes, bpp;
    int      double_buffer, depth_bits;     /* depth: 16 with 16-bit colour, 24 (Z24S8) with 32 */
    int      z_pitch_bytes;                 /* the depth buffer's rows (its own pixels' size at the colour's pitch) */
    uint32_t front_off, back_off, z_off, heap_off, heap_end;
    int      front_is_a, draw_front;
} rg_radeon_ctx;

extern rg_radeon_ctx rg_radeon;
extern int rg_radeon_vram_mb[2];            /* megabytes A and B the device may use, set before its probe (0, 0: all) */
uint32_t rg_radeon_color_off(int front);     /* the colour buffer shown (1) or hidden (0) now */

#endif
