/* vram.c - the texture heap in the VRAM left after the colour and depth
 * buffers (PRD FR-HAL-4, §8.3): first fit, blocks coalesced on free, 32-byte
 * granularity (TEXORG's alignment). Small text textures come and go all the
 * time in ClassiCube, so fragmentation is the risk this keeps down.
 *
 * A block that queued draws may still read is retired rather than freed:
 * it goes back to the heap only after the next completed engine sync
 * (dgl_vram_sync_done), so a new upload can never overwrite texels the
 * engine has yet to fetch. */
#include "gl_tex.h"
#include <stdlib.h>

#define GRAIN 32u
#define MAX_BLOCKS 4096

typedef struct { uint32_t off, size; int used; } block;

#define MAX_RETIRED 1024

static block blocks[MAX_BLOCKS];
static int nblocks;
static uint32_t heap_used;
static uint32_t retired[MAX_RETIRED];
static int nretired;

void dgl_vram_init(uint32_t start, uint32_t end)
{
    start = (start + GRAIN - 1) & ~(GRAIN - 1);
    nblocks = 0;
    heap_used = 0;
    nretired = 0;
    if (end > start) {
        blocks[0].off = start;
        blocks[0].size = end - start;
        blocks[0].used = 0;
        nblocks = 1;
    }
}

uint32_t dgl_vram_used(void) { return heap_used; }

int dgl_vram_alloc(uint32_t size, uint32_t *off)
{
    int i, j;
    size = (size + GRAIN - 1) & ~(GRAIN - 1);
    if (!size)
        size = GRAIN;
    for (i = 0; i < nblocks; i++) {
        if (blocks[i].used || blocks[i].size < size)
            continue;
        if (blocks[i].size > size) {
            if (nblocks == MAX_BLOCKS)
                return -1;
            for (j = nblocks; j > i + 1; j--)
                blocks[j] = blocks[j - 1];
            nblocks++;
            blocks[i + 1].off = blocks[i].off + size;
            blocks[i + 1].size = blocks[i].size - size;
            blocks[i + 1].used = 0;
            blocks[i].size = size;
        }
        blocks[i].used = 1;
        heap_used += size;
        *off = blocks[i].off;
        return 0;
    }
    return -1;
}

void dgl_vram_free(uint32_t off)
{
    int i, j;
    for (i = 0; i < nblocks && blocks[i].off != off; i++)
        ;
    if (i == nblocks || !blocks[i].used)
        return;
    blocks[i].used = 0;
    heap_used -= blocks[i].size;
    /* Merge with the free neighbours. */
    if (i + 1 < nblocks && !blocks[i + 1].used) {
        blocks[i].size += blocks[i + 1].size;
        for (j = i + 1; j + 1 < nblocks; j++)
            blocks[j] = blocks[j + 1];
        nblocks--;
    }
    if (i > 0 && !blocks[i - 1].used) {
        blocks[i - 1].size += blocks[i].size;
        for (j = i; j + 1 < nblocks; j++)
            blocks[j] = blocks[j + 1];
        nblocks--;
    }
}

int dgl_vram_blocks(void) { return nblocks; }

uint32_t dgl_vram_largest_free(void)
{
    uint32_t m = 0;
    int i;
    for (i = 0; i < nblocks; i++)
        if (!blocks[i].used && blocks[i].size > m)
            m = blocks[i].size;
    return m;
}

int dgl_vram_retire(uint32_t off)
{
    if (nretired == MAX_RETIRED)
        return -1;
    retired[nretired++] = off;
    return 0;
}

int dgl_vram_retired(void) { return nretired; }

void dgl_vram_sync_done(void)
{
    while (nretired)
        dgl_vram_free(retired[--nretired]);
}
