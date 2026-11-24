/* dcopy.c - DCOPY: one copy between two surfaces at a time, each checked alone (RD1: DTWOD's full run leaves a few
 * stray pixels at the same screen places wherever the surfaces are in memory, some just past a copy's right edge).
 *
 *   DCOPY [8|16|32] [n=N] [base=MB] [cpubg] [seed=N]
 *
 * The screen (640x480, the mode's pitch) and a source surface (640x300, the same pitch) from megabyte base (16) on.
 * The source holds a pattern made from each pixel's place (written by the CPU once). N (200) times: the screen
 * filled with one value (by the engine; cpubg: by the CPU), one radeon_copy_from of a random rectangle, a sync, and
 * every screen pixel compared: inside the rectangle the source's, outside the fill's. DCOPY-BAD lines (up to 16):
 * the rectangle, the pixels wrong inside and outside it and the box around each kind, a first wrong pixel of each
 * with what was read and wanted. */
#include "hx.h"
#include "mga/sys.h"
#include "radeon/radeon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define SH 300

static uint32_t rs = 1;
static uint32_t rnd(uint32_t n) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) % n; }
static int bpp = 8, bytes = 1;

static uint32_t spat(int x, int y)
{
    uint32_t v = ((uint32_t)x * 0x9E3779B1u) ^ ((uint32_t)y * 0x85EBCA6Bu) ^ 0x5A5AA5A5u;
    v ^= v >> 15;
    return bpp == 8 ? v & 0xFFu : bpp == 16 ? v & 0xFFFFu : v;
}

static uint32_t get(const volatile uint8_t *row, int x)
{
    return bpp == 8 ? row[x] : bpp == 16 ? ((const volatile uint16_t *)row)[x] : ((const volatile uint32_t *)row)[x];
}

static void put(volatile uint8_t *row, int x, uint32_t v)
{
    if (bpp == 8) row[x] = (uint8_t)v;
    else if (bpp == 16) ((volatile uint16_t *)row)[x] = (uint16_t)v;
    else ((volatile uint32_t *)row)[x] = v;
}

int main(int argc, char **argv)
{
    radeon_mode m;
    uint32_t vbase = 16u << 20, soff, bg;
    int i, n = 200, cpubg = 0, said = 0, bad_rects = 0, x, y, pitch;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "8")) bpp = 8, bytes = 1;
        else if (!strcmp(argv[i], "16")) bpp = 16, bytes = 2;
        else if (!strcmp(argv[i], "32")) bpp = 32, bytes = 4;
        else if (!strncmp(argv[i], "n=", 2)) n = atoi(argv[i] + 2);
        else if (!strncmp(argv[i], "base=", 5)) vbase = (uint32_t)atoi(argv[i] + 5) << 20;
        else if (!strncmp(argv[i], "seed=", 5)) rs = (uint32_t)atoi(argv[i] + 5);
        else if (!strcmp(argv[i], "cpubg")) cpubg = 1;
    hx_init(1, argv, "dcopy");
    if (radeon_find() < 0 || radeon_map() < 0) {
        hx_test("map", 0, "no Radeon mapped");
        hx_done(HX_INIT_FAILED);
    }
    if (radeon_find_mode(W, H, bpp, &m) < 0 || radeon_set_mode(&m) < 0) {
        hx_test("mode", 0, "no %dx%dx%d mode", W, H, bpp);
        hx_done(1);
    }
    pitch = m.pitch_bytes;
    soff = vbase + (2u << 20);
    radeon_set_start(vbase);
    radeon_sync(1000000);
    for (y = 0; y < SH; y++)
        for (x = 0; x < W; x++)
            put(radeon.fb + soff + (uint32_t)y * (uint32_t)pitch, x, spat(x, y));
    radeon_vram_written(soff + (uint32_t)SH * (uint32_t)pitch - 4u);
    hx_log("DCOPY %d bits, pitch %d, the screen at %lu MB, the source 2 MB on; %d copies, the background by the %s", bpp,
           pitch, (unsigned long)(vbase >> 20), n, cpubg ? "CPU" : "engine");
    for (i = 0; i < n; i++) {
        int w = 1 + (int)rnd(300), h = 1 + (int)rnd(200), sx = (int)rnd((uint32_t)(W - w + 1)), sy = (int)rnd((uint32_t)(SH - h + 1));
        int dx = (int)rnd((uint32_t)(W - w + 1)), dy = (int)rnd((uint32_t)(H - h + 1));
        int in_bad = 0, out_bad = 0, ix0 = W, iy0 = H, ix1 = -1, iy1 = -1, ox0 = W, oy0 = H, ox1 = -1, oy1 = -1;
        int fx = -1, fy = -1, gx = -1, gy = -1;
        uint32_t fgot = 0, fwant = 0, ggot = 0, gwant = 0;
        bg = spat(i * 7 + 3, i * 13 + 5) | 1u;
        if (bpp == 8) bg &= 0xFF; else if (bpp == 16) bg &= 0xFFFF;
        radeon_set_target(vbase, pitch, bpp);
        radeon_set_clip(0, 0, W, H);
        if (cpubg) {
            radeon_sync(1000000);
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    put(radeon.fb + vbase + (uint32_t)y * (uint32_t)pitch, x, bg);
            radeon_vram_written(vbase + (uint32_t)H * (uint32_t)pitch - 4u);
        } else
            radeon_fill(0, 0, W, H, bg);
        radeon_copy_from(soff, pitch, sx, sy, dx, dy, w, h);
        if (radeon_sync(2000000) < 0) {
            hx_log("DCOPY-BAD copy %d: the engine never went idle", i);
            break;
        }
        for (y = 0; y < H; y++) {
            const volatile uint8_t *row = radeon.fb + vbase + (uint32_t)y * (uint32_t)pitch;
            for (x = 0; x < W; x++) {
                int in = x >= dx && x < dx + w && y >= dy && y < dy + h;
                uint32_t want = in ? spat(sx + x - dx, sy + y - dy) : bg, got = get(row, x);
                if (got == want)
                    continue;
                if (in) {
                    if (!in_bad++) fx = x, fy = y, fgot = got, fwant = want;
                    if (x < ix0) ix0 = x;
                    if (x > ix1) ix1 = x;
                    if (y < iy0) iy0 = y;
                    if (y > iy1) iy1 = y;
                } else {
                    if (!out_bad++) gx = x, gy = y, ggot = got, gwant = want;
                    if (x < ox0) ox0 = x;
                    if (x > ox1) ox1 = x;
                    if (y < oy0) oy0 = y;
                    if (y > oy1) oy1 = y;
                }
            }
        }
        if (in_bad || out_bad) {
            bad_rects++;
            if (said++ < 16)
                hx_log("DCOPY-BAD copy %d: %d,%d -> %d,%d %dx%d: inside %d wrong (%d,%d-%d,%d; at %d,%d %lx want %lx), "
                       "outside %d wrong (%d,%d-%d,%d; at %d,%d %lx want %lx)", i, sx, sy, dx, dy, w, h, in_bad, ix0, iy0, ix1,
                       iy1, fx, fy, (unsigned long)fgot, (unsigned long)fwant, out_bad, ox0, oy0, ox1, oy1, gx, gy,
                       (unsigned long)ggot, (unsigned long)gwant);
        }
    }
    radeon_text_mode();
    radeon_unmap();
    hx_test("dcopy", bad_rects == 0, "%d of %d copies left wrong pixels", bad_rects, n);
    hx_done(0);
    return 0;
}
