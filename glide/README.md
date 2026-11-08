# MGA-Glide

Glide 2.x for Matrox cards under MS-DOS: a drop-in `GLIDE2X.OVL` that lets
DOS games written for 3dfx Voodoo boards run on a Matrox G100, G200, G400 or G450.

> **Not affiliated with 3dfx or Matrox.** "Glide" and "Voodoo" are names of
> 3dfx Interactive products; "Matrox", "Millennium" and "Productiva" are
> trademarks of Matrox. MGA-Glide is an independent, clean-room
> implementation written from public documentation and from measurements of
> the interface's behaviour; it contains no 3dfx code or headers.

## Status

| Card | State |
|---|---|
| G100 | Complete in 86Box: all conformance tests match the Voodoo; Screamer Rally and GTA run and replay within tolerance. Physical-card verification pending (milestone S) |
| G200 | Complete against a locally emulated G200 (native blending, alpha test, specular, mipmaps), whose texturing and blending details were checked on a G200-family chip (a server G200eR2, `docs/loop-c-results.md`). Retail-card verification pending |
| G400, G450 | Conformance passes on locally emulated cards running Matrox's own G400 and G450 BIOSes (`docs/emulated-g400.md`). Physical-card verification pending |

Acceptance games: Screamer Rally (3dfx build) and Grand Theft Auto (1997,
3dfx build). What the G100 cannot express (additive or subtractive colour
combines, destination-read blends) is approximated and listed in
`docs/combine-coverage.md`.

## Using it

1. Keep the game's own `GLIDE2X.OVL` somewhere safe.
2. Copy MGA-Glide's `GLIDE2X.OVL` into the game directory.
3. Run the game as usual. Options (resolution override, 32-bit colour,
   diagnostics) go in `MGAGLIDE.CFG` or the `MGAGLIDE` environment variable:
   see `docs/config.md`.

Requirements: a Matrox G100, G200, G400 or G450 with a VESA 2.0 linear frame
buffer, and a game that loads Glide 2.x from `GLIDE2X.OVL` (DOS/4GW games;
statically linked Glide games are out of scope).

## Building

Linux host, Open Watcom v2 and (for the HAL's second toolchain) DJGPP:

```
make setup-ow setup-djgpp
make                 # build/ow/GLIDE2X.OVL
make tests-host check-exports check-clib
```

Testing runs the DOS programs and the games in a patched 86Box with an
emulated Matrox card next to an emulated Voodoo for reference images
(`docs/loops.md`, `docs/toolchain.md`). The same patched 86Box builds on
Windows for manual use: `tools/86box/mkwinkit.sh` packages a kit whose
`build-windows.sh` runs in MSYS2 (`tools/86box/windows/README.md`), and
`tools/86box/mkwinvm.py` packages a ready-to-boot machine.

## Documentation

| File | Contents |
|---|---|
| `PRD.md` | Requirements and decisions |
| `docs/config.md` | Runtime options |
| `docs/combine-coverage.md` | Which Glide combine states each card reproduces |
| `docs/api-coverage.md` | Every Glide function and its status |
| `docs/abi-facts.md` | The interface facts the implementation relies on |
| `docs/reference-gaps.md` | Where the emulated reference Voodoo differs from the real one |
| `docs/emulated-g200.md` | The local 86Box G200 model |
| `docs/emulated-g400.md` | The local 86Box G400 and G450 models |
| `docs/trace.md` | Call traces, GLPLAY, the trace proxy and host replay |
| `docs/loop-c-results.md` | Measurements on a G200eR2 |
| `docs/dosgl-handoff.md` | What DOS-GL inherits |
| `docs/loops.md`, `docs/toolchain.md`, `docs/bench.md`, `docs/loader.md` | Testing and building |

## Licence

See `LICENSE`.
