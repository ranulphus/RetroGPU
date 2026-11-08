/* version.c - library identity. */
#include <GL/dosgl.h>

#ifndef DGL_BUILD_ID
#define DGL_BUILD_ID "unknown"
#endif

const char *dglVersion(void)
{
    return "DOS-GL 0.3 (" DGL_BUILD_ID ")";
}
