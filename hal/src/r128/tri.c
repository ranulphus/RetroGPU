/* tri.c - the Rage 128's 3D engine through the CCE (docs/rage128.md, R2): the 3D context as Mesa 7.x's r128 driver
 * starts it (r128_state.c's r128DDInitState) and Linux's DRM sends it (r128_state.c's r128_emit_context,
 * r128_emit_setup, r128_emit_masks, r128_emit_window: one type-0 packet each), then triangles as 3D_RNDR_GEN_PRIM
 * packets with the vertices inline (X.org's r128_exa_render.c's R128CCEComposite; MIT). The depth buffer is linear
 * (no Z_TILE), so the CPU can read it as Glide's LFB does. */
#include "r128/r128.h"
#include "r128/regs_r128.h"
#include <string.h>

r128_ctx3d r128_3d;
int r128_3d_stale = 1;
static int vfmt = R128_VC_FRMT_RHW | R128_VC_FRMT_DIFFUSE_ARGB;

static uint32_t pitch_offset(uint32_t off, int pitch_bytes, int bytes)
{
    return ((uint32_t)(pitch_bytes / bytes / 8) << 21) | (off >> 5);
}

void r128_3d_init(uint32_t color_off, int pitch_bytes, int bpp, uint32_t z_off, int z_pitch_bytes, int zbits)
{
    int bytes = bpp == 32 ? 4 : 2;
    uint32_t dt = bpp == 32 ? R128_DT_ARGB8888 : bpp == 15 ? R128_DT_ARGB1555 : R128_DT_RGB565;
    uint32_t zw = zbits == 32 ? R128_Z_PIX_WIDTH_32 : zbits == 24 ? R128_Z_PIX_WIDTH_24 : R128_Z_PIX_WIDTH_16;
    r128_ctx3d *c = &r128_3d;
    c->dst_pitch_offset_c = pitch_offset(color_off, pitch_bytes, bytes);
    c->dp_gui_master_cntl_c = R128_GMC_DST_PITCH_OFFSET_CNTL | R128_GMC_DST_CLIPPING | R128_GMC_BRUSH_SOLID_COLOR |
                              dt << R128_GMC_DST_DATATYPE_SHIFT | R128_GMC_SRC_DATATYPE_COLOR | R128_GMC_BYTE_MSB_TO_LSB |
                              R128_ROP3_S | R128_DP_SRC_SOURCE_MEMORY | R128_GMC_3D_FCN_EN |
                              R128_GMC_CLR_CMP_CNTL_DIS | R128_GMC_AUX_CLIP_DIS | R128_GMC_WR_MSK_DIS;
    c->sc_top_left_c = 0;
    c->sc_bottom_right_c = 0x1FFF1FFFu;
    c->z_offset_c = z_off;
    c->z_pitch_c = (uint32_t)(z_pitch_bytes / (zbits == 16 ? 2 : 4)) >> 3;     /* (pixels / 8; linear) */
    c->z_sten_cntl_c = zw | 1u << R128_Z_TEST_SHIFT | R128_STENCIL_TEST_ALWAYS;  /* (less; stencil kept) */
    c->tex_cntl_c = R128_Z_WRITE_ENABLE | R128_TEX_CACHE_FLUSH | 0x3Fu << R128_LOD_BIAS_SHIFT;
    c->misc_3d_state_cntl_reg = R128_MISC_SCALE_3D_TEXMAP_SHADE | R128_MISC_SCALE_PIX_REPLICATE |
                                (uint32_t)R128_BLEND_ONE << R128_ALPHA_BLEND_SRC_SHIFT |
                                (uint32_t)R128_BLEND_ZERO << R128_ALPHA_BLEND_DST_SHIFT |
                                7u << R128_ALPHA_TEST_SHIFT;                     /* (the alpha test: always) */
    c->texture_clr_cmp_clr_c = 0;
    c->texture_clr_cmp_msk_c = 0xFFFFFFFFu;
    c->fog_color_c = 0;
    c->setup_cntl = R128_COLOR_GOURAUD | R128_PRIM_TYPE_TRI | R128_TEXTURE_ST_MULT_W | R128_STARTING_VERTEX_1 |
                    R128_ENDING_VERTEX_3 | R128_SU_POLY_LINE_NOT_LAST | R128_SUB_PIX_4BITS;
    c->pm4_vc_fpu_setup = R128_FRONT_DIR_CCW | R128_BACKFACE_SOLID | R128_FRONTFACE_SOLID | R128_FPU_COLOR_GOURAUD |
                          R128_FPU_SUB_PIX_4BITS | R128_FPU_MODE_3D | R128_TRAP_BITS_DISABLE | R128_FLAT_SHADE_VERTEX_OGL;
    c->dp_write_mask = 0xFFFFFFFFu;
    c->sten_ref_mask_c = 0xFFFF0000u;
    c->plane_3d_mask_c = 0xFFFFFFFFu;
    c->window_xy_offset = 0;
    c->constant_color_c = 0x00FFFFFFu;
    c->scale_3d_cntl = R128_SCALE_DITHER_TABLE | R128_DITHER_INIT_RESET | R128_SCALE_3D_TEXMAP_SHADE |
                       R128_SCALE_PIX_REPLICATE | (uint32_t)R128_BLEND_ONE << R128_ALPHA_BLEND_SRC_SHIFT |
                       (uint32_t)R128_BLEND_ZERO << R128_ALPHA_BLEND_DST_SHIFT | 7u << R128_ALPHA_TEST_SHIFT |
                       R128_TEX_MAP_ALPHA_IN_TEXTURE | R128_TEX_CACHE_LINE_SIZE_4QW;
    c->prim_tex_cntl_c = 0, c->prim_texture_combine_cntl_c = 0, c->tex_size_pitch_c = 0;
    memset(c->prim_tex_offset_c, 0, sizeof c->prim_tex_offset_c);
    r128_3d_stale = 1;
}

/* The context to the engine in the DRM's order (r128_emit_state): the core first (SCALE_3D_CNTL: its blend fields
 * are overridden by MISC_3D_STATE_CNTL_REG's written after it; sent after it instead, they undid the blend in RTRI's
 * cases on the P3, and without it the texel's alpha never reached the blend: TEX_MAP_ALPHA_IN_TEXTURE), then the
 * context's 12 registers from DST_PITCH_OFFSET_C, SETUP_CNTL and PM4_VC_FPU_SETUP, the masks, the window, the
 * constant colour, and the first texture unit's block. */
void r128_3d_emit(void)
{
    const r128_ctx3d *c = &r128_3d;
    uint32_t p[16];
    int i;
    r128_cce_reg(R128_SCALE_3D_CNTL, c->scale_3d_cntl);
    p[0] = R128_CCE_PACKET0 | 11u << 16 | (R128_DST_PITCH_OFFSET_C >> 2);
    p[1] = c->dst_pitch_offset_c, p[2] = c->dp_gui_master_cntl_c, p[3] = c->sc_top_left_c;
    p[4] = c->sc_bottom_right_c, p[5] = c->z_offset_c, p[6] = c->z_pitch_c, p[7] = c->z_sten_cntl_c;
    p[8] = c->tex_cntl_c, p[9] = c->misc_3d_state_cntl_reg, p[10] = c->texture_clr_cmp_clr_c;
    p[11] = c->texture_clr_cmp_msk_c, p[12] = c->fog_color_c;
    r128_cce_out(p, 13);
    r128_cce_reg(R128_SETUP_CNTL, c->setup_cntl);
    r128_cce_reg(R128_PM4_VC_FPU_SETUP, c->pm4_vc_fpu_setup);
    r128_cce_reg(R128_DP_WRITE_MASK, c->dp_write_mask);
    r128_cce_reg(R128_STEN_REF_MASK_C, c->sten_ref_mask_c);
    r128_cce_reg(R128_PLANE_3D_MASK_C, c->plane_3d_mask_c);
    r128_cce_reg(R128_WINDOW_XY_OFFSET, c->window_xy_offset);
    r128_cce_reg(R128_CONSTANT_COLOR_C, c->constant_color_c);
    p[0] = R128_CCE_PACKET0 | 13u << 16 | (R128_PRIM_TEX_CNTL_C >> 2);
    p[1] = c->prim_tex_cntl_c, p[2] = c->prim_texture_combine_cntl_c, p[3] = c->tex_size_pitch_c;
    r128_cce_out(p, 4);
    for (i = 0; i < 11; i++)
        r128_cce_out(&c->prim_tex_offset_c[i], 1);
    r128_cce_reg(R128_PC_GUI_CTLSTAT, R128_PC_FLUSH_GUI);
    r128_3d_stale = 0;
}

void r128_3d_format(int fmt) { vfmt = fmt; }

static uint32_t fbits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

/* One triangle: its vertices inline, in the format r128_3d_format set (x, y, z, then rhw, diffuse ARGB, specular,
 * s and t, s2 and t2 as it says). */
void r128_tri(const r128_vertex *a, const r128_vertex *b, const r128_vertex *c)
{
    uint32_t p[3 + 3 * 10];
    const r128_vertex *v[3];
    int n = 3, i;
    if (r128_3d_stale)
        r128_3d_emit();
    v[0] = a, v[1] = b, v[2] = c;
    for (i = 0; i < 3; i++) {
        p[n++] = fbits(v[i]->x), p[n++] = fbits(v[i]->y), p[n++] = fbits(v[i]->z);
        if (vfmt & R128_VC_FRMT_RHW) p[n++] = fbits(v[i]->rhw);
        if (vfmt & R128_VC_FRMT_DIFFUSE_ARGB) p[n++] = v[i]->argb;
        if (vfmt & R128_VC_FRMT_SPEC_FRGB) p[n++] = v[i]->spec;
        if (vfmt & R128_VC_FRMT_S_T) p[n++] = fbits(v[i]->s), p[n++] = fbits(v[i]->t);
        if (vfmt & R128_VC_FRMT_S2_T2) p[n++] = fbits(v[i]->s2), p[n++] = fbits(v[i]->t2);
    }
    p[0] = R128_CCE_PACKET3 | R128_3D_RNDR_GEN_PRIM | (uint32_t)(n - 2) << 16;
    p[1] = (uint32_t)vfmt;
    p[2] = R128_VC_PRIM_TRI_LIST | R128_VC_WALK_RING | 3u << R128_VC_NUM_SHIFT;
    r128_cce_out(p, n);
}
