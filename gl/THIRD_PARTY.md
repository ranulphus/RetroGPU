# Third-party material

| Path | What | Licence |
|---|---|---|
| `third_party/classicube/` | ClassiCube, pinned submodule (the reference consumer, PRD §11.6) | BSD-3-Clause (ClassiCube's own licence) |
| `third_party/sdl/` | SDL3, pinned upstream submodule; `tools/sdl/patches/` adds OpenGL through DOS-GL and DOS fixes (`docs/sdl.md`) | zlib (SDL's own licence); the patches are offered under the same terms |

Derived register definitions inside the HAL carry their upstream notices
(X.org `mga` and Mesa/DRI `mga` drivers, MIT), as recorded in MGA-Glide.
No Matrox BIOS images, retail software or game data are committed; the
86Box ROM set and Matrox's BIOS package are fetched into the local cache.
