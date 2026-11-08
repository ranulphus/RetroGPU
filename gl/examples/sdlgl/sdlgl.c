/* sdlgl - DOS-GL through SDL3 (tools/sdl/patches/0001): an SDL_WINDOW_OPENGL
 * window, its context and swaps, with SDL's keyboard, joystick and Sound
 * Blaster audio running beside it. Draws TEXCUBE's scene (examples/common),
 * so its last frame must equal TEXCUBE's, and plays a 440 Hz tone
 * throughout. Key and joystick events go to COM1 (HX-SDLKEY, HX-SDLJOY).
 *
 *   SDLGL [--frames N] [--mode WxH] [--slow MS]
 *   SDLGL --modes        every 16-bit mode SDL lists: window, context, a few
 *                        frames, snapshot, context and window destroyed
 *
 * --mode picks the window size (any of DOS-GL's, including the scaled and
 * zoomed ones); --slow MS makes every frame take MS more milliseconds of
 * CPU work, pumping SDL's events every 10 ms as a game's loop would. */
#include "hx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/dosgl.h>
#include <dpmi.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "../common/texscene.h"

#define RATE 22050

static void tone_fill(SDL_AudioStream *s, double *phase)
{
    static Sint16 buf[RATE / 10];
    int i;
    while (SDL_GetAudioStreamQueued(s) < (int)sizeof buf * 3) {
        for (i = 0; i < RATE / 10; i++) {
            buf[i] = (Sint16)(8000.0 * sin(*phase));
            *phase += 2.0 * M_PI * 440.0 / RATE;
        }
        *phase = fmod(*phase, 2.0 * M_PI);
        SDL_PutAudioStreamData(s, buf, sizeof buf);
    }
}

/* The video mode as the BIOS reports it, and where DOS-GL shows its picture. */
static void log_display(const char *when)
{
    __dpmi_regs r;
    const DGLDeviceInfo *d = dglGetDeviceInfo();
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F03;
    __dpmi_int(0x10, &r);
    hx_log("HX-SDLGL %s: vbe ax=%04x mode=%04x; DOS-GL %dx%d on %dx%d, picture %d,%d %dx%d (%s)", when, r.x.ax,
           r.x.bx, d ? d->width : 0, d ? d->height : 0, d ? d->display_width : 0, d ? d->display_height : 0,
           d ? d->picture_x : 0, d ? d->picture_y : 0, d ? d->picture_width : 0, d ? d->picture_height : 0,
           d && d->fit ? d->fit : "-");
}

static void log_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
            hx_log("HX-SDLKEY %s scancode=%d", e.type == SDL_EVENT_KEY_DOWN ? "down" : "up", (int)e.key.scancode);
        else if (e.type == SDL_EVENT_JOYSTICK_AXIS_MOTION)
            hx_log("HX-SDLJOY axis %d %d", e.jaxis.axis, e.jaxis.value);
        else if (e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || e.type == SDL_EVENT_JOYSTICK_BUTTON_UP)
            hx_log("HX-SDLJOY button %d %s", e.jbutton.button, e.jbutton.down ? "down" : "up");
    }
}

int main(int argc, char **argv)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, RATE };
    SDL_AudioStream *audio = NULL;
    SDL_Joystick *joy = NULL;
    SDL_JoystickID *joys;
    SDL_Window *win;
    SDL_GLContext ctx;
    double phase = 0;
    int w = 640, h = 480, slow = 0, f, i, n = 0, pw = 0, ph = 0, all_modes = 0;

    for (i = 1; i < argc; i++) {
        int used = 0;
        if (!strcmp(argv[i], "--mode") && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &w, &h) == 2)
            used = 2;
        else if (!strcmp(argv[i], "--slow") && i + 1 < argc)
            slow = atoi(argv[i + 1]), used = 2;
        else if (!strcmp(argv[i], "--modes"))
            all_modes = 1, used = 1;
        if (used) {
            memmove(&argv[i], &argv[i + used], (size_t)(argc - i - used + 1) * sizeof *argv);
            argc -= used;
            i--;
        }
    }
    hx_init(argc, argv, "sdlgl");
    hx_test("init", SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK), "%s", SDL_GetError());
    if (hx_failures())
        hx_done(HX_INIT_FAILED);

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (all_modes) {
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n);
        int good = 0, tried = 0;
        for (i = 0; modes && i < n; i++) {
            char name[32];
            if (modes[i]->format != SDL_PIXELFORMAT_RGB565)
                continue;
            tried++;
            win = SDL_CreateWindow("sdlgl", modes[i]->w, modes[i]->h, SDL_WINDOW_OPENGL);
            ctx = win ? SDL_GL_CreateContext(win) : NULL;
            pw = ph = 0;
            if (win)
                SDL_GetWindowSizeInPixels(win, &pw, &ph);
            sprintf(name, "mode-%dx%d", modes[i]->w, modes[i]->h);
            hx_test(name, ctx != NULL && pw == modes[i]->w && ph == modes[i]->h, "drawing %dx%d%s%s", pw, ph,
                    ctx ? "" : ": ", ctx ? "" : SDL_GetError());
            if (ctx) {
                good++;
                texscene_setup();
                for (f = 0; f < 3; f++) {
                    texscene_draw(f);
                    SDL_GL_SwapWindow(win);
                }
                log_display(name);
                sprintf(name, "sglm%02d", i);          /* 8.3: C:\OUT\SGLMnn.PPM */
                hx_log("HX-SDLGL snapshot %s is %dx%d", name, modes[i]->w, modes[i]->h);
                hx_snap_screen(name);
                SDL_GL_DestroyContext(ctx);
            }
            if (win)
                SDL_DestroyWindow(win);
        }
        SDL_free(modes);
        hx_test("modes", tried > 0 && good == tried, "%d of %d modes", good, tried);
        SDL_Quit();
        hx_done(hx_failures() ? 1 : 0);
    }
    win = SDL_CreateWindow("sdlgl", w, h, SDL_WINDOW_OPENGL);
    hx_test("window", win != NULL, "%dx%d: %s", w, h, win ? "" : SDL_GetError());
    if (!win)
        hx_done(HX_INIT_FAILED);
    ctx = SDL_GL_CreateContext(win);
    hx_test("context", ctx != NULL, "%s", ctx ? (const char *)glGetString(GL_RENDERER) : SDL_GetError());
    if (!ctx)
        hx_done(HX_INIT_FAILED);
    log_display("context");
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    hx_test("size", pw == w && ph == h, "asked %dx%d, drawing %dx%d", w, h, pw, ph);
    hx_test("proc", SDL_GL_GetProcAddress("glDrawElements") != NULL, "SDL_GL_GetProcAddress finds GL 1.1");

    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    hx_log("audio %s", audio ? SDL_GetCurrentAudioDriver() : SDL_GetError());
    if (audio) {
        tone_fill(audio, &phase);
        SDL_ResumeAudioStreamDevice(audio);
    }
    joys = SDL_GetJoysticks(&n);
    if (joys && n > 0)
        joy = SDL_OpenJoystick(joys[0]);
    SDL_free(joys);
    hx_log("HX-SDLJOY %s axes=%d buttons=%d", joy ? SDL_GetJoystickName(joy) : "none",
           joy ? SDL_GetNumJoystickAxes(joy) : 0, joy ? SDL_GetNumJoystickButtons(joy) : 0);

    texscene_setup();
    /* SDL keeps the current window in thread-local storage (patch 0002). */
    hx_test("current", SDL_GL_GetCurrentWindow() == win && SDL_GL_GetCurrentContext() == ctx,
            "current window %p (ours %p), context %p", (void *)SDL_GL_GetCurrentWindow(), (void *)win,
            (void *)SDL_GL_GetCurrentContext());
    hx_test("ready", 1, "drawing %d frames", hx_args.frames);
    for (f = 0; f < hx_args.frames; f++) {
        log_events();
        if (audio)
            tone_fill(audio, &phase);
        if (slow) {                             /* CPU work with a pump every 10 ms */
            Uint64 end = SDL_GetTicks() + (Uint64)slow, pump = SDL_GetTicks() + 10;
            while (SDL_GetTicks() < end)
                if (SDL_GetTicks() >= pump) {
                    SDL_PumpEvents();
                    pump += 10;
                }
        }
        texscene_draw(f);
        if (f < hx_args.frames - 1 && !SDL_GL_SwapWindow(win) && f == 0)
            hx_test("swap", 0, "%s", SDL_GetError());
    }
    hx_test("gl-errors", glGetError() == GL_NO_ERROR, "");
    SDL_GL_SwapWindow(win);
    log_display("last frame");
    hx_snap_screen("sdlgl");
    log_events();
    if (joy)
        SDL_CloseJoystick(joy);
    if (audio)
        SDL_DestroyAudioStream(audio);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    hx_done(hx_failures() ? 1 : 0);
    return 0;
}
