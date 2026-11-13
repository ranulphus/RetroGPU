/* mga_be.h - GL's Matrox drawing back end, its own parts (docs/b1.md): the
 * texture copy in VRAM each texture object carries (be), the heap, the
 * hardware formats, the combiner's words, the engine's sync epochs. */
#ifndef DGL_MGA_BE_H
#define DGL_MGA_BE_H
#include "../backend.h"
#include "../../dgl/dgl.h"
#include "rg/mga_dev.h"

#include "mga_be_fmt.h"                 /* the hardware texel formats (texconv.c) */

/* A texture's copy in VRAM: every defined level from 0, in one format
 * (dgl_texture.be; made by tex_ready, gone with tex_free). */
typedef struct {
    int      resident, hwfmt, hw_levels;
    int      grey;                  /* luminance or intensity: RGB565 greys stay grey */
    uint32_t vram_off, level_off[DGL_MAX_LEVELS], vram_size;
    int      hw_w_log2, hw_h_log2;  /* level 0 as stored (>= 8 texels) */
    uint32_t drawn;                 /* dgl_sync_epoch + 1 when last drawn, 0 = never */
    const dgl_palette *pal_used;    /* the palette and generation the VRAM copy was expanded with */
    unsigned pal_gen;
} mga_tex;
#define MT(t) ((mga_tex *)(t)->be)

/* Engine syncs: the device's sync runs dgl_mga_synced when it completes,
 * counting them in dgl_sync_epoch. A texture drawn since the last completed
 * sync is busy: queued draws may still read its VRAM copy. */
extern uint32_t dgl_sync_epoch;
#define dgl_texture_busy(t) (MT(t)->drawn == dgl_sync_epoch + 1)
#define dgl_texture_drawn(t) (MT(t)->drawn = dgl_sync_epoch + 1)
void dgl_mga_synced(void);

#include "vram.h"                       /* the texture heap */

/* tex.c */
int  dgl_texture_ready(dgl_texture *t);         /* upload if needed; 0 = drawable */
void dgl_texture_lut(const dgl_texture *t);     /* before drawing a TW8 texture: its palette in the LUT */
int  dgl_white_texture(uint32_t *off);          /* resident 8x8 white TW16 */
int  dgl_mga_tex_sub(dgl_texture *t, int level, int x, int y, int w, int h);
void dgl_mga_tex_free(dgl_texture *t);
int  dgl_mga_tex_info(const dgl_texture *t, int rgba[4]);
void dgl_mga_tex_reset(uint32_t heap_start, uint32_t heap_end);

/* combine.c */
int dgl_stage_word(int stage, const dgl_texenv *e, const dgl_texture *t, uint32_t *w);
int dgl_env_needs_combiner(GLenum env, const dgl_texture *t);

/* emit.c */
void dgl_emit_install(void);
void dgl_mga_clear(GLbitfield mask);

#endif
