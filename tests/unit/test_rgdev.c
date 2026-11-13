/* test_rgdev.c - RetroGPU's device choice (hal/src/rg/device.c): DGL_DEVICE
 * by name, or the first device whose probe succeeds; the reasons given. */
#include "unit.h"
#include "rg/device.h"
#include <string.h>

static int mga_ok, soft_ok, probes;
static int probe_mga(void) { probes++; if (!mga_ok) rg_set_error("no Matrox card"); return mga_ok ? 0 : -1; }
static int probe_soft(void) { probes++; return soft_ok ? 0 : -1; }

const rg_device rg_mga_device = { .name = "mga", .probe = probe_mga };
static const rg_device soft = { .name = "soft", .probe = probe_soft };

static char logged[256];
static void log_line(const char *line) { strncpy(logged, line, sizeof logged - 1); }

int unit_main(void)
{
    const rg_device *const two[] = { &rg_mga_device, &soft };

    /* The build's own list: the Matrox device alone. */
    mga_ok = 1;
    CHECK(rg_choose(NULL) == &rg_mga_device);
    CHECK(rg_choose("") == &rg_mga_device);
    CHECK(rg_choose("mga") == &rg_mga_device);
    mga_ok = 0;
    CHECK(rg_choose(NULL) == NULL);
    CHECK_STR(rg_error(), "no Matrox card");
    CHECK(rg_choose("soft") == NULL);
    CHECK_STR(rg_error(), "DGL-DEVICE soft: not built");

    /* A longer list: by name, or the first that probes, in order. */
    mga_ok = 0, soft_ok = 1;
    CHECK(rg_choose_from(two, 2, NULL) == &soft);
    mga_ok = 1;
    probes = 0;
    CHECK(rg_choose_from(two, 2, NULL) == &rg_mga_device);
    CHECK_EQ(probes, 1);                        /* (the first that works: the rest are not probed) */
    CHECK(rg_choose_from(two, 2, "soft") == &soft);
    soft_ok = 0;
    CHECK(rg_choose_from(two, 2, "soft") == NULL);

    /* The log: nothing until a logger is set, then each line whole. */
    rg_logf("DGL-X %d", 1);
    CHECK_STR(logged, "");
    rg_set_log(log_line);
    RG_ERR("DGL-WARP tris=%lu", 42ul);
    CHECK_STR(logged, "DGL-WARP tris=42");
    rg_set_log(NULL);
    return 0;
}
