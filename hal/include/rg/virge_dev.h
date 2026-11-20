/* virge_dev.h - the ViRGE/DX device's open context (hal/src/rg/virge.c), for drawing back ends on the ViRGE layer
 * (Glide's glide/src/backend/virge), as rg/tnt2_dev.h is the TNT2 one's. */
#ifndef RG_VIRGE_DEV_H
#define RG_VIRGE_DEV_H
#include <stdint.h>

/* Where the open context's buffers are: the colour buffers A and B (B 0 when single buffered), the depth buffer and
 * its pitch in bytes (0 without one), and the colour buffers' pitch and bits a pixel (15, 16 or 24). 0, or -1 when
 * nothing is open. */
int rg_virge_layout(uint32_t *front, uint32_t *back, uint32_t *z, int *z_pitch, int *pitch_bytes, int *bpp);

#endif
