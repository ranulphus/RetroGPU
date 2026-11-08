/* test_gl_h_abi.c - DOS-GL's <GL/gl.h> and ClassiCube's own GL 1.1
 * declarations in one translation unit. ClassiCube never includes our
 * header (it declares its own prototypes), so any difference in a
 * prototype, typedef or token spelling is a redeclaration error here under
 * -Werror instead of a link-time or run-time surprise. */
#include <GL/gl.h>
#include <GL/glext.h>

#define CC_BUILD_GL11 1
#include "misc/opengl/GLCommon.h"
#define GL_FUNC(retType, name, args) GLAPI retType APIENTRY name args;
#include "misc/opengl/GL1Funcs.h"

#include <stdio.h>

int main(void)
{
    /* Tokens ClassiCube defines under its own names in _GLShared.h. */
    if (GL_TEXTURE_MAX_LEVEL != 0x813D || GL_BGRA_EXT != 0x80E1) {
        puts("test_gl_h_abi: token mismatch");
        return 1;
    }
    puts("test_gl_h_abi: ok");
    return 0;
}
