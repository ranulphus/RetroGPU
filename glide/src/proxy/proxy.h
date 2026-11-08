/* proxy.h - GLTRACE.OVL: a call-recording proxy in front of a retail
 * runtime (docs/trace.md). Installed as GLIDE2X.OVL; it loads the retail
 * OVL named by MGAGLIDE retail=PATH (default GLIDE2X.3DF in the current
 * directory) with leload and forwards every call to it. */
#ifndef MG_PROXY_H
#define MG_PROXY_H
#include "glapi.h"

extern glapi_t px;
extern int px_loaded;

void   px_load(void);                  /* load and bind the retail OVL (first call) */
void   px_init(void);                  /* grGlideInit: configuration, trace start */
void   px_missing(int id);
void   px_frame(void);                 /* after each forwarded grBufferSwap */
void   px_shutdown(void);

void   px_gu_alloc(GrMipMapId_t id, GrChipID_t tmu, FxU8 odd_even_mask, int width, int height,
                   GrTextureFormat_t fmt, GrMipMapMode_t mm_mode, GrLOD_t smallest_lod, GrLOD_t largest_lod,
                   GrAspectRatio_t aspect, GrTextureClampMode_t s_clamp_mode, GrTextureClampMode_t t_clamp_mode,
                   GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode, float lod_bias,
                   FxBool trilinear);

FxBool px_lfb_lock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin,
                   FxBool pixelPipeline, GrLfbInfo_t *info);
void   px_lfb_unlock(GrLock_t type, GrBuffer_t buffer);          /* record the written spans */
FxBool px_lfb_unlock_forward(GrLock_t type, GrBuffer_t buffer);  /* copy them in, then unlock */
#endif
