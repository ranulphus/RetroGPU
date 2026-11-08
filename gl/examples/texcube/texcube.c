/* texcube - M4 rung 4: textured, mipmapped, fogged cubes spinning, with an
 * alpha-tested cutout and a translucent quad; --frames N, --bench. --hang:
 * after 30 frames it begins one (a clear: the engine is its own) and never
 * finishes it, for GLOS's watchdog (GLOS M8). */
#include "hx.h"
#include <GL/gl.h>
#include <GL/dosgl.h>
#include <string.h>
#include <time.h>
#include "../common/texscene.h"

int main(int argc, char **argv)
{
    int f, i, bench = 0;
    uclock_t t0;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--bench")) {
            bench = 1;
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof *argv);
            argc--; i--;
        }
    hx_init(argc, argv, "texcube");
    if (dglInit(NULL) != 0) {
        hx_test("init", 0, "%s", dglGetErrorString());
        hx_done(HX_INIT_FAILED);
    }
    if (bench)
        dglSetVSync(0);
    texscene_setup();
    t0 = uclock();
    for (f = 0; f < hx_args.frames; f++) {
        if (hx_args.hang && f == 30) {
            hx_log("HX-TEXCUBE hang at frame %d", f);
            glClear(GL_COLOR_BUFFER_BIT);
            for (;;)
                ;
        }
        texscene_draw(f);
        if (f < hx_args.frames - 1)
            dglSwapBuffers();
    }
    if (bench) {
        double s = (double)(uclock() - t0) / UCLOCKS_PER_SEC;
        hx_stat("bench frames=%d seconds=%.2f fps=%.1f", f, s, f / (s > 0 ? s : 1));
    }
    hx_test("gl-errors", glGetError() == GL_NO_ERROR, "");
    dglSwapBuffers();
    hx_snap_screen("texcube");
    dglShutdown();
    hx_done(0);
    return 0;
}
