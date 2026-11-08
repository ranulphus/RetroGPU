/* layout.h - compile-time checks that the clean-room structures match the
 * shipped Glide 2.x ABI. Pointers are 32-bit on every DOS target; the
 * pointer-bearing checks are skipped on 64-bit hosts. */
#ifndef MGAGLIDE_LAYOUT_H
#define MGAGLIDE_LAYOUT_H
#include <stddef.h>
#include "glide/glide2.h"

#define MGA_LAYOUT_ASSERT(name, cond) typedef char mga_layout_##name[(cond) ? 1 : -1]

MGA_LAYOUT_ASSERT(vertex_size, sizeof(GrVertex) == 60);
MGA_LAYOUT_ASSERT(vertex_r, offsetof(GrVertex, r) == 12);
MGA_LAYOUT_ASSERT(vertex_ooz, offsetof(GrVertex, ooz) == 24);
MGA_LAYOUT_ASSERT(vertex_a, offsetof(GrVertex, a) == 28);
MGA_LAYOUT_ASSERT(vertex_oow, offsetof(GrVertex, oow) == 32);
MGA_LAYOUT_ASSERT(vertex_tmu, offsetof(GrVertex, tmuvtx) == 36);
MGA_LAYOUT_ASSERT(state_size, sizeof(GrState) == 312);
MGA_LAYOUT_ASSERT(hwconfig_size, sizeof(GrHwConfiguration) == 148);
MGA_LAYOUT_ASSERT(ncc_size, sizeof(GuNccTable) == 112);
MGA_LAYOUT_ASSERT(perf_size, sizeof(GrSstPerfStats_t) == 20);
#if !defined(__x86_64__) && !defined(__aarch64__)
MGA_LAYOUT_ASSERT(lfbinfo_size, sizeof(GrLfbInfo_t) == 20);
MGA_LAYOUT_ASSERT(texinfo_size, sizeof(GrTexInfo) == 20);
MGA_LAYOUT_ASSERT(info3df_size, sizeof(Gu3dfInfo) == 1056);
#endif
#endif
