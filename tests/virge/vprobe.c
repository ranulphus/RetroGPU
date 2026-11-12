/* vprobe.c - S3 ViRGE/DX bring-up (gl/docs/backends.md, the first non-Matrox
 * card): find the chip on PCI, read its ID and memory registers, list every
 * VBE mode its BIOS offers (VPROBE-MODE lines, unfiltered), then show that a
 * linear framebuffer is reachable even through a VBE 1.2 BIOS: set a 640x480
 * high-colour mode through the BIOS, open the chip's linear address window
 * (CR58 bit 4, at the PCI BAR, CR59/CR5A), draw colour bars through it and
 * read the screen back through 86Box's unit tester.
 *
 *   VPROBE [--list-only]
 *
 * Lines: VPROBE-PCI, VPROBE-REG, VPROBE-VBE, VPROBE-MODE; tests "pci", "id",
 * "vbe", "mode", "lfb". */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>

#define S3_VENDOR 0x5333

/* The S3 3D chips by PCI device ID (S3's data books, 86Box's vid_s3_virge.c). */
static const char *s3_name(uint16_t id)
{
    switch (id) {
    case 0x5631: return "ViRGE (86C325)";
    case 0x8A01: return "ViRGE/DX or /GX (86C375/385)";
    case 0x883D: return "ViRGE/VX (86C988)";
    case 0x8A10: return "ViRGE/GX2 (86C357)";
    case 0x8A13: return "Trio3D (86C365)";
    case 0x8811: return "Trio32/64 (no 3D engine)";
    default:     return "unknown";
    }
}

static uint8_t cr_read(uint8_t i)
{
    sys_outb(0x3D4, i);
    return sys_inb(0x3D5);
}

static void cr_write(uint8_t i, uint8_t v)
{
    sys_outb(0x3D4, i);
    sys_outb(0x3D5, v);
}

/* A whole frame: the VGA status register's vertical retrace bit (3DAh bit 3)
 * goes low then high, twice, so the screen shows what was drawn before. */
static void frame_wait(void)
{
    int k;
    for (k = 0; k < 2; k++) {
        uint32_t t0 = sys_time_us();
        while ((sys_inb(0x3DA) & 0x08) && sys_time_us() - t0 < 100000)
            ;
        while (!(sys_inb(0x3DA) & 0x08) && sys_time_us() - t0 < 100000)
            ;
    }
}

/* VBE through the BIOS: a DOS buffer for the info blocks. */
static uint8_t *vbuf;
static uint16_t vseg;

static int vbe(sys_rmregs *r)
{
    if (sys_rm_int(0x10, r) < 0)
        return -1;
    return (r->eax & 0xFFFF) == 0x004F ? 0 : -1;
}

static const char *far_str(const uint8_t *p, char *out, int n)
{
    uint16_t off = (uint16_t)(p[0] | (p[1] << 8)), seg = (uint16_t)(p[2] | (p[3] << 8));
    const char *s = (seg || off) ? (const char *)sys_real_ptr(seg, off) : NULL;
    int i = 0;
    if (s)
        for (; i < n - 1 && s[i] && s[i] >= 32 && s[i] < 127; i++)
            out[i] = s[i];
    out[i] = 0;
    return out;
}

typedef struct {
    uint16_t mode, w, h, pitch;
    uint8_t bpp, model, attr;
    uint32_t lfb;
} vmode;

static int mode_info(uint16_t m, vmode *v)
{
    sys_rmregs r;
    uint8_t *b = vbuf + 512;
    memset(b, 0, 256);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F01;
    r.ecx = m;
    r.es = vseg;
    r.edi = 512;
    if (vbe(&r) < 0)
        return -1;
    v->mode = m;
    v->attr = b[0];
    v->pitch = (uint16_t)(b[0x10] | (b[0x11] << 8));
    v->w = (uint16_t)(b[0x12] | (b[0x13] << 8));
    v->h = (uint16_t)(b[0x14] | (b[0x15] << 8));
    v->bpp = b[0x19];
    v->model = b[0x1B];
    v->lfb = (uint32_t)b[0x28] | ((uint32_t)b[0x29] << 8) | ((uint32_t)b[0x2A] << 16) | ((uint32_t)b[0x2B] << 24);
    return 0;
}

int main(int argc, char **argv)
{
    int bus, dev, found = 0, bx = 0, dx = 0, i, n = 0, list_only = 0;
    uint16_t did = 0, list[128];
    uint32_t bar0 = 0, cmd = 0;
    uint8_t lock38, lock39;
    sys_rmregs r;
    vmode best = { 0 };
    char oem[64], vend[64], prod[64];

    hx_init(argc, argv, "vprobe");
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--list-only"))
            list_only = 1;

    /* PCI: the first S3 device on buses 0-1. */
    for (bus = 0; bus < 2 && !found; bus++)
        for (dev = 0; dev < 32 && !found; dev++) {
            uint32_t id = mga_pci_read32(bus, dev, 0, 0);
            if ((id & 0xFFFF) == S3_VENDOR) {
                found = 1;
                bx = bus;
                dx = dev;
                did = (uint16_t)(id >> 16);
            }
        }
    hx_test("pci", found, "S3 device on PCI");
    if (!found)
        hx_done(1);
    cmd = mga_pci_read32(bx, dx, 0, 0x04);
    bar0 = mga_pci_read32(bx, dx, 0, 0x10);
    hx_log("VPROBE-PCI bus=%d dev=%d id=5333:%04x (%s) rev=%02x cmd=%04x bar0=%08x subsys=%08x", bx, dx, did,
           s3_name(did), (unsigned)(mga_pci_read32(bx, dx, 0, 0x08) & 0xFF), (unsigned)(cmd & 0xFFFF),
           (unsigned)bar0, (unsigned)mga_pci_read32(bx, dx, 0, 0x2C));

    /* The S3 extended registers: unlock (CR38 = 48h, CR39 = A5h), read, relock later. */
    lock38 = cr_read(0x38);
    lock39 = cr_read(0x39);
    cr_write(0x38, 0x48);
    cr_write(0x39, 0xA5);
    hx_log("VPROBE-REG cr2d=%02x cr2e=%02x cr2f=%02x cr30=%02x cr36=%02x cr53=%02x cr58=%02x cr59=%02x cr5a=%02x",
           cr_read(0x2D), cr_read(0x2E), cr_read(0x2F), cr_read(0x30), cr_read(0x36), cr_read(0x53), cr_read(0x58),
           cr_read(0x59), cr_read(0x5A));
    /* CR2D/CR2E repeat the PCI device ID: 8A01 for the ViRGE/DX and /GX (CR2F tells them apart). */
    hx_test("id", cr_read(0x2D) == (did >> 8) && cr_read(0x2E) == (did & 0xFF), "CR2D/CR2E = %02x%02x, PCI %04x",
            cr_read(0x2D), cr_read(0x2E), did);

    /* VBE: version, names, memory, and every mode the BIOS lists. */
    vbuf = (uint8_t *)sys_dos_alloc(1024, &vseg);
    if (!vbuf)
        hx_done(1);
    memset(vbuf, 0, 512);
    memcpy(vbuf, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00;
    r.es = vseg;
    r.edi = 0;
    if (vbe(&r) < 0 || memcmp(vbuf, "VESA", 4) != 0) {
        hx_test("vbe", 0, "no VBE BIOS");
        hx_done(1);
    }
    {
        uint16_t ver = (uint16_t)(vbuf[4] | (vbuf[5] << 8));
        const uint16_t *p = (const uint16_t *)sys_real_ptr((uint16_t)(vbuf[0x10] | (vbuf[0x11] << 8)),
                                                             (uint16_t)(vbuf[0x0E] | (vbuf[0x0F] << 8)));
        while (p && *p != 0xFFFF && n < 128)
            list[n++] = *p++;
        hx_log("VPROBE-VBE ver=%x.%x mem=%uKB modes=%d oem=\"%s\" vendor=\"%s\" product=\"%s\"", ver >> 8, ver & 0xFF,
               (unsigned)(vbuf[0x12] | (vbuf[0x13] << 8)) * 64, n, far_str(vbuf + 6, oem, sizeof oem),
               ver >= 0x200 ? far_str(vbuf + 0x16, vend, sizeof vend) : "",
               ver >= 0x200 ? far_str(vbuf + 0x1A, prod, sizeof prod) : "");
        hx_test("vbe", 1, "VBE %x.%x", ver >> 8, ver & 0xFF);
    }
    for (i = 0; i < n; i++) {
        vmode v;
        if (mode_info(list[i], &v) < 0)
            continue;
        hx_log("VPROBE-MODE m=%03x %ux%ux%u model=%u attr=%02x pitch=%u lfb=%08x", v.mode, v.w, v.h, v.bpp,
               v.model, v.attr, v.pitch, (unsigned)v.lfb);
        /* The test's mode: 640x480 at 15 or 16 bits, direct colour, graphics. */
        if (v.w == 640 && v.h == 480 && (v.bpp == 15 || v.bpp == 16) && (v.attr & 0x11) == 0x11 &&
            (best.mode == 0 || v.bpp == 16))
            best = v;
    }
    hx_test("mode", best.mode != 0, "640x480 high colour: %03x (%u bits)", best.mode, best.bpp);
    if (list_only || best.mode == 0) {
        cr_write(0x39, lock39);
        cr_write(0x38, lock38);
        hx_done(hx_failures() ? 1 : 0);
    }

    /* Set the mode through the BIOS (no LFB flag: VBE 1.2 has none), then open
     * the linear address window at BAR0 ourselves: CR58 bit 4 enables it,
     * bits 2:0 = 3 make it 4 MB; CR59/CR5A hold bits 31:16 of its address
     * (CR59 follows the PCI BAR). */
    {
        uint32_t base = bar0 & 0xFFFFFFF0u, size = 4u << 20;
        volatile uint16_t *fb;
        int ok = 1, bars = 8, x, y;
        static const uint8_t rgb[8][3] = { { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
                                           { 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 0, 0, 0 } };
        memset(&r, 0, sizeof r);
        r.eax = 0x4F02;
        r.ebx = best.mode;
        if (vbe(&r) < 0) {
            hx_test("lfb", 0, "mode set %03x failed", best.mode);
            hx_done(1);
        }
        cr_write(0x38, 0x48);
        cr_write(0x39, 0xA5);
        cr_write(0x59, (uint8_t)(base >> 24));
        cr_write(0x5A, (uint8_t)(base >> 16));
        cr_write(0x58, (uint8_t)((cr_read(0x58) & ~0x17) | 0x10 | 0x03));
        hx_log("VPROBE-REG after mode %03x: cr58=%02x cr59=%02x cr5a=%02x cr31=%02x cr51=%02x cr13=%02x pitch=%u",
               best.mode, cr_read(0x58), cr_read(0x59), cr_read(0x5A), cr_read(0x31), cr_read(0x51), cr_read(0x13),
               best.pitch);
        fb = (volatile uint16_t *)sys_map_phys(base, size);
        if (!fb) {
            hx_test("lfb", 0, "cannot map %08x", (unsigned)base);
            hx_done(1);
        }
        for (y = 0; y < 480; y++)
            for (x = 0; x < 640; x++) {
                const uint8_t *c = rgb[x * bars / 640];
                uint16_t px = best.bpp == 16 ? (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3))
                                             : (uint16_t)(((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3));
                fb[y * (best.pitch / 2) + x] = px;
            }
        /* What the monitor shows, through the unit tester: one pixel in the middle of each bar. */
        frame_wait();
        if (hx_ut_present()) {
            uint16_t cw = 0, ch = 0;
            if (hx_ut_capture(&cw, &ch) < 0 || cw != 640 || ch != 480) {
                hx_log("VPROBE-PIX capture %ux%u", cw, ch);
                ok = 0;
            }
            for (i = 0; ok && i < bars; i++) {
                uint8_t bgrx[4];
                int px = i * 640 / bars + 640 / bars / 2;
                if (hx_ut_read(px, 240, 1, 1, bgrx) < 0) {
                    ok = 0;
                    break;
                }
                if ((bgrx[2] >> 3) != (rgb[i][0] >> 3) || (bgrx[1] >> 3) != (rgb[i][1] >> 3) ||
                    (bgrx[0] >> 3) != (rgb[i][2] >> 3)) {
                    hx_log("VPROBE-PIX bar=%d want=%02x%02x%02x got=%02x%02x%02x", i, rgb[i][0], rgb[i][1], rgb[i][2],
                           bgrx[2], bgrx[1], bgrx[0]);
                    ok = 0;
                }
            }
            hx_snap_screen("VPROBE");
            hx_test("lfb", ok, "colour bars through the linear window at %08x, mode %03x", (unsigned)base, best.mode);
        } else
            hx_log("VPROBE-LFB no unit tester: bars drawn, not read back");
        sys_unmap_phys(fb, size);
        cr_write(0x58, (uint8_t)(cr_read(0x58) & ~0x10));
        memset(&r, 0, sizeof r);                    /* text mode by the BIOS (not vbe_set_text_mode: Matrox) */
        r.eax = 0x0003;
        sys_rm_int(0x10, &r);
        cr_write(0x39, lock39);
        cr_write(0x38, lock38);
    }
    hx_done(hx_failures() ? 1 : 0);
}
