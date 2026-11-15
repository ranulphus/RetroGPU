/* engine.c - drawing-engine initialisation, synchronisation, render
 * targets, clipping and solid fills. */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"
#include "mga/setup.h"

static mga_target cur;
uint32_t engine_resets, engine_timeouts;
uint32_t engine_regs_epoch;            /* (hal.h) */
unsigned setup_valid;                   /* (setup.h) */
int setup_stale, setup_wrote_texsize;   /* (setup.h) */

void setup_invalidate(void) { setup_valid = 0; }
void setup_forget_dwgctl(void) { setup_valid &= ~SETUP_VALID_DWG; }
const mga_target *engine_target = &cur;
static uint32_t cur_maccess;
static int cur_ydstorg_px;
static int clip_x0, clip_y0, clip_x1 = 0x7FFF, clip_y1 = 0x7FFF;
int mga_dma_pending;                    /* vertices buffered or in flight by DMA (warp.c; hal.h) */
int mga_warp_pending;                   /* WARP triangles since the engine was last idle (hal.h) */
uint32_t mga_warp_dwgctl;               /* the DWGCTL they were drawn with (setup_dwgctl; hal.h) */
void (*mga_dma_flush)(void);            /* sends and waits for them: register writes come after */
void (*mga_dma_kick)(void);             /* sends what is queued, without waiting (hal.h) */
void (*mga_reset_hook)(void);
int mga_cmd_open;                       /* the DMA command stream is taking register writes (mmio.h) */
int (*mga_cmd_route)(uint32_t off, uint32_t v);
uint32_t mga_opmode = MGA_OPMODE_UNKNOWN;   /* what OPMODE holds (ILOAD here, vertices in warp.c) */
static int pitch_reg = -1;         /* the value in PITCH: targets may differ in pitch */

/* Before a register write that triangles the WARPs are still setting up
 * would see (on a real G400 the next clip cut them): two zero-length draws,
 * as Linux's mga DRM puts before every clip on a G400 (mga_emit_clip_rect),
 * with the triangles' own DWGCTL as the DRM's are: the draws reach them too
 * (an untextured DWGCTL here spoiled GLQuake's on bench-g400; 86Box 0124
 * reports it). */
static void warp_barrier(void)
{
    const uint32_t dwg = mga_warp_dwgctl ? mga_warp_dwgctl
                                         : DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_SHFTZERO | DWG_BOP_COPY;
    if (!mga_warp_pending)
        return;
    fifo_reserve(4);
    MGA_WR32(MGAREG_DWGCTL, dwg);
    MGA_WR32(MGAREG_LEN + MGAREG_EXEC, 0x80000000u);
    MGA_WR32(MGAREG_DWGCTL, dwg);
    MGA_WR32(MGAREG_LEN + MGAREG_EXEC, 0x80000000u);
    setup_forget_dwgctl();
}

/* The clip registers alone: after a draw the host started behind
 * engine_warp_order, with no WARP triangle since (engine_vram_copy_end). */
static void clip_regs(void)
{
    uint32_t base = (uint32_t)cur_ydstorg_px;
    int x0 = clip_x0, y0 = clip_y0, x1 = clip_x1, y1 = clip_y1;
    if (x1 <= x0 || y1 <= y0) {
        x0 = 0; x1 = 0; y0 = 1; y1 = 0;      /* empty: nothing passes */
    }
    fifo_reserve(3);
    MGA_WR32(MGAREG_CXBNDRY, ((uint32_t)(x1 - 1) << 16) | (uint32_t)x0);
    MGA_WR32(MGAREG_YTOP, (uint32_t)y0 * (uint32_t)cur.pitch_px + base);
    MGA_WR32(MGAREG_YBOT, (uint32_t)(y1 - 1) * (uint32_t)cur.pitch_px + base);
}

static void emit_clip(void)
{
    warp_barrier();
    clip_regs();
}

static uint32_t maccess_extra;     /* caller-owned bits: NODITHER, FOGEN */

static uint32_t maccess_for(int bpp, int zbits)
{
    uint32_t m = (bpp == 32 ? MACCESS_PW32 : bpp == 8 ? MACCESS_PW8 : MACCESS_PW16) | maccess_extra;
    if (zbits == 32)
        m |= MACCESS_ZW32;
    return m;
}

void engine_init(int pitch_px, int bpp)
{
    engine_regs_epoch++;
    setup_invalidate();                     /* after a reset or a mode set nothing is known */
    engine_sync(200000);
    fifo_reset();
    fifo_reserve(10);
    MGA_WR32(MGAREG_PITCH, (uint32_t)pitch_px);
    if (mga.has_ydstorg)
        MGA_WR32(MGAREG_YDSTORG, 0);
    maccess_extra = 0;
    cur_maccess = maccess_for(bpp, 16);
    MGA_WR32(MGAREG_MACCESS, cur_maccess);
    MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
    MGA_WR32(MGAREG_FCOL, 0);
    MGA_WR32(MGAREG_BCOL, 0);
    MGA_WR32(MGAREG_CXBNDRY, 0xFFFF0000u);
    MGA_WR32(MGAREG_YTOP, 0);
    MGA_WR32(MGAREG_YBOT, 0x007FFFFFu);
    if (mga.has_dstorg) {
        fifo_reserve(2);
        MGA_WR32(MGAREG_SRCORG, 0);
        MGA_WR32(MGAREG_DSTORG, 0);
    }
    /* Chips that blend apply ALPHACTRL to every 3D trapezoid, and its reset
     * value (zero x source + zero x destination) draws black: start opaque. */
    if (mga.has_alpha_blend) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_ALPHACTRL, ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) |
                                   ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
    }
    if (mga.has_texctl2) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_TEXCTL2, 0);
    }
    cur.pitch_px = pitch_px;
    pitch_reg = pitch_px;
    cur.bpp = bpp;
    cur.zbits = 16;
    cur.color_off = cur.z_off = 0;
    cur_ydstorg_px = 0;
    mga_opmode = MGA_OPMODE_UNKNOWN;
}

/* WARP triangles the drawing engine may not have had yet: the WARPs set
 * them up after the bus FIFO has emptied, and STATUS can read idle before
 * they are drawn (on a real G400 a framebuffer read after them found the
 * frame still clear). A fence: DWGSYNC takes the value written to it only
 * once the drawing engine has finished the primitives sent before it. */
static uint32_t fence_tag;
static int fence_out;
uint32_t engine_timeout_diag[8];        /* the first timeout: STATUS, FIFOSTATUS, DWGSYNC, the tag awaited,
                                           PRIMADDRESS, PRIMEND, WIADDRNB, WIADDRNB1 */

/* The tags DWGSYNC is given, the fence's and the WARP's DMA kicks' (warp.c)
 * alike: one sequence, so a later tag passes an earlier one. */
uint32_t mga_dwgsync_next(void)
{
    fence_tag += 4u;
    return fence_tag;
}

uint32_t mga_dwgsync_last(void)
{
    return fence_tag;
}

static void warp_fence(void)
{
    if (!mga_warp_pending || !mga.has_dwgsync || fence_out)
        return;
    fifo_reserve(1);
    MGA_WR32(MGAREG_DWGSYNC, mga_dwgsync_next());
    fence_out = 1;
}

/* On bench-g400 a DWGSYNC write after WARP vertices is now and then never
 * taken: it keeps the tag before for good, with the bus FIFO empty, the
 * drawing engine idle and the DMA ended (docs/g400-warp.md). A fence still
 * not taken once all of that has held for FENCE_GRACE_US is let go
 * (engine_fence_stuck counts them) rather than timing out into a reset. */
#define FENCE_GRACE_US 20000u
#define FENCE_GRACE_POLLS 4096u         /* (and as many looks: some ports' clocks tick every 55 ms) */
uint32_t engine_fence_stuck;
static uint32_t stuck_since, stuck_polls;
static int stuck_timing;

static int fence_passed(void)
{
    uint32_t st;
    if (!fence_out || (int32_t)(MGA_RD32(MGAREG_DWGSYNC) - fence_tag) >= 0) {
        stuck_timing = 0;
        return 1;
    }
    st = MGA_RD32(MGAREG_STATUS);
    if (!(MGA_RD32(MGAREG_FIFOSTATUS) & (1u << 9)) || (st & STATUS_DWGENGSTS) || !(st & (1u << 17))) {
        stuck_timing = 0;               /* (something is still going on) */
        return 0;
    }
    if (!stuck_timing) {
        stuck_timing = 1;
        stuck_since = sys_time_us();
        stuck_polls = 0;
        return 0;
    }
    if (++stuck_polls < FENCE_GRACE_POLLS || (uint32_t)(sys_time_us() - stuck_since) < FENCE_GRACE_US)
        return 0;
    stuck_timing = 0;
    engine_fence_stuck++;
    return 1;
}

int engine_sync(uint32_t timeout_us)
{
    warp_fence();
    if (mga_dma_pending && mga_dma_flush)
        mga_dma_flush();
    if (timeout_us < 120000u)
        timeout_us = 120000u;          /* at least two BIOS ticks */
    uint32_t start = sys_time_us(), polls = 0;
    /* Idle means the bus FIFO is empty (FIFOSTATUS.bempty, bit 9) and the
     * drawing engine is not busy; commands can still be queued while the
     * engine reports idle. */
    while (!(MGA_RD32(MGAREG_FIFOSTATUS) & (1u << 9)) || (MGA_RD32(MGAREG_STATUS) & STATUS_DWGENGSTS) ||
           !fence_passed()) {
        if ((++polls & 1023) == 0 && (uint32_t)(sys_time_us() - start) > timeout_us) {
            if (!engine_timeouts) {     /* the first one's state, for the log */
                engine_timeout_diag[0] = MGA_RD32(MGAREG_STATUS);
                engine_timeout_diag[1] = MGA_RD32(MGAREG_FIFOSTATUS);
                engine_timeout_diag[2] = MGA_RD32(MGAREG_DWGSYNC);
                engine_timeout_diag[3] = fence_out ? fence_tag : 0;
                engine_timeout_diag[4] = MGA_RD32(MGAREG_PRIMADDRESS);
                engine_timeout_diag[5] = MGA_RD32(MGAREG_PRIMEND);
                engine_timeout_diag[6] = MGA_RD32(MGAREG_WIADDRNB);
                engine_timeout_diag[7] = MGA_RD32(MGAREG_WIADDRNB1);
            }
            engine_timeouts++;
            engine_reset();
            return -1;
        }
    }
    /* Flush the read cache before the CPU touches VRAM (writing the CRTC
     * index register is the documented way). */
    MGA_WR8(MGAREG_CRTC_INDEX, 0);
    fifo_reset();
    mga_warp_pending = 0;
    fence_out = 0;
    return 0;
}

/* A fence after everything queued so far (hal.h): DWGSYNC takes the tag
 * when the drawing engine has finished what came before it. Queued in the
 * WARPs' DMA list while it is open, and the list handed to the chip, so the
 * fence doesn't wait for the next frame's vertices to be sent. */
uint32_t engine_fence(void)
{
    uint32_t tag;
    if (!mga.has_dwgsync)
        return 0;
    tag = mga_dwgsync_next();
    fifo_reserve(1);
    MGA_WR32(MGAREG_DWGSYNC, tag);
    if (mga_cmd_open && mga_dma_pending && mga_dma_kick)
        mga_dma_kick();
    return tag;
}

int engine_fence_passed(uint32_t tag)
{
    return !tag || (int32_t)(MGA_RD32(MGAREG_DWGSYNC) - tag) >= 0;
}

/* A fence not taken within FENCE_WAIT_US (the G400's DWGSYNC that now and
 * then never takes its tag, fence_passed; or one lost to a reset): the
 * caller waits for everything instead, counted in engine_fence_stuck. */
#define FENCE_WAIT_US 50000u
int engine_fence_wait(uint32_t tag, uint32_t timeout_us)
{
    uint32_t start = sys_time_us(), polls = 0;
    if (!tag)
        return engine_sync(timeout_us);
    while ((int32_t)(MGA_RD32(MGAREG_DWGSYNC) - tag) < 0)
        if ((++polls & 255) == 0 && (uint32_t)(sys_time_us() - start) > FENCE_WAIT_US) {
            engine_fence_stuck++;
            return engine_sync(timeout_us);
        }
    return 0;
}

/* Non-blocking: nothing queued in the bus FIFO and the drawing engine not
 * busy (engine_sync's condition), for callers with other work to do while
 * they wait (DOS-GL's swap, which lets a program's audio thread run). */
int engine_idle(void)
{
    warp_fence();
    if (mga_dma_pending && mga_dma_flush)
        mga_dma_flush();
    if ((MGA_RD32(MGAREG_FIFOSTATUS) & (1u << 9)) && !(MGA_RD32(MGAREG_STATUS) & STATUS_DWGENGSTS) && fence_passed()) {
        mga_warp_pending = 0;
        fence_out = 0;
        return 1;
    }
    return 0;
}

/* The engine from another owner (GLOS's GPU lock changing hands): WARP
 * triangles its copy of the HAL sent may still be in flight, which this
 * copy's mga_warp_pending knows nothing of, and STATUS can read idle before
 * they are drawn (a program's in a GLOS window landed on GLOS's target on
 * bench-g400). On the G400 family, once the other owner's DMA has ended
 * (STATUS<17>, as the DRM's idle test) and the bus FIFO is empty: a fence
 * with a tag far from any the other owner gave DWGSYNC. Then as engine_sync. */
int engine_take(uint32_t timeout_us)
{
    if (mga.family == MGA_FAMILY_G400 && mga.has_dwgsync) {
        uint32_t start = sys_time_us(), polls = 0;
        while (!(MGA_RD32(MGAREG_STATUS) & (1u << 17)) || !(MGA_RD32(MGAREG_FIFOSTATUS) & (1u << 9)))
            if ((++polls & 1023) == 0 && (uint32_t)(sys_time_us() - start) > timeout_us)
                break;                  /* (engine_sync times out too, and resets) */
        fence_tag = (MGA_RD32(MGAREG_DWGSYNC) & ~3u) + 0x40000000u;
        fence_out = 0;
        mga_warp_pending = 1;
    }
    return engine_sync(timeout_us);
}

/* Another program has had the engine (GLOS's GPU lock changing hands):
 * nothing this copy of the HAL knows about the registers holds any more.
 * The FIFO's free slots are read again, OPMODE and the pitch written
 * before they are next relied on, the set-up's shadows dropped, and the
 * target, clip and MACCESS written now. */
void engine_resync(void)
{
    engine_regs_epoch++;
    fifo_reset();
    setup_invalidate();
    mga_opmode = MGA_OPMODE_UNKNOWN;
    pitch_reg = -1;
    fifo_reserve(1);
    MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);    /* (every HAL path writes all planes) */
    engine_set_target(&cur);
    emit_clip();
}

void engine_reset(void)
{
    mga_engine_state s;
    engine_resets++;
    engine_save(&s);
    MGA_WR32(MGAREG_RST, 1);
    sys_delay_us(20);
    MGA_WR32(MGAREG_RST, 0);
    fifo_reset();
    fence_out = 0;                      /* (nothing drawn is waited for any more) */
    stuck_timing = 0;
    mga_warp_pending = 0;
    if (mga_reset_hook)
        mga_reset_hook();
    engine_init(cur.pitch_px ? cur.pitch_px : 1024, cur.bpp ? cur.bpp : 16);
    if (s.t.pitch_px && s.t.bpp)
        engine_restore(&s);             /* (engine_init leaves the target at offset 0) */
}

static void write_origin(uint32_t color_off, int bpp, uint32_t z_off, int zbits)
{
    uint32_t bpp_bytes = (uint32_t)bpp / 8, zbytes = (uint32_t)zbits / 8;
    if (mga.has_dstorg) {
        fifo_reserve(2);
        MGA_WR32(MGAREG_DSTORG, color_off);
        MGA_WR32(MGAREG_ZORG, z_off);
        cur_ydstorg_px = 0;
    } else {
        /* G100: the origin is YDSTORG in pixels; the Z address is computed
         * from the same linear pixel index, so ZORG compensates. */
        cur_ydstorg_px = (int)(color_off / bpp_bytes);
        fifo_reserve(2);
        MGA_WR32(MGAREG_YDSTORG, (uint32_t)cur_ydstorg_px);
        if (mga.zorg_ydst_relative)
            MGA_WR32(MGAREG_ZORG, z_off - (uint32_t)cur_ydstorg_px * zbytes);
        else
            MGA_WR32(MGAREG_ZORG, z_off);
        emit_clip();        /* YTOP/YBOT are linear addresses including YDSTORG */
    }
}

void engine_set_target(const mga_target *t)
{
    int repitch = t->pitch_px != pitch_reg;
    cur = *t;
    cur_maccess = maccess_for(t->bpp, t->zbits);
    if (repitch) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_PITCH, (uint32_t)t->pitch_px);
        pitch_reg = t->pitch_px;
    }
    write_origin(t->color_off, t->bpp, t->z_off, t->zbits);
    fifo_reserve(1);
    MGA_WR32(MGAREG_MACCESS, cur_maccess);
    if (repitch && mga.has_dstorg)
        emit_clip();        /* YTOP/YBOT are y * pitch (write_origin does it for YDSTORG chips) */
}

void engine_save(mga_engine_state *s)
{
    s->t = cur;
    s->clip[0] = clip_x0; s->clip[1] = clip_y0; s->clip[2] = clip_x1; s->clip[3] = clip_y1;
    s->maccess_flags = maccess_extra;
}

void engine_restore(const mga_engine_state *s)
{
    maccess_extra = s->maccess_flags;               /* engine_set_target writes MACCESS with it */
    clip_x0 = s->clip[0]; clip_y0 = s->clip[1]; clip_x1 = s->clip[2]; clip_y1 = s->clip[3];
    engine_set_target(&s->t);
    emit_clip();
}

/* Extra MACCESS bits kept across retargets and depth fills; written only
 * when they change. */
void engine_set_maccess_flags(uint32_t flags)
{
    if (flags == maccess_extra)
        return;
    maccess_extra = flags;
    cur_maccess = maccess_for(cur.bpp, cur.zbits);
    fifo_reserve(1);
    MGA_WR32(MGAREG_MACCESS, cur_maccess);
}

void engine_set_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = x0; clip_y0 = y0; clip_x1 = x1; clip_y1 = y1;
    emit_clip();
}

/* Before a draw the host starts (fills, blits, loads), and before the state
 * it writes for one, while WARP triangles may still be in flight: two
 * DWGSYNC writes, as Linux's mga DRM puts before its clears, blits and
 * loads (mga_dma_dispatch_clear, _blit), with tags from the fence's
 * sequence. Not warp_barrier: on bench-g400 its zero-length draws' DWGCTL
 * reached triangles the WARPs were still setting up (GLQuake -mtex, frame
 * 600 of demo1: 1,056 pixels of a pillar unlike DGL_TEXMOVE=0's), while
 * with the DWGSYNC writes alone every frame matched. */
void engine_warp_order(void)
{
    if (!mga_warp_pending || !mga.has_dwgsync)
        return;
    fifo_reserve(2);
    MGA_WR32(MGAREG_DWGSYNC, mga_dwgsync_next());
    MGA_WR32(MGAREG_DWGSYNC, mga_dwgsync_next());
}

static void solid_fill(int x, int y, int w, int h, uint32_t fcol)
{
    if (w <= 0 || h <= 0)
        return;
    engine_warp_order();
    fifo_reserve(4);
    setup_forget_dwgctl();
    MGA_WR32(MGAREG_DWGCTL, DWG_OPCOD_TRAP | DWG_ATYPE_RSTR | DWG_SOLID | DWG_ARZERO |
                            DWG_SGNZERO | DWG_SHFTZERO | DWG_BOP_COPY | DWG_TRANSC |
                            DWG_BLTMOD_BMONOLEF);
    MGA_WR32(MGAREG_FCOL, fcol);
    MGA_WR32(MGAREG_FXBNDRY, ((uint32_t)(x + w) << 16) | ((uint32_t)x & 0xFFFFu));   /* (x may be negative) */
    MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)y << 16) | (uint32_t)h);
}

/* A pixel value as the colour registers take it: repeated to fill 32 bits. */
uint32_t engine_colour(uint32_t value)
{
    if (cur.bpp == 8)
        return (value & 0xFF) * 0x01010101u;
    if (cur.bpp == 16)
        return (value & 0xFFFF) | (value << 16);
    return value;
}

void engine_fill(int x, int y, int w, int h, uint32_t value)
{
    solid_fill(x, y, w, h, engine_colour(value));
}

void engine_fill_depth(int x, int y, int w, int h, uint32_t zvalue)
{
    int zb = cur.zbits;
    uint32_t fcol = zb == 16 ? ((zvalue & 0xFFFF) | (zvalue << 16)) : zvalue;
    if (!cur.z_off && !mga.has_dstorg)
        return;
    /* Retarget the colour path at the depth buffer, fill, and restore. */
    write_origin(cur.z_off, zb, cur.z_off, zb);
    fifo_reserve(1);
    MGA_WR32(MGAREG_MACCESS, (zb == 32 ? MACCESS_PW32 : MACCESS_PW16) | MACCESS_NODITHER);
    solid_fill(x, y, w, h, fcol);
    write_origin(cur.color_off, cur.bpp, cur.z_off, cur.zbits);
    fifo_reserve(1);
    MGA_WR32(MGAREG_MACCESS, cur_maccess);
}

int engine_in_vblank(void)
{
    return (MGA_RD8(MGAREG_INSTS1) & 0x08) != 0;
}

uint32_t engine_vcount(void)
{
    return MGA_RD32(MGAREG_VCOUNT) & 0xFFF;
}

int engine_vsync_wait(uint32_t timeout_us)
{
    if (timeout_us < 120000u)
        timeout_us = 120000u;
    uint32_t start = sys_time_us();
    while (engine_in_vblank())
        if ((uint32_t)(sys_time_us() - start) > timeout_us)
            return -1;
    while (!engine_in_vblank())
        if ((uint32_t)(sys_time_us() - start) > timeout_us)
            return -1;
    return 0;
}

/* Load the texture lookup table that TW8 (and TW4) texels index, entries
 * first..first+count-1, from count RGB565 values at VRAM offset off: the
 * BITBLT recipe of the G200 and G400 specifications (atype RSTR, linear
 * source at AR3..AR0, MACCESS.tlutload; docs/g400-dual-texture.md §6).
 * Queued like a draw, so draws before it keep the old table. */
void engine_tlut_load(uint32_t off, int first, int count)
{
    uint32_t src = off / 2;
    if (count <= 0)
        return;
    engine_warp_order();
    fifo_reserve(9);
    MGA_WR32(MGAREG_MACCESS, MACCESS_PW16 | MACCESS_TLUTLOAD);
    MGA_WR32(MGAREG_PITCH, 1024);
    if (mga.has_dstorg) {
        MGA_WR32(MGAREG_DSTORG, 0);
        MGA_WR32(MGAREG_SRCORG, 0);
    } else
        MGA_WR32(MGAREG_YDSTORG, 0);
    MGA_WR32(MGAREG_AR0, src + (uint32_t)count - 1);
    MGA_WR32(MGAREG_AR3, src);
    MGA_WR32(MGAREG_FXBNDRY, 0);
    setup_forget_dwgctl();
    MGA_WR32(MGAREG_DWGCTL, 0x0E0C6098u);          /* BITBLT, RSTR, linear, SRC, sgnzero, shftzero */
    MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)first << 16) | (uint32_t)count);
    fifo_reserve(1);
    MGA_WR32(MGAREG_PITCH, (uint32_t)cur.pitch_px);
    pitch_reg = cur.pitch_px;
    engine_set_target(&cur);                        /* origins, MACCESS */
}

/* ILOAD (G400 specification §4.5.8; docs/matrox-2d.md): data from the CPU
 * written through the DMA window in blit mode. The engine takes exactly the
 * pixels it expects; fewer would hang it and more would be read as register
 * writes, so engine_iload_end pads any rows the caller did not send. The
 * specifications fix atype at RSTR on the G200 and G400 (with bop S it reads
 * nothing back). */
static int iload_row_dw, iload_rows;
static uint32_t dmawin_pos;

static void dma_blit_mode(void)
{
    if (mga_opmode != OPMODE_DMAMOD_BLIT) {
        /* OPMODE is not queued behind drawing, and on a G400 it may not
         * leave general purpose while the WARPs still work: after the list
         * had only been fetched, the loads came out as without iload_done. */
        engine_sync(200000);
        MGA_WR32(MGAREG_OPMODE, OPMODE_DMAMOD_BLIT);
        mga_opmode = OPMODE_DMAMOD_BLIT;
    }
}

/* Start an ILOAD into the target the registers hold: columns xl..xr of h
 * rows from y, AR0 as the kind of load needs it; rows of row_dw dwords
 * follow (engine_iload_row). DWGCTL goes first: the G100 forbids writing
 * the AR registers while the last DWGCTL's arzero is set (a fill's is). */
static void iload_go(uint32_t dwgctl, int xl, int xr, int y, int h, uint32_t ar0, int row_dw)
{
    /* The load starts by direct writes, never from the WARP's DMA list
     * (warp.c): the list carries DMAPAD packets after it, and a real G400
     * takes a DMAPAD written during an ILOAD as the load's data, as 86Box
     * does (DOS-GL's t29 on bench-g400: the texels landed shifted). The
     * list is sent first, and the data would have waited for that anyway. */
    if (mga_dma_pending && mga_dma_flush)
        mga_dma_flush();
    fifo_reserve(6);
    setup_forget_dwgctl();
    MGA_WR32_DIRECT(MGAREG_DWGCTL, dwgctl);
    MGA_WR32_DIRECT(MGAREG_AR0, ar0);
    MGA_WR32_DIRECT(MGAREG_AR3, 0);
    MGA_WR32_DIRECT(MGAREG_AR5, 0);
    MGA_WR32_DIRECT(MGAREG_FXBNDRY, ((uint32_t)xr << 16) | ((uint32_t)xl & 0xFFFFu));
    MGA_WR32_DIRECT(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)y << 16) | (uint32_t)h);
    iload_row_dw = row_dw;
    iload_rows = h;
}

/* After a load, with the WARPs' DMA stream open (warp.c): OPMODE back to
 * general purpose, which the stream's DMA assumes (warp_select sets it once
 * at a pipe's start). On bench-g400, loads that left it in blit mode among
 * the WARPs' DMA spoiled GLQuake's lightmap updates (demo1: rows of wrong
 * texels, more as the demo went on, then triangles across the screen).
 * Waiting for the engine before or after each load changed nothing; with
 * OPMODE put back, every frame matched the CPU's writes (DGL_ILOAD=0).
 * 86Box reads OPMODE for the DMA window only. The next load sets blit mode
 * again (dma_blit_mode). Going back needed no wait on the card (none, or
 * the bus FIFO empty: frames exact, no faster); the sync stays, as for any
 * OPMODE write. */
static void iload_done(void)
{
    if (mga_cmd_open && mga_opmode != 0) {
        engine_sync(200000);                         /* OPMODE is not queued behind drawing */
        MGA_WR32(MGAREG_OPMODE, 0);
        mga_opmode = 0;
    }
}

#define ILOAD_FULL (DWG_OPCOD_ILOAD | DWG_ATYPE_RSTR | DWG_SGNZERO | DWG_SHFTZERO | DWG_BOP_COPY | DWG_BLTMOD_BFCOL)

/* Another surface than the target, without clipping (clipdis, and the clip
 * registers opened for chips that ignore it: they are linear addresses in
 * the render target's pitch). */
int engine_iload_begin(uint32_t off, int pitch_px, int bpp, int x, int y, int w, int h)
{
    int align = bpp == 8 ? 63 : 31, dw = (w * bpp + 31) / 32;
    if (!mga.has_dstorg || (bpp != 8 && bpp != 16) || w <= 0 || h <= 0 || (off & 63) ||
        pitch_px <= 0 || pitch_px > 4096 || (pitch_px & align) || x < 0 || x + w > pitch_px)
        return 0;
    dma_blit_mode();
    fifo_reserve(6);
    MGA_WR32(MGAREG_DSTORG, off);
    MGA_WR32(MGAREG_PITCH, (uint32_t)pitch_px);
    pitch_reg = pitch_px;
    MGA_WR32(MGAREG_MACCESS, bpp == 8 ? MACCESS_PW8 : MACCESS_PW16);
    MGA_WR32(MGAREG_CXBNDRY, 0x0FFF0000u);
    MGA_WR32(MGAREG_YTOP, 0);
    MGA_WR32(MGAREG_YBOT, 0x00FFFFFFu);
    iload_go(ILOAD_FULL | DWG_CLIPDIS, x, x + w - 1, y, h, (uint32_t)w - 1, dw);
    return dw;
}

int engine_image_begin(int x, int y, int w, int h)
{
    int dw = (w * cur.bpp + 31) / 32;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return 0;
    dma_blit_mode();
    iload_go(ILOAD_FULL, x, x + w - 1, y, h, (uint32_t)w - 1, dw);
    return dw;
}

/* Colour expansion from the CPU. Its source is linear (the specifications
 * allow no other): one stream of bits, rows not padded. So the destination
 * is made as wide as rows padded to whole dwords, and the clip cuts the
 * padding off (X.org's way). AR0 counts the pixels of the whole load: in
 * parts of at most 2^17 (18 bits, signed, on the G100 and G200). */
void engine_mono(int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg, uint32_t bg,
                 unsigned flags)
{
    uint32_t row[128], dwg = DWG_OPCOD_ILOAD | DWG_ATYPE_RSTR | DWG_LINEAR | DWG_SGNZERO | DWG_SHFTZERO |
                             DWG_BOP_COPY | DWG_BLTMOD_BMONOWF | (flags & MGA_MONO_TRANSPARENT ? DWG_TRANSC : 0);
    int ndw = (w + 31) / 32, wp = ndw * 32, nb = (w + 7) / 8, most = 131071 / (ndw * 32), done, n, i, j, k;
    int cl = clip_x0 > x ? clip_x0 : x, cr = clip_x1 < x + w ? clip_x1 : x + w;
    if (w <= 0 || h <= 0 || w > 4096 || cr <= cl || clip_y1 <= clip_y0)
        return;
    dma_blit_mode();
    fifo_reserve(3);
    MGA_WR32(MGAREG_CXBNDRY, ((uint32_t)(cr - 1) << 16) | (uint32_t)cl);
    MGA_WR32(MGAREG_FCOL, engine_colour(fg));
    MGA_WR32(MGAREG_BCOL, engine_colour(bg));
    for (done = 0; done < h; done += n) {
        n = h - done < most ? h - done : most;
        iload_go(dwg, x, x + wp - 1, y + done, n, (uint32_t)(wp * n - 1), ndw);
        for (j = 0; j < n; j++) {
            const uint8_t *b = bits + (long)(done + j) * stride;
            for (i = 0; i < ndw; i++) {
                uint32_t d = 0;
                for (k = 0; k < 4 && 4 * i + k < nb; k++)
                    d |= (uint32_t)b[4 * i + k] << (8 * k);
                row[i] = d;
            }
            engine_iload_row(row);
        }
    }
    iload_done();
    emit_clip();
}

void engine_iload_row(const uint32_t *d)
{
    int i = 0, n = iload_row_dw;
    if (iload_rows <= 0)
        return;
    iload_rows--;
    while (i < n) {
        int k = n - i < mga.fifo_depth ? n - i : mga.fifo_depth;
        fifo_reserve(k);
        for (; k > 0; k--, i++) {
            MGA_WR32(MGAREG_DMAWIN + dmawin_pos, d ? d[i] : 0);
            dmawin_pos = dmawin_pos + 4 < MGA_DMAWIN_SIZE ? dmawin_pos + 4 : 0;
        }
    }
}

void engine_iload_end(void)
{
    while (iload_rows > 0)
        engine_iload_row(NULL);
    iload_done();
    fifo_reserve(1);
    MGA_WR32(MGAREG_PITCH, (uint32_t)cur.pitch_px);
    pitch_reg = cur.pitch_px;
    engine_set_target(&cur);                        /* DSTORG, ZORG, MACCESS */
    emit_clip();
}

/* Copies from one surface in VRAM to another, queued like draws (G200+):
 * DOS-GL changes a texture that queued draws still read in a new block,
 * the CPU writing the new texels there and these copies the rest from the
 * old block, with no wait for the engine. A copy's rows are pitch_px pixels
 * apart in both surfaces; dst and src are its first pixel's byte addresses.
 * DSTORG and SRCORG are rounded down to 64 bytes (the G400's units) and the
 * rest goes in the columns and AR3. Between the first copy and
 * engine_vram_copy_end the registers hold the copies' surface;
 * engine_vram_copy_end writes the target back (PITCH, origins, MACCESS,
 * the clip). PLNWT is left all ones. */
static int vcopy_open;

int engine_vram_copy(uint32_t dst, uint32_t src, int pitch_px, int bpp, int w, int h)
{
    uint32_t b = (uint32_t)bpp / 8, sp = (uint32_t)pitch_px, W = (uint32_t)w - 1, x0 = (dst & 63u) / b,
             room = mga.family == MGA_FAMILY_G400 ? (1u << 21) - 1 : (1u << 17) - 1;   /* what AR0 can hold */
    int band, done, n;
    if (!mga.has_dstorg || (bpp != 8 && bpp != 16 && bpp != 32) || pitch_px <= 0 || pitch_px > 4096 ||
        ((uint32_t)pitch_px * b) & 63u || dst % b || src % b || w > 4096 - (int)x0)
        return 0;
    if (w <= 0 || h <= 0)
        return 1;
    if (!vcopy_open) {
        engine_regs_epoch++;                    /* (PLNWT) */
        engine_warp_order();                    /* (DSTORG, PITCH, MACCESS and DWGCTL change) */
        fifo_reserve(4);
        MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
        MGA_WR32(MGAREG_CXBNDRY, 0x0FFF0000u);      /* (for chips that ignore clipdis) */
        MGA_WR32(MGAREG_YTOP, 0);
        MGA_WR32(MGAREG_YBOT, 0x00FFFFFFu);
        vcopy_open = 1;
    }
    fifo_reserve(6);
    setup_forget_dwgctl();
    MGA_WR32(MGAREG_DWGCTL, DWG_OPCOD_BITBLT | DWG_ATYPE_RSTR | DWG_SGNZERO | DWG_SHFTZERO | DWG_BOP_COPY |
                            DWG_BLTMOD_BFCOL | DWG_CLIPDIS);
    MGA_WR32(MGAREG_MACCESS, b == 4 ? MACCESS_PW32 : b == 2 ? MACCESS_PW16 : MACCESS_PW8);
    MGA_WR32(MGAREG_PITCH, sp);
    pitch_reg = pitch_px;
    MGA_WR32(MGAREG_DSTORG, dst & ~63u);
    MGA_WR32(MGAREG_AR5, sp);
    MGA_WR32(MGAREG_FXBNDRY, ((x0 + W) << 16) | x0);
    band = (int)((room - 63 / b - W) / sp) + 1;     /* rows whose addresses fit AR0 (blit.c) */
    for (done = 0; done < h; done += n) {
        uint32_t at = src + (uint32_t)done * sp * b, first = (at & 63u) / b;
        n = h - done < band ? h - done : band;
        fifo_reserve(4);
        MGA_WR32(MGAREG_SRCORG, at & ~63u);
        MGA_WR32(MGAREG_AR0, first + W);
        MGA_WR32(MGAREG_AR3, first);
        MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)done << 16) | (uint32_t)n);
    }
    return 1;
}

/* bytes bytes from src to dst (not overlapping), as rows of 32 pixels (64
 * at 8 bits) of the widest pixel the addresses and the length allow. */
int engine_vram_move(uint32_t dst, uint32_t src, uint32_t bytes)
{
    uint32_t a = dst | src | bytes, b = !(a & 3u) ? 4u : !(a & 1u) ? 2u : 1u, px = b == 1 ? 64u : 32u,
             row = px * b, rows = bytes / row, rest = bytes % row;
    return engine_vram_copy(dst, src, (int)px, (int)b * 8, (int)px, (int)rows) &&
           engine_vram_copy(dst + rows * row, src + rows * row, (int)px, (int)b * 8, (int)(rest / b), 1);
}

void engine_vram_copy_end(void)
{
    if (!vcopy_open)
        return;
    vcopy_open = 0;
    fifo_reserve(1);
    MGA_WR32(MGAREG_PITCH, (uint32_t)cur.pitch_px);
    pitch_reg = cur.pitch_px;
    engine_set_target(&cur);                        /* DSTORG, ZORG, MACCESS */
    /* Without warp_barrier: the copies began with engine_warp_order, as the
     * DRM's blits do, and no WARP triangle has come since. On bench-g400
     * its zero-length draws with the triangles' textured DWGCTL, right
     * after copies into a texture's new block, spoiled that texture's
     * first texels for the triangles after (t32: three rows of level 0
     * showed level 1's; correct with an untextured DWGCTL, with no barrier
     * here, or with DGL_TEXMOVE=0). */
    clip_regs();
}
