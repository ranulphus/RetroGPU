# Glide's drawing back ends

The Glide 2 runtime (`GLIDE2X.OVL`) draws through one **back end** per family of cards: a table of functions in
`glide/src/backend/backend.h` (`mg_backend`), chosen when the runtime finds its card. The plan of 2026-11-19 (T5 of
`docs/tnt2.md`, V5 of `docs/virge.md`) put the Matrox code behind the table and added the Riva TNT2's beside it.

## The split

- **The API** (`glide/src/glide`, `tex`, `lfb`, `combine`): the entry points, the Glide state mirror (`mg.st`), the
  resolution table, the buffer rotation, the texture records and their shadows (`texmgr.c`: TMU addresses, the heap,
  levels converted to 16-bit texels by `texconv.c`), the LFB's shadow and CPU pixel pipeline (`lfb.c`), the fog
  table (`fog.c`), the depth helpers (`depth.c`), and the combine units evaluated at a vertex (`combine/eval.c`).
- **A back end** (`glide/src/backend/<card>/`): the card found and mapped; the window (mode, buffers, heap); the
  engine's sync and status; clears, swaps and the scaled present; the raster state; the triangle; a converted
  texture level into video memory in the card's layout, and where texture blocks start.

`mg_be` is the back end found, `mg_vram` its video memory. `mg_device_open` tries the Matrox back end first (so
nothing changes on a Matrox card), then the TNT2's.

## The back ends

| Back end | Files | What it draws with |
|---|---|---|
| `mga` | `backend/mga/{card,draw,combine}.c` | The runtime's Matrox code as it was: the HAL, the WARP, CPU setup, LOD bands, keyed variants |
| `tnt2` | `backend/tnt2/tnt2.c` | The TNT2 device (`hal/src/rg/tnt2.c`) and layer (`hal/src/tnt2`): the multitexture triangle object's two-stage combiner |

## How the Matrox move was checked

Moves and adapters only (DOS-GL's B1 rule): `git mv` where a file moved whole, code cut from the original text
elsewhere. A `REGCRC=1` build (`mk/ow.mk`) defines `MGA_TRACE_MMIO`: the HAL's register trace keeps a running CRC
of every register write and WARP vertex, printed at each `grBufferSwap` and at close (`MGL-REGCRC`).
`glide/tools/conform/regcrc.py` keeps each card's conformance run (`snap`, `snaprun` for a replay) and compares two
(`compare`): the same CRC lines and frames byte for byte. Main (64ab560) against the moved code (c53d9cf): the 26
compared conformance tests on the G100, G200, G400 and G450, and the GTA and Screamer Rally replays on the G200,
G400 and G450, all the same (t23 draws for a time: not compared).

## Conformance on another card

`glide/tools/conform/run.py check --bench PC` runs each test on a bench PC through `tools/bench/run.py` and compares
it with the emulated Voodoo's references as in Loop A; the manifest's class is the PC's card (`bench.local.toml`).
t22 (gamma) captures the display, which a bench can't: Loop A only.
