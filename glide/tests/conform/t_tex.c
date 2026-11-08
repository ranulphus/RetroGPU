/* Texture conformance tests t10-t12. */
#include "ct.h"
#include <math.h>
#include <string.h>

static uint8_t texbuf[256 * 256 * 2 * 2];
static GuTexPalette pal;
static GuNccTable ncc;
static FxU32 next_addr;

static void tex_setup_combine(int modulate)
{
    gl.grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    if (modulate)
        gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    else
        gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
}

/* A test pattern: bit-rich gradients with a checker overlay. */
static uint32_t pattern(int x, int y, int w, int h)
{
    int r = x * 255 / (w > 1 ? w - 1 : 1), g = y * 255 / (h > 1 ? h - 1 : 1);
    int b = ((x / 4 + y / 4) & 1) ? 255 : 32;
    int a = ((x + y) * 255) / (w + h > 2 ? w + h - 2 : 1);
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void make_tables(void)
{
    int i;
    for (i = 0; i < 256; i++)
        pal.data[i] = ((uint32_t)i << 16) | ((uint32_t)(255 - i) << 8) | (uint32_t)((i * 7) & 0xFF);
    memset(&ncc, 0, sizeof ncc);
    for (i = 0; i < 16; i++)
        ncc.yRGB[i] = (FxU8)(i * 16 + 8);
    for (i = 0; i < 4; i++) {
        ncc.iRGB[i][0] = (FxI16)(i * 30 - 45); ncc.iRGB[i][1] = (FxI16)(-i * 10 + 15); ncc.iRGB[i][2] = (FxI16)(i * 5);
        ncc.qRGB[i][0] = (FxI16)(i * 8 - 12); ncc.qRGB[i][1] = (FxI16)(i * 12 - 18); ncc.qRGB[i][2] = (FxI16)(40 - i * 25);
    }
    for (i = 0; i < 4; i++)
        ncc.packed_data[i] = (FxU32)ncc.yRGB[i * 4] | ((FxU32)ncc.yRGB[i * 4 + 1] << 8) |
                             ((FxU32)ncc.yRGB[i * 4 + 2] << 16) | ((FxU32)ncc.yRGB[i * 4 + 3] << 24);
    for (i = 0; i < 4; i++) {
        ncc.packed_data[4 + i] = ((FxU32)(ncc.iRGB[i][0] & 0x1FF) << 18) | ((FxU32)(ncc.iRGB[i][1] & 0x1FF) << 9) |
                                 (FxU32)(ncc.iRGB[i][2] & 0x1FF);
        ncc.packed_data[8 + i] = ((FxU32)(ncc.qRGB[i][0] & 0x1FF) << 18) | ((FxU32)(ncc.qRGB[i][1] & 0x1FF) << 9) |
                                 (FxU32)(ncc.qRGB[i][2] & 0x1FF);
    }
}

/* Encode the pattern in a Glide format (w x h texels at 'dst'). */
static void encode(GrTextureFormat_t fmt, uint8_t *dst, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t c = pattern(x, y, w, h);
            int a = (int)(c >> 24), r = (int)((c >> 16) & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)(c & 0xFF);
            int i = y * w + x, gray = (r + g + b) / 3;
            uint16_t *d16 = (uint16_t *)dst;
            switch (fmt) {
            case GR_TEXFMT_RGB_332: dst[i] = (uint8_t)((r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)); break;
            case GR_TEXFMT_YIQ_422: dst[i] = (uint8_t)(((gray >> 4) << 4) | ((x & 3) << 2) | (y & 3)); break;
            case GR_TEXFMT_ALPHA_8: dst[i] = (uint8_t)a; break;
            case GR_TEXFMT_INTENSITY_8: dst[i] = (uint8_t)gray; break;
            case GR_TEXFMT_ALPHA_INTENSITY_44: dst[i] = (uint8_t)((a & 0xF0) | (gray >> 4)); break;
            case GR_TEXFMT_P_8: dst[i] = (uint8_t)((x * 13 + y * 7) & 0xFF); break;
            case GR_TEXFMT_ARGB_8332: d16[i] = (uint16_t)((a << 8) | (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)); break;
            case GR_TEXFMT_AYIQ_8422: d16[i] = (uint16_t)((a << 8) | ((gray >> 4) << 4) | ((x & 3) << 2) | (y & 3)); break;
            case GR_TEXFMT_RGB_565: d16[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); break;
            case GR_TEXFMT_ARGB_1555: d16[i] = (uint16_t)(((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); break;
            case GR_TEXFMT_ARGB_4444: d16[i] = (uint16_t)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); break;
            case GR_TEXFMT_ALPHA_INTENSITY_88: d16[i] = (uint16_t)((a << 8) | gray); break;
            case GR_TEXFMT_AP_88: d16[i] = (uint16_t)((a << 8) | ((x * 13 + y * 7) & 0xFF)); break;
            default: break;
            }
        }
}

/* Download a single-level texture; returns its info (data points into texbuf). */
static void download(GrTexInfo *ti, GrTextureFormat_t fmt, GrLOD_t lod, GrAspectRatio_t aspect, FxU32 *addr)
{
    int w, h, size = 256 >> lod;
    if (aspect < GR_ASPECT_1x1) { w = size; h = size >> (GR_ASPECT_1x1 - aspect); }
    else { h = size; w = size >> (aspect - GR_ASPECT_1x1); }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    encode(fmt, texbuf, w, h);
    ti->smallLod = lod; ti->largeLod = lod; ti->aspectRatio = aspect; ti->format = fmt; ti->data = texbuf;
    *addr = next_addr;
    gl.grTexDownloadMipMap(GR_TMU0, *addr, GR_MIPMAPLEVELMASK_BOTH, ti);
    next_addr += gl.grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, ti);
}

/* Screen quad with texture coordinates s0..s1, t0..t1 (Glide 0..256 space). */
static void tquad(float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1)
{
    GrVertex v[4];
    ct_vtx(&v[0], x0, y0, 255, 255, 255, 255); v[0].tmuvtx[0].sow = s0; v[0].tmuvtx[0].tow = t0;
    ct_vtx(&v[1], x1, y0, 255, 255, 255, 255); v[1].tmuvtx[0].sow = s1; v[1].tmuvtx[0].tow = t0;
    ct_vtx(&v[2], x1, y1, 255, 255, 255, 255); v[2].tmuvtx[0].sow = s1; v[2].tmuvtx[0].tow = t1;
    ct_vtx(&v[3], x0, y1, 255, 255, 255, 255); v[3].tmuvtx[0].sow = s0; v[3].tmuvtx[0].tow = t1;
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[3]);
}

/* t10: every format, then every aspect ratio and the smallest LODs. */
static void t10(void)
{
    static const GrTextureFormat_t fmts[] = {
        GR_TEXFMT_RGB_565, GR_TEXFMT_ARGB_1555, GR_TEXFMT_ARGB_4444, GR_TEXFMT_RGB_332,
        GR_TEXFMT_ALPHA_8, GR_TEXFMT_INTENSITY_8, GR_TEXFMT_ALPHA_INTENSITY_44, GR_TEXFMT_ALPHA_INTENSITY_88,
        GR_TEXFMT_ARGB_8332, GR_TEXFMT_P_8, GR_TEXFMT_AP_88, GR_TEXFMT_YIQ_422, GR_TEXFMT_AYIQ_8422 };
    int i;
    GrTexInfo ti;
    FxU32 addr;
    if (ct_open(2, 1) < 0) return;
    make_tables();
    gl.grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
    gl.grTexDownloadTable(GR_TMU0, GR_TEXTABLE_NCC0, &ncc);
    gl.grTexNCCTable(GR_TMU0, GR_NCCTABLE_NCC0);
    tex_setup_combine(0);
    gl.grBufferClear(0x00101010, 0, 0);
    next_addr = gl.grTexMinAddress(GR_TMU0);
    for (i = 0; i < 13; i++) {
        float x = 10.0f + (i % 4) * 157, y = 10.0f + (i / 4) * 115;
        download(&ti, fmts[i], GR_LOD_64, GR_ASPECT_1x1, &addr);
        gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
        tquad(x, y, x + 150, y + 108, 0, 0, 256, 256);
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    /* Aspects at LOD 64 and tiny LODs, wrap then clamp. */
    gl.grBufferClear(0x00101010, 0, 0);
    next_addr = 0;
    for (i = 0; i < 7; i++) {
        float x = 10.0f + i * 90;
        /* Glide's coordinate range is 0..256 on the long side and
         * 256/aspect on the short side. */
        float smax = i > 3 ? 256.0f / (float)(1 << (i - 3)) : 256.0f;
        float tmax = i < 3 ? 256.0f / (float)(1 << (3 - i)) : 256.0f;
        download(&ti, GR_TEXFMT_RGB_565, GR_LOD_64, (GrAspectRatio_t)i, &addr);
        gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
        tquad(x, 10, x + 80, 150, 0, 0, smax, tmax);
    }
    for (i = 0; i < 4; i++) {
        float x = 10.0f + i * 150;
        download(&ti, GR_TEXFMT_RGB_565, (GrLOD_t)(GR_LOD_8 + i), GR_ASPECT_1x1, &addr);
        gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
        gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
        tquad(x, 170, x + 140, 300, 0, 0, 512, 512);
        gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
        tquad(x, 310, x + 140, 470, -128, -128, 384, 384);
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t11: point vs bilinear, wrap vs clamp, perspective. */
static void t11(void)
{
    GrTexInfo ti;
    FxU32 addr;
    GrVertex v[4];
    int i;
    if (ct_open(2, 1) < 0) return;
    tex_setup_combine(0);
    next_addr = 0;
    download(&ti, GR_TEXFMT_RGB_565, GR_LOD_16, GR_ASPECT_1x1, &addr);
    gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grBufferClear(0, 0, 0);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    tquad(10, 10, 300, 230, 0, 0, 256, 256);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    tquad(330, 10, 620, 230, 0, 0, 256, 256);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    tquad(10, 250, 200, 470, -256, -256, 512, 512);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    tquad(220, 250, 410, 470, -256, -256, 512, 512);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_CLAMP);
    tquad(430, 250, 620, 470, -256, -256, 512, 512);
    ct_capture(GR_BUFFER_BACKBUFFER);
    /* Perspective-correct ground plane (w from 1 to 20). */
    gl.grBufferClear(0, 0, 0);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    download(&ti, GR_TEXFMT_RGB_565, GR_LOD_64, GR_ASPECT_1x1, &addr);
    gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    for (i = 0; i < 2; i++) {
        float wn = 1.0f, wf = 20.0f, ox = i * 320.0f;
        gl.grTexFilterMode(GR_TMU0, i ? GR_TEXTUREFILTER_BILINEAR : GR_TEXTUREFILTER_POINT_SAMPLED,
                           i ? GR_TEXTUREFILTER_BILINEAR : GR_TEXTUREFILTER_POINT_SAMPLED);
        ct_vtx(&v[0], ox + 150, 60, 255, 255, 255, 255); v[0].oow = 1 / wf; v[0].tmuvtx[0].sow = 0; v[0].tmuvtx[0].tow = 0;
        ct_vtx(&v[1], ox + 170, 60, 255, 255, 255, 255); v[1].oow = 1 / wf; v[1].tmuvtx[0].sow = 1024 / wf; v[1].tmuvtx[0].tow = 0;
        ct_vtx(&v[2], ox + 310, 470, 255, 255, 255, 255); v[2].oow = 1 / wn; v[2].tmuvtx[0].sow = 1024 / wn; v[2].tmuvtx[0].tow = 2048 / wn;
        ct_vtx(&v[3], ox + 10, 470, 255, 255, 255, 255); v[3].oow = 1 / wn; v[3].tmuvtx[0].sow = 0; v[3].tmuvtx[0].tow = 2048 / wn;
        gl.grDrawTriangle(&v[0], &v[1], &v[2]);
        gl.grDrawTriangle(&v[0], &v[2], &v[3]);
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t12: mipmaps, the gu allocator, level and partial downloads, modulate. */
static void t12(void)
{
    static uint16_t chain[256 * 256 * 2];
    GrTexInfo ti;
    GrVertex v[4];
    GrMipMapId_t id;
    int l, i, n = 0;
    uint16_t *p = chain;
    static const uint16_t lodcol[9] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x8410, 0x4208 };
    if (ct_open(2, 1) < 0) return;
    for (l = 0; l <= 8; l++) {
        int s = 256 >> l;
        for (i = 0; i < s * s; i++)
            p[i] = ((i / s / (s > 8 ? s / 8 : 1) + (i % s) / (s > 8 ? s / 8 : 1)) & 1) ? lodcol[l] : (uint16_t)(lodcol[l] >> 1 & 0x7BEF);
        p += s * s;
        n += s * s;
    }
    tex_setup_combine(0);
    gl.grBufferClear(0, 0, 0);
    ti.smallLod = GR_LOD_1; ti.largeLod = GR_LOD_256; ti.aspectRatio = GR_ASPECT_1x1; ti.format = GR_TEXFMT_RGB_565;
    ti.data = chain;
    gl.grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    for (i = 0; i < 2; i++) {
        float ox = i * 320.0f, wn = 1.0f, wf = 30.0f;
        gl.grTexMipMapMode(GR_TMU0, i ? GR_MIPMAP_NEAREST : GR_MIPMAP_DISABLE, FXFALSE);
        ct_vtx(&v[0], ox + 150, 40, 255, 255, 255, 255); v[0].oow = 1 / wf; v[0].tmuvtx[0].sow = 0; v[0].tmuvtx[0].tow = 0;
        ct_vtx(&v[1], ox + 170, 40, 255, 255, 255, 255); v[1].oow = 1 / wf; v[1].tmuvtx[0].sow = 2048 / wf; v[1].tmuvtx[0].tow = 0;
        ct_vtx(&v[2], ox + 310, 300, 255, 255, 255, 255); v[2].oow = 1 / wn; v[2].tmuvtx[0].sow = 2048 / wn; v[2].tmuvtx[0].tow = 8192 / wn;
        ct_vtx(&v[3], ox + 10, 300, 255, 255, 255, 255); v[3].oow = 1 / wn; v[3].tmuvtx[0].sow = 0; v[3].tmuvtx[0].tow = 8192 / wn;
        gl.grDrawTriangle(&v[0], &v[1], &v[2]);
        gl.grDrawTriangle(&v[0], &v[2], &v[3]);
    }
    /* Fixed-size quads that pick each level, via LOD bias. */
    gl.grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
    for (l = 0; l < 8; l++) {
        gl.grTexLodBiasValue(GR_TMU0, (float)l);
        tquad(10.0f + l * 78, 320, 80.0f + l * 78, 390, 0, 0, 256, 256);
    }
    gl.grTexLodBiasValue(GR_TMU0, 0);
    gl.grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
    /* gu allocator path with modulate. */
    gl.guTexMemReset();
    id = gl.guTexAllocateMemory(GR_TMU0, GR_MIPMAPLEVELMASK_BOTH, 64, 64, GR_TEXFMT_RGB_565, GR_MIPMAP_DISABLE,
                                GR_LOD_64, GR_LOD_64, GR_ASPECT_1x1, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP,
                                GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED, 0.0f, FXFALSE);
    encode(GR_TEXFMT_RGB_565, texbuf, 64, 64);
    gl.guTexDownloadMipMap(id, texbuf, NULL);
    gl.guTexSource(id);
    tex_setup_combine(1);
    {
        GrVertex a, b, c;
        ct_vtx(&a, 20, 400, 255, 0, 0, 255); a.tmuvtx[0].sow = 0; a.tmuvtx[0].tow = 0;
        ct_vtx(&b, 300, 400, 0, 255, 0, 255); b.tmuvtx[0].sow = 256; b.tmuvtx[0].tow = 0;
        ct_vtx(&c, 160, 475, 0, 0, 255, 255); c.tmuvtx[0].sow = 128; c.tmuvtx[0].tow = 256;
        gl.grDrawTriangle(&a, &b, &c);
    }
    /* Level download + partial download. */
    encode(GR_TEXFMT_RGB_565, texbuf, 64, 64);
    gl.grTexDownloadMipMapLevel(GR_TMU0, 0x100000, GR_LOD_64, GR_LOD_64, GR_ASPECT_1x1, GR_TEXFMT_RGB_565,
                                GR_MIPMAPLEVELMASK_BOTH, texbuf);
    memset(texbuf, 0xFF, 64 * 16 * 2);
    gl.grTexDownloadMipMapLevelPartial(GR_TMU0, 0x100000, GR_LOD_64, GR_LOD_64, GR_ASPECT_1x1, GR_TEXFMT_RGB_565,
                                       GR_MIPMAPLEVELMASK_BOTH, texbuf, 20, 35);
    ti.smallLod = GR_LOD_64; ti.largeLod = GR_LOD_64; ti.aspectRatio = GR_ASPECT_1x1; ti.format = GR_TEXFMT_RGB_565;
    gl.grTexSource(GR_TMU0, 0x100000, GR_MIPMAPLEVELMASK_BOTH, &ti);
    tex_setup_combine(0);
    tquad(340, 400, 620, 475, 0, 0, 256, 256);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t25: texture state defaults after grSstWinOpen (nothing set but the
 * colour combine and the source). */
static void t25(void)
{
    GrTexInfo ti;
    FxU32 addr;
    if (ct_open(2, 1) < 0) return;
    gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                      GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    next_addr = 0;
    download(&ti, GR_TEXFMT_RGB_565, GR_LOD_16, GR_ASPECT_1x1, &addr);
    gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grBufferClear(0x00202020, 0, 0);
    tquad(10, 10, 310, 230, -256, -256, 512, 512);      /* clamp or wrap? */
    tquad(330, 10, 630, 230, 0, 0, 256, 256);           /* point or bilinear? */
    download(&ti, GR_TEXFMT_RGB_565, GR_LOD_256, GR_ASPECT_1x1, &addr);
    gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    tquad(10, 250, 110, 350, 0, 0, 256, 256);           /* minification: mipmapping? */
    ct_capture(GR_BUFFER_BACKBUFFER);
    /* Frame 2: the same with a texture combine function set (coordinates
     * iterate), still no clamp/filter/mipmap calls. */
    gl.grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    gl.grBufferClear(0x00202020, 0, 0);
    download(&ti, GR_TEXFMT_RGB_565, GR_LOD_16, GR_ASPECT_1x1, &addr);
    gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    tquad(10, 10, 310, 230, -256, -256, 512, 512);
    tquad(330, 10, 630, 230, 0, 0, 256, 256);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* Tinted 64x64 565 texture (tint chooses a colour transform). */
static void tinted64(int tint)
{
    int x, y;
    uint16_t *d = (uint16_t *)texbuf;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            uint32_t c = pattern(x, y, 64, 64);
            int r = (int)((c >> 16) & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)(c & 0xFF);
            r = (r + tint * 37) & 0xFF; g = (g ^ (tint * 53)) & 0xFF; b = (b + tint * 91) & 0xFF;
            if ((x >> 4) == (tint & 3) && (y >> 4) == ((tint >> 2) & 3))
                r = g = b = 255;                 /* a position marker per texture */
            d[y * 64 + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
}

static void info64(GrTexInfo *ti)
{
    ti->smallLod = GR_LOD_64; ti->largeLod = GR_LOD_64; ti->aspectRatio = GR_ASPECT_1x1;
    ti->format = GR_TEXFMT_RGB_565; ti->data = texbuf;
}

#define T19_N 96
#define T19_SIZE 8192u                    /* 64x64 565 */

static void t19_grid(void)
{
    GrTexInfo ti;
    int i;
    info64(&ti);
    for (i = 0; i < T19_N; i++) {
        float x = 4.0f + (i % 12) * 53, y = 4.0f + (i / 12) * 59;
        gl.grTexSource(GR_TMU0, (FxU32)i * T19_SIZE, GR_MIPMAPLEVELMASK_BOTH, &ti);
        tquad(x, y, x + 50, y + 56, 0, 0, 256, 256);
    }
}

/* t19: TMU address stress: many textures, re-downloads at the same
 * addresses, an overlapping download, partial level downloads, palette
 * changes between draws, a source that starts inside a mip chain. */
static void t19(void)
{
    GrTexInfo ti;
    int i, x, y;
    if (ct_open(2, 1) < 0) return;
    tex_setup_combine(0);
    info64(&ti);
    for (i = 0; i < T19_N; i++) {
        tinted64(i);
        gl.grTexDownloadMipMap(GR_TMU0, (FxU32)i * T19_SIZE, GR_MIPMAPLEVELMASK_BOTH, &ti);
    }
    gl.grBufferClear(0x00101010, 0, 0);
    t19_grid();
    ct_capture(GR_BUFFER_BACKBUFFER);

    /* Frame 1: every third texture replaced in place, then drawn again. */
    for (i = 0; i < T19_N; i += 3) {
        tinted64(i + 200);
        gl.grTexDownloadMipMap(GR_TMU0, (FxU32)i * T19_SIZE, GR_MIPMAPLEVELMASK_BOTH, &ti);
    }
    /* Rows 16..31 of texture 7 replaced by a partial download. */
    tinted64(99);
    gl.grTexDownloadMipMapLevelPartial(GR_TMU0, 7 * T19_SIZE, GR_LOD_64, GR_LOD_64, GR_ASPECT_1x1,
                                       GR_TEXFMT_RGB_565, GR_MIPMAPLEVELMASK_BOTH,
                                       (uint16_t *)texbuf + 16 * 64, 16, 31);
    gl.grBufferClear(0x00101010, 0, 0);
    t19_grid();
    ct_capture(GR_BUFFER_BACKBUFFER);

    /* Frame 2: a 128x128 565 texture overlapping textures 0..3; texture 5
     * (untouched) still drawable; a palette change between two draws of
     * one P8 texture; a source starting at the 32x32 level of a chain. */
    gl.grBufferClear(0x00101010, 0, 0);
    encode(GR_TEXFMT_RGB_565, texbuf, 128, 128);
    ti.smallLod = GR_LOD_128; ti.largeLod = GR_LOD_128; ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_RGB_565; ti.data = texbuf;
    gl.grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &ti);
    tquad(10, 10, 210, 210, 0, 0, 256, 256);
    info64(&ti);
    gl.grTexSource(GR_TMU0, 5 * T19_SIZE, GR_MIPMAPLEVELMASK_BOTH, &ti);
    tquad(220, 10, 420, 210, 0, 0, 256, 256);

    make_tables();
    encode(GR_TEXFMT_P_8, texbuf, 64, 64);
    ti.format = GR_TEXFMT_P_8;
    gl.grTexDownloadMipMap(GR_TMU0, 1024u * 1024u, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 1024u * 1024u, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal);
    tquad(430, 10, 630, 105, 0, 0, 256, 128);
    {
        /* Not a per-channel permutation of 0..255: 86Box's Voodoo keys its
         * texture cache on the XOR of the palette entries, which is 0 for
         * any such palette, so two of them look identical to it. */
        static GuTexPalette pal2;
        for (i = 0; i < 256; i++)
            pal2.data[i] = ((uint32_t)(255 - i) << 16) | ((uint32_t)((i * 5) & 0xFF) << 8) | (uint32_t)(i < 255 ? i : 0);   /* XOR of blues = 255 */
        gl.grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal2);
    }
    tquad(430, 115, 630, 210, 0, 128, 256, 256);

    /* Mip chain 128..8 at 1.5 MB; source its 32x32 level onwards. */
    {
        uint16_t *d = (uint16_t *)texbuf;
        int w, off = 0, lod;
        for (lod = GR_LOD_128, w = 128; lod <= GR_LOD_8; lod++, w >>= 1) {
            for (y = 0; y < w; y++)
                for (x = 0; x < w; x++)
                    d[off + y * w + x] = (uint16_t)((lod * 5 & 31) << 11 | ((x * 63 / w) << 5) | ((y * 31) / w));
            off += w * w;
        }
        ti.smallLod = GR_LOD_8; ti.largeLod = GR_LOD_128; ti.format = GR_TEXFMT_RGB_565; ti.data = texbuf;
        gl.grTexDownloadMipMap(GR_TMU0, 1536u * 1024u, GR_MIPMAPLEVELMASK_BOTH, &ti);
        ti.largeLod = GR_LOD_32;
        gl.grTexSource(GR_TMU0, 1536u * 1024u + (128 * 128 + 64 * 64) * 2, GR_MIPMAPLEVELMASK_BOTH, &ti);
        tquad(10, 230, 210, 430, 0, 0, 256, 256);
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* Perspective quad: vertex oow and TMU oow may differ (STW hint). */
static void pquad(float x0, float y0, float x1, float y1, float wl, float wr, int tmu_w)
{
    GrVertex v[4];
    int i;
    for (i = 0; i < 4; i++) {
        float w = (i == 0 || i == 3) ? wl : wr, s = (i == 1 || i == 2) ? 256.0f : 0.0f, t = (i >= 2) ? 256.0f : 0.0f;
        ct_vtx(&v[i], (i == 1 || i == 2) ? x1 : x0, (i >= 2) ? y1 : y0, 255, 255, 255, 255);
        v[i].oow = tmu_w ? 1.0f : 1.0f / w;
        v[i].tmuvtx[0].oow = 1.0f / w;
        v[i].tmuvtx[0].sow = s / w;
        v[i].tmuvtx[0].tow = t / w;
    }
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[3]);
}

/* t20: grGlideGetState / grGlideSetState round trips, and the STW hint. */
static void t20(void)
{
    static GrState sa, sb;
    GrTexInfo t1, t2;
    FxU32 a1, a2;
    GrVertex v[3];
    if (ct_open(2, 1) < 0) return;
    next_addr = 0;
    download(&t1, GR_TEXFMT_RGB_565, GR_LOD_64, GR_ASPECT_1x1, &a1);
    download(&t2, GR_TEXFMT_ARGB_4444, GR_LOD_32, GR_ASPECT_2x1, &a2);
    gl.grBufferClear(0x00203040, 0, 0);
    /* State A: modulated bilinear clamped texture, translucent. */
    gl.grTexSource(GR_TMU0, a1, GR_MIPMAPLEVELMASK_BOTH, &t1);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    tex_setup_combine(1);
    gl.grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grConstantColorValue(0x80FFFFFFu);
    gl.grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    tquad(10, 10, 210, 150, -64, -64, 320, 320);
    gl.grGlideGetState(&sa);
    /* State B: something else entirely. */
    gl.grTexSource(GR_TMU0, a2, GR_MIPMAPLEVELMASK_BOTH, &t2);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    tex_setup_combine(0);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    gl.grChromakeyValue(0);
    gl.grChromakeyMode(GR_CHROMAKEY_ENABLE);
    gl.grCullMode(GR_CULL_NEGATIVE);
    tquad(220, 10, 420, 150, -64, -64, 320, 320);
    /* Back to A. */
    gl.grGlideSetState(&sa);
    tquad(430, 10, 630, 150, -64, -64, 320, 320);
    gl.grGlideGetState(&sb);
    hx_test("state-roundtrip", memcmp(&sa, &sb, sizeof sa) == 0, "GetState after SetState differs");
    /* Culling restored too: this clockwise/counter-clockwise pair draws
     * both halves only if culling is off again. */
    ct_vtx(&v[0], 10, 170, 255, 0, 0, 255); ct_vtx(&v[1], 110, 170, 0, 255, 0, 255);
    ct_vtx(&v[2], 10, 250, 0, 0, 255, 255);
    gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                      GR_COMBINE_OTHER_NONE, FXFALSE);
    gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    gl.grDrawTriangle(&v[0], &v[1], &v[2]);
    gl.grDrawTriangle(&v[0], &v[2], &v[1]);
    /* STW hint: TMU oow differs from vertex oow. */
    gl.grTexSource(GR_TMU0, a1, GR_MIPMAPLEVELMASK_BOTH, &t1);
    tex_setup_combine(0);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    pquad(10, 270, 310, 470, 1.0f, 8.0f, 0);
    gl.grHints(GR_HINT_STWHINT, GR_STWHINT_W_DIFF_TMU0);
    pquad(330, 270, 630, 470, 1.0f, 8.0f, 1);
    gl.grHints(GR_HINT_STWHINT, 0);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t26: a texture replaced in place while it is the current source, drawn
 * without calling grTexSource again: whole-chain, single-level and partial
 * downloads. The TMU reads whatever the memory now holds, so each quad
 * shows the texture as it was when it was drawn. */
static void t26(void)
{
    GrTexInfo ti;
    if (ct_open(2, 1) < 0) return;
    tex_setup_combine(0);
    info64(&ti);
    gl.grBufferClear(0x00101010, 0, 0);
    tinted64(1);
    gl.grTexDownloadMipMap(GR_TMU0, 0x10000, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexSource(GR_TMU0, 0x10000, GR_MIPMAPLEVELMASK_BOTH, &ti);
    tquad(10, 10, 310, 230, 0, 0, 256, 256);
    tinted64(2);
    gl.grTexDownloadMipMap(GR_TMU0, 0x10000, GR_MIPMAPLEVELMASK_BOTH, &ti);
    tquad(330, 10, 630, 230, 0, 0, 256, 256);
    tinted64(3);
    gl.grTexDownloadMipMapLevel(GR_TMU0, 0x10000, GR_LOD_64, GR_LOD_64, GR_ASPECT_1x1, GR_TEXFMT_RGB_565,
                                GR_MIPMAPLEVELMASK_BOTH, texbuf);
    tquad(10, 250, 310, 470, 0, 0, 256, 256);
    tinted64(4);
    gl.grTexDownloadMipMapLevelPartial(GR_TMU0, 0x10000, GR_LOD_64, GR_LOD_64, GR_ASPECT_1x1,
                                       GR_TEXFMT_RGB_565, GR_MIPMAPLEVELMASK_BOTH,
                                       (uint16_t *)texbuf + 32 * 64, 32, 63);
    tquad(330, 250, 630, 470, 0, 0, 256, 256);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t27: the window reopened with another VRAM layout while textures are
 * resident. 640x480 without a depth buffer puts them just past the colour
 * buffers; reopened with one, the depth buffer lies there. New textures are
 * downloaded and drawn (uploading them), the depth buffer is cleared, and
 * the same textures drawn again must be intact: MGA-Glide once freed the
 * old window's blocks into the new heap and placed textures inside the new
 * depth buffer. */
static void t27_row(int first, float y0, float y1, int download)
{
    GrTexInfo ti;
    int i;
    info64(&ti);
    for (i = 0; i < 8; i++) {
        if (download) {
            tinted64(first + i);
            gl.grTexDownloadMipMap(GR_TMU0, 0x10000u * (FxU32)i, GR_MIPMAPLEVELMASK_BOTH, &ti);
        }
        gl.grTexSource(GR_TMU0, 0x10000u * (FxU32)i, GR_MIPMAPLEVELMASK_BOTH, &ti);
        tquad(8.0f + 79.0f * (float)i, y0, 79.0f + 79.0f * (float)i, y1, 0, 0, 256, 256);
    }
}

static void t27(void)
{
    if (ct_open(2, 0) < 0) return;
    tex_setup_combine(0);
    t27_row(1, 10, 80, 1);
    gl.grSstWinClose();
    if (!gl.grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        hx_test("reopen", 0, "grSstWinOpen with a depth buffer failed");
        ct_close();
        return;
    }
    tex_setup_combine(0);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
    gl.grDepthBufferFunction(GR_CMP_ALWAYS);
    gl.grDepthMask(FXTRUE);
    t27_row(9, 10, 80, 1);
    gl.grBufferClear(0x00101010, 0, 0xFFFF);
    t27_row(9, 100, 240, 0);
    t27_row(17, 260, 400, 1);
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

/* t24: texture-coordinate magnitude sweep: large s,t offsets (wrapped)
 * and a range of q, exercising the per-triangle prescale. */
static void t24(void)
{
    GrTexInfo ti;
    FxU32 addr;
    int i, j;
    if (ct_open(2, 1) < 0) return;
    next_addr = 0;
    download(&ti, GR_TEXFMT_RGB_565, GR_LOD_64, GR_ASPECT_1x1, &addr);
    gl.grTexSource(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    gl.grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
    gl.grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    tex_setup_combine(0);
    gl.grBufferClear(0x00101010, 0, 0);
    for (j = 0; j < 4; j++) {                 /* q scale 1, 1/16, 1/256, 1/4096 */
        float q = 1.0f / (float)(1 << (4 * j));
        for (i = 0; i < 8; i++) {             /* s,t offset 0, 256*2^(2i-2)... */
            GrVertex v[4];
            float off = i ? 256.0f * (float)(1 << (2 * i - 1)) : 0.0f, x = 5.0f + i * 79, y = 5.0f + j * 118;
            int k;
            for (k = 0; k < 4; k++) {
                float s = off + ((k == 1 || k == 2) ? 512.0f : 0.0f), t = off + ((k >= 2) ? 512.0f : 0.0f);
                ct_vtx(&v[k], (k == 1 || k == 2) ? x + 75 : x, (k >= 2) ? y + 112 : y, 255, 255, 255, 255);
                v[k].oow = q; v[k].tmuvtx[0].oow = q;
                v[k].tmuvtx[0].sow = s * q; v[k].tmuvtx[0].tow = t * q;
            }
            gl.grDrawTriangle(&v[0], &v[1], &v[2]);
            gl.grDrawTriangle(&v[0], &v[2], &v[3]);
        }
    }
    ct_capture(GR_BUFFER_BACKBUFFER);
    ct_close();
}

const ct_test ct_tex_tests[] = {
    { "t19", t19, "TMU address stress" },
    { "t20", t20, "Get/SetState, hints" },
    { "t24", t24, "texture coordinate magnitude sweep" },
    { "t25", t25, "texture state defaults" },
    { "t26", t26, "texture replaced in place while sourced" },
    { "t27", t27, "textures after reopening the window with another layout" },
    { "t10", t10, "texture formats, aspects, small LODs" },
    { "t11", t11, "filtering, clamping, perspective" },
    { "t12", t12, "mipmaps, gu allocator, level downloads" },
    { NULL, NULL, NULL }
};
