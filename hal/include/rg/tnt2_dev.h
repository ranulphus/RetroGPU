/* tnt2_dev.h - the TNT2 device's open context (hal/src/rg/tnt2.c), for DOS-GL's TNT2 drawing back end
 * (gl/src/backend/tnt2), as rg/mga_dev.h is the Matrox one's. */
#ifndef RG_TNT2_DEV_H
#define RG_TNT2_DEV_H
#include <stdint.h>

typedef struct {
    int      active;
    int      width, height, pitch_bytes, bpp;
    int      double_buffer, depth_bits;     /* depth: 16 with 16-bit colour, 24 (Z24S8) with 32 */
    uint32_t front_off, back_off, z_off, heap_off, heap_end;
    int      front_is_a, draw_front;
} rg_tnt2_ctx;

extern rg_tnt2_ctx rg_tnt2;
uint32_t rg_tnt2_color_off(int front);     /* the colour buffer shown (1) or hidden (0) now */

#endif
