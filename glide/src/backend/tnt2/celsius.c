/* celsius.c - Glide's triangles on the NV1x's own 3D class, Celsius (0x96): the Quadro2 MXR and the GeForce2 MX
 * (docs/nv10.md, the Quadro2 plan's Q2; the owner's choice of 2026-11-27 after the D3D5/D3D6 classes, which the
 * TNT2 back end draws with, drew nothing on the NV11).
 *
 * Celsius set up as DOS-GL's back end sets it up (gl/src/backend/nv10: the layer's captured init, the 2048-offset
 * window space), with Glide's state as Celsius's methods:
 *  - the vertices from a vertex area in video memory, batched until the state changes (or a clear, a swap, a sync,
 *    a texture written): window coordinates times w with w = 1/oow, so the chip divides and interpolates the texture
 *    coordinates in perspective as the Voodoo does, a projection that only scales z and moves into Celsius's space;
 *  - the colour and alpha combines on the register combiners, which compute A B + C D over inputs that may be
 *    inverted or negated: every one of Glide's functions is exact, the subtracting ones included (the TNT2 adds only),
 *    the three-term ones on two combiners; inverting in the final combiner; TMU0's own combine as an input where it
 *    is one (the texel, its alpha, inverted or not), else on the first combiner;
 *  - table fog per vertex as the TNT2 back end works it out (its splitting), the factor in the fog slot, the final
 *    combiner blending toward the fog colour (MULT2, ADD2 as the TNT2's);
 *  - chroma key as the TNT2 back end's: the keyed texture variant (keyed texels alpha 0) and the alpha test;
 *  - depth, blending, the alpha test and dithering as Celsius's GL-like methods. */
#include "cel.h"
#include "backend/backend.h"
#include "tex/texmgr.h"
#include "tex/texconv.h"
#include "mga/fp.h"
#include "dll/config.h"
#include <string.h>

#define S7 7                                    /* Celsius's subchannel (tnt2_celsius_init) */

/* Celsius's methods (Mesa's nv10_3d.xml.h, envytools' rnndb; DOS-GL's nv10 back end). */
#define M_RT_HORIZ        0x200
#define M_RT_VERT         0x204
#define M_TEX_OFFSET(i)   (0x218u + 4u * (uint32_t)(i))
#define M_TEX_FORMAT(i)   (0x220u + 4u * (uint32_t)(i))
#define M_TEX_ENABLE(i)   (0x228u + 4u * (uint32_t)(i))
#define M_TEX_FILTER(i)   (0x248u + 4u * (uint32_t)(i))
#define M_RC_IN_ALPHA(i)  (0x260u + 4u * (uint32_t)(i))
#define M_RC_IN_RGB(i)    (0x268u + 4u * (uint32_t)(i))
#define M_RC_COLOR(i)     (0x270u + 4u * (uint32_t)(i))
#define M_RC_OUT_ALPHA(i) (0x278u + 4u * (uint32_t)(i))
#define M_RC_OUT_RGB(i)   (0x280u + 4u * (uint32_t)(i))
#define M_RC_FINAL0       0x288
#define M_RC_FINAL1       0x28C
#define M_FOG_MODE        0x29C
#define M_FOG_COORD       0x2A0
#define M_FOG_ENABLE      0x2A4
#define M_FOG_COLOR       0x2A8
#define M_CLIP_HORIZ      0x2C0
#define M_CLIP_VERT       0x2E0
#define M_ALPHA_ENABLE    0x300
#define M_BLEND_ENABLE    0x304
#define M_CULL_ENABLE     0x308
#define M_DEPTH_ENABLE    0x30C
#define M_DITHER_ENABLE   0x310
#define M_LIGHTING_ENABLE 0x314
#define M_STENCIL_ENABLE  0x32C
#define M_OFFSET_POINT    0x330
#define M_OFFSET_LINE     0x334
#define M_OFFSET_FILL     0x338
#define M_ALPHA_FUNC      0x33C
#define M_ALPHA_REF       0x340
#define M_BLEND_SRC       0x344
#define M_BLEND_DST       0x348
#define M_BLEND_EQUATION  0x350
#define M_DEPTH_FUNC      0x354
#define M_COLOR_MASK      0x358
#define M_DEPTH_WRITE     0x35C
#define M_SHADE_MODEL     0x37C
#define M_DEPTH_NEAR      0x394
#define M_PROJECTION      0x500
#define M_FOG_COEFF       0x680
#define M_VP_TRANSLATE    0x6E8
#define M_VTXBUF_VALIDATE 0xCF0
#define M_VTXBUF_OFFSET(i) (0xD00u + 8u * (uint32_t)(i))
#define M_VTXBUF_FMT(i)   (0xD04u + 8u * (uint32_t)(i))
#define M_BEGIN_END       0x13FC
#define M_BATCH           0x1400

#define TF_DMA0      0x00000001u
#define TF_BASE      0x00001050u                /* (Mesa's 5 << 4 | 1 << 12) */
#define TF_A1R5G5B5  0x00000100u
#define TF_A4R4G4B4  0x00000200u
#define TF_R5G6B5    0x00000280u
#define TE_ENABLE    0x40000000u

/* The register combiners' inputs: a byte of source, usage (alpha 0x10) and mapping (<< 5). */
enum { RS_ZERO = 0, RS_CONST0 = 1, RS_FOG = 3, RS_PRIMARY = 4, RS_TEX0 = 8, RS_SPARE0 = 0xC };
#define RU_ALPHA   0x10u
#define RM_INVERT  0x20u                        /* 1 - x */
#define RM_NEGATE  0xE0u                        /* -x (SIGNED_NEGATE) */
#define R_ONE      (RS_ZERO | RM_INVERT)
#define RO_AB_SPARE0  0x000000C0u
#define RO_SUM_SPARE0 0x00000C00u
#define IN(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* A vertex in the vertex area: 32 bytes. */
typedef struct { float x, y, z, w; uint32_t col; float s, t, fog; } gvert;
#define VSIZE 32u
#define VA_SIZE (128u * 1024u)
#define VA_PARTS 4
#define VA_PART (VA_SIZE / VA_PARTS)

static int warned[8];
/* The Voodoo's fill rule on Celsius: Glide's snapped coordinates moved 3/32 of a pixel left and 1/32 up (the TNT2
 * back end's own -1/64 in x undone): t04 on bench E then differs in 1 pixel of 307,200 (none moved: 338; Celsius
 * keeps sixteenths, so a shift under one changes nothing; x from -5/64 to -7/64 alike). MGL_CEL_DX/DY (256ths of a
 * pixel) replace the two for bring-up. */
static float nudge_x, nudge_y;

static void warn_once(int k, const char *what)
{
    if (!warned[k]) {
        warned[k] = 1;
        mg_log(MG_LOG_WARN, "tnt2 celsius: %s", what);
    }
}

/* ---- Methods, the values sent kept: a change flushes the batched triangles first ----------------------------- */

#define SH_N (0x700 / 4)
static uint32_t sh[SH_N];
static unsigned char sh_ok[SH_N];
static uint32_t va_off, va_n, va_first, va_mark[VA_PARTS];
static unsigned draws;

static void m1(uint32_t mthd, uint32_t v) { tnt2_methods(S7, mthd, &v, 1); }
static uint32_t fbits(float f) { union { float f; uint32_t u; } c; c.f = f; return c.u; }

static void mc(uint32_t mthd, uint32_t v)
{
    uint32_t i = mthd >> 2;
    if (i < SH_N) {
        if (sh_ok[i] && sh[i] == v)
            return;
        sh[i] = v, sh_ok[i] = 1;
    }
    if (va_n != va_first)
        cel_flush();
    m1(mthd, v);
}

/* (a vector: the chip takes it when its last word is written, so it goes whole: DOS-GL's mcv) */
static void mcv(uint32_t mthd, const uint32_t *v, int n)
{
    uint32_t i0 = mthd >> 2;
    int i, same = i0 + (uint32_t)n <= SH_N;
    for (i = 0; same && i < n; i++)
        same = sh_ok[i0 + (uint32_t)i] && sh[i0 + (uint32_t)i] == v[i];
    if (same)
        return;
    for (i = 0; i < n && i0 + (uint32_t)i < SH_N; i++)
        sh[i0 + (uint32_t)i] = v[i], sh_ok[i0 + (uint32_t)i] = 1;
    if (va_n != va_first)
        cel_flush();
    tnt2_methods(S7, mthd, v, n);
}

/* ---- The vertex area ------------------------------------------------------------------------------------------ */

void cel_flush(void)
{
    uint32_t first = va_first, n = va_n - va_first;
    if (!n)
        return;
    /* (before every draw: the chip's vertex fetch keeps video memory's lines: DOS-GL's t16/t18) */
    m1(M_VTXBUF_VALIDATE, 0);
    m1(M_BEGIN_END, 5);                         /* GL_TRIANGLES + 1 */
    while (n) {
        uint32_t k = n > 255u ? 255u : n;       /* (whole triangles: 255 = 85 of them) */
        m1(M_BATCH, (k - 1u) << 24 | first);
        first += k, n -= k;
    }
    m1(M_BEGIN_END, 0);
    va_first = va_n;
    va_mark[(va_n - 1u) * VSIZE / VA_PART] = tnt2_mark();
    draws++;
    tnt2_kick_soon();
}

/* Room for k more vertices: past the area's end, back to its start; into a quarter once the chip is done with it. */
static int va_room(uint32_t k)
{
    uint32_t end = (va_n + k) * VSIZE, part0 = va_n ? (va_n * VSIZE - 1u) / VA_PART : (uint32_t)-1, p;
    if (end > VA_SIZE) {
        cel_flush();
        va_n = va_first = 0;
        part0 = (uint32_t)-1;
        end = k * VSIZE;
    }
    for (p = part0 + 1u; p * VA_PART < end; p++)
        if (p != part0 && tnt2_mark_wait(va_mark[p], 1000000) != 0) {
            warn_once(0, "the chip didn't finish with the vertex area: a triangle dropped");
            return -1;
        }
    return 0;
}

static void fixed_state(void)
{
    static const uint32_t fmt[8] = {
        0x01000000u | VSIZE << 8 | 4u << 4 | 2u,    /* position: x y z w, floats, homogeneous */
        VSIZE << 8 | 4u << 4 | 0u,                  /* colour: B8G8R8A8 */
        0x0002u,                                    /* (the secondary colour: off) */
        VSIZE << 8 | 2u << 4 | 2u,                  /* texture 0: s t */
        0x0002u, 0x0002u, 0x0002u,                  /* (texture 1, normal, weight: off) */
        VSIZE << 8 | 1u << 4 | 2u };                /* fog */
    static const uint32_t at[8] = { 0, 16, 0, 20, 0, 0, 0, 28 };
    uint32_t pm[16], tr[4], dr[2], k[3];
    int i;
    for (i = 0; i < 8; i++) {
        m1(M_VTXBUF_FMT(i), fmt[i]);
        if (fmt[i] != 0x0002u)
            m1(M_VTXBUF_OFFSET(i), va_off + at[i]);
    }
    /* Clip space in, as DOS-GL's back end sends it (the chip's clipper works in it: window coordinates times w with
     * an identity projection, x/w up to 640, drew nothing): the projection the viewport alone (rows down: y not
     * flipped), z's [-1, 1] to Z16's range, the translation into Celsius's space, 2048 below the window's, after the
     * division. */
    for (i = 0; i < 16; i++)
        pm[i] = 0;
    pm[0] = fbits((float)mg.rw * 0.5f), pm[5] = fbits((float)mg.rh * 0.5f), pm[10] = fbits(32767.5f);
    pm[15] = fbits(1.0f);
    tr[0] = fbits((float)mg.rw * 0.5f - 2048.0f), tr[1] = fbits((float)mg.rh * 0.5f - 2048.0f);
    tr[2] = fbits(32767.5f), tr[3] = 0;
    dr[0] = fbits(0.0f), dr[1] = fbits(65535.0f);
    mcv(M_PROJECTION, pm, 16);
    mcv(M_VP_TRANSLATE, tr, 4);
    mcv(M_DEPTH_NEAR, dr, 2);
    /* Fog: linear, f = k0 + k1 c - 1 (DCFOG's model) with k0 = k1 = 1: the factor is the vertex's own value. */
    mc(M_FOG_MODE, 0x2601);
    mc(M_FOG_COORD, 0);
    k[0] = fbits(1.0f), k[1] = fbits(1.0f), k[2] = fbits(0.0f);
    mcv(M_FOG_COEFF, k, 3);
    mc(M_CULL_ENABLE, 0);                       /* (culled in tnt2.c) */
    mc(M_LIGHTING_ENABLE, 0);
    mc(M_SHADE_MODEL, 0x1D01);
    mc(M_STENCIL_ENABLE, 0);
    mc(M_OFFSET_FILL, 0), mc(M_OFFSET_LINE, 0), mc(M_OFFSET_POINT, 0);
    mc(M_TEX_ENABLE(1), 0);
    mc(M_BLEND_EQUATION, 0x8006);               /* GL_FUNC_ADD */
}

/* (a signed decimal: the OVL's library has no atoi) */
static int num(const char *e)
{
    int v = 0, neg = *e == '-';
    if (neg)
        e++;
    while (*e >= '0' && *e <= '9')
        v = v * 10 + (*e++ - '0');
    return neg ? -v : v;
}

uint32_t cel_open(uint32_t heap_off, uint32_t heap_end)
{
    memset(sh_ok, 0, sizeof sh_ok);
    memset(warned, 0, sizeof warned);
    {
        const char *e = mg_getenv("MGL_CEL_DX");
        nudge_x = 1.0f / 64.0f + (float)(e ? num(e) : -24) / 256.0f;
        e = mg_getenv("MGL_CEL_DY");
        nudge_y = (float)(e ? num(e) : -8) / 256.0f;
    }
    draws = 0;
    if (tnt2_celsius_init() != 0)
        warn_once(1, "Celsius's set-up didn't finish");
    if (heap_end < heap_off + VA_SIZE + 256u) {
        warn_once(2, "no room for the vertex area");
        va_off = 0;
        return heap_end;
    }
    va_off = (heap_end - VA_SIZE) & ~255u;
    va_n = va_first = 0;
    memset(va_mark, 0, sizeof va_mark);
    fixed_state();
    cel_target();
    tnt2_kick();
    return va_off;
}

void cel_close(void) { cel_flush(); }
unsigned cel_draws(void) { return draws; }

void cel_target(void)
{
    int i, x0 = mg.st.clip_x0, x1 = mg.st.clip_x1, y0 = mg.st.clip_y0, y1 = mg.st.clip_y1;
    cel_flush();
    tnt2_celsius_target(mg_buffer_offset(mg.render_buffer), mg.aux_off, mg.pitch_px * 2, mg.rw, mg.rh, 16);
    for (i = 0x200 / 4; i <= 0x214 / 4; i++)    /* (written there: no longer what the cache holds) */
        sh_ok[i] = 0;
    mc(M_CLIP_HORIZ, (uint32_t)(0x800 + mg.rw - 1) << 16 | 0x800u);
    mc(M_CLIP_VERT, (uint32_t)(0x800 + mg.rh - 1) << 16 | 0x800u);
    if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
        y0 = mg.height - mg.st.clip_y1;
        y1 = mg.height - mg.st.clip_y0;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > mg.rw) x1 = mg.rw;
    if (y1 > mg.rh) y1 = mg.rh;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    /* (Glide's clip window as the render target's window: DOS-GL's scissor) */
    mc(M_RT_HORIZ, (uint32_t)(x1 - x0) << 16 | (uint32_t)x0);
    mc(M_RT_VERT, (uint32_t)(y1 - y0) << 16 | (uint32_t)y0);
}

void cel_tex_written(void)
{
    int u;
    cel_flush();
    for (u = 0; u < 2; u++) {                   /* (sent again: the texture cache keeps texels it read) */
        sh_ok[M_TEX_OFFSET(u) >> 2] = 0, sh_ok[M_TEX_FORMAT(u) >> 2] = 0;
        sh_ok[M_TEX_FILTER(u) >> 2] = 0, sh_ok[M_TEX_ENABLE(u) >> 2] = 0;
    }
}

/* ---- The combines on the register combiners -------------------------------------------------------------------- */

/* The texture as an input: TEX0 (or SPARE0 where TMU0's combine needed the first combiner), what reading it gives
 * (ONE or ZERO where nothing is bound: the TNT2's white texel; its alpha ONE for a texture without alpha, which the
 * Voodoo takes as opaque). */
static uint32_t tex_src;                        /* RS_TEX0, RS_SPARE0, or ~0: none bound (reads as one) */
static int tex_noalpha;                         /* bound without alpha (565) */
static uint32_t tex_rgb_map, tex_a_map;         /* TMU0's combine when it is an input: RM_INVERT, RU_ALPHA ... */

static int approx_said;
static void approx(const char *what)
{
    if (!approx_said) {
        approx_said = 1;
        mg_log(MG_LOG_WARN, "tnt2 celsius: combine approximated (%s)", what);
    }
}

/* An input byte: a Glide source in a combine (rgb: the colour portion; alpha: the value's alpha), inverted or not. */
enum { G_ZERO, G_ITER, G_CONST, G_TEX };
static uint32_t in_of(int g, int alpha, int inv)
{
    uint32_t b;
    switch (g) {
    case G_ITER: b = RS_PRIMARY | (alpha ? RU_ALPHA : 0u); break;
    case G_CONST: b = RS_CONST0 | (alpha ? RU_ALPHA : 0u); break;
    case G_TEX:
        if (tex_src == ~0u || (alpha && tex_noalpha && tex_src == RS_TEX0))
            return inv ? RS_ZERO : R_ONE;
        if (tex_src == RS_SPARE0)
            b = RS_SPARE0 | (alpha ? RU_ALPHA : 0u);
        else {
            uint32_t m = alpha ? tex_a_map : tex_rgb_map;
            b = RS_TEX0 | (m & RU_ALPHA) | (alpha ? RU_ALPHA : 0u);
            if (m & RM_INVERT)
                inv = !inv;
            if ((m & RU_ALPHA) && tex_noalpha)
                return inv ? RS_ZERO : R_ONE;   /* (its alpha replicated: one) */
        }
        break;
    default: return inv ? R_ONE : RS_ZERO;
    }
    return b | (inv ? RM_INVERT : 0u);
}

/* -x of an input: SIGNED_NEGATE where the input is itself (an inverted one has no negated mapping: approximated). */
static uint32_t neg_of(uint32_t b)
{
    if ((b & 0xE0u) == 0)
        return b | RM_NEGATE;
    approx("a negated inverted input");
    return b;
}

static int g_of_local(GrCombineLocal_t l)
{
    if (l == GR_COMBINE_LOCAL_DEPTH)
        approx("depth as the local value");
    return l == GR_COMBINE_LOCAL_CONSTANT ? G_CONST : G_ITER;
}
static int g_of_other(GrCombineOther_t o)
{
    return o == GR_COMBINE_OTHER_TEXTURE ? G_TEX : o == GR_COMBINE_OTHER_CONSTANT ? G_CONST : G_ITER;
}

/* One combine (colour: alpha 0; alpha: 1) as up to two combiner stages {A, B, C, D, sum}, writing SPARE0. */
typedef struct { uint32_t a, b, c, d; int sum; } stage;

static uint32_t factor_in(GrCombineFactor_t f, int alpha, int L, int aL, int aO)
{
    int inv = (f & 8) != 0;
    switch (f & 7) {
    case GR_COMBINE_FACTOR_LOCAL: return in_of(alpha ? aL : L, alpha, inv);
    case GR_COMBINE_FACTOR_OTHER_ALPHA: return in_of(aO, 1, inv) | RU_ALPHA;
    case GR_COMBINE_FACTOR_LOCAL_ALPHA: return in_of(aL, 1, inv) | RU_ALPHA;
    case GR_COMBINE_FACTOR_TEXTURE_ALPHA: return in_of(G_TEX, 1, inv);
    case GR_COMBINE_FACTOR_TEXTURE_RGB:
        if (alpha)
            approx("the texture's colour as an alpha factor");
        return in_of(G_TEX, alpha, inv);
    default: return inv ? R_ONE : RS_ZERO;     /* ZERO, and ONE as its inverse */
    }
}

static int stages_of(GrCombineFunction_t fn, uint32_t O, uint32_t L, uint32_t aL, uint32_t F, stage *s)
{
    memset(s, 0, 2 * sizeof *s);
    switch (fn) {
    case GR_COMBINE_FUNCTION_ZERO:
        s[0].a = RS_ZERO, s[0].b = RS_ZERO; return 1;
    case GR_COMBINE_FUNCTION_LOCAL:
        s[0].a = L, s[0].b = R_ONE; return 1;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        s[0].a = aL, s[0].b = R_ONE; return 1;
    case GR_COMBINE_FUNCTION_SCALE_OTHER:
        s[0].a = O, s[0].b = F; return 1;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
        s[0].a = O, s[0].b = F, s[0].c = L, s[0].d = R_ONE, s[0].sum = 1; return 1;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        s[0].a = O, s[0].b = F, s[0].c = aL, s[0].d = R_ONE, s[0].sum = 1; return 1;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:              /* f O + f (-L) */
        s[0].a = O, s[0].b = F, s[0].c = neg_of(L), s[0].d = F, s[0].sum = 1; return 1;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:    /* f O + (1 - f) L */
        s[0].a = O, s[0].b = F, s[0].c = L, s[0].d = F ^ RM_INVERT, s[0].sum = 1; return 1;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:  /* (f O - f L) + aL */
        s[0].a = O, s[0].b = F, s[0].c = neg_of(L), s[0].d = F, s[0].sum = 1;
        s[1].a = RS_SPARE0, s[1].b = R_ONE, s[1].c = aL, s[1].d = R_ONE, s[1].sum = 1;
        return 2;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:          /* (1 - f) L */
        s[0].a = L, s[0].b = F ^ RM_INVERT; return 1;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:    /* f (-L) + aL */
        s[0].a = F, s[0].b = neg_of(L), s[0].c = aL, s[0].d = R_ONE, s[0].sum = 1; return 1;
    default:
        s[0].a = L, s[0].b = R_ONE; return 1;
    }
}

/* (a second stage's reads of SPARE0 keep the usage the portion needs: the alpha portion's its alpha) */
static uint32_t usage(uint32_t b, int alpha)
{
    return alpha && (b & 0x0Fu) == RS_SPARE0 ? b | RU_ALPHA : b;
}

/* TMU0's texture combine (its other, a second TMU's output, zero) as the texture's input where it is one: the
 * texel or its alpha, inverted or not; else on the first combiner (1: it took it). */
static int tmu_stage(stage *rgb0, stage *a0)
{
    const tex_unit *u = &tmu0;
    GrCombineFunction_t rf = u->combine_set ? u->rgb_func : GR_COMBINE_FUNCTION_LOCAL;
    GrCombineFunction_t af = u->combine_set ? u->alpha_func : GR_COMBINE_FUNCTION_LOCAL;
    int ri = u->combine_set && u->rgb_invert, ai = u->combine_set && u->alpha_invert, simple = 1;
    tex_rgb_map = ri ? RM_INVERT : 0u, tex_a_map = ai ? RM_INVERT : 0u;
    switch (rf) {
    case GR_COMBINE_FUNCTION_LOCAL: case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: break;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA: case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        tex_rgb_map |= RU_ALPHA; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
        if ((u->rgb_factor & 7) != GR_COMBINE_FACTOR_ZERO)
            simple = 0;                         /* ((1 - f) T: a product) */
        else if (u->rgb_factor & 8)
            tex_rgb_map = ri ? RM_INVERT : 0u, simple = -1;   /* (f one: zero) */
        break;
    default: simple = -1; break;                /* (other zero: zero) */
    }
    if (simple == -1) {                         /* (zero, or one inverted, as the texture) */
        simple = 0;
        rgb0->a = ri ? R_ONE : RS_ZERO, rgb0->b = R_ONE;
        a0->a = RS_TEX0 | RU_ALPHA, a0->b = R_ONE;
        if (af != GR_COMBINE_FUNCTION_LOCAL && af != GR_COMBINE_FUNCTION_LOCAL_ALPHA)
            approx("TMU0's alpha combine with its colour's zero");
        return 1;
    }
    switch (af) {
    case GR_COMBINE_FUNCTION_LOCAL: case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA: break;
    default:
        approx("TMU0's alpha combine");
        break;
    }
    if (simple)
        return 0;
    {
        /* (1 - f) T on the first combiner, f the texel's own colour or alpha */
        int fa = (u->rgb_factor & 7) == GR_COMBINE_FACTOR_LOCAL_ALPHA;
        rgb0->a = RS_TEX0, rgb0->b = (RS_TEX0 | (fa ? RU_ALPHA : 0u)) ^ ((u->rgb_factor & 8) ? 0u : RM_INVERT);
        if (ri)
            approx("an inverted texture combine on a combiner");
        a0->a = RS_TEX0 | RU_ALPHA, a0->b = R_ONE;
        tex_rgb_map = tex_a_map = 0;
        return 1;
    }
}

static void combiners(int textured, int chroma, int fogged)
{
    const mg_state *st = &mg.st;
    stage cs[2], as[2], t0rgb, t0a;
    uint32_t ain[2] = { 0, 0 }, aout[2] = { 0, 0 }, cin[2] = { 0, 0 }, cout[2] = { 0, 0 }, f0, f1, b, g;
    int nc, na, base = 0, i, L = g_of_local(st->cc_local), O = g_of_other(st->cc_other);
    int aL = g_of_local(st->ac_local), aO = g_of_other(st->ac_other);
    memset(&t0rgb, 0, sizeof t0rgb), memset(&t0a, 0, sizeof t0a);
    tex_src = textured ? RS_TEX0 : ~0u;
    tex_rgb_map = tex_a_map = 0;
    if (textured && tmu_stage(&t0rgb, &t0a)) {
        base = 1;
        cin[0] = IN(t0rgb.a, t0rgb.b, RS_ZERO, RS_ZERO), cout[0] = RO_AB_SPARE0;
        ain[0] = IN(t0a.a, t0a.b, RS_ZERO, RS_ZERO), aout[0] = RO_AB_SPARE0;
        tex_src = RS_SPARE0;
    }
    nc = stages_of(st->cc_func, in_of(O, 0, 0), in_of(L, 0, 0), in_of(aL, 1, 0) | RU_ALPHA,
                   factor_in(st->cc_factor, 0, L, aL, aO), cs);
    na = stages_of(st->ac_func, in_of(aO, 1, 0) | RU_ALPHA, in_of(aL, 1, 0) | RU_ALPHA, in_of(aL, 1, 0) | RU_ALPHA,
                   factor_in(st->ac_factor, 1, L, aL, aO) | RU_ALPHA, as);
    if (mg_fog_source() != GR_FOG_DISABLE && (st->fog_mode & GR_FOG_MULT2)) {
        nc = 1;                                 /* (MULT2: the fog term alone, f times the fog colour) */
        memset(cs, 0, sizeof cs);
    }
    if (chroma && textured) {
        /* (keyed texels' alpha 0 into the alpha the test reads: the alpha times the texel's) */
        if (na == 1 && !as[0].sum && as[0].b == R_ONE)
            as[0].b = in_of(G_TEX, 1, 0) | RU_ALPHA;
        else if (na == 1) {
            as[1].a = RS_SPARE0 | RU_ALPHA, as[1].b = in_of(G_TEX, 1, 0) | RU_ALPHA, na = 2;
        } else
            approx("chroma key with a two-stage alpha combine");
    }
    if (base + nc > 2 || base + na > 2) {
        approx("TMU0's combine with a three-term combine");
        if (base + nc > 2) nc = 2 - base;
        if (base + na > 2) na = 2 - base;
    }
    for (i = 0; i < nc; i++) {
        cin[base + i] = IN(cs[i].a, cs[i].b, cs[i].c, cs[i].d);
        cout[base + i] = cs[i].sum ? RO_SUM_SPARE0 : RO_AB_SPARE0;
    }
    for (i = 0; i < na; i++) {
        ain[base + i] = IN(usage(as[i].a, 1), usage(as[i].b, 1), usage(as[i].c, 1), usage(as[i].d, 1));
        aout[base + i] = as[i].sum ? RO_SUM_SPARE0 : RO_AB_SPARE0;
    }
    /* A portion with fewer stages than the other passes SPARE0 on unchanged (its value at the first stage's end). */
    if (base + nc < base + na || (base && !nc))
        for (i = base + nc; i < 2; i++)
            cin[i] = IN(RS_SPARE0, R_ONE, RS_ZERO, RS_ZERO), cout[i] = RO_AB_SPARE0;
    if (base + na < base + nc || (base && !na))
        for (i = base + na; i < 2; i++)
            ain[i] = IN(RS_SPARE0 | RU_ALPHA, R_ONE, RS_ZERO, RS_ZERO), aout[i] = RO_AB_SPARE0;
    {
        int n = base + (nc > na ? nc : na);
        cout[1] |= (n >= 2 ? 5u : 3u) << 27;    /* (Mesa: the second combiner's operation bits: 3 one, 5 two) */
    }
    for (i = 0; i < 2; i++) {
        mc(M_RC_IN_ALPHA(i), ain[i]);
        mc(M_RC_IN_RGB(i), cin[i]);
        mc(M_RC_COLOR(i), mg_color_to_argb(st->constant_color));
        mc(M_RC_OUT_ALPHA(i), aout[i]);
        mc(M_RC_OUT_RGB(i), cout[i]);
    }
    /* Final: A B + (1 - A) C + D, alpha G; A the fog factor (or one), C the fog colour, B the combine (inverted in
     * the mapping where Glide inverts it), G its alpha likewise. */
    b = RS_SPARE0 | (st->cc_invert ? RM_INVERT : 0u);
    g = RS_SPARE0 | RU_ALPHA | (st->ac_invert ? RM_INVERT : 0u);
    if (fogged)
        f0 = IN(RS_FOG | RU_ALPHA, b, RS_FOG, RS_ZERO), f1 = IN(RS_FOG | RU_ALPHA, RS_ZERO, g, 0);
    else
        f0 = IN(R_ONE, b, R_ONE, RS_ZERO), f1 = IN(R_ONE, RS_ZERO, g, 0);
    mc(M_RC_FINAL0, f0);
    mc(M_RC_FINAL1, f1);
}

/* ---- State and triangles --------------------------------------------------------------------------------------- */

static uint32_t blend_src(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return 0;
    case GR_BLEND_SRC_ALPHA: return 0x302;
    case GR_BLEND_DST_COLOR: return 0x306;
    case GR_BLEND_DST_ALPHA: return 1;          /* no destination alpha: reads as 1 (as the Matrox, the TNT2) */
    case GR_BLEND_ONE: return 1;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return 0x303;
    case GR_BLEND_ONE_MINUS_DST_COLOR: return 0x307;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return 0;
    case GR_BLEND_ALPHA_SATURATE: return 0x308;
    default: return 1;
    }
}

static uint32_t blend_dst(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return 0;
    case GR_BLEND_SRC_ALPHA: return 0x302;
    case GR_BLEND_SRC_COLOR: return 0x300;
    case GR_BLEND_DST_ALPHA: return 1;
    case GR_BLEND_ONE: return 1;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return 0x303;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return 0x301;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return 0;
    default: return 0;
    }
}

/* Glide's compare (NEVER 0 .. ALWAYS 7) is GL's order: GL_NEVER 0x200 on; W-buffering's linear 1/w reversed. */
static uint32_t gl_cmp(GrCmpFnc_t f, int reversed)
{
    if (reversed)
        switch (f) {
        case GR_CMP_LESS: f = GR_CMP_GREATER; break;
        case GR_CMP_GREATER: f = GR_CMP_LESS; break;
        case GR_CMP_LEQUAL: f = GR_CMP_GEQUAL; break;
        case GR_CMP_GEQUAL: f = GR_CMP_LEQUAL; break;
        default: break;
        }
    return 0x200u + (uint32_t)f;
}

void cel_draw(const GrVertex *a, const GrVertex *b, const GrVertex *c, const tnt2_vertex v[3], GrLOD_t lod,
              int wmode, int depth, int fog)
{
    const mg_state *st = &mg.st;
    tex_level_hw hw;
    tex_variant var;
    gvert o[3];
    double s[3], t[3], ks = 0, kt = 0, ms = 0, mt = 0;
    float hx = (float)mg.rw * 0.5f, hy = (float)mg.rh * 0.5f, kx = 1.0f / hx, ky = 1.0f / hy;
    int n, bound, chroma = st->chroma_mode == GR_CHROMAKEY_ENABLE;
    if (!va_off)
        return;
    memset(&var, 0, sizeof var);
    if (chroma) {
        var.kind = TV_CHROMA;
        var.chroma = mg_color_to_argb(st->chroma_value) & 0xFFFFFFu;
    }
    bound = tnt2be_uses_texture() && tmu0.has_source && tex_bind_level(lod, &var, &hw) == 0;
    if (bound) {
        uint32_t fmt = hw.hwfmt == HW_TW15 ? TF_A1R5G5B5 : hw.hwfmt == HW_TW12 ? TF_A4R4G4B4 : TF_R5G6B5;
        uint32_t wrap_s = tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP ? 3u : 1u, wrap_t = tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP ? 3u : 1u;
        uint32_t mag = tmu0.magf == GR_TEXTUREFILTER_BILINEAR ? 2u : 1u, min = tmu0.minf == GR_TEXTUREFILTER_BILINEAR ? 2u : 1u;
        mc(M_TEX_OFFSET(0), hw.org);
        mc(M_TEX_FORMAT(0), TF_DMA0 | TF_BASE | fmt | (uint32_t)hw.h_log2 << 20 | (uint32_t)hw.w_log2 << 16 |
                            wrap_s << 24 | wrap_t << 28);
        mc(M_TEX_FILTER(0), mag << 28 | min << 24);
        mc(M_TEX_ENABLE(0), TE_ENABLE);
        ks = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.w_log2));
        kt = (double)hw.logical_maxdim / (256.0 * (double)(1 << hw.h_log2));
        tex_noalpha = hw.hwfmt == HW_TW16;
    } else {
        mc(M_TEX_ENABLE(0), 0);
        tex_noalpha = 0;
    }
    combiners(bound, chroma, fog);
    mc(M_FOG_ENABLE, (uint32_t)fog);
    if (fog)
        mc(M_FOG_COLOR, (st->fog_mode & GR_FOG_ADD2) ? 0xFF000000u : 0xFF000000u | ((mg_color_to_argb(st->fog_color) & 0xFFu) << 16) |
                        (mg_color_to_argb(st->fog_color) & 0xFF00u) | ((mg_color_to_argb(st->fog_color) >> 16) & 0xFFu));
    if (!st->color_mask_rgb || (st->blend_src == GR_BLEND_ONE && st->blend_dst == GR_BLEND_ZERO))
        mc(M_BLEND_ENABLE, 0);
    else {
        mc(M_BLEND_ENABLE, 1);
        mc(M_BLEND_SRC, blend_src(st->blend_src));
        mc(M_BLEND_DST, blend_dst(st->blend_dst));
    }
    mc(M_COLOR_MASK, st->color_mask_rgb ? 0x00010101u : 0u);
    if (st->alpha_test_func != GR_CMP_ALWAYS) {
        mc(M_ALPHA_ENABLE, 1);
        mc(M_ALPHA_FUNC, gl_cmp(st->alpha_test_func, 0));
        mc(M_ALPHA_REF, (uint32_t)st->alpha_test_ref & 0xFFu);
    } else if (chroma && bound) {
        mc(M_ALPHA_ENABLE, 1);
        mc(M_ALPHA_FUNC, gl_cmp(GR_CMP_GREATER, 0));
        mc(M_ALPHA_REF, 0);
    } else
        mc(M_ALPHA_ENABLE, 0);
    if (depth) {
        mc(M_DEPTH_ENABLE, 1);
        mc(M_DEPTH_FUNC, gl_cmp(st->depth_func, wmode));
        mc(M_DEPTH_WRITE, (uint32_t)!!st->depth_mask);
    } else {
        mc(M_DEPTH_ENABLE, 0);
        mc(M_DEPTH_WRITE, 0);
    }
    mc(M_DITHER_ENABLE, (uint32_t)(st->dither != GR_DITHER_DISABLE));
    /* Coordinates: s and t (0..256 over the larger side) in the bound level's texture, whole periods taken off when
     * it repeats (the TNT2 back end's: the chip interpolates in floats). */
    tnt2be_st_of(a, &s[0], &t[0]); tnt2be_st_of(b, &s[1], &t[1]); tnt2be_st_of(c, &s[2], &t[2]);
    if (!tmu0.combine_set)
        ks = kt = 0;                    /* (retail Glide: no s, t until a texture combine is set, as the Matrox's) */
    for (n = 0; n < 3; n++)
        s[n] *= ks, t[n] *= kt;
    if (bound && tmu0.clamp_s != GR_TEXTURECLAMP_CLAMP) {
        ms = s[0] < s[1] ? s[0] : s[1];
        ms = s[2] < ms ? s[2] : ms;
        ms = ms > -1e9 && ms < 1e9 ? mga_floor(ms) : 0;
    }
    if (bound && tmu0.clamp_t != GR_TEXTURECLAMP_CLAMP) {
        mt = t[0] < t[1] ? t[0] : t[1];
        mt = t[2] < mt ? t[2] : mt;
        mt = mt > -1e9 && mt < 1e9 ? mga_floor(mt) : 0;
    }
    for (n = 0; n < 3; n++) {
        /* (window coordinates times w, w = 1/oow: the chip's division gives them back, and its perspective is the
         * texture's) */
        float w = v[n].rhw > 0 ? 1.0f / v[n].rhw : 1.0f, z = v[n].sz;
        o[n].x = (v[n].sx + nudge_x - hx) * kx * w, o[n].y = (v[n].sy + nudge_y - hy) * ky * w, o[n].z = (2.0f * z - 1.0f) * w, o[n].w = w;
        o[n].col = v[n].color;
        o[n].s = (float)(s[n] - ms), o[n].t = (float)(t[n] - mt);
        o[n].fog = (float)(v[n].specular >> 24) * (1.0f / 255.0f);
    }
    if (va_room(3) != 0)
        return;
    memcpy((void *)(tnt2_fb + va_off + va_n * VSIZE), o, 3 * VSIZE);
    va_n += 3;
}
