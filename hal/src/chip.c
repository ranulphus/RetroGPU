/* chip.c - per-chip capabilities and VRAM sizing. */
#include "mga/hal.h"
#include "mga/sys.h"

void mga_chip_caps(mga_chip *c)
{
    c->min_tex_dim = 8;
    switch (c->family) {
    case MGA_FAMILY_G100:
        c->fifo_depth = 64; c->fifo_mask = 0x7F;
        c->max_mip_levels = 1; c->max_tex_size = 2048;
        c->ar_bits = 18;
        c->has_ydstorg = 1; c->zorg_ydst_relative = 1;
        c->fog_textrap_only = 1; c->alphasel_tex_always = 1;
        break;
    case MGA_FAMILY_G200:
    case MGA_FAMILY_G200E:
        c->fifo_depth = 64; c->fifo_mask = 0x7F;
        c->max_mip_levels = 5; c->max_tex_size = 2048;
        c->ar_bits = 18;
        c->has_dwgsync = 1; c->has_dstorg = 1; c->has_ydstorg = 1;
        c->has_texctl2 = 1; c->has_alpha_blend = 1; c->has_alpha_test = 1;
        c->has_tlut = 1; c->has_specular = 1; c->has_decalblend = 1;
        /* Bilinear filtering at texel centres, as the G400's: DOS-GL's
         * conformance passes 32 of 32 on a real G200 only so (bench-g400's PC,
         * 2026-11-03), where the half-texel shift for texel corners failed 5. */
        c->texel_centre = 1;
        break;
    case MGA_FAMILY_G400:
        c->fifo_depth = 16; c->fifo_mask = 0x1F;
        c->max_mip_levels = 11; c->max_tex_size = 2048;
        c->ar_bits = 22;
        c->has_dwgsync = 1; c->has_dstorg = 1;
        c->has_texctl2 = 1; c->has_alpha_blend = 1; c->has_alpha_test = 1;
        /* No TLUT: the G200's load (BITBLT RSTR) and TW8 draw black on a
         * G400 (bench-g400, G4EXP E5 under DGL_TLUT=1); paletted textures
         * are expanded, as the specification advises for 8-bit textures. */
        c->has_specular = 1; c->has_decalblend = 1;
        c->g400_clip_quirk = 1;
        c->has_dual_tex = 1;
        c->texel_centre = 1;
        break;
    default:
        break;
    }
}

/* Size VRAM by writing a signature at each megabyte and looking for
 * aliasing. Must run before anything useful is in VRAM. */
/* Each MB's first word written and read back, the first MB's unchanged (no
 * mirror). Between the write and the read: the CPU's write-combining buffers
 * drained (a locked instruction: GLOS maps the frame buffer write-combining)
 * and another value put on the bus by a write to memory that is there, or
 * memory that isn't reads back the last value on the bus, which was the
 * write's own: the real G100 in bench-g400 (4 MB, its BIOS says) measured
 * 16 MB, the aperture, so (2026-11-18). */
uint32_t mga_probe_vram(void)
{
    volatile uint32_t *fb = (volatile uint32_t *)mga_fb;
    uint32_t mb, max = mga.fb_size >> 20, v;
    fb[0] = 0x4D474130u;             /* "MGA0" */
    for (mb = 1; mb < max; mb++) {
        volatile uint32_t *p = (volatile uint32_t *)(mga_fb + (mb << 20));
        v = 0x4D470000u | mb;
        p[0] = v;
        fb[1] = ~v;                  /* (another value on the bus) */
#if defined(__GNUC__) && defined(__i386__)
        __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory");
#endif
        if (fb[0] != 0x4D474130u || p[0] != v || fb[1] != ~v)
            break;
    }
    return mb << 20;
}
