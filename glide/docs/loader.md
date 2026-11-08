# How GLIDE2X.OVL gets loaded (M0 loader spike)

Results of the M0 loader experiments (PRD D18, plan §5). All runs are in
Loop A: 86Box master `bcce80a` with the local patches, machine `bf6`
(Pentium II 350), G100 AGP + Voodoo Graphics PCI, FreeDOS 1.4.

## Findings

| Spike | Question | Result |
|---|---|---|
| S1/S2 | Does DOS/4G call a DLL's init routine when a game loads the OVL? | **Yes, once.** GTA's built-in DOS/4GW Professional called MGA-Glide's `__DLLstart_` before `grGlideInit`; the stack arguments hold no meaningful values (`inst=0x21292929 reason=554246409`). MGA-Glide only sets a flag there and does all real work in `grGlideInit`. |
| S2 | First calls GTA makes | `grGlideInit`, `grSstQueryHardware`, then `grGlideShutdown` when the query reports no board. GTA then shows "What 3DFX Card?". GTA loads the OVL only after "Play" is chosen; its menus are not drawn through Glide. |
| S2 | Screamer Rally (external DOS/4GW 1.97) | The DLL is loaded and its init called with `inst=0 reason=0`; then `grGlideGetVersion`, `grGlideInit`, `grSstQueryHardware`, `grGlideShutdown`, and the game exits when no board is reported. **Plain DOS/4GW 1.97 does support loading a DLL at run time**; only import-linked EXEs are refused (S3). With its retail OVL the game runs its 3D attract-mode race on the emulated Voodoo. |
| S3 | Does DOS/4GW 1.97 resolve an EXE's imports from a DLL? | **No.** `DOS/4GW error (2302): DLL modules not supported`, then `(2301) can't find glide2x._GRGLIDEGETVERSION@4` and `(1313) can't resolve external references`. Import-linked test programs are not an option. |
| S4 | Does `leload` load the retail OVL and our OVL? | **Yes.** GTA's retail OVL (`e077ab1c…`): 2 objects, 3,669 fixups (all internal 32-bit offsets), all 131 exports resolved, `grGlideGetVersion` returns `"2.3"`. Screamer Rally's (`6981a7bd…`): 3 objects, 3,933 fixups, 129 of 131 (it predates `guFbReadRegion`/`guFbWriteRegion`). MGA-Glide's OVL: all 131. |
| S5 | Does the retail OVL render correctly under `leload` without its init routine, repeatably? | **Yes.** `LESPIKE --draw`: `grSstWinOpen(640x480)` succeeds on the emulated Voodoo, a Gouraud triangle is drawn, `grLfbReadRegion` returns it. Three runs gave identical CRCs (readback `27cdd07c`, display `5f1ef5ad`). |

## Decision

Test programs load an OVL with **`leload`** (`tests/shim/leload.c`) and call it
through the generated `glapi_t` table (`tests/shim/glbind.c`). The same
binary runs against a retail OVL on the Voodoo and against MGA-Glide on the
G100. The DLL init routine is not called by `leload`; the retail runtime
does not need it.

Only the real games exercise the game-side loader path
(`LINEXE_LOADMODULE` via the extender). The acceptance runs cover it.

## Reproducing

```
make dostests
tools/dev python3 tools/loopa/run.py --name spike-ref-draw --exe build/ow/dos/LESPIKE.EXE \
    --ovl $FIXTURES_DIR/ovl/gta.ovl --args=--draw
tools/dev python3 tools/loopa/run.py --name gta-mga --game gta --ovl build/ow/GLIDE2X.OVL \
    --keys 15:0x1c,20:0x1c,25:0x1c --shots 24,40 --timeout 100 --idle 90
```

S3 is reproduced by linking `tests/spike/s3import.c` with
`tests/spike/s3import.lnk` and running it with the OVL copied to
`C:\TEST\GLIDE2X.DLL`.
