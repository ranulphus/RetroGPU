/* sst.c - initialisation, hardware query and window open/close. */
#include "glide/mg.h"
#include "backend/backend.h"
#include "trace/trace.h"
#include "mga/sys.h"
#include "mga/mmio.h"
#include <string.h>

mg_ctx mg;

void mg_fatal(const char *msg)
{
    mg_log(MG_LOG_ERROR, "fatal: %s", msg);
    if (mg.error_cb)
        mg.error_cb(msg, FXTRUE);
}

const mg_backend *mg_be;
volatile uint8_t *mg_vram;

/* The card: the first back end that finds one, the Matrox cards' first. Asked again (the board queries), a
 * back end other than the Matrox one answers for itself without the Matrox one's search. */
int mg_device_open(void)
{
    if (mg_be && mg_be != &mg_mga_backend)
        return mg_be->probe();
    if (mg_mga_backend.probe() == 0) {
        mg_be = &mg_mga_backend;
        return 0;
    }
    if (mg_tnt2_backend.probe() == 0) {
        mg_be = &mg_tnt2_backend;
        return 0;
    }
    if (mg_virge_backend.probe() == 0) {
        mg_be = &mg_virge_backend;
        return 0;
    }
    if (mg_r128_backend.probe() == 0) {
        mg_be = &mg_r128_backend;
        return 0;
    }
    if (mg_radeon_backend.probe() == 0) {
        mg_be = &mg_radeon_backend;
        return 0;
    }
    return -1;
}

GR_ENTRY(void, grGlideInit, (void))
{
    GrErrorCallbackFnc_t cb = mg.error_cb;
    if (mg.initialised)
        return;
    mg_log_init();
    memset(&mg, 0, sizeof mg);
    mg.error_cb = cb;
    mg_line("MGL-INIT MGA-Glide %s", MGA_GLIDE_VERSION);
    mg_config_load();
    trace_init_hook();
    mg.gamma = 1.7f;            /* retail Voodoo Graphics default, measured by t22_d */
    strcpy(mg.version, mg_config.report_version);
    mg_state_defaults();
    if (mg_device_open() == 0)
        mg.initialised = 1;
}

GR_ENTRY(void, grGlideShutdown, (void))
{
    if (mg.open)
        grSstWinClose();
    trace_flush();
    mg_line("MGL-SHUTDOWN frames=%u", mg.frame);
    mg.initialised = 0;
}

GR_ENTRY(void, grGlideGetVersion, (char version[80]))
{
    /* May be called before grGlideInit: load the options on demand. */
    if (!mg_config.report_version[0])
        mg_config_load();
    strcpy(version, mg_config.report_version);
}

GR_ENTRY(void, grErrorSetCallback, (void (*fnc)(const char *string, FxBool fatal)))
{
    mg.error_cb = fnc;
}

static void fill_voodoo(GrVoodooConfig_t *v)
{
    v->fbRam = mg_config.report_fb_mb;
    v->fbiRev = 2;
    v->nTexelfx = mg_config.report_tmus;
    v->sliDetect = FXFALSE;
    v->tmuConfig[0].tmuRev = 1;
    v->tmuConfig[0].tmuRam = mg_config.report_tmu_mb;
    v->tmuConfig[1].tmuRev = 1;
    v->tmuConfig[1].tmuRam = mg_config.report_tmu_mb;
}

GR_ENTRY(FxBool, grSstQueryBoards, (GrHwConfiguration *hwConfig))
{
    memset(hwConfig, 0, sizeof *hwConfig);
    if (mg_device_open() < 0)
        return FXFALSE;
    hwConfig->num_sst = 1;
    return FXTRUE;
}

GR_ENTRY(FxBool, grSstQueryHardware, (GrHwConfiguration *hwConfig))
{
    memset(hwConfig, 0, sizeof *hwConfig);
    if (mg_device_open() < 0)
        return FXFALSE;
    hwConfig->num_sst = 1;
    hwConfig->SSTs[0].type = mg_config.report_voodoo2 ? GR_SSTTYPE_Voodoo2 : GR_SSTTYPE_VOODOO;
    fill_voodoo(&hwConfig->SSTs[0].sstBoard.VoodooConfig);
    return FXTRUE;
}

GR_ENTRY(void, grSstSelect, (int which_sst))
{
    mg.sst = which_sst;
}

static const struct { int res, w, h; } resolutions[] = {
    { GR_RESOLUTION_320x200, 320, 200 }, { GR_RESOLUTION_320x240, 320, 240 },
    { GR_RESOLUTION_400x256, 400, 256 }, { GR_RESOLUTION_512x384, 512, 384 },
    { GR_RESOLUTION_640x200, 640, 200 }, { GR_RESOLUTION_640x350, 640, 350 },
    { GR_RESOLUTION_640x400, 640, 400 }, { GR_RESOLUTION_640x480, 640, 480 },
    { GR_RESOLUTION_800x600, 800, 600 }, { GR_RESOLUTION_960x720, 960, 720 },
    { GR_RESOLUTION_856x480, 856, 480 }, { GR_RESOLUTION_512x256, 512, 256 },
    { GR_RESOLUTION_1024x768, 1024, 768 }, { GR_RESOLUTION_1280x1024, 1280, 1024 },
    { GR_RESOLUTION_1600x1200, 1600, 1200 }, { GR_RESOLUTION_400x300, 400, 300 },
};

GR_ENTRY(FxBool, grSstWinOpen, (FxU32 hWnd, GrScreenResolution_t screen_resolution,
                                GrScreenRefresh_t refresh_rate, GrColorFormat_t color_format,
                                GrOriginLocation_t origin_location, int nColBuffers, int nAuxBuffers))
{
    int w = 640, h = 480;
    unsigned r;
    MGA_UNUSED(hWnd);
    MGA_UNUSED(refresh_rate);
    if (!mg.initialised)
        grGlideInit();
    if (!mg.initialised)
        return FXFALSE;
    if (mg.open)
        grSstWinClose();
    for (r = 0; r < MGA_ARRAY_LEN(resolutions); r++)
        if ((unsigned)resolutions[r].res == screen_resolution) {
            w = resolutions[r].w;
            h = resolutions[r].h;
        }
    return mg_be->win_open(w, h, color_format, origin_location, nColBuffers, nAuxBuffers);
}

/* A fault while the window is open: leave the screen readable (the host
 * prints its register dump next) and the engine idle, then chain. */
static void on_fault(int exc, uint32_t err, uint32_t eip)
{
    mg_line("MGL-EXC %02x err=%x eip=%x", exc, err, eip);
    trace_flush();
    if (mg.open) {
        mg_be->fault();                 /* (no waiting for a chip that may be stuck) */
        mg.open = 0;
    }
    sys_unhook_exit();
    sys_unhook_faults();
}

/* The game exits to DOS without grGlideShutdown. */
static void on_exit(void)
{
    trace_flush();
    if (mg.open) {
        mg_line("MGL-EXIT-HOOK video restored");
        mg_be->exit_restore();
        mg.open = 0;
    }
    sys_unhook_faults();
    sys_unhook_exit();
}

void mg_hooks_install(void)
{
    if (!mg_config.hooks)
        return;
    if (sys_hook_faults(on_fault) < 0 || sys_hook_exit(on_exit) < 0)
        mg_log(MG_LOG_WARN, "fault/exit hooks unavailable");
}

#ifdef MGA_REGCRC
/* REGCRC=1 builds: the running CRC of every register write and WARP vertex so far (the HAL's register trace), at
 * each swap and at close: the check that moving the Matrox code behind the back-end table changed no write
 * (glide/tools/conform/regcrc.py, gl/docs/b1.md's method). */
void mg_regcrc(const char *at)
{
    mg_line("MGL-REGCRC %s frame=%u crc=%08lx n=%lu", at, mg.frame, (unsigned long)mga_trace_crc(),
            (unsigned long)mga_trace_crc_count());
}
#endif

GR_ENTRY(void, grSstWinClose, (void))
{
    if (!mg.open)
        return;
#ifdef MGA_REGCRC
    mg_regcrc("close");
#endif
    mg_be->win_close();
}

GR_ENTRY(FxU32, grSstScreenWidth, (void)) { return (FxU32)(mg.open ? mg.width : 640); }
GR_ENTRY(FxU32, grSstScreenHeight, (void)) { return (FxU32)(mg.open ? mg.height : 480); }

GR_ENTRY(void, grSstIdle, (void))
{
    if (mg.open)
        mg_be->sync();
}

GR_ENTRY(FxBool, grSstIsBusy, (void))
{
    return mg.open && mg_be->busy() ? FXTRUE : FXFALSE;
}

GR_ENTRY(FxU32, grSstStatus, (void))
{
    /* Voodoo status: bit 6 vertical retrace, bits 7-9 FBI/TMU/SST busy. */
    FxU32 s = 0x0FFFF03F;      /* FIFO free counts */
    if (mg.open && mg_be->in_vblank())
        s |= 1u << 6;
    if (grSstIsBusy())
        s |= (1u << 7) | (1u << 9);
    return s;
}

GR_ENTRY(FxBool, grSstVRetraceOn, (void))
{
    return mg.open && mg_be->in_vblank() ? FXTRUE : FXFALSE;
}

GR_ENTRY(FxU32, grSstVideoLine, (void))
{
    return mg.open ? mg_be->vcount() : 0;
}

GR_ENTRY(void, grSstOrigin, (GrOriginLocation_t origin))
{
    mg.st.origin = origin == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
}

int mg_hx(int x) { return mg.scaled ? (int)mga_floor(x * mg.sx + 0.5) : x; }
int mg_hy(int y) { return mg.scaled ? (int)mga_floor(y * mg.sy + 0.5) : y; }
