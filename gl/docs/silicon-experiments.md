# Silicon experiments (G4EXP)

A few things DOS-GL does on the G400/G450, and one on the G200, rest on
readings of the specification that no public driver confirms. The open
questions are listed in MGA-Glide's `docs/g400-dual-texture.md` §7. 86Box
models the readings DOS-GL chose (local patches 0009 and 0010), so in the
emulator every experiment passes by construction. Only the silicon can say
whether they hold.

`examples/g4exp` (`build/exe/G4EXP.EXE`) draws one small scene per
experiment and reads it back. It prints what it found (`HX-STAT g4exp ...`)
and a verdict (`HX-TEST`), and saves the picture as `E<n><tag>.PPM`. A PASS
means the card does what DOS-GL assumes. The pictures are synthetic, with no
game data in them.

## Running

```
make examples dostools
tools/silicon/g4exp.py loopa [--card g450,g400,g200]   # emulator: out/g4exp-CARD/
tools/silicon/g4exp.py bench --pc bench-g400           # a bench PC (run on the host)
tools/silicon/g4exp.py report DIR...                   # the table again, from DIR/serial.log
```

`bench` publishes the job through MGA-Glide's `tools/bench/run.py` (the checkout
in `MGA_GLIDE`, default `~/MGA-Glide`). The results land in that checkout's
`out/bench/<pc>/g4exp-*/`. On silicon a FAIL job is an answer, not an error.

To dry-run without hardware, use the virtual bench PC:
`MGA_DOCKER_NETWORK=host tools/dev python3 tools/bench/vpc.py start g450` in
MGA-Glide, then `--pc vbench-g450`.

One job runs G4EXP six times, each under one DOS-GL switch. The tag names
the run's pictures (`E2C.PPM` is E2 under `DGL_COMBINER=1`):

| Tag | Switch | Runs | For |
|---|---|---|---|
| D | none (the defaults) | E1 E2 E4 E5 E6 E7 | everything |
| C | `DGL_COMBINER=1` | E1 E2 E4 E6 | E2 |
| T | `DGL_TC2_EXTRA=8000` | E1 E2 E4 E5 E6 | E3 |
| L | `DGL_TLUT=1` | E5 | E5 on the G400/G450 |
| S | `DGL_TLUT=0` | E5 | E5 reference |
| P | `DGL_ILOAD=0` | E6 | E6 with CPU writes |

`DGL_COMBINER` and `DGL_TC2_EXTRA` act only on cards with two texture units.
E1 and E4 need two units; on the G200 they are skipped.

## Emulator baseline (2026-09-29)

Every experiment passes under every switch on the emulated G450, G400 and
G200. The job also passed on the virtual bench PC (`vbench-g450`: picked
up, 22 files uploaded; that was before run P existed), so the bench path is
ready.

In E3, every T picture is identical to its D picture. E5 uses the LUT where
expected: in D on the G200, and in L everywhere. E6 writes through the
engine in D and by the CPU in P. The PCI values 86Box
reports, for comparison with the cards:

| Card | Device, revision | OPTION (40h) | OPTION2 (50h), OPTION3 (54h) |
|---|---|---|---|
| G450 | 0525, 82 | 40091120 | 0, 0 (not modelled) |
| G400 | 0525, 03 | 50040120 | 0, 0 (not modelled) |
| G200 | 0521, 00 | 4007dd21 | 0, 0 |

## The experiments

### E1: map-1 routing and coordinates (G400/G450)

**DOS-GL assumes** the reading of `TEXCTL2.map1` in which the map-1 bit takes
effect on the *next* register write. For each trapezoid, the HAL
(`hal/src/setup/trap.c`) writes:

1. `TEXCTL2|MAP1`;
2. map 1's TEXCTL, sizes and TMR0-5 (first trapezoid only);
3. map 1's TMR6-8;
4. `TEXCTL2` again, map1 clear;
5. then draws.

X's EXA code relies on the same reading.

**E1a:** unit 0 is REPLACE with a red|green texture. Unit 1 is DECAL with a
blue|clear texture on swapped coordinates.

| Quadrants (bottom, top) | Meaning |
|---|---|
| blue blue, red green | as assumed |
| red green, red green | unit 1 is not seen: map 1 was never programmed, or stage 1 does not run |
| blue green, blue green | map 1 was sampled with map 0's coordinates: TMR writes meant for map 1 reached map 0 (or were shared) |
| anything else | look at `E1A?.PPM` |

If E1a fails, the map-1 write order in `trap.c` is wrong for this silicon. Try
the other reading: map1 effective on the write that sets it. Then update
86Box patch 0009 to match.

**E1b, E1c:** a perspective floor. Both units use the same coordinates on
checkerboards: unit 0 red/green, unit 1 half-transparent blue/clear. Where
the maps agree, the floor is purple/green. Red or teal pixels are where map
1's coordinates drifted from map 0's. In E1c, map 1 is 32x32, which gives it
a different texture-coordinate prescale.

The line gives the counts (`agree=`, `red=`, `teal=`), and the test passes
below 0.2%. Where the pixels are in `E1B?.PPM` tells you which part of the
map-1 setup is wrong:

- near the horizon: q precision, the map-1 prescale `k1`;
- along one side: the left-edge correction for map 1's TMRs.

### E2: modulate with one texture (all cards; the question is the G400/G450)

**DOS-GL assumes** that on the G400 the legacy lighting module still works
beside the stage combiner. DOS-GL draws MODULATE with `TEXCTL.tmodulate`, and
both TDUALSTAGE words 0 (texel pass-through), which is the specification's
rule for the "Rev A" chips. Mesa instead uses the combiner word
(`0x40600000`, colour and alpha) with tmodulate off. `DGL_COMBINER=1` does
what Mesa does.

The scene is a white texture, with alpha 1 on the left and 1/2 on the right,
under the vertex colour (1, 1/2, 0) with alpha 1/2, blended over black. The
verdict names the outcome:

| Verdict | Left, right | Meaning |
|---|---|---|
| modulated | (128,64,0), (64,32,0) | colour and alpha modulated |
| colour-not-modulated | (128,128,128), (64,64,64) | the texel colour passed through |
| alpha-not-modulated | (255,128,0), (128,64,0) | the texel alpha passed through |
| neither-modulated | (255,255,255), (128,128,128) | the legacy module is off |

What each combination means:

- **D modulated:** keep the legacy path.
- **D not modulated, C modulated:** make the combiner the default on that
  chip (`force_combiner` in `src/gl/emit.c`). Then update 86Box patch 0009,
  which applies tmodulate before the stages.
- **C wrong, D modulated:** this is Rev A behaviour; the combiner words must
  stay 0.
- **Both wrong:** look at the pictures.

On the G200 only the legacy path exists, so a FAIL there is a DOS-GL bug.

### E3: TEXCTL2 bit 15 (G400/G450)

Every Linux and X G400 path ORs 0x8000 into TEXCTL2 (`MGA_G400_TC2_MAGIC`),
but the specification marks the bit reserved. Run T sets the bit on every
TEXCTL2 write. The report compares each T picture's CRC with D's.

- **Identical:** the bit makes no visible difference in these scenes. DOS-GL
  leaves it off. The switch stays available for the games (Quake 2 under
  `DGL_TC2_EXTRA=8000` against its timedemo pictures).
- **Different:** find which pictures differ and whether T or D matches the
  expected colours. If only T is right, set the bit always.

### E4: leaving dual texturing (G400/G450)

When the Linux kernel leaves the dual-texture pipe (`mga_g400_emit_pipe`), it
runs a sequence of its own: a dummy draw, DWGSYNC, then TEXCTL2 toggling
dualtex. MGA-Glide's `docs/g400-dual-texture.md` §4 lists the steps. DOS-GL
just rewrites TEXCTL2.

The scene is a grid of 32 cells, dual and single interleaved with no sync
between them. Dual cells are (192,64,64) and single cells are cyan. Any
wrong cell means DOS-GL needs the kernel's sequence at the dual-to-single
switch (in the HAL's `tex_emit`). The verdict gives the first wrong cell.
The pattern is in `g4exp.c`.

### E5: paletted textures through the LUT (G200, and the G400/G450 under L)

**DOS-GL assumes:**

- the LUT holds RGB565 entries;
- the BITBLT RSTR recipe loads it (`engine_tlut_load`, 86Box patch 0010);
- `atype` does not matter during the load.

The scene is four stripes of indices 0-3 through the shared palette, then the
palette is reversed and a second quad is drawn: two LUT loads within one
frame. Entry 0 is (0,132,0). As RGB565 that is 0x0420; if the LUT read it as
ARGB1555, the stripe would come out near (8,8,0). The `e5 entry=` lines give
every stripe's colour, and the verdict says which path ran (`tlut` or
`expanded`).

- **S (expanded)** must pass on every card. A FAIL there is a DOS-GL bug.
- **G200 D fails, S passes:** the load recipe or entry format is wrong on
  silicon. Make `DGL_TLUT=0` the default and look at the entry colours.
- **G400 L passes:** the LUT works there too, despite the specification's
  advice to expand 8-bit textures. Consider making it the default: it halves
  the texture VRAM of GLQuake's paletted textures, as it does on the G200.

### E6: texture cache after in-place texel writes (all cards)

DOS-GL writes a `glTexSubImage2D` rectangle straight into the texture's VRAM
when it can (`sub_in_place` in `src/gl/texture.c`). There are two paths:

- **D:** through the drawing engine (ILOAD), queued in order with the draws.
  This is the default where the level is wide enough.
- **P:** `DGL_ILOAD=0`, written by the CPU through the framebuffer. If the
  texture was drawn since the last sync, DOS-GL waits for the engine first.

86Box has no texture cache. On silicon, stale texels could survive either
kind of write.

The texture is first drawn red. Then:

| Step | Write | Then | Expect |
|---|---|---|---|
| a | `glFinish`; whole texture green | drawn with no register change | green |
| b | at once, an 8x8 corner blue (P: after a sync) | drawn with no register change | blue corner |
| c | another texture drawn; corner yellow | drawn (TEXORG rewritten) | yellow corner |

The verdict counts stale samples per step (1024 samples per quad; 16 in the
corner). It also reports how the writes went:

- D: "engine 3, cpu 0 (after sync 0), re-uploaded 0";
- P: "engine 0, cpu 3 (after sync 2), re-uploaded 0".

Any other split means the path was not exercised, so the result is
inconclusive.

| Outcome | Change |
|---|---|
| all clean | nothing |
| D clean, P stale | keep ILOAD; send the CPU path's rectangles through a re-upload instead |
| a or b stale, c clean | a TEXORG write refreshes the cache: after an in-place write, mark the texture state dirty so validate re-emits it |
| c stale too | the cache survives register writes: drop that path's in-place writes (always re-upload), or find a flush |

### E7: identity (all cards)

E7 prints the chip, revision, VRAM, the DOS-GL capabilities, and PCI config
space 00h-5Ch. That covers OPTION (40h), and on the G400 OPTION2 (50h) and
OPTION3 (54h). The specification's "Rev A" notes (the E2 question) name no
PCI revision, so E7 records every card's values next to its E2 outcome.

### E9: texture coordinates, CPU setup against the WARP (G400/G450)

`examples/texexp` (`TEXEXP.EXE`) draws sixteen cases a part, one in each 160x120 cell. Each case varies one
property of the texture or its coordinates:
- **Part 1:** flat and in perspective; repeat and clamp; nearest, bilinear and mipmapped; one block of a
  128x128 lightmap-style atlas; sizes from 8 to 256; coordinates 100 periods out.
- **Part 2:** the same triangles placed inside their cell and across its clip (left, right, both, the screen's
  edge, far into the guard band).

`tools/silicon/texexp.py bench --pc NAME` runs both parts with `DGL_WARP=0` (tag C) and `DGL_WARP=1` (tag W).
`texexp.py report DIR [REF]` gives each cell's share of pixels more than 32 apart: C against W, and each against
a reference such as 86Box's run (`texexp.py loopa`).

Two readings:
- The WARP's frames should match 86Box's, which match Mesa's.
- The CPU setup's frames should match them too, where 86Box models the same registers.

## Recording results

For each card, add a row with the date, card, revision and the report's table
to this document. Turn each FAIL into three things:

- a DOS-GL or HAL change;
- an 86Box patch update, so the emulator models the silicon;
- a new `g4exp.py loopa` baseline.

Record each answer in MGA-Glide's `docs/g400-dual-texture.md` §7 as well.

## Results

### bench-g400: a G400 (2026-10-26)

The card: an AGP G400 16 MB (`102B:0525` revision 05, subsystem `0338102B`, VBE 2.0 BIOS) in an IBM PC 300PL
(Pentium II, 440BX) under GLOS on MS-DOS 7.1. DOS-GL main 6fd0969; the job is MGA-Glide's
`out/bench/bench-g400/g4exp-1793037525`.

E7 reads OPTION `50040120`, the value 86Box gives. It also reads OPTION2 `01003000` and OPTION3 `0190A419`;
86Box models neither.

```
                 D     C     T     L     S     P
e1a-routing      PASS  PASS  PASS  -     -     -
e1b-coords       PASS  PASS  PASS  -     -     -
e1c-coords32     PASS  PASS  PASS  -     -     -
e2-modulate      PASS  PASS  PASS  -     -     -
e4-leave-dual    PASS  PASS  PASS  -     -     -
e5-palette       PASS  -     PASS  FAIL  PASS  -
e6-texcache      FAIL  FAIL  FAIL  -     -     FAIL
E3 bit 15: 6 pictures identical to D, differ: E6
```

- **E1.** All three pass under D, C and T. The map-1 bit takes effect on the next write, as `trap.c` assumes.
- **E2.** D and C both say "modulated", so the legacy lighting module works beside the combiner on revision 05.
  Keep the legacy path.
- **E3.** Bit 15 changes nothing in E1, E2, E4 or E5. It does change E6's picture: the stale texels (below) end
  in a ragged edge instead of a straight one. So the bit affects how the texture cache fills, not the colours.
  DOS-GL leaves it off.
- **E4.** Every cell is right, with no sync. Rewriting TEXCTL2 is enough to leave dual texturing.
- **E5.**
  - L (the LUT on the G400) draws every sample black, with fine vertical lines where the stripes are. The
    G200's load recipe doesn't give the G400 a working LUT, so DOS-GL keeps expanding paletted textures on the
    G400 (its default). S passes.
- **E6.** Every switch fails the same way: a=129, b=10, c=0 stale samples. Engine writes and CPU writes are
  equally affected.
  - E6D.PPM shows that step a's stale texels are 8-pixel blocks of the old red along the quad's diagonal.
  - Step b leaves half of the 8x8 corner unchanged.
  - Step c, where another texture was drawn and TEXORG rewritten, is clean.
  - So the chip caches texels across writes to texture memory, and rewriting the texture registers clears the
    cache. A follow-up job (`e6flush-1793037995`, an experimental `DGL_TEXFLUSH` switch on branch
    `silicon-g400`) narrowed it down:

    | DGL_TEXFLUSH | After an in-place write | Engine (D) | CPU (P) |
    |---|---|---|---|
    | 0 | nothing (6fd0969) | a=129 b=10 c=0 | a=129 b=10 c=0 |
    | 1 | the texture state re-emitted at the next draw (TEXORG rewritten with the same value) | clean | clean |
    | 2 | as 1, with TEXORG first written once with another value | clean | clean |

    Re-emitting the state is enough. **Change:** after a `sub_in_place` write, set `DGL_DIRTY_TEXTURE`. That
    costs one texture-state emit per `glTexSubImage2D`.
    - 86Box has no texture cache, so it can't show the bug.
    - The G200 is untested, but the change is cheap enough to make on every card.
- **E8** (run by hand under D and P) passes: a queued ILOAD followed by a `glTexImage2D` gives blue.

DOS-GL's conformance suite on the same card (`make conform-bench PC=bench-g400`, 2026-10-26) passes 23 of 27;
86Box's G400 passes 27 of 27. Two causes:

- **t18** (`glTexSubImage2D` between draws) is E6. It passes under `DGL_TEXFLUSH=1`.
- **t07, t20, t25** (bilinear filtering) come out exactly half a texel off Mesa in s and t. On t07's magnified
  8x8 texture the silicon equals Mesa moved by (-14, +12) pixels, half of a 28x23-pixel texel.
  - The HAL's `adjust()` (`hal/src/texhw.c`) subtracts half a texel for bilinear filtering. That is GL's
    texel-centre rule on a chip that would filter from texel corners, as 86Box models the G200 and G400.
  - The G400 already samples at texel centres. With the shift off (experimental switch `DGL_TEXCENTRE=1`),
    t07, t20 and t25 match Mesa with no pixel beyond tolerance.
  - **Change:** no half-texel shift on the G400 and G450, in the HAL's caps.
  - 86Box patch 0009 should model texel-centre sampling on those cards.
  - The G200 is unmeasured (the G200eR2 runs compared DOS-GL with itself), so it keeps the shift until a rig
    run on the G200eR2 says otherwise.

With both changes, the four tests pass on the card, every frame within tolerance (worst fraction 0).

**Changed since (2026-10-26):**
- **The texture cache:** `glTexSubImage2D` sets `DGL_DIRTY_TEXTURE` after every in-place write (`src/gl/texture.c`),
  on every card. The next draw rewrites the texture registers, TEXORG included, and that empties the cache.
- **Texel centres and the LUT:** the HAL has `mga.texel_centre` on the G400 family and no half-texel shift there.
  The G400 family also no longer has `has_tlut`, so `DGL_TLUT=1` expands too.
- **The DAC:** `dac_set_ramp` writes the BIOS's layout. All of these come from MGA-Glide branch `g400-silicon`.
- **86Box:**
  - patch 0003 is dropped;
  - 0119 fixes FIFOSTATUS's status byte;
  - 0120 filters around texel centres on the G400 family;
  - 0121 models its texture cache, sized to catch any missing flush. DOS-GL 6fd0969's G4EXP fails E6 there
    (a=1024 b=1024 c=0), as on the card, and this library passes.
- **On the card with these changes:** conformance passes 27 of 27, and G4EXP passes everything but E5 under L.
  That job was built before `has_tlut` changed.
- **Emulator baseline (2026-10-27, 86Box with 0119-0121, MGA-Glide c45ac7f):** every experiment passes under every
  switch on the G450, G400 and G200.
  - The G400 family reports `tlut=0`, and run L expands as D does.
  - E6 runs through 0121's cache model, which fails DOS-GL 6fd0969 as the card did.
  - E3: 7 pictures identical to D on the G400 family, 3 on the G200.

**E9 and the WARP (2026-10-27).** DOSBench's Arena on the card, through the CPU setup, drew bilinear lightmaps
dark, black or too bright by whole faces, and some walls displaced: 6-38% of its pixels differ from 86Box's.
Through the WARP it matched exactly. E9 found two faults, both fixed in the HAL:

- **The WARP and the clip.** When the scissor changed between quads, the WARP drew only one triangle of each:
  the second WARP's triangle was cut by the next clip. Linux's mga DRM writes DWGCTL and a zero-length draw
  twice before every clip on a G400 (`mga_emit_clip_rect`). `emit_clip` now does the same while WARP triangles
  are pending (MGA-Glide 3bdd96d). Every flat cell then matches 86Box.
- **The CPU setup in perspective.** TEXWIDTH/TEXHEIGHT's `rfw` was `8 - log2(size) - k`, k being the setup's
  prescale of s and q (the specification's formula, counting the extra q fraction bits). On the card that
  displaced the texels of prescaled triangles in perspective. The perspective lightmap block differed from
  86Box in 1-3% of its pixels wherever it was placed, clip or not.
  - With `rfw = 8 - log2(size)` every cell matches 86Box (which ignores `rfw`) and the WARP.
  - Capping the prescale instead changed nothing, and `rfw` 2 higher nearly matched.
  - The Arena's frames now match 86Box's exactly through the CPU setup too (MGA-Glide 0407e76).

What's left differs only through the WARP, by its own arithmetic. Minified floors at 32 repeats (nearest, so
moiré) differ from 86Box's and the CPU setup's in 4-27% of their pixels, and the 256x256 grid by one texel row
(4%). Its `rfw` makes no difference (bases 2, 5 and 8 drew identically), and conformance and the Arena are
within tolerance.

| Part 1 cell   | C vs W | C vs 86Box | W vs 86Box |   | Part 2 cell | C vs W | C vs 86Box | W vs 86Box |
|---------------|-------:|-----------:|-----------:|---|-------------|-------:|-----------:|-----------:|
| flat (4)      | 0      | 0          | 0          |   | atlas (4)   | 0      | 0          | 0          |
| floor x4      | 0.087  | 0          | 0.087      |   | grid in     | 0.042  | 0          | 0.042      |
| floor x32     | 0.225  | 0.001      | 0.223      |   | grid L/R/LR | 0.06-0.09 | 0       | 0.06-0.09  |
| floor mip     | 0.126  | 0.020      | 0.163      |   | flat (4)    | 0      | 0          | 0          |
| floor bilin   | 0.267  | 0          | 0.265      |   | walls (4)   | 0.006-0.015 | 0-0.006 | 0.007-0.015 |
| atlas (2)     | 0      | 0          | 0          |   |             |        |            |            |
| 16, 8, 32x8   | 0      | 0          | 0          |   |             |        |            |            |
| 256           | 0.043  | 0          | 0.043      |   |             |        |            |            |
| edge-on, huge | 0.010  | 0-0.002    | 0.010      |   |             |        |            |            |

Before the `rfw` fix, the CPU column against 86Box read 0.019-0.032 on the perspective atlas and 0.083 on the
256 grid.
