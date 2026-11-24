/* cursor.c - the Rage 128's hardware cursor (docs/rage128.md, R1), as X.org's xf86-video-r128 drives it
 * (r128_cursor.c, MIT): 64 x 64 at 2 bits a pixel, each row 8 bytes of the AND mask then 8 of the XOR mask, the
 * leftmost pixel in bit 7 (the flags SOURCE_MASK_INTERLEAVE_64, BIT_ORDER_MSBFIRST: Windows' AND/XOR meaning);
 * true-colour CUR_CLR0/1; a position off the top or the left made by the offset registers and the image's start. */
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include <string.h>

static uint32_t cur_off;

int r128_cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512])
{
    uint32_t gen;
    volatile uint8_t *d;
    int y, i;
    if ((off & 15u) || off + R128_CURSOR_BYTES > r128.vram_bytes)
        return -1;
    cur_off = off;
    gen = r128_rd(R128_CRTC_GEN_CNTL);
    r128_wr(R128_CRTC_GEN_CNTL, gen & ~R128_CRTC_CUR_EN);      /* (hidden while its image changes) */
    d = r128.fb + off;
    for (y = 0; y < 64; y++)
        for (i = 0; i < 8; i++) {
            d[y * 16 + i] = and_mask[y * 8 + i];
            d[y * 16 + 8 + i] = xor_mask[y * 8 + i];
        }
    r128_wr(R128_CUR_OFFSET, cur_off);
    r128_wr(R128_CRTC_GEN_CNTL, gen);
    return 0;
}

void r128_cursor_colours(uint32_t c0, uint32_t c1)
{
    r128_wr(R128_CUR_CLR0, c0 & 0xFFFFFFu);
    r128_wr(R128_CUR_CLR1, c1 & 0xFFFFFFu);
}

void r128_cursor_show(int on)
{
    uint32_t gen = r128_rd(R128_CRTC_GEN_CNTL);
    r128_wr(R128_CRTC_GEN_CNTL, on ? gen | R128_CRTC_CUR_EN : gen & ~R128_CRTC_CUR_EN);
}

/* (r128_crtc_set_cursor_position's: off the left or the top, the origin moves into the image and its start moves
 * down a row of 16 bytes for each line cut) */
void r128_cursor_move(int x, int y)
{
    int xo = 0, yo = 0;
    if (x < 0) xo = -x + 1;
    if (y < 0) yo = -y + 1;
    if (xo > 63) xo = 63;
    if (yo > 63) yo = 63;
    r128_wr(R128_CUR_HORZ_VERT_OFF, R128_CUR_LOCK | ((uint32_t)xo << 16) | (uint32_t)yo);
    r128_wr(R128_CUR_HORZ_VERT_POSN, R128_CUR_LOCK | ((uint32_t)(xo ? 0 : x) << 16) | (uint32_t)(yo ? 0 : y));
    r128_wr(R128_CUR_OFFSET, cur_off + (uint32_t)yo * 16u);
}
