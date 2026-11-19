/* tnt2.h - the Riva TNT2's register layer (docs/tnt2.md, T1): the NV4 family (the TNT and TNT2) found on PCI and
 * mapped, its graphics engine fed by a command ring in video memory, and its 2D operations. Calls mirror the
 * ViRGE/DX layer's (virge/virge.h) and the Matrox HAL's engine_* where they can. */
#ifndef TNT2_TNT2_H
#define TNT2_TNT2_H

#include <stdint.h>

typedef struct {
    int      bus, dev;
    uint16_t device;            /* the PCI device ID: 0020 TNT, 0028 TNT2, 0029 Ultra, 002C Vanta, 002D M64, 00A0 */
    uint8_t  revision;
    uint32_t bar0, bar1;        /* the register window (16 MB) and the frame buffer */
    uint32_t boot0;             /* PMC_BOOT_0 */
    uint32_t fb_size;           /* bytes of video memory (from PFB_BOOT_0) */
    uint32_t fb_usable;         /* below the 128 KB the layer keeps at the top: the command ring, then the cursor */
    const char *name;
} tnt2_chip;

extern tnt2_chip tnt2;
extern volatile uint32_t *tnt2_regs;    /* BAR0, once mapped */
extern volatile uint8_t *tnt2_fb;       /* BAR1, once mapped */

int  tnt2_find(void);           /* 0: an NV4-family chip on PCI (buses 0-3); fills tnt2's PCI fields */
int  tnt2_map(void);            /* 0: the registers and the frame buffer mapped, the memory size read */
void tnt2_unmap(void);

/* The graphics engine, its FIFO and instance memory set up, the command ring started; the BIOS's display mode is
 * left as it is. 0, or -1 when the engine doesn't answer. */
int  tnt2_accel_init(void);
int  tnt2_sync(uint32_t timeout_us);    /* every command done: 0, or -1 on time-out */
void tnt2_kick(void);                   /* the ring's written commands to the chip now */

/* 2D. The target: a surface at byte offset off in video memory, pitch_bytes apart, 8, 16 or 32 bits a pixel
 * (offsets and pitches multiples of 64 bytes). The clip is inclusive-exclusive. */
void tnt2_set_target(uint32_t off, int pitch_bytes, int bpp);
void tnt2_set_clip(int x0, int y0, int x1, int y1);
void tnt2_fill(int x, int y, int w, int h, uint32_t value);
void tnt2_copy(int sx, int sy, int dx, int dy, int w, int h);   /* within the target, overlapping allowed */

#endif
