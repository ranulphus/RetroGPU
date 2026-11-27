/* npramin.c - the NV1x's instance memory in video memory (docs/nv10.md, Q1): where PRAMIN (BAR0 0x700000, the chip's
 * object tables) lands in the card's memory, so the texture heap stays below it. Nouveau reserves 512 KB at the top
 * on these chips (instmem/nv04.c), the TNT2 layer only 128 KB: measured here, not guessed.
 *
 * For each of three instance offsets nothing uses before the layer's set-up (0x20000, 0x40000, 0x7FFF0): the word
 * read, a marker written, read back through PRAMIN, searched for (dword by dword) in video memory's top 2 MB through
 * BAR1, then the old word written back and checked. Nothing else is written to the chip.
 *
 *   NPRAMIN
 *
 * Lines: NPRAMIN-CARD, NPRAMIN-HIT off=... vram=... from_end=..., NPRAMIN-MAP; tests "nv1x", "marker", "restored". */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdio.h>

#define NV_VENDOR 0x10DE
#define SCAN (2u << 20)

int main(int argc, char **argv)
{
    static const uint32_t offs[3] = { 0x20000, 0x40000, 0x7FFF0 };
    int bus, dev, found = 0, bx = 0, dx = 0, k, ok_marker = 1, ok_restored = 1;
    uint16_t did = 0;
    uint32_t bar0, bar1, size, vram[3];
    volatile uint32_t *r32, *pr, *top;

    hx_init(argc, argv, "npramin");
    for (bus = 0; bus < 4 && !found; bus++)
        for (dev = 0; dev < 32 && !found; dev++) {
            uint32_t id = mga_pci_read32(bus, dev, 0, 0);
            if ((id & 0xFFFF) == NV_VENDOR) {
                found = 1, bx = bus, dx = dev;
                did = (uint16_t)(id >> 16);
            }
        }
    hx_test("nv1x", found && ((did & 0x0FF0) == 0x0100 || (did & 0x0FF0) == 0x0110), "10de:%04x", did);
    if (!found || !((did & 0x0FF0) == 0x0100 || (did & 0x0FF0) == 0x0110))
        hx_done(1);
    bar0 = mga_pci_read32(bx, dx, 0, 0x10) & ~0xFu;
    bar1 = mga_pci_read32(bx, dx, 0, 0x14) & ~0xFu;
    r32 = (volatile uint32_t *)sys_map_phys(bar0, 0x01000000u);
    if (!r32)
        hx_done(1);
    size = r32[0x10020C / 4] & 0xFFF00000u;
    hx_log("NPRAMIN-CARD id=10de:%04x bar0=%08lx bar1=%08lx vram=%lu MB", did, (unsigned long)bar0,
           (unsigned long)bar1, (unsigned long)(size >> 20));
    if (size < SCAN) {
        hx_test("marker", 0, "video memory size %08lx", (unsigned long)size);
        hx_done(1);
    }
    pr = r32 + 0x700000 / 4;
    top = (volatile uint32_t *)sys_map_phys(bar1 + size - SCAN, SCAN);
    if (!top)
        hx_done(1);
    for (k = 0; k < 3; k++) {
        uint32_t old = pr[offs[k] / 4], mark = 0x5EA1C0DEu ^ offs[k], back, i, hits = 0;
        vram[k] = 0xFFFFFFFFu;
        pr[offs[k] / 4] = mark;
        back = pr[offs[k] / 4];
        for (i = 0; i < SCAN / 4; i++)
            if (top[i] == mark) {
                uint32_t at = size - SCAN + i * 4u;
                if (!hits)
                    vram[k] = at;
                hits++;
                hx_log("NPRAMIN-HIT off=%05lx vram=%08lx from_end=%06lx", (unsigned long)offs[k], (unsigned long)at,
                       (unsigned long)(size - at));
            }
        pr[offs[k] / 4] = old;
        if (pr[offs[k] / 4] != old)
            ok_restored = 0;
        hx_log("NPRAMIN-WORD off=%05lx old=%08lx marker=%08lx readback=%08lx hits=%lu", (unsigned long)offs[k],
               (unsigned long)old, (unsigned long)mark, (unsigned long)back, (unsigned long)hits);
        if (back != mark || hits != 1)
            ok_marker = 0;
    }
    if (ok_marker) {
        /* (linear: video memory address = base + offset; reversed: the window counted down from an end) */
        long d1 = (long)vram[1] - (long)vram[0], d2 = (long)vram[2] - (long)vram[0];
        hx_log("NPRAMIN-MAP base=%08lx (vram - off at 0x20000) step %ld/%ld for +0x20000/+0x5FFF0: %s",
               (unsigned long)(vram[0] - offs[0]), d1, d2,
               d1 == 0x20000 && d2 == 0x5FFF0 ? "linear" : d1 == -0x20000 ? "reversed" : "other");
    }
    hx_test("marker", ok_marker, "each marker read back and found once in the top 2 MB");
    hx_test("restored", ok_restored, "each word written back");
    sys_unmap_phys(top, SCAN);
    sys_unmap_phys(r32, 0x01000000u);
    hx_done(ok_marker && ok_restored ? 0 : 1);
    return 0;
}
