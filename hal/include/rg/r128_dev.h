/* r128_dev.h - the Rage 128 device's open context (hal/src/rg/r128.c), for the drawing back ends (DOS-GL's
 * gl/src/backend/r128, Glide's glide/src/backend/r128), as rg/tnt2_dev.h is the TNT2's. */
#ifndef RG_R128_DEV_H
#define RG_R128_DEV_H
#include <stdint.h>

typedef struct {
    int      active;
    int      width, height, pitch_bytes, bpp;
    int      double_buffer, depth_bits;     /* depth: 16 with 16-bit colour, 24 (Z24S8) with 32 */
    int      z_pitch_bytes;                 /* the depth buffer's rows (its own pixels' size at the colour's pitch) */
    uint32_t front_off, back_off, z_off, heap_off, heap_end;
    int      front_is_a, draw_front;
} rg_r128_ctx;

extern rg_r128_ctx rg_r128;
uint32_t rg_r128_color_off(int front);     /* the colour buffer shown (1) or hidden (0) now */

#endif
