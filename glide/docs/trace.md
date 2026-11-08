# Call traces

MGA-Glide can record every Glide call a program makes, and GLPLAY replays
a recording through any `GLIDE2X.OVL`. Replaying one recording through the
retail runtime on the emulated Voodoo and through MGA-Glide on the G100
gives frame pairs of real game content to compare, like the conformance
tests but with the games' own state and data.

## Recording

```
SET MGAGLIDE=trace=1 trace_path=C:\OUT\GAME.BIN trace_to=400 exit_after=400
```

| Option | Meaning |
|---|---|
| `trace=1` | record |
| `trace_path` | output file (default `MGTRACE.BIN` in the current directory) |
| `trace_from`, `trace_to` | frame window; recording starts at `grGlideInit` when `trace_from` is 0 |

Every export is a generated thunk (`build/gen/thunks.c`, from
`tools/abi/gen_trace.py`) that records the call when tracing and then calls
the implementation (`impl_<name>`, defined with `GR_ENTRY`). Calls one Glide
function makes to another are not recorded: only the outermost call is.
The recording ends at `trace_to`, `grGlideShutdown`, `exit_after`, a fault
or the program's exit (the exit hook).

LFB write locks are recorded as the pixels written: for a direct lock the
buffer is snapshotted at lock time and compared at unlock; for a shadowed
lock the shadow's sentinel shows the written pixels. Either way GLPLAY
writes the same values through its own lock.

Windows that do not start at frame 0 would need a keyframe of texture
memory and state; that is not implemented, so such a recording starts
without them (logged).

## Format (version 1)

A header of four little-endian `u32`: `"MGTR"`, version, API function
count, reserved. Then records:

```
u16 op   u8 nargs   u8 nblobs   u32 args[nargs]
nblobs x { u32 len; len bytes, padded to 4 }
```

- `op` is the function's index in `abi/glide2x.api` (`build/gen/api.json`);
  ops from `0xF000` are pseudo-records: `0xF001` LFB write spans (args:
  buffer, write mode, bytes per pixel; blob: spans of
  `{u16 y, u16 x, u16 n, u16 0, n pixels}`), `0xFFFF` end.
- `args` are the call's arguments as passed (floats as their bits).
- Pointer arguments carry their data as blobs, per the rules in
  `tools/abi/gen_trace.py` (a vertex, a vertex list, a texture's levels, a
  palette...). Output pointers carry nothing; the replayer passes scratch.
- `len = 0xFFFFFFFF`: NULL pointer. Bit 31 set: vertex-cache reference
  (`len & 0xFFFF`). Bits 31..30 = `01`: blob-cache reference.
- Vertex cache: 4096 slots; every 60-byte vertex blob is stored at
  `tr_vhash(vertex)`. Blob cache: 512 slots; every blob of 64 bytes or more
  is stored at `tr_bhash(blob) & 511`. Writer and reader apply the same
  rule, so a reference always names the last blob stored in that slot.

## Tools

| Tool | Use |
|---|---|
| `GLPLAY <trace> [frames] [--glide=PATH]` | replay; captures the back buffer before the listed swaps as `GLP_<n>.PPM` |
| `tools/trace/trdump.py [--stats] TRACE` | list calls, or counts per function |
