# Toolchain and build

Everything builds on Linux. The runtime and the DOS test programs use Open
Watcom; the HAL also builds with DJGPP (for DOS-GL) and with the host
compiler (for the unit tests and the reference rasteriser).

## Pinned inputs (`tools/setup/versions.mk`)

| Input | Pin |
|---|---|
| Open Watcom v2 | `2026-09-01-Build` snapshot, sha256-checked |
| DJGPP | build-djgpp v3.4 (gcc 12.2.0), sha256-checked |
| 86Box | commit `bcce80a8`, plus the local patches in `tools/86box/series` |
| 86Box ROMs | commit `c761288e` |

`make setup-ow`, `make setup-djgpp` and `make 86box` fetch and build these
into `~/.local/opt` and `~/.cache/mga-glide`. `tools/dev` runs any command
in the Ubuntu 24.04 development container (`tools/docker/`), which has the
packages the harness needs (Xvfb, mtools, SDL2 and the 86Box build
dependencies); it passes through the `BOX86_*`, `LOOPA_*`, `VOODOO_*`,
`MGA_*`, `FIXTURES_DIR`, `REF_OVL`, `GAMES_DIR`, `WATCOM` and
`DJGPP_PREFIX` variables.

## The runtime DLL

`GLIDE2X.OVL` is a DOS/4G LE DLL, like the retail runtime:

- Compiler flags: `-bt=dos -mf -3s -fp5 -fpi87 -zri -ei -j -zastd=c99 -we -wx`,
  plus `-bd -s -zl` for DLL code (no stack probes, no default libraries).
- Link: `format os2 le dll initglobal`, `option modname=glide2x`,
  `start=__DLLstart_`, `nocaseexact`, exports generated from
  `glide/abi/glide2x.api`. `glide/tools/abi/lefix.py` then sets the module flags to
  `0x8200` and the lowercase module name the games' loader expects.
- Only self-contained C-library modules may be linked (`make check-clib`);
  the DLL has no C-library startup.
- `make check-exports` verifies the module name, the flags and every name
  the two acceptance games import.

**Known Open Watcom bug (the pinned 2026-09-01 snapshot, at `-oxt`).** A
condition that bounds a variable, directly guarding a `for` loop that
assigns that variable, leaves the variable at its guarded value after the
loop, although the loop body runs:

```
if (!n) for (n = 0; n < 10; n++) out[n] = tab[n];    /* n reads 0 afterwards */
```

`n == 0` and `n < 1` as the guard fail the same way; a `while` loop, a
separate counter, or `-od` are correct. Found in `tests/hal/scale.c`
(2026-09-29) by building the reduced case for Linux with the same flags.
`tools/ci/ow_loops.py` (run by `make tests-host` and CI) rejects the shape
anywhere Open Watcom compiles.

## Generated code (`build/gen/`)

From `glide/abi/glide2x.api`:

| Generator | Output |
|---|---|
| `glide/tools/abi/gen_exports.py` | function ids and names, logged stubs, export list, `api.json`, the `glapi_t` table for test programs |
| `glide/tools/abi/gen_trace.py` | the exported thunks (call tracing) and GLPLAY's dispatcher |
| `glide/tools/abi/gen_header.py` | `glide/include/glide/glide2.h` from `glide/abi/constants.tsv` and `glide/abi/types.h.frag` (committed; `make check-header`) |

## Make targets

| Target | Does |
|---|---|
| `runtime` (default) | `build/ow/GLIDE2X.OVL` |
| `dostests` | the DOS test programs and 16-bit helpers in `build/ow/dos/` |
| `tests-host` | host unit tests (reference rasteriser, setup, formats, loader) |
| `check-exports`, `check-clib`, `check-header` | ABI checks |
| `hal-ow`, `hal-djgpp`, `hal-host` | the HAL on each toolchain |
| `86box` | the pinned, patched emulator |
| `loopa TEST=` | one program in 86Box |
| `loopa-selftest` | the harness's own statuses, the fault / exit hooks, and the emulator's DPMI stack check (`STACKPG`) |
| `conform`, `conform-ref` | the conformance suite, and its Voodoo references |

Header dependencies are tracked (`-ad` with the real object and source
names), so changing a header rebuilds whatever includes it.
