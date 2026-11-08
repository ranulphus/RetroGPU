/* sdlinfo - SDL3 on DOS: version, drivers, display modes, joysticks, audio,
 * reported on COM1 (HX- lines). The first of DOS-GL's SDL examples
 * (docs/sdl.md): checks the pinned, patched SDL builds and starts. */
#include "hx.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char **argv)
{
    int v = SDL_GetVersion(), n = 0, i;
    SDL_DisplayID *displays;
    SDL_JoystickID *joys;

    hx_init(argc, argv, "sdlinfo");
    hx_log("SDL %d.%d.%d %s", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
           SDL_GetRevision());
    hx_test("init", SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK), "%s", SDL_GetError());
    if (hx_failures())
        hx_done(1);
    hx_log("drivers video=%s audio=%s", SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver());

    displays = SDL_GetDisplays(&n);
    hx_test("displays", displays && n > 0, "%d display(s)", n);
    if (displays && n > 0) {
        int count = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(displays[0], &count);
        for (i = 0; modes && i < count; i++)
            hx_log("mode %dx%d %s", modes[i]->w, modes[i]->h, SDL_GetPixelFormatName(modes[i]->format));
        hx_test("modes", count > 0, "%d fullscreen mode(s)", count);
        SDL_free(modes);
    }
    SDL_free(displays);

    joys = SDL_GetJoysticks(&n);
    hx_log("joysticks %d", n);
    for (i = 0; joys && i < n; i++) {
        SDL_Joystick *j = SDL_OpenJoystick(joys[i]);
        if (j) {
            hx_log("joystick %d \"%s\" axes=%d buttons=%d", i, SDL_GetJoystickName(j),
                   SDL_GetNumJoystickAxes(j), SDL_GetNumJoystickButtons(j));
            SDL_CloseJoystick(j);
        }
    }
    SDL_free(joys);

    SDL_Quit();
    hx_done(0);
    return 0;
}
