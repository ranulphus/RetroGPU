/* sdlkeys - SDL3's DOS keyboard (its IRQ 1 handler, installed with the video
 * driver) seen through SDL events: every key down and up goes to COM1. Loop A
 * types Enter, A and Escape after "HX-TEST ready" (tools/sdl/loopa.sh); the
 * program ends at Escape's release, or after 30 seconds. */
#include "hx.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Event e;
    Uint64 end;
    int enter = 0, a = 0, esc = 0;

    hx_init(argc, argv, "sdlkeys");
    hx_test("init", SDL_Init(SDL_INIT_VIDEO), "%s", SDL_GetError());
    win = SDL_CreateWindow("sdlkeys", 320, 200, 0);       /* gives the keyboard a focus window */
    hx_test("window", win != NULL, "%s", SDL_GetError());
    if (hx_failures())
        hx_done(1);
    hx_test("ready", 1, "type now");
    end = SDL_GetTicks() + 30000;
    while (SDL_GetTicks() < end && esc < 2) {
        while (SDL_PollEvent(&e)) {
            if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP)
                continue;
            hx_log("HX-SDLKEY %s scancode=%d key=%s", e.type == SDL_EVENT_KEY_DOWN ? "down" : "up",
                   (int)e.key.scancode, SDL_GetKeyName(e.key.key));
            if (e.key.scancode == SDL_SCANCODE_RETURN)
                enter |= e.type == SDL_EVENT_KEY_DOWN ? 1 : 2;
            if (e.key.scancode == SDL_SCANCODE_A)
                a |= e.type == SDL_EVENT_KEY_DOWN ? 1 : 2;
            if (e.key.scancode == SDL_SCANCODE_ESCAPE)
                esc += 1;
        }
        SDL_Delay(10);
    }
    hx_test("enter", enter == 3, "down and up seen: %d", enter);
    hx_test("a", a == 3, "down and up seen: %d", a);
    hx_test("escape", esc >= 2, "events seen: %d", esc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    hx_done(hx_failures() ? 1 : 0);
    return 0;
}
