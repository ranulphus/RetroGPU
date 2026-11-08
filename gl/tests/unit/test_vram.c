/* test_vram.c - the texture heap: alignment, first fit, coalescing, retired
 * blocks, and a randomised alloc/free sequence that must never overlap or leak. */
#include "unit.h"
#include "../../src/gl/gl_tex.h"
#include <stdlib.h>

void unit_run(void)
{
    uint32_t a, b, c, offs[400], sizes[400];
    int live[400] = { 0 }, i, j, k;
    dgl_vram_init(1000, 1000 + 4096);
    CHECK(dgl_vram_alloc(100, &a) == 0 && a == 1024);          /* start aligned up to 32 */
    CHECK(dgl_vram_alloc(1, &b) == 0 && b == a + 128);         /* 100 -> 128 */
    CHECK(dgl_vram_alloc(4096, &c) == -1);                     /* too big */
    dgl_vram_free(a);
    CHECK(dgl_vram_alloc(64, &c) == 0 && c == a);              /* first fit reuses the hole */
    dgl_vram_free(c);
    dgl_vram_free(b);
    CHECK(dgl_vram_blocks() == 1 && dgl_vram_used() == 0);      /* fully coalesced */
    /* A retired block stays allocated until the engine has synced. */
    CHECK(dgl_vram_alloc(4000, &a) == 0);
    CHECK(dgl_vram_retire(a) == 0 && dgl_vram_retired() == 1);
    CHECK(dgl_vram_alloc(4000, &b) == -1);                     /* still held */
    dgl_vram_sync_done();
    CHECK(dgl_vram_retired() == 0 && dgl_vram_alloc(4000, &b) == 0 && b == a);
    dgl_vram_free(b);
    /* Random churn. */
    srand(7);
    dgl_vram_init(0, 1u << 20);
    for (k = 0; k < 20000; k++) {
        i = rand() % 400;
        if (live[i]) {
            dgl_vram_free(offs[i]);
            live[i] = 0;
        } else {
            sizes[i] = 32 + (uint32_t)(rand() % 8192);
            if (dgl_vram_alloc(sizes[i], &offs[i]) == 0) {
                live[i] = 1;
                CHECK(offs[i] % 32 == 0 && offs[i] + sizes[i] <= (1u << 20));
                for (j = 0; j < 400; j++)
                    if (j != i && live[j])
                        CHECK(offs[i] + sizes[i] <= offs[j] || offs[j] + sizes[j] <= offs[i]);
            }
        }
    }
    for (i = 0; i < 400; i++)
        if (live[i])
            dgl_vram_free(offs[i]);
    CHECK(dgl_vram_blocks() == 1 && dgl_vram_used() == 0);
}
