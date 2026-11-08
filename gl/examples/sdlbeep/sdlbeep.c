/* sdlbeep - SDL3's DOS audio (the Sound Blaster driver: DMA, IRQ, and the
 * ring its cooperative audio thread fills): a 440 Hz tone for two seconds.
 * Loop A runs it with an SB16, BLASTER set and --wav, and looks for the tone
 * in the recording (tools/sdl/loopa.sh). SDL_Delay is what lets the audio
 * thread run: the scheduler switches only at yields.
 *
 *   SDLBEEP [--busy MS]   the main thread works MS milliseconds between
 *                         yields (one SDL_Delay(0) each; with no video,
 *                         SDL_PumpEvents does not yield): a game that yields
 *                         once a frame; the tone must not break up
 *
 * Either way, no chunk may underrun: the driver counts the chunks the card
 * played as silence because the ring was empty (SDL patch 0005). */
#include "hx.h"
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define RATE 22050

static int underruns = -1, chunks = 0;

/* SDL's log to COM1, and the driver's underrun count from it (SDL patch
 * 0005: "SoundBlaster: U of N chunks underran ..." when the device closes;
 * under GLOS, patch 0009's "GLOS: U of N chunks underran ..."). */
static void log_to_serial(void *u, int category, SDL_LogPriority priority, const char *msg)
{
    (void)u;
    (void)category;
    (void)priority;
    hx_log("SDL %s", msg);
    if (SDL_sscanf(msg, "SoundBlaster: %d of %d chunks underran", &underruns, &chunks) != 2)
        SDL_sscanf(msg, "GLOS: %d of %d chunks underran", &underruns, &chunks);
}

int main(int argc, char **argv)
{
    static Sint16 tone[RATE * 2];
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, RATE };
    SDL_AudioStream *s;
    Uint64 end;
    int i, busy = 0;

    for (i = 1; i + 1 < argc; i++)                /* ours, taken out before the harness's */
        if (!SDL_strcmp(argv[i], "--busy")) {
            busy = SDL_atoi(argv[i + 1]);
            SDL_memmove(&argv[i], &argv[i + 2], (size_t)(argc - i - 1) * sizeof *argv);
            argc -= 2;
            i--;
        }
    hx_init(argc, argv, "sdlbeep");
    SDL_SetLogOutputFunction(log_to_serial, NULL);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_AUDIO, SDL_LOG_PRIORITY_DEBUG);
    hx_test("init", SDL_Init(SDL_INIT_AUDIO), "%s", SDL_GetError());
    if (hx_failures())
        hx_done(1);
    hx_log("audio driver %s, busy %d ms", SDL_GetCurrentAudioDriver(), busy);
    s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    hx_test("open", s != NULL, "%s", SDL_GetError());
    if (!s)
        hx_done(1);
    for (i = 0; i < RATE * 2; i++)
        tone[i] = (Sint16)(12000.0 * sin(2.0 * M_PI * 440.0 * i / RATE));
    hx_test("queue", SDL_PutAudioStreamData(s, tone, sizeof tone), "%s", SDL_GetError());
    SDL_FlushAudioStream(s);                    /* no more data: let the resampler finish the tail */
    SDL_ResumeAudioStreamDevice(s);
    end = SDL_GetTicks() + 6000;
    while (SDL_GetAudioStreamQueued(s) > 0 && SDL_GetTicks() < end) {
        if (busy) {                             /* a frame of work, then one yield */
            Uint64 until = SDL_GetTicks() + (Uint64)busy;
            while (SDL_GetTicks() < until)
                continue;
            SDL_Delay(0);
        } else
            SDL_Delay(10);
    }
    hx_test("drained", SDL_GetAudioStreamQueued(s) == 0, "%d bytes left", SDL_GetAudioStreamQueued(s));
    SDL_Delay(300);                             /* let the ring and the DMA buffer play out */
    SDL_DestroyAudioStream(s);
    SDL_Quit();
    /* Every chunk the card played was mixed in time: the audio thread kept the
     * ring fed between the main thread's yields. */
    hx_test("underruns", underruns == 0, "%d of %d chunks underran", underruns, chunks);
    hx_done(hx_failures() ? 1 : 0);
    return 0;
}
