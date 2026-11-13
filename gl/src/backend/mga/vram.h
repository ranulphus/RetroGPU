/* vram.h - the Matrox back end's texture heap (vram.c), apart from mga_be.h
 * so the host's unit test needs no HAL headers. */
#ifndef DGL_MGA_VRAM_H
#define DGL_MGA_VRAM_H
#include <stdint.h>

void     dgl_vram_init(uint32_t start, uint32_t end);
int      dgl_vram_alloc(uint32_t size, uint32_t *off);     /* 0 ok, -1 out of memory */
void     dgl_vram_free(uint32_t off);
uint32_t dgl_vram_used(void);
int      dgl_vram_blocks(void);
uint32_t dgl_vram_largest_free(void);
int      dgl_vram_retire(uint32_t off);   /* free after the next sync; -1 = list full, sync first */
int      dgl_vram_retired(void);           /* blocks waiting for a sync */
void     dgl_vram_sync_done(void);         /* the engine is idle: free the retired blocks */

#endif
