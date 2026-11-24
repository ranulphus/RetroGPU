/* cursor.c - the Radeon's hardware cursor (docs/radeon.md, RD1): the Rage 128 layer's cursor.c with the Radeon's
 * names, the registers at the same offsets (X.org's r128_cursor.c and radeon_cursor.c, MIT): 64 x 64 at 2 bits a
 * pixel, each row 8 bytes of the AND mask then 8 of the XOR mask, the leftmost pixel in bit 7; true-colour
 * CUR_CLR0/1; a position off the top or the left made by the offset registers and the image's start. To be checked
 * on the card with the device (RD4). */
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include <string.h>

static uint32_t cur_off;

int radeon_cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512])
{
    uint32_t gen;
    volatile uint8_t *d;
    int y, i;
    if ((off & 15u) || off + RADEON_CURSOR_BYTES > radeon.vram_bytes)
        return -1;
    cur_off = off;
    gen = radeon_rd(RADEON_CRTC_GEN_CNTL);
    radeon_wr(RADEON_CRTC_GEN_CNTL, gen & ~RADEON_CRTC_CUR_EN);      /* (hidden while its image changes) */
    d = radeon.fb + off;
    for (y = 0; y < 64; y++)
        for (i = 0; i < 8; i++) {
            d[y * 16 + i] = and_mask[y * 8 + i];
            d[y * 16 + 8 + i] = xor_mask[y * 8 + i];
        }
    radeon_wr(RADEON_CUR_OFFSET, cur_off);
    radeon_wr(RADEON_CRTC_GEN_CNTL, gen);
    return 0;
}

void radeon_cursor_colours(uint32_t c0, uint32_t c1)
{
    radeon_wr(RADEON_CUR_CLR0, c0 & 0xFFFFFFu);
    radeon_wr(RADEON_CUR_CLR1, c1 & 0xFFFFFFu);
}

void radeon_cursor_show(int on)
{
    uint32_t gen = radeon_rd(RADEON_CRTC_GEN_CNTL);
    radeon_wr(RADEON_CRTC_GEN_CNTL, on ? gen | RADEON_CRTC_CUR_EN : gen & ~RADEON_CRTC_CUR_EN);
}

/* (radeon_crtc_set_cursor_position's: off the left or the top, the origin moves into the image and its start moves
 * down a row of 16 bytes for each line cut) */
void radeon_cursor_move(int x, int y)
{
    int xo = 0, yo = 0;
    if (x < 0) xo = -x + 1;
    if (y < 0) yo = -y + 1;
    if (xo > 63) xo = 63;
    if (yo > 63) yo = 63;
    radeon_wr(RADEON_CUR_HORZ_VERT_OFF, RADEON_CUR_LOCK | ((uint32_t)xo << 16) | (uint32_t)yo);
    radeon_wr(RADEON_CUR_HORZ_VERT_POSN, RADEON_CUR_LOCK | ((uint32_t)(xo ? 0 : x) << 16) | (uint32_t)(yo ? 0 : y));
    radeon_wr(RADEON_CUR_OFFSET, cur_off + (uint32_t)yo * 16u);
}
