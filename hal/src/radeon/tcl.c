/* tcl.c - the transform engine of the RV200 (Radeon 7500) and R100 (7200), RADEON_HAS_TCL (gl/docs/dx7.md, X3), as
 * Mesa 7.0.3's radeon driver drives it (radeon_state_init.c's defaults, radeon_state.c's matrices and lights,
 * radeon_ioctl.c's packets) and as DTCL0 proved it on the P3: vertices in object coordinates fetched by the chip from
 * an array in video memory (3D_LOAD_VBPNTR), drawn by 3D_DRAW_VBUF or 3D_DRAW_INDX with TCL_ENABLE; the matrices in
 * the engine's vector memory; the viewport transform in the setup engine. Vertices inline in the stream
 * (3D_DRAW_IMMD) are not taken through the engine (DTCL0 immd), so there are none here. Needs the command
 * processor: every write here is a packet.
 *
 * radeon_tcl_on switches radeon_3d between the window-space vertices radeon_3d_init sets up (tri.c) and the
 * engine's; the engine's own registers (radeon_tcl) go out at radeon_tcl_emit, those that changed. */
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "radeon/regs3d_radeon.h"
#include <string.h>

#define SE_VPORT_XSCALE          0x1d98u
#define SE_TCL_VECTOR_INDX_REG   0x2200u
#define SE_TCL_VECTOR_DATA_REG   0x2204u
#define SE_TCL_SCALAR_INDX_REG   0x2208u
#define SE_TCL_SCALAR_DATA_REG   0x220cu
#define SE_TCL_OUTPUT_VTX_FMT    0x2254u
#define SE_TCL_STATE_FLUSH       0x2284u
#define VPORT_XY_XFORM_ENABLE    (1u << 24)
#define VPORT_Z_XFORM_ENABLE     (1u << 25)
#define VTX_W0_IS_NOT_1_OVER_W0  (1u << 16)
#define TEX1_W_ROUTING_USE_Q1    (1u << 26)
#define LOAD_VBPNTR              0x00002F00u
#define DRAW_VBUF                0x00002800u
#define DRAW_INDX                0x00002A00u
#define RNDR_GEN_INDX_PRIM       0x00002300u
#define VC_WALK_IND              0x00000010u
#define VC_WALK_LIST             0x00000020u
#define VC_MAOS_ENABLE           0x00000080u
#define VC_RADEON_MODE           0x00000100u
#define VC_TCL_ENABLE            0x00000200u

radeon_tcl_regs radeon_tcl;
static radeon_tcl_regs sent;
static int known, on, have_window;
int radeon_tcl_live;                    /* (on, for tri.c's test without a call) */
static uint32_t window_coord_fmt, window_cntl;

static uint32_t fbits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

void radeon_tcl_defaults(void)
{
    /* (radeonInitState's: x, y, z and w and a packed diffuse out; texture coordinates passed as they come; the
     * matrix slots MODEL_PROJ 0, MODEL 1, MODEL_IT 2, TEXMAT_0..2 3..5; clip planes in clip space) */
    memset(&radeon_tcl, 0, sizeof radeon_tcl);
    radeon_tcl.output_vtx_fmt = RADEON_TCL_VTX_Z0 | RADEON_TCL_VTX_W0 | RADEON_TCL_VTX_PK_DIFFUSE;
    radeon_tcl.output_vtx_sel = RADEON_TCL_COMPUTE_XYZW | 0u << RADEON_TCL_TEX_0_OUTPUT_SHIFT |
                                1u << RADEON_TCL_TEX_1_OUTPUT_SHIFT | 2u << RADEON_TCL_TEX_2_OUTPUT_SHIFT;
    radeon_tcl.matrix_select_0 = 1u << 0 | 2u << 16;
    radeon_tcl.matrix_select_1 = 0u << 0 | 3u << 16 | 4u << 20 | 5u << 24;
    radeon_tcl.ucp_vert_blend_ctl = RADEON_UCP_IN_CLIP_SPACE | RADEON_CULL_FRONT_IS_CCW;
    known = 0;
}

void radeon_tcl_on(int yes)
{
    if (yes == on)
        return;
    if (yes) {
        /* (the window-space words, radeon_3d_init's and what the caller set since, kept to go back to: the
         * engine's draws change SE_CNTL's culling and shading as they need) */
        window_coord_fmt = radeon_3d.se_coord_fmt;
        window_cntl = radeon_3d.se_cntl;
        have_window = 1;
        radeon_3d.se_coord_fmt = VTX_W0_IS_NOT_1_OVER_W0 | TEX1_W_ROUTING_USE_Q1;
        radeon_3d.se_cntl |= VPORT_XY_XFORM_ENABLE | VPORT_Z_XFORM_ENABLE;
    } else if (have_window) {
        radeon_3d.se_coord_fmt = window_coord_fmt;
        radeon_3d.se_cntl = window_cntl;
    }
    on = radeon_tcl_live = yes;
    radeon_3d_dirty = 1;
}

uint32_t radeon_tcl_window_cntl(void) { return on ? window_cntl : radeon_3d.se_cntl; }

int radeon_tcl_is_on(void) { return on; }

void radeon_tcl_forget(void)
{
    known = 0;
    have_window = 0;
    on = radeon_tcl_live = 0;
}

void radeon_tcl_emit(void)
{
    const uint32_t *now = (const uint32_t *)&radeon_tcl, *was = (const uint32_t *)&sent;
    int k, n = (int)(sizeof radeon_tcl / 4);
    if (radeon_3d_dirty)
        radeon_3d_emit();               /* (it settles first, 6) */
    for (k = 0; k < n; k++)                     /* (SE_TCL_OUTPUT_VTX_FMT on: the registers are consecutive) */
        if (!known || now[k] != was[k]) {
            if (radeon_tcl_sync == 6)
                radeon_tcl_settle();
            radeon_cp_reg(SE_TCL_OUTPUT_VTX_FMT + 4u * (uint32_t)k, now[k]);
        }
    sent = radeon_tcl;
    known = 1;
}

void radeon_tcl_viewport(const float v[6])
{
    int i;
    if (radeon_tcl_sync == 6)
        radeon_tcl_settle();
    for (i = 0; i < 6; i++)
        radeon_cp_reg(SE_VPORT_XSCALE + 4u * (uint32_t)i, fbits(v[i]));
}

/* n words to one register in turn (a type-0 packet with ONE_REG_WR) */
static void table(uint32_t reg, const uint32_t *v, int n)
{
    uint32_t *d;
    int i;
    while (n > 0) {
        int k = n > 256 ? 256 : n;
        d = radeon_cp_reserve(k + 1);
        if (!d)
            return;
        d[0] = RADEON_CP_PACKET0(reg, k - 1) | RADEON_CP_ONE_REG_WR;
        for (i = 0; i < k; i++)
            d[1 + i] = v[i];
        radeon_cp_advance(k + 1);
        v += k, n -= k;
    }
}

void radeon_tcl_vectors(unsigned start, unsigned stride, const float *v, int n)
{
    uint32_t w[64];
    int i;
    radeon_cp_reg(SE_TCL_STATE_FLUSH, 0);
    radeon_cp_reg(SE_TCL_VECTOR_INDX_REG, start | stride << 16);
    while (n > 0) {
        int k = n > 64 ? 64 : n;
        for (i = 0; i < k; i++)
            w[i] = fbits(v[i]);
        table(SE_TCL_VECTOR_DATA_REG, w, k);
        v += k, n -= k;
    }
}

void radeon_tcl_scalars(unsigned start, unsigned stride, const float *v, int n)
{
    uint32_t w[64];
    int i;
    radeon_cp_reg(SE_TCL_SCALAR_INDX_REG, start | stride << 16);
    while (n > 0) {
        int k = n > 64 ? 64 : n;
        for (i = 0; i < k; i++)
            w[i] = fbits(v[i]);
        table(SE_TCL_SCALAR_DATA_REG, w, k);
        v += k, n -= k;
    }
}

void radeon_tcl_matrix(int slot, const float m[16])
{
    float r[16];
    int i;
    for (i = 0; i < 4; i++)                     /* (Mesa's upload_matrix: GL's columns as the engine's rows) */
        r[i * 4 + 0] = m[i], r[i * 4 + 1] = m[i + 4], r[i * 4 + 2] = m[i + 8], r[i * 4 + 3] = m[i + 12];
    radeon_tcl_vectors((unsigned)slot * 4u, 1, r, 16);
}

void radeon_tcl_matrix_t(int slot, const float m[16])
{
    radeon_tcl_vectors((unsigned)slot * 4u, 1, m, 16);
}

void radeon_tcl_guard_band(void)
{
    static const float one[4] = { 1, 1, 1, 1 };     /* (radeonInitState's guard-band adjustments) */
    radeon_tcl_scalars(48, 1, one, 4);
}

void radeon_tcl_draw(uint32_t prim, uint32_t vfmt, uint32_t addr, int dwords, int n)
{
    uint32_t *d;
    radeon_tcl_emit();
    d = radeon_cp_reserve(7);
    if (!d)
        return;
    d[0] = RADEON_CP_PACKET3(LOAD_VBPNTR, 2);
    d[1] = 1;
    d[2] = (uint32_t)dwords << 8 | (uint32_t)dwords;    /* (stride, size) */
    d[3] = addr;
    d[4] = RADEON_CP_PACKET3(DRAW_VBUF, 1);
    d[5] = vfmt;
    d[6] = prim | VC_WALK_LIST | VC_MAOS_ENABLE | VC_RADEON_MODE | VC_TCL_ENABLE | (uint32_t)n << 16;
    radeon_cp_advance(7);
}

int radeon_tcl_old_packets = 1;
/* After every draw packet, the 3D engine waited for (WAIT_UNTIL 3D_IDLE: 5, the default), so the next packet, a draw's
 * next piece or the state of the next draw, meets an idle engine. Without it the P3's 7500 stopped for good, through
 * every reset: on Echoes' far terrain (945 indices in four pieces) at some placements of its vertices in memory, with
 * either packet; with waits between the pieces (4), ~9,800 placements and vertex shifts of that draw ran clean (DTMIX
 * mode R, 2026-11-25), but the fly scene then stopped in the state writes between a 96-index draw and a textured one
 * (the engine's output format changing while the draw was in flight). A wait before each draw alone didn't help. The
 * Linux DRM waits for 3D idle between repeated command batches for a lockup it couldn't explain either
 * (radeon_emit_packet3_cliprect). 0 none, 1 3D idle and clean before every draw packet, 2 before each draw, 3 between
 * pieces idle and clean, 4 between pieces idle. */
int radeon_tcl_sync = 5;
int radeon_tcl_piece = RADEON_TCL_PIECE;
int radeon_tcl_pending;                      /* (6: an engine draw sent, no wait since) */

/* (6) Before state other than a matrix changes after an engine draw: the 3D engine waited for, once. */
void radeon_tcl_settle(void)
{
    if (radeon_tcl_pending) {
        radeon_tcl_pending = 0;
        radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLE);
    }
}    /* (a multiple of 6; DOS-GL's DGL_TNL_PIECE, for tests) */

/* (lists, of triangles, lines or points, in pieces of RADEON_TCL_PIECE indices: a multiple of 6, so a piece ends at
 * a primitive's end; a strip or fan must come in one piece) */
void radeon_tcl_draw_indexed(uint32_t prim, uint32_t vfmt, uint32_t addr, int dwords, const uint16_t *idx, int n)
{
    radeon_tcl_draw_indexed_s(prim, vfmt, addr, dwords, dwords, idx, n);
}

/* (the same, the vertices stride dwords apart, holding dwords of data each: a stride other than the size takes
 * LOAD_VBPNTR's own stride field, so DRAW_INDX) */
void radeon_tcl_draw_indexed_s(uint32_t prim, uint32_t vfmt, uint32_t addr, int dwords, int stride, const uint16_t *idx,
                               int n)
{
    uint32_t *d;
    int i, first = 1;
    radeon_tcl_emit();
    if (radeon_tcl_old_packets && stride == dwords) {
        /* (Mesa 7's R100 driver as it shipped (RADEON_OLD_PACKETS 1): 3D_RNDR_GEN_INDX_PRIM with the vertices' address,
         * the largest index (0xFFFF, Mesa's), the format, then the indices; the vertex size from the format, no
         * LOAD_VBPNTR, no MAOS_ENABLE. With LOAD_VBPNTR and DRAW_INDX, the path Mesa compiled out, the P3's 7500
         * stopped for good on Echoes' far terrain at some placements of its vertices in memory) */
        (void)dwords;
        while (n > 0) {
            int k = n > radeon_tcl_piece ? radeon_tcl_piece : n, words = (k + 1) / 2;
            if (radeon_tcl_sync == 1 || (radeon_tcl_sync == 2 && first) || (radeon_tcl_sync == 3 && !first))
                radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLECLEAN);
            else if ((radeon_tcl_sync == 4 || radeon_tcl_sync == 6) && !first)
                radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLE);
            first = 0;
            d = radeon_cp_reserve(5 + words);
            if (!d)
                return;
            d[0] = RADEON_CP_PACKET3(RNDR_GEN_INDX_PRIM, 3 + words);
            d[1] = addr;
            d[2] = 0xFFFFu;
            d[3] = vfmt;
            d[4] = prim | VC_WALK_IND | VC_RADEON_MODE | VC_TCL_ENABLE | (uint32_t)k << 16;
            for (i = 0; i < words; i++)         /* (two to a dword, the first in the low half) */
                d[5 + i] = (uint32_t)idx[2 * i] | (2 * i + 1 < k ? (uint32_t)idx[2 * i + 1] << 16 : 0u);
            radeon_cp_advance(5 + words);
            if (radeon_tcl_sync == 5)
                radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLE);
            idx += k, n -= k;
        }
        radeon_tcl_pending = radeon_tcl_sync == 6;
        return;
    }
    if (radeon_tcl_sync == 1 || radeon_tcl_sync == 2)
        radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLECLEAN);
    d = radeon_cp_reserve(4);
    if (!d)
        return;
    d[0] = RADEON_CP_PACKET3(LOAD_VBPNTR, 2);
    d[1] = 1;
    d[2] = (uint32_t)stride << 8 | (uint32_t)dwords;    /* (stride, size) */
    d[3] = addr;
    radeon_cp_advance(4);
    while (n > 0) {
        int k = n > radeon_tcl_piece ? radeon_tcl_piece : n, words = (k + 1) / 2;
        if ((radeon_tcl_sync == 1 || radeon_tcl_sync == 3) && !first)
            radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLECLEAN);
        else if ((radeon_tcl_sync == 4 || radeon_tcl_sync == 6) && !first)
            radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLE);
        first = 0;
        d = radeon_cp_reserve(3 + words);
        if (!d)
            return;
        d[0] = RADEON_CP_PACKET3(DRAW_INDX, 1 + words);
        d[1] = vfmt;
        d[2] = prim | VC_WALK_IND | VC_MAOS_ENABLE | VC_RADEON_MODE | VC_TCL_ENABLE | (uint32_t)k << 16;
        for (i = 0; i < words; i++)             /* (two to a dword, the first in the low half) */
            d[3 + i] = (uint32_t)idx[2 * i] | (2 * i + 1 < k ? (uint32_t)idx[2 * i + 1] << 16 : 0u);
        radeon_cp_advance(3 + words);
        if (radeon_tcl_sync == 5)
            radeon_cp_reg(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLE);
        idx += k, n -= k;
    }
    radeon_tcl_pending = radeon_tcl_sync == 6;
}
