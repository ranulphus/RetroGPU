/* sdlcrash - a program that dies (an illegal instruction, #UD) while SDL's
 * keyboard handler is hooked, its Sound Blaster is playing and DOS-GL owns
 * the screen. Afterwards the machine must be usable: text mode (DOS-GL's
 * fault hook), the keyboard back with the BIOS and the Sound Blaster
 * silent (SDL's: tools/sdl/patches/0003). tools/sdl/loopa.sh follows it
 * with VECCHK check, KEYWAIT and a recording. */
#include "hx.h"
#include <math.h>
#include <GL/gl.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define RATE 22050

int main(int argc, char **argv)
{
    static Sint16 tone[RATE * 4];
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, RATE };
    SDL_AudioStream *s;
    SDL_Window *win;
    int i;

    hx_init(argc, argv, "sdlcrash");
    hx_test("init", SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), "%s", SDL_GetError());
    win = SDL_CreateWindow("sdlcrash", 640, 480, SDL_WINDOW_OPENGL);
    hx_test("context", win && SDL_GL_CreateContext(win), "%s", SDL_GetError());
    s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    hx_test("audio", s != NULL, "%s", SDL_GetError());
    if (hx_failures())
        hx_done(1);
    for (i = 0; i < RATE * 4; i++)
        tone[i] = (Sint16)(8000.0 * sin(2.0 * M_PI * 440.0 * i / RATE));
    SDL_PutAudioStreamData(s, tone, sizeof tone);
    SDL_ResumeAudioStreamDevice(s);
    for (i = 0; i < 30; i++) {
        glClearColor(0.2f, 0.4f, 0.8f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(win);
        SDL_Delay(10);
    }
    hx_log("HX-SDLCRASH now");
    __asm__ volatile("ud2");                    /* #UD: SIGILL */
    hx_test("unreachable", 0, "");
    hx_done(1);
    return 0;
}
