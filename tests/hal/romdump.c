/* romdump.c - ROMDUMP: the video BIOS image at C000:0000 (its length from the header's byte 2, in 512-byte
 * units) to C:\OUT\VIDEOROM.BIN, with its PInS block's place, length and version on COM1/stdout (HX-STAT
 * romdump). Reads only. For the emulator's ROM set (RetroGPU local patch 0129: the emulated G450 with bench F's
 * own BIOS, whose PInS v5 block the HAL's memory set-up reads). */
#include "hx.h"
#include "mga/sys.h"
#include <stdio.h>
#include <stdint.h>

int main(int argc, char **argv)
{
    const volatile uint8_t *rom;
    unsigned len, at, i;
    uint8_t sum = 0;
    FILE *f;
    (void)argc;
    hx_init(1, argv, "romdump");
    rom = (const volatile uint8_t *)sys_real_ptr(0xC000, 0);
    if (!rom || rom[0] != 0x55 || rom[1] != 0xAA) {
        hx_test("romdump", 0, "no option ROM signature at C000:0000");
        hx_done(1);
    }
    len = (unsigned)rom[2] * 512u;
    at = (unsigned)(rom[0x7FFC] | rom[0x7FFD] << 8);
    for (i = 0; i < len; i++)
        sum = (uint8_t)(sum + rom[i]);
    f = fopen("C:\\OUT\\VIDEOROM.BIN", "wb");
    if (!f) {
        hx_test("romdump", 0, "cannot write C:\\OUT\\VIDEOROM.BIN");
        hx_done(1);
    }
    for (i = 0; i < len; i++)
        fputc(rom[i], f);
    fclose(f);
    hx_log("HX-STAT romdump length=%u checksum=%02x pins_at=%04x sig=%02x%02x pins_len=%u pins_ver=%u", len, sum, at,
           rom[at], rom[at + 1], rom[at + 2], rom[at + 5]);
    hx_test("romdump", 1, "%u bytes to C:\\OUT\\VIDEOROM.BIN", len);
    hx_done(0);
}
