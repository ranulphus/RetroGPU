/* cursor.c - the ViRGE/DX's hardware cursor and display start (docs/virge.md,
 * V1; data book 15.5 and section 18), with the Matrox HAL's calls' meaning
 * (cursor_* and crtc_set_start in mga/hal.h). The cursor is 64 x 64, its AND
 * and XOR masks word-interleaved in VRAM on a 1 KB boundary, in the Windows
 * sense (CR55 bit 4 clear); its two colours are pixel values of the mode, so
 * they are converted from 0xRRGGBB here. The display start counts 4-byte
 * units, 20 bits of them (CR0D, CR0C, CR69). */
#include "virge/virge.h"
#include "virge/regs_virge.h"
#include "mga/sys.h"

int virge_mode_bpp;                 /* 8, 15, 16, 24 or 32: set by virge_set_mode */

static uint32_t pixel_of(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    switch (virge_mode_bpp) {
    case 15: return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    case 16: return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case 24:
    case 32: return rgb & 0xFFFFFF;
    default: return rgb & 0xFF;     /* 8 bits: a palette index */
    }
}

int virge_cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512])
{
    volatile uint8_t *d;
    uint32_t seg = off >> 10;
    int k;
    if ((off & 1023) || off + VIRGE_CURSOR_BYTES > virge.vram_bytes || !virge.fb)
        return -1;
    virge_sync(200000);
    d = virge.fb + off;
    for (k = 0; k < 256; k++) {     /* AND word k, then XOR word k: 16 pixels of a row each */
        d[k * 4 + 0] = and_mask[k * 2];
        d[k * 4 + 1] = and_mask[k * 2 + 1];
        d[k * 4 + 2] = xor_mask[k * 2];
        d[k * 4 + 3] = xor_mask[k * 2 + 1];
    }
    virge_cr_write(0x39, 0xA5);
    virge_cr_write(0x55, (uint8_t)(virge_cr_read(0x55) & ~0x10));      /* Windows-style masks */
    virge_cr_write(0x4C, (uint8_t)((seg >> 8) & 0x0F));
    virge_cr_write(0x4D, (uint8_t)seg);
    return 0;
}

static void stack(uint8_t index, uint32_t v)
{
    (void)virge_cr_read(0x45);      /* reading CR45 resets both colour stacks */
    virge_cr_write(index, (uint8_t)v);
    virge_cr_write(index, (uint8_t)(v >> 8));
    virge_cr_write(index, (uint8_t)(v >> 16));
}

void virge_cursor_colours(uint32_t c0, uint32_t c1)
{
    virge_cr_write(0x39, 0xA5);
    stack(0x4B, pixel_of(c0));      /* background: AND 0, XOR 0 */
    stack(0x4A, pixel_of(c1));      /* foreground: AND 0, XOR 1 */
}

void virge_cursor_show(int on)
{
    virge_cr_write(0x39, 0xA5);
    virge_cr_write(0x45, (uint8_t)((virge_cr_read(0x45) & ~0x01) | (on ? 1 : 0)));
}

void virge_cursor_move(int x, int y)
{
    int ox = 0, oy = 0;
    if (x < 0) ox = -x > 63 ? 63 : -x, x = 0;
    if (y < 0) oy = -y > 63 ? 63 : -y, y = 0;
    if (x > 2047) x = 2047;
    if (y > 2047) y = 2047;
    virge_cr_write(0x39, 0xA5);
    virge_cr_write(0x4E, (uint8_t)ox);
    virge_cr_write(0x4F, (uint8_t)oy);
    virge_cr_write(0x46, (uint8_t)(x >> 8));
    virge_cr_write(0x47, (uint8_t)x);
    virge_cr_write(0x48, (uint8_t)(y >> 8));
    virge_cr_write(0x49, (uint8_t)y);       /* (written last: the position takes effect) */
}

int virge_set_start(uint32_t byte_off)
{
    uint32_t u = byte_off >> 2;
    if ((byte_off & 3) || byte_off >= virge.vram_bytes)
        return -1;
    virge_cr_write(0x38, 0x48);
    virge_cr_write(0x39, 0xA5);
    virge_cr_write(0x0D, (uint8_t)u);
    virge_cr_write(0x0C, (uint8_t)(u >> 8));
    virge_cr_write(0x31, (uint8_t)((virge_cr_read(0x31) & ~0x30) | (((u >> 16) & 3) << 4)));
    virge_cr_write(0x51, (uint8_t)((virge_cr_read(0x51) & ~0x03) | ((u >> 18) & 3)));
    virge_cr_write(0x69, (uint8_t)((virge_cr_read(0x69) & ~0x0F) | ((u >> 16) & 0x0F)));
    return 0;
}

int virge_vsync_wait(uint32_t timeout_us)
{
    uint32_t t0 = sys_time_us();
    while (sys_inb(0x3DA) & 0x08)               /* out of any retrace in progress */
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    while (!(sys_inb(0x3DA) & 0x08))            /* to the next one's start */
        if (sys_time_us() - t0 >= timeout_us)
            return -1;
    return 0;
}
