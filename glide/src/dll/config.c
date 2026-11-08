/* config.c - options from MGAGLIDE.CFG (current directory) and the
 * MGAGLIDE environment variable, e.g.
 *     SET MGAGLIDE=log=3 exit_after=600 snap=60,300 trace=1
 * Later sources override earlier ones; every option is logged. */
#include "dll/config.h"
#include "rt/rt.h"
#include <string.h>

mg_config_t mg_config;

static int to_int(const char *s)
{
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

static void set_opt(const char *key, const char *val)
{
    if (!strcmp(key, "version")) { strncpy(mg_config.report_version, val, sizeof mg_config.report_version - 1); }
    else if (!strcmp(key, "fb_mb")) mg_config.report_fb_mb = to_int(val);
    else if (!strcmp(key, "tmu_mb")) mg_config.report_tmu_mb = to_int(val);
    else if (!strcmp(key, "tmus")) mg_config.report_tmus = to_int(val);
    else if (!strcmp(key, "voodoo2")) mg_config.report_voodoo2 = to_int(val);
    else if (!strcmp(key, "z32")) mg_config.force_z32 = to_int(val);
    else if (!strcmp(key, "log")) mg_config.log_level = to_int(val);
    else if (!strcmp(key, "exit_after")) mg_config.exit_after = to_int(val);
    else if (!strcmp(key, "trace")) mg_config.trace = to_int(val);
    else if (!strcmp(key, "trace_path")) strncpy(mg_config.trace_path, val, sizeof mg_config.trace_path - 1);
    else if (!strcmp(key, "retail")) strncpy(mg_config.retail_path, val, sizeof mg_config.retail_path - 1);
    else if (!strcmp(key, "trace_from")) mg_config.trace_from = to_int(val);
    else if (!strcmp(key, "trace_to")) mg_config.trace_to = to_int(val);
    else if (!strcmp(key, "stats")) mg_config.stats_every = to_int(val);
    else if (!strcmp(key, "g100_additive")) mg_config.g100_additive = to_int(val);
    else if (!strcmp(key, "strict")) mg_config.combine_strict = to_int(val);
    else if (!strcmp(key, "bilinear")) mg_config.forced_bilinear = to_int(val);
    else if (!strcmp(key, "gamma")) mg_config.gamma_enable = to_int(val);
    else if (!strcmp(key, "census")) mg_config.census = to_int(val);
    else if (!strcmp(key, "hooks")) mg_config.hooks = to_int(val);
    else if (!strcmp(key, "bpp")) mg_config.bpp = to_int(val);
    else if (!strcmp(key, "trilinear")) mg_config.trilinear = to_int(val);
    else if (!strcmp(key, "hwmip")) mg_config.hwmip = to_int(val);
    else if (!strcmp(key, "scale")) mg_config.scale = !strcmp(val, "force") ? 2 : to_int(val);
    else if (!strcmp(key, "scale_filter")) mg_config.scale_filter = !strcmp(val, "bilinear") || !strcmp(val, "1");
    else if (!strcmp(key, "zoom")) mg_config.zoom = to_int(val);
    else if (!strcmp(key, "warp")) mg_config.warp = to_int(val);
    else if (!strcmp(key, "fast")) mg_config.fast = to_int(val);
    else if (!strcmp(key, "agp")) mg_config.agp = to_int(val);
    else if (!strcmp(key, "dma_ring")) mg_config.dma_ring_kb = to_int(val);
    else if (!strcmp(key, "dma_list")) mg_config.dma_list_div = to_int(val);
    else if (!strcmp(key, "dma_kick")) mg_config.dma_kick_kb = to_int(val);
    else if (!strcmp(key, "res")) {
        const char *xp = val;
        mg_config.res_w = to_int(val);
        while (*xp && *xp != 'x' && *xp != 'X') xp++;
        mg_config.res_h = *xp ? to_int(xp + 1) : 0;
    }
    else if (!strcmp(key, "snap")) {
        const char *p = val;
        mg_config.nsnap = 0;
        while (*p && mg_config.nsnap < 8) {
            mg_config.snap[mg_config.nsnap++] = to_int(p);
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
    } else {
        mg_log(MG_LOG_WARN, "config: unknown option %s", key);
        return;
    }
    mg_line("MGL-CONFIG %s=%s", key, val);
}

/* Parse "key=value" items separated by spaces, commas-in-values allowed,
 * newlines or semicolons; '#' starts a comment. */
static void parse(const char *s)
{
    char key[32], val[64];
    while (*s) {
        int k = 0, v = 0;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == ';') s++;
        if (*s == '#') { while (*s && *s != '\n') s++; continue; }
        while (*s && *s != '=' && *s != ' ' && *s != '\n' && *s != '\r' && k < 31) key[k++] = *s++;
        key[k] = 0;
        if (*s != '=') { while (*s && *s != '\n' && *s != ' ') s++; continue; }
        s++;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && *s != ';' && v < 63) val[v++] = *s++;
        val[v] = 0;
        if (k)
            set_opt(key, val);
    }
}

void mg_config_load(void)
{
    char buf[1024];
    const char *e;
    memset(&mg_config, 0, sizeof mg_config);
    strcpy(mg_config.report_version, "2.43");
    mg_config.report_fb_mb = 2;
    mg_config.report_tmu_mb = 2;
    mg_config.report_tmus = 1;
    mg_config.log_level = MG_LOG_INFO;
    mg_config.force_z32 = -1;           /* auto: 32-bit depth when VRAM allows */
    mg_config.gamma_enable = 1;         /* 0: keep a linear ramp whatever the game asks */
    mg_config.hooks = 1;
    mg_config.bpp = 16;
    mg_config.scale = 1;
    mg_config.warp = 1;
    mg_config.fast = 1;
    mg_config.agp = 1;
    mg_config.dma_ring_kb = 128;        /* (Glide writes many registers a triangle: half of it list) */
    mg_config.dma_list_div = 2;
    strcpy(mg_config.trace_path, "MGTRACE.BIN");
    strcpy(mg_config.retail_path, "GLIDE2X.3DF");
    mg_config.trace_to = 0x7FFFFFFF;
    if (mg_read_file("MGAGLIDE.CFG", buf, sizeof buf) > 0)
        parse(buf);
    e = mg_getenv("MGAGLIDE");
    if (e)
        parse(e);
    mg_log_set_level(mg_config.log_level);
}
