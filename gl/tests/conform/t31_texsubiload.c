/* t31: t29 through the drawing engine (ILOAD) on every card: DGL_ILOAD=1
 * (manifest.json). A G400 or G450 drawing through the WARPs writes
 * sub-images by the CPU by default, so t29 no longer reaches the ILOAD
 * there. On bench-g400 an ILOAD among the WARPs' DMA left OPMODE in blit
 * mode and spoiled GLQuake's lightmaps (the HAL's engine.c); t29 itself
 * passed on the card before that fix, so this is the path, not the proof. */
#define T29_FRAME "t31_0"
#include "t29_texsubdraw.c"
