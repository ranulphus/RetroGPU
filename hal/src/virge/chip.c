/* chip.c - the S3 ViRGE/DX: finding it on PCI, its registers and window,
 * and modes through its BIOS (docs/virge.md, V1). The BIOS may be VBE 1.2,
 * with no linear framebuffer: the mode is set by the BIOS, then the chip's
 * linear address window (CR58) and new MMIO (CR53) are opened here, at the
 * PCI BAR (data book sections 15.1.2 and 15.2.2). */
#include "virge/virge.h"
#include "virge/regs_virge.h"
#include "mga/sys.h"
#include <string.h>

virge_chip virge;

static uint32_t pci_read32(int bus, int dev, int reg)
{
    sys_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | (uint32_t)(reg & 0xFC));
    return sys_inl(0xCFC);
}

uint8_t virge_cr_read(uint8_t index)
{
    sys_outb(0x3D4, index);
    return sys_inb(0x3D5);
}

void virge_cr_write(uint8_t index, uint8_t value)
{
    sys_outb(0x3D4, index);
    sys_outb(0x3D5, value);
}

static void unlock(void)
{
    virge_cr_write(VIRGE_CR_LOCK1, 0x48);
    virge_cr_write(VIRGE_CR_LOCK2, 0xA5);
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

static int vbe_info(void)
{
    sys_rmregs r;
    if (!vbuf && !(vbuf = (uint8_t *)sys_dos_alloc(1024, &vseg)))
        return -1;
    memset(vbuf, 0, 512);
    memcpy(vbuf, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00;
    r.es = vseg;
    r.edi = 0;
    if (vbe(&r) < 0 || memcmp(vbuf, "VESA", 4) != 0)
        return -1;
    return 0;
}

int virge_find(void)
{
    int bus, dev;
    for (bus = 0; bus < 2; bus++)
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0);
            if ((id & 0xFFFF) == VIRGE_PCI_VENDOR && (id >> 16) == VIRGE_PCI_DX) {
                memset(&virge, 0, sizeof virge);
                virge.bus = bus;
                virge.dev = dev;
                virge.device_id = (uint16_t)(id >> 16);
                virge.revision = (uint8_t)pci_read32(bus, dev, 0x08);
                virge.bar0 = pci_read32(bus, dev, 0x10) & 0xFC000000u;   /* a 64 MB window */
                virge.vram_bytes = vbe_info() == 0 ? (uint32_t)(vbuf[0x12] | (vbuf[0x13] << 8)) << 16 : 0;
                if (!virge.vram_bytes)
                    virge.vram_bytes = 2u << 20;
                return 0;
            }
        }
    return -1;
}

/* The window over the whole of VRAM and the new MMIO: after every mode set,
 * which leaves them as the BIOS likes. CR58's size field: 2 for 2 MB, 3 for 4 MB. */
static void open_window(void)
{
    unlock();
    virge_cr_write(VIRGE_CR_EXT_MISC, (uint8_t)(virge_cr_read(VIRGE_CR_EXT_MISC) | 0x01));
    virge_cr_write(VIRGE_CR_LAW_HI, (uint8_t)(virge.bar0 >> 24));
    virge_cr_write(VIRGE_CR_LAW_LO, (uint8_t)(virge.bar0 >> 16));
    virge_cr_write(VIRGE_CR_LAW_CTL, (uint8_t)((virge_cr_read(VIRGE_CR_LAW_CTL) & ~0x13) | 0x10 |
                                              (virge.vram_bytes > (2u << 20) ? 3 : 2)));
    virge_cr_write(VIRGE_CR_MMIO, (uint8_t)((virge_cr_read(VIRGE_CR_MMIO) & ~0x18) | 0x08));
}

int virge_map(void)
{
    if (!virge.bar0)
        return -1;
    open_window();
    virge.fb = (volatile uint8_t *)sys_map_phys(virge.bar0, virge.vram_bytes);
    virge.mmio = (volatile uint8_t *)sys_map_phys(virge.bar0 + VIRGE_MMIO_OFFSET, VIRGE_MMIO_SIZE);
    if (!virge.fb || !virge.mmio) {
        virge_unmap();
        return -1;
    }
    return 0;
}

void virge_unmap(void)
{
    if (virge.fb)
        sys_unmap_phys(virge.fb, virge.vram_bytes);
    if (virge.mmio)
        sys_unmap_phys(virge.mmio, VIRGE_MMIO_SIZE);
    virge.fb = virge.mmio = 0;
}

int virge_find_mode(int w, int h, int bpp, virge_mode *out)
{
    const uint16_t *p;
    uint16_t list[128];
    int n = 0, i;
    if (vbe_info() < 0)
        return -1;
    p = (const uint16_t *)sys_real_ptr((uint16_t)(vbuf[0x10] | (vbuf[0x11] << 8)),
                                       (uint16_t)(vbuf[0x0E] | (vbuf[0x0F] << 8)));
    while (p && *p != 0xFFFF && n < 128)
        list[n++] = *p++;
    for (i = 0; i < n; i++) {
        sys_rmregs r;
        uint8_t *b = vbuf + 512;
        int mbpp, model;
        memset(b, 0, 256);
        memset(&r, 0, sizeof r);
        r.eax = 0x4F01;
        r.ecx = list[i];
        r.es = vseg;
        r.edi = 512;
        if (vbe(&r) < 0 || (b[0] & 0x11) != 0x11)      /* supported, graphics */
            continue;
        mbpp = b[0x19];
        model = b[0x1B];
        if (mbpp == 16 && model == 6 && b[0x21] == 5)   /* 5 green bits: 15-bit colour */
            mbpp = 15;
        if ((b[0x12] | (b[0x13] << 8)) != w || (b[0x14] | (b[0x15] << 8)) != h || mbpp != bpp)
            continue;
        if (!((bpp == 8 && model == 4) || (bpp > 8 && model == 6)))
            continue;
        out->mode = list[i];
        out->width = (uint16_t)w;
        out->height = (uint16_t)h;
        out->bpp = (uint16_t)bpp;
        out->pitch_bytes = (uint16_t)(b[0x10] | (b[0x11] << 8));
        return 0;
    }
    return -1;
}

int virge_set_mode(const virge_mode *m)
{
    sys_rmregs r;
    virge_sync(100000);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = m->mode;
    if (vbe(&r) < 0)
        return -1;
    open_window();
    return 0;
}

void virge_text_mode(void)
{
    sys_rmregs r;
    virge_sync(100000);
    memset(&r, 0, sizeof r);
    r.eax = 0x0003;
    sys_rm_int(0x10, &r);
}
