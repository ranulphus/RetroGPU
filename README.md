# RetroGPU

Hardware 3D for MS-DOS on the graphics cards of 1998-2001: a hardware layer
(the HAL) and two APIs on top of it, OpenGL 1.1 (DOS-GL) and Glide 2.x
(MGA-Glide), with the platform that builds and tests them in an emulator
and on real PCs. The HAL drives Matrox G100, G200, G400 and G450 cards today;
NVIDIA, ATi and a software renderer are planned.

```
Programs   retail Glide games  |  Quake, Quake II, Half-Life, Doom, ClassiCube,  |  GLOS's desktop
                               |  Fifth Wheel, Echoes of the Void, GLOS windows  |
APIs       Glide 2.x (glide/)  |  OpenGL 1.1: DOS-GL (gl/), SDL 3 on it          |  (2D, the HAL)
Hardware   the HAL (hal/): Matrox today; one GL and one Glide over every back end
Platform   86Box with local patches, Loop A, the bench, the dev container, toolchains
```

## The parts

| Path | What | Read first |
|---|---|---|
| `hal/` | The hardware layer: chip set-up, the 3D engine, WARP and DMA, 2D, video modes | `hal/include/mga/hal.h` |
| `gl/` | DOS-GL: `libGL.a` for DJGPP programs, its SDL 3 bridge, the game ports' builds | `gl/README.md`, `gl/PRD.md` |
| `glide/` | MGA-Glide: a drop-in `GLIDE2X.OVL` for DOS games written for 3dfx boards | `glide/README.md`, `glide/PRD.md` |
| `tools/`, `tests/`, `mk/`, `docs/` | The platform: 86Box and its patch series, Loop A (`tools/loopa`), the bench (`tools/bench`), the dev container (`tools/dev`), toolchains (`tools/setup`), the DOS helpers and test programs, the HAL's tests | `docs/loops.md`, `docs/toolchain.md`, `docs/bench.md` |

The platform's decisions (one HAL for every API, 86Box patches kept local,
the clean-room rule, the loops) were made in the Glide runtime's PRD, so
"PRD Dn" in the root's docs and comments means `glide/PRD.md`; in `gl/` it
means `gl/PRD.md`. A path in a doc or comment is relative to the part it
is in (the root, `gl/` or `glide/`); one that is not in its own part is the
root's.

## Building and testing

Linux host; the toolchains are pinned and installed under `~/.local/opt`:

```
make setup-ow setup-djgpp      # Open Watcom v2 and DJGPP gcc 12.2
make 86box                     # the pinned, locally patched 86Box (~/.cache/mga-glide/86box)
make                           # build/ow/GLIDE2X.OVL (the Glide runtime)
make gl-lib                    # gl/build/lib/libGL.a; any DOS-GL target as gl-TARGET (or make -C gl TARGET)
tools/regress.sh host          # the host tests of every part (minutes)
tools/regress.sh smoke         # a short Loop A run of each part (about 10 minutes)
tools/regress.sh merge         # everything, before a merge to main (run it alone on the host)
```

A change to the HAL is checked against both APIs before it reaches main.
Work in a worktree (`tools/wt-add.sh NAME`), not in the main checkout, while
other sessions use it.

## Using it from another project

Fifth Wheel, Echoes of the Void, DOSBench and GLOS build from this checkout
on main, with no pinned copies. They find it through one variable,
`RETROGPU` (default `~/RetroGPU`):

- DOS-GL: `-I$(RETROGPU)/gl/include`, `$(RETROGPU)/gl/build/lib/libGL.a`
  (`make -C $(RETROGPU)/gl lib`), SDL 3 in `$(RETROGPU)/gl/build/sdl/`.
- The HAL: `$(RETROGPU)/hal/include`, sources in `$(RETROGPU)/hal/src`.
- The harness: `$(RETROGPU)/tools/dev`, `$(RETROGPU)/tools/loopa/run.py`,
  the guest shim `$(RETROGPU)/tests/shim/hx.c`.

## History

RetroGPU joined two repositories on 2026-11-08: MGA-Glide, whose history is
this repository's main line, and DOS-GL (github.com/ranulphus/DOSGL), merged
into `gl/` with its history. Commit IDs from either still resolve here.

## Licence

MIT (`LICENSE`); third-party material in `gl/THIRD_PARTY.md`. Not
affiliated with 3dfx, Matrox, NVIDIA or ATi: see `glide/README.md`.
