/* tri.c - the Radeon's 3D engine by plain register writes (docs/radeon.md, RD2): no command processor, no
 * microcode. The state's registers and their first values are Mesa 7.0.3's radeon driver's (radeon_state_init.c)
 * with vertices in window coordinates as X.org's EXA code sends them (radeon_commonfuncs.c's RADEONInit3DEngine,
 * radeon_exa_render.c: SE_VF_CNTL, then the vertices' words to SE_PORT_DATA0); both MIT. Each register write is a
 * bus write, as the Rage 128's PIO: the command processor's ring follows (RD3). */
#include "radeon/radeon.h"
#include "radeon/regs_radeon.h"
#include "radeon/regs3d_radeon.h"
#include "mga/sys.h"
#include <string.h>

radeon_ctx3d radeon_3d;
int radeon_3d_dirty = 1;
static radeon_ctx3d sent;               /* the words the chip has (while known) */
static int known;                       /* (0: every register is written at the next emit) */
static int mode3d;                      /* the last drawing was 3D (the engines take turns) */
static uint32_t vfmt;
static int vwhat = RADEON_VTX_RHW | RADEON_VTX_ARGB;

static void fifo(int n)
{
    uint32_t t0 = sys_time_us();
    while ((int)(radeon_rd(RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK) < n && sys_time_us() - t0 < 200000u)
        ;
}

void radeon_3d_init(uint32_t color_off, int pitch_bytes, int bpp, uint32_t z_off, int z_pitch_bytes, int zbits,
                    int width, int height)
{
    radeon_ctx3d *c = &radeon_3d;
    int cpp = bpp == 32 ? 4 : 2, zpp = zbits == 16 ? 2 : 4, i;
    memset(c, 0, sizeof *c);
    c->pp_misc = RADEON_ALPHA_TEST_PASS | RADEON_CHROMA_FUNC_FAIL | RADEON_CHROMA_KEY_NEAREST |
                 RADEON_SHADOW_FUNC_EQUAL | RADEON_SHADOW_PASS_1;
    c->pp_fog_color = RADEON_FOG_VERTEX | RADEON_FOG_USE_SPEC_ALPHA;
    c->rb3d_blendcntl = RADEON_COMB_FCN_ADD_CLAMP | RADEON_SRC_BLEND_GL_ONE | RADEON_DST_BLEND_GL_ZERO;
    c->rb3d_depthoffset = radeon.fb_location + z_off;
    c->rb3d_depthpitch = ((uint32_t)(zbits ? z_pitch_bytes / zpp : pitch_bytes / cpp) & RADEON_DEPTHPITCH_MASK) |
                         RADEON_DEPTH_ENDIAN_NO_SWAP;
    c->rb3d_zstencilcntl = (zbits == 16 || !zbits ? RADEON_DEPTH_FORMAT_16BIT_INT_Z : RADEON_DEPTH_FORMAT_24BIT_INT_Z) |
                           RADEON_Z_TEST_LESS | RADEON_STENCIL_TEST_ALWAYS | RADEON_STENCIL_FAIL_KEEP |
                           RADEON_STENCIL_ZPASS_KEEP | RADEON_STENCIL_ZFAIL_KEEP | (zbits ? RADEON_Z_WRITE_ENABLE : 0u);
    c->pp_cntl = RADEON_SCISSOR_ENABLE | RADEON_ANTI_ALIAS_NONE | RADEON_TEX_BLEND_0_ENABLE;
    c->rb3d_cntl = RADEON_PLANE_MASK_ENABLE | (bpp == 32 ? RADEON_COLOR_FORMAT_ARGB8888 : RADEON_COLOR_FORMAT_RGB565) |
                   RADEON_ZBLOCK16 | (bpp == 32 ? 0u : RADEON_DITHER_ENABLE);
    c->rb3d_coloroffset = (radeon.fb_location + color_off) & RADEON_COLOROFFSET_MASK;
    c->rb3d_colorpitch = ((uint32_t)(pitch_bytes / cpp) & RADEON_COLORPITCH_MASK) | RADEON_COLOR_ENDIAN_NO_SWAP;
    c->re_top_left = 0;
    c->re_width_height = (uint32_t)(width - 1) << RADEON_RE_WIDTH_SHIFT | (uint32_t)(height - 1) << RADEON_RE_HEIGHT_SHIFT;
    /* (window coordinates: no viewport transform; both faces drawn, every interpolant Gouraud; OpenGL's pixel
     * centres; vertices rounded to sixteenths of a pixel: Mesa's eighths, truncated, moved DTRI0's edges, 94
     * pixels of coverage and 56 of depth order) */
    c->se_cntl = RADEON_BFACE_SOLID | RADEON_FFACE_SOLID | RADEON_FLAT_SHADE_VTX_LAST | RADEON_DIFFUSE_SHADE_GOURAUD |
                 RADEON_ALPHA_SHADE_GOURAUD | RADEON_SPECULAR_SHADE_GOURAUD | RADEON_FOG_SHADE_GOURAUD |
                 RADEON_VTX_PIX_CENTER_OGL | RADEON_ROUND_MODE_ROUND | RADEON_ROUND_PREC_16TH_PIX;
    /* (x, y and z as they come, the fourth coordinate 1/w, textures' s and t divided by the chip) */
    c->se_coord_fmt = RADEON_VTX_XY_PRE_MULT_1_OVER_W0 | RADEON_VTX_Z_PRE_MULT_1_OVER_W0 | RADEON_TEX1_W_ROUTING_USE_W0;
    /* (a chip with a transform engine keeps it out of these vertices by SE_VF_CNTL's TCL bit, clear here; one
     * without is told so: Mesa's radeon_state_init.c, X.org's RADEONInit3DEngine) */
    c->se_cntl_status = radeon.flags & RADEON_HAS_TCL ? 0u : RADEON_TCL_BYPASS;
    c->rb3d_stencilrefmask = 0xFFu << RADEON_STENCIL_MASK_SHIFT | 0xFFu << RADEON_STENCIL_WRITEMASK_SHIFT;
    c->rb3d_ropcntl = RADEON_ROP_COPY;
    c->rb3d_planemask = 0xFFFFFFFFu;
    for (i = 0; i < 3; i++) {
        /* (a unit passes what comes to it: the vertex colour at the first) */
        c->tex[i].txcblend = RADEON_COLOR_ARG_A_ZERO | RADEON_COLOR_ARG_B_ZERO | RADEON_COLOR_ARG_C_CURRENT_COLOR |
                             RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        c->tex[i].txablend = RADEON_ALPHA_ARG_A_ZERO | RADEON_ALPHA_ARG_B_ZERO | RADEON_ALPHA_ARG_C_CURRENT_ALPHA |
                             RADEON_BLEND_CTL_ADD | RADEON_SCALE_1X | RADEON_CLAMP_TX;
        /* (ST_ROUTE_STQn; without PERSPECTIVE_ENABLE a unit maps its texture affinely whatever 1/w says: DTRI) */
        c->tex[i].txformat = RADEON_TXFORMAT_RGB565 | (uint32_t)i << 24 | RADEON_TXFORMAT_PERSPECTIVE_ENABLE;
    }
    if (zbits)
        c->rb3d_cntl |= RADEON_Z_ENABLE;
    known = 0;
    radeon_3d_dirty = 1;
}

#define W1(reg, f) do { if (!known || c->f != sent.f) { fifo(1); radeon_wr(reg, c->f); } } while (0)

void radeon_3d_emit(void)
{
    static const uint32_t tx[3][7] = {
        { RADEON_PP_TXFILTER_0, RADEON_PP_TXFORMAT_0, RADEON_PP_TXOFFSET_0, RADEON_PP_TXCBLEND_0, RADEON_PP_TXABLEND_0,
          RADEON_PP_TFACTOR_0, RADEON_PP_BORDER_COLOR_0 },
        { RADEON_PP_TXFILTER_1, RADEON_PP_TXFORMAT_1, RADEON_PP_TXOFFSET_1, RADEON_PP_TXCBLEND_1, RADEON_PP_TXABLEND_1,
          RADEON_PP_TFACTOR_1, RADEON_PP_BORDER_COLOR_1 },
        { RADEON_PP_TXFILTER_2, RADEON_PP_TXFORMAT_2, RADEON_PP_TXOFFSET_2, RADEON_PP_TXCBLEND_2, RADEON_PP_TXABLEND_2,
          RADEON_PP_TFACTOR_2, RADEON_PP_BORDER_COLOR_2 },
    };
    const radeon_ctx3d *c = &radeon_3d;
    int i, k;
    if (!mode3d) {
        /* (after 2D drawing: the queue held until the 2D engine is idle and its cache clean, RADEON_SWITCH_TO_3D) */
        fifo(1);
        radeon_wr(RADEON_WAIT_UNTIL, RADEON_WAIT_2D_IDLECLEAN | RADEON_WAIT_3D_IDLECLEAN | RADEON_WAIT_HOST_IDLECLEAN);
        mode3d = 1;
    }
    W1(RADEON_SE_CNTL_STATUS, se_cntl_status);
    W1(RADEON_SE_COORD_FMT, se_coord_fmt);
    W1(RADEON_RE_TOP_LEFT, re_top_left);
    W1(RADEON_RE_WIDTH_HEIGHT, re_width_height);
    W1(RADEON_RB3D_PLANEMASK, rb3d_planemask);
    W1(RADEON_SE_CNTL, se_cntl);
    W1(RADEON_PP_MISC, pp_misc);
    W1(RADEON_PP_FOG_COLOR, pp_fog_color);
    W1(RADEON_RE_SOLID_COLOR, re_solid_color);
    W1(RADEON_RB3D_BLENDCNTL, rb3d_blendcntl);
    W1(RADEON_RB3D_DEPTHOFFSET, rb3d_depthoffset);
    W1(RADEON_RB3D_DEPTHPITCH, rb3d_depthpitch);
    W1(RADEON_RB3D_ZSTENCILCNTL, rb3d_zstencilcntl);
    W1(RADEON_PP_CNTL, pp_cntl);
    W1(RADEON_RB3D_CNTL, rb3d_cntl);
    W1(RADEON_RB3D_COLOROFFSET, rb3d_coloroffset);
    W1(RADEON_RB3D_COLORPITCH, rb3d_colorpitch);
    W1(RADEON_RB3D_STENCILREFMASK, rb3d_stencilrefmask);
    W1(RADEON_RB3D_ROPCNTL, rb3d_ropcntl);
    for (i = 0; i < 3; i++) {
        const uint32_t *now = (const uint32_t *)&c->tex[i], *was = (const uint32_t *)&sent.tex[i];
        for (k = 0; k < 7; k++)
            if (!known || now[k] != was[k]) {
                fifo(1);
                radeon_wr(tx[i][k], now[k]);
            }
    }
    if (!known) {
        fifo(1);
        radeon_wr(RADEON_AUX_SC_CNTL, 0);
    }
    sent = *c;
    known = 1;
    radeon_3d_dirty = 0;
}

void radeon_3d_format(int what)
{
    vwhat = what;
    vfmt = RADEON_SE_VTX_FMT_XY | RADEON_SE_VTX_FMT_Z;
    if (what & RADEON_VTX_RHW) vfmt |= RADEON_SE_VTX_FMT_W0;
    if (what & RADEON_VTX_ARGB) vfmt |= RADEON_SE_VTX_FMT_PKCOLOR;
    if (what & RADEON_VTX_SPEC) vfmt |= RADEON_SE_VTX_FMT_PKSPEC;
    if (what & RADEON_VTX_ST) vfmt |= RADEON_SE_VTX_FMT_ST0;
    if (what & RADEON_VTX_ST2) vfmt |= RADEON_SE_VTX_FMT_ST1;
    if (what & RADEON_VTX_ST3) vfmt |= RADEON_SE_VTX_FMT_ST2;
}

static uint32_t fbits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

/* One triangle: the vertex format, SE_VF_CNTL (a triangle list of three, its vertices following as data, the
 * Radeon's own vertex layout), then each vertex's words in the format's order. */
void radeon_tri(const radeon_vertex *a, const radeon_vertex *b, const radeon_vertex *c)
{
    static uint32_t fmt_sent;
    static int fmt_known;
    const radeon_vertex *v[3];
    uint32_t p[3 * 12];
    int n = 0, i;
    if (!vfmt)
        radeon_3d_format(vwhat);
    if (radeon_3d_dirty || !mode3d || !known)
        radeon_3d_emit();
    v[0] = a, v[1] = b, v[2] = c;
    for (i = 0; i < 3; i++) {
        p[n++] = fbits(v[i]->x), p[n++] = fbits(v[i]->y), p[n++] = fbits(v[i]->z);
        if (vwhat & RADEON_VTX_RHW) p[n++] = fbits(v[i]->rhw);
        if (vwhat & RADEON_VTX_ARGB) p[n++] = v[i]->argb;
        if (vwhat & RADEON_VTX_SPEC) p[n++] = v[i]->spec;
        if (vwhat & RADEON_VTX_ST) p[n++] = fbits(v[i]->s), p[n++] = fbits(v[i]->t);
        if (vwhat & RADEON_VTX_ST2) p[n++] = fbits(v[i]->s2), p[n++] = fbits(v[i]->t2);
        if (vwhat & RADEON_VTX_ST3) p[n++] = fbits(v[i]->s3), p[n++] = fbits(v[i]->t3);
    }
    if (!fmt_known || fmt_sent != vfmt) {
        fifo(1);
        radeon_wr(RADEON_SE_VTX_FMT, vfmt);
        fmt_sent = vfmt, fmt_known = 1;
    }
    fifo(1 + n);
    radeon_wr(RADEON_SE_VF_CNTL, RADEON_VF_PRIM_TYPE_TRIANGLE_LIST | RADEON_VF_PRIM_WALK_DATA | RADEON_VF_RADEON_MODE |
                                 3u << RADEON_VF_NUM_VERTICES_SHIFT);
    for (i = 0; i < n; i++)
        radeon_wr(RADEON_SE_PORT_DATA0, p[i]);
}

/* 2D drawing after 3D: the queue held until the 3D engine is idle and its cache clean (RADEON_SWITCH_TO_2D). */
void radeon_2d_begin(void)
{
    if (!mode3d)
        return;
    fifo(1);
    radeon_wr(RADEON_WAIT_UNTIL, RADEON_WAIT_3D_IDLECLEAN | RADEON_WAIT_HOST_IDLECLEAN);
    mode3d = 0;
}
