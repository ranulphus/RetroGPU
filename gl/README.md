# DOS-GL

A hardware-accelerated OpenGL 1.1 subset (`libGL.a`) for 32-bit DOS programs
built with DJGPP, driving Matrox G400, G450 and G200 cards. The reference
consumer is ClassiCube's GL 1.1 backend. See `PRD.md` for the requirements.

**Status:** every milestone's 86Box exit is met on emulated G450, G400 and
G200 cards running Matrox's own BIOSes: the conformance tests (the PRD's 13
plus a texture-coordinate sweep) match Mesa's rendering, and ClassiCube runs
on its GL 1.1 backend. Real-hardware verification (the bench) and Pentium II
performance numbers are pending. See `docs/porting.md` for using the library
and `docs/testing.md` for the test loops.

DOS-GL shares its Matrox hardware layer, test harness, bench tooling and
86Box patches with [MGA-Glide](https://github.com/ranulphus/MGA-Glide), built
from its checkout beside this one (`MGA_GLIDE`, default `~/MGA-Glide`), on main.

## Building

```
make setup-djgpp          # pinned DJGPP gcc 12.2 + CWSDPMI into ~/.local/opt
make                      # build/lib/libGL.a and build/exe/*.EXE
make tests-host           # host unit tests (incl. gl.h against ClassiCube's declarations)
make 86box                # the pinned, locally patched 86Box (shared cache with MGA-Glide)
make loopa TEST=hello CARD=g450      # run an example in 86Box: out/hello/
```

Consumers link `-lGL` and include `<GL/gl.h>` and `<GL/dosgl.h>`.

New games can use SDL3 with it: `make sdl` builds the pinned SDL3 with
OpenGL windows through DOS-GL (`SDL_WINDOW_OPENGL`, `SDL_GL_*`) beside SDL's
DOS keyboard, mouse, gameport and Sound Blaster support; see `docs/sdl.md`.

## Licence

MIT (`LICENSE`); third-party material in `THIRD_PARTY.md`.
