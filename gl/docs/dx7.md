# DOS-GL on DirectX 7-class cards (X1-X4)

The owner's programme of 2026-11-24: better pictures and higher presets for programs on the cards of OpenGL 1.3's
generation, in four steps. The plan, with what Echoes of the Void and Fifth Wheel asked for, is the session's plan
file; this file records what is built and what the cards showed.

| Step | What | State |
|---|---|---|
| X1 | Picture-quality features, per card, and settings that apply them to programs that don't ask | under way |
| X2 | GL 1.1's lighting, normals, texture-coordinate generation and clip planes on the CPU (accepted and ignored until now) | done |
| X3 | Hardware transform and lighting on the Radeon 7500 (RV200) and 7200 (R100): `GL_DGL_hw_tnl`, on by default (`DGL_TNL=0` turns it off) | transform (X3a), the hangs cured and on by default since 2026-11-26; lighting, fog, linear texture-coordinate generation, user clip planes and static buffer objects on the engine (X3b, done 2026-11-26) |
| X4 | SSE in the software transform (needs GLOS to enable it) | to do |

## X1: what each renderer offers

`GL_EXTENSIONS` is built from the back end's caps (`dgl_get_caps`), so a program sees only what its card draws.

| | Software renderer | Radeon (7000, 7500, 7200) | Matrox, TNT2, Rage 128 |
|---|---|---|---|
| `GL_EXT_texture_filter_anisotropic` | 16:1 (reference span) | 16:1 | not yet |
| `GL_EXT_texture_lod_bias` | +-15 | -1 to +127/128 (reported 1) | not yet (the Rage 128 has a field) |
| `GL_ARB_texture_env_dot3` | yes | yes (DOT3_RGBA's alpha: see below) | no |
| `GL_ARB_texture_compression`, `GL_EXT_texture_compression_s3tc` | yes | yes | yes |
| `GL_ARB_texture_cube_map` | yes | yes (base level only: the chip can't mipmap one) | no |

Compressed textures (DXT1, DXT3, DXT5) are decoded by the CPU into the level's RGBA8 copy (`src/gl/texcomp.c`),
so every card takes them; the blocks are kept for `glGetCompressedTexImageARB`. Compressed internal formats given
uncompressed texels stay uncompressed (`GL_TEXTURE_COMPRESSED_ARB` false), as the extension allows. The Radeons
could take the blocks as they are (less memory, less bus); not done yet.

The settings `DGL_TRILINEAR`, `DGL_ANISO` and `DGL_LODBIAS` (`docs/porting.md`) apply these to every texture, for
programs (GLQuake) that never ask.

## X1: what the cards showed

- **Radeon 7500, anisotropic filtering** (P3, 2026-11-24): the chip's own anisotropic minifying filters with the
  ratio field (Mesa's `radeonSetTexFilter`) matched Mesa's llvmpipe on t37 to within 0.01% of pixels; the far
  rows' stripe contrast went from 1.7 (isotropic) to 82.6 (16:1).
- **Radeon 7500, LOD bias**: the field (`PP_TXFILTER` bits 15:8) is signed, a 128th of a level each on both sides:
  raw 64 drew +0.51, 96 +0.78, 127 +0.99 (t36's trilinear squares). Mesa 7.0.3's `radeonTexEnv` scales the positive
  half as a 32nd (a stated range of -1 to +4), which draws a quarter of the bias asked for. So the range is -1 to
  +127/128, and DOS-GL reports `GL_MAX_TEXTURE_LOD_BIAS_EXT` as 1.
- **Radeon 7500, dot3**: DOT3_RGB as Mesa forms it (`BLEND_CTL_DOT3` at the chip's x4) matches Mesa. DOT3_RGBA's
  product did not reach the alpha with Mesa's words (alpha combine as set, x4 or x1), nor with the alpha's own
  operation set to dot3, so the alpha is the alpha combine's (said once); t38's DOT3_RGBA quad is excused for the
  Radeon in the manifest. Bump mapping uses DOT3_RGB.
- **Radeon 7500, the whole suite** (P3, 2026-11-24 15:30): 38 of 38 with these changes, then t39.

## Tests

- t36 (LOD bias), t37 (anisotropic filtering), t38 (dot3), t39 (S3TC: every block form, a sub-image, the
  blocks read back). Each skips itself where its extension isn't advertised
  (`ct_has`, `CT-SKIP`; `SKIPPED` in `tools/conform/run.py`), as on the Matrox cards in Loop A.
- t37 judges the card's anisotropic frame by its own contrast check (`ct_check`), since cards filter
  anisotropically each in their own way.

## X2: lighting on the CPU

`src/gl/light.c`: OpenGL 1.1's section 2.13 as written: eight lights (directional, positional with attenuation,
spot), the light model (ambient, local viewer, two-sided), front and back materials, colour material, normals
from `glNormal*` and `glNormalPointer` (bytes, shorts, ints, floats, doubles), `GL_NORMALIZE` and
`GL_RESCALE_NORMAL_EXT` (both normalise). A lit vertex's colour is computed in `dgl_transform`; two-sided lighting
keeps the back's colour too and the assembly picks by the triangle's facing (the sign of the homogeneous
determinant of x, y and w, which holds for the part a clip leaves). While lighting is on, draws take the general
vertex path: no back end's fast path transforms them (`DGL_VTX_EXTRA`). t40 matches Mesa to the pixel on the
software renderer at 8, 16 and 32 bits. Display lists still execute state calls at compile time (FR-DL-3), so a
`glMaterial` inside a list takes effect then, not at its call.

## X2: clip planes and texture-coordinate generation

`src/gl/texgen.c`. A clip plane is taken into eye coordinates at `glClipPlane` (the modelview's inverse) and into
clip coordinates at each draw (the projection's inverse), where a point's distance is the same linear function;
the assembly cuts a triangle (fanned back out with its provoking vertex), a line or a point against the enabled
planes before the back end sees it, so no back end changed. Texture coordinates s and t of each unit are generated
object-linear, eye-linear (the plane into eye coordinates at `glTexGen`) or sphere-mapped, before the texture
matrix. Both take the general vertex path. t41 matches Mesa to the pixel on the software renderer (orthographic and
perspective frames).

## X2: the r coordinate and cube maps

Texture coordinates now carry r through `glTexCoord3/4`, `glMultiTexCoord3/4`, arrays of three or four, the texture
matrix and clipping; generation covers r and q too, with `GL_REFLECTION_MAP_ARB` and `GL_NORMAL_MAP_ARB`.
`GL_ARB_texture_cube_map` (where the back end says `cube`): cube-map objects with their own binding per unit and a
default object, six square faces through every upload and query (`glTexImage2D`, sub-images, copies, compressed
images, `glGetTexImage`, level parameters), `GL_TEXTURE_CUBE_MAP_ARB` above `GL_TEXTURE_2D` on a unit. The software
renderer samples them in its reference span (the face by the major axis, its level of detail from the face
coordinates a pixel along); its tiles don't keep cube-mapped drawing, which is drawn at once. t42 (the six faces
unfolded by explicit directions, an environment-mapped and a normal-mapped sphere, a cube map above a 2D texture,
the binding and a face read back) matches Mesa to the pixel. The Radeons can't mipmap a cube map (Mesa's driver
sends the base level only): their cube map is the six base levels in one block, faces 0 to 4 at the
`PP_CUBIC_OFFSET_Tn_m` registers and face 5 at `TXOFFSET`, faces 1 to 4's sizes in `PP_CUBIC_FACES_n` and face 5's
in `TXFORMAT`'s F5 fields (without those the P3's 7500 drew face 5 as one texel), r in the vertex's q slot. t42 on
the 7500 matches Mesa to the pixel.

## X3: the first silicon results (DTCL0, the P3's 7500, 2026-11-24)

`tests/radeon/dtcl0.c`: DTRI0's three triangles given in object coordinates, taken to the window by the chip. What
works, as Mesa's radeon driver does it:

- The state: `SE_CNTL_STATUS` without `TCL_BYPASS`; `SE_COORD_FMT` = `VTX_W0_IS_NOT_1_OVER_W0`; `SE_CNTL`'s viewport
  transforms on and `SE_VPORT_*` set (x: W/2, W/2; y: -H/2, H/2; z: 0.5, 0.5); `SE_TCL_OUTPUT_VTX_FMT` Z0, W0 and a
  packed diffuse; `SE_TCL_OUTPUT_VTX_SEL` computing x, y, z and w; the matrix slots as Mesa numbers them
  (model-projection 0, model 1, its inverse transpose 2, texture matrices 3 to 5); the matrix's rows written into
  the vector memory (`SE_TCL_STATE_FLUSH`, `SE_TCL_VECTOR_INDX_REG`, then `SE_TCL_VECTOR_DATA_REG` as a one-register
  table); the four guard-band scalars at 1.0.
- The vertices: an array in video memory (`3D_LOAD_VBPNTR`: one component, its size and stride in dwords, its
  address in the chip's space), then `3D_DRAW_VBUF` with `MAOS_ENABLE` and `TCL_ENABLE`.
- The result: coverage identical to the reference (0 pixels differ), colours within 9 levels, with the matrix
  turning the triangles 0, 30 or 117 degrees (the vertices given turned back).
- Vertices inline in the stream (`3D_DRAW_IMMD` with `TCL_ENABLE`) are not taken that way: the engine stays well
  but the triangles come out wrong (the whole screen drawn). Mesa's driver uses arrays alone for T&L; so will X3,
  dynamic vertices copied into an area the chip reads.
- The rate: a 64 x 64 grid of 4-pixel cells (7,938 triangles as an unindexed list, 23,814 vertices) drawn 50
  times: 7.68 million triangles a second, 23 million vertices (DOS-GL's CPU path on the P3: in the hundreds of
  thousands).

## X3a: DOS-GL's draws through the engine

The front end tries a back end's `tnl` sink hook before anything else for every draw (`src/gl/vertex.c`); the Radeon
back end takes it where its engine does the state (so far: no lighting, fog, texture-coordinate generation, clip
planes, projective texture matrix, cube map, polygon offset or line and point primitives; texture coordinates
within 1,024, as the chip's floats lose a fraction beyond, conformance t14). It writes the draw's vertices in object
coordinates (x, y, z, w, the colour, the units' s and t, through the texture matrix where it is affine) into a vertex
area, makes indices for GL's primitive, sent in pieces of at most 300 (strips and fans as the chip's up to 300
vertices, else lists; quads and quad strips as lists with the general path's diagonal, or ending at the quad's
last vertex when flat; a flat polygon's first vertex as the provoking one), sets the engine's state (the
model-view-projection matrix and the viewport where they changed, outputs, shading, culling in both the setup engine
and the engine, as Mesa's radeonCullFace and radeonFrontFace) and sends the indexed draw (`hal/src/radeon/tcl.c`). A
window-space draw after it puts the window's words back (`tri.c`). `GL_DGL_hw_tnl` is in `GL_EXTENSIONS` while it is
on; it is on by default (`DGL_TNL=0` turns it off: below for its history); `DGL-STAT` counts `tnl/s`; the first such draw says
`DGL-RADEON the transform engine draws`.

On the P3's 7500 (2026-11-24): the whole suite passes with it, 32 of the 42 tests drawn through the engine (the rest:
fog, lighting, texture generation, cube maps, lines and points, a capped texture heap), against the same Mesa
references.

## X3: the vertex area in system memory

(Since 2026-11-26 the vertices go to video memory by default, and this area only with `DGL_RADEON_VAREA=n`: the
engine fetching them through the GART made it the bottleneck; see the last section.)

The engine's vertices go into a 128 KB area of conventional memory beside the command processor's ring, mapped
through the same PCI GART (`radeon_cp_varea`, asked for at device open on chips with the engine unless `DGL_TNL=0`;
`DGL_RADEON_VAREA` kilobytes, 0 for none). The CPU writes it cached; the chip fetches it as it fetches the ring. It is
used in two halves: when one is full, a fence (the command processor writes its number into `SCRATCH_REG1` when it
gets there, as the DRM ages its vertex buffers) marks the half's last draw, and the other half is written again only
once its own fence has passed. Without it (DOS hadn't the room, or `DGL_RADEON_VAREA=0`) the vertices go to a 1 MB
area in video memory as before, which the CPU writes uncached across the bus and which is reused after a full sync.

GLQuake on the P3's 7500 (T&L on, 320x200): 89.97 fps from video memory, 91.24 from system memory; the CPU path
91.00. GLQuake draws polygons of four or five vertices one at a time, so the engine saves it little: its time is the
CPU's, in its own code and DOS-GL's per-draw work.

## X1 and X2's cost to GLQuake on the P3, and the instruction cache

X1 and X2 made GLQuake's timedemo on the P3's 7500 (a Pentium III, 16 KB instruction cache) 12% slower, though none
of their new work runs for it. Measured from clean builds only: an incremental build (the archive's members
replaced in place) and a clean one of the same source differ by up to 4% on this CPU.

| GLQuake M6 / M3, T&L off (fps) | |
|---|---|
| before X1 (8148677) | 91.05 / 98.22 |
| X1 and X2 as merged (608ff23) | 80.11 / 85.29 |
| the state emission's comparison fixed (17d691b) | 85.58 / 91.99 |
| dx7-x3b | 87.0 / 93.5 |

The Pentium III's performance counters (GLOS's `glos pmc`, and the profiling build's stages counted by RDPMC) showed
where it went: instruction fetch. Over the timedemo the merged code took 22% more instruction-cache misses and 19%
more cycles waiting for instructions; the vertex stage's misses per triangle doubled, validation's rose a third.
Each draw now called into code elsewhere in memory (the clip planes' setup, the transform engine's hook) even when
it did nothing, and the hot functions carried X1/X2's rare cases inline. What brought most of it back:

- the per-draw calls made only when they do something (`clip_enabled`; the engine's hook installed only when the
  engine will draw);
- rare cases out of line: the transform's r and generated coordinates, two-sided lighting and clip planes in
  `tri`, normals and r in `imm_vertex`, anisotropy, bias and cube maps in `unit_words`, dot3 in validation, the
  cube map's q in `pack`, cube maps in `glBindTexture`;
- `MGA_HOT` (`hal/include/mga/types.h`): the functions run for every vertex, draw or bind go to `.text.hot`, which
  DJGPP's linker places together after all other code, so they no longer collide with each other in the cache
  wherever their objects land (21 KB).

Instruction-cache misses are back to before X1 (22.6 million against 21.8); what is left is about 5% more
instructions retired. Copying the larger vertex (76 bytes against 52) by GCC's dword loop is part of it, but an
unrolled copy, inline or shared, measured slower: on this CPU code size costs more than those instructions.

## X3: the P3's hang of 2026-11-25, and the pieces' size

Echoes' ECHB fly scene hung the P3 with the engine on (main 2c6081c): a fence never reached (`DGL-WARN ... a fence
not passed in a second`), the job killed, the PC down until a power cycle. Its ground is drawn in big indexed draws,
which DOS-GL sent to the engine in pieces of up to 4,092 indices inline in the command stream; Mesa's driver never
sends more than 300 (`radeon_tcl.c`'s `GET_MAX_HW_ELTS`: "testing shows a maximum around here"). Nothing tested
before used big indexed draws (GLQuake's polygons, the conformance spheres, DTCL0's unindexed list). Now:

- pieces of 300 indices (`RADEON_TCL_PIECE`), strips and fans longer than that drawn as lists;
- a fence not passed in a second switches the engine off for the rest of the run (the CPU's path from there), instead
  of writing over vertices the chip may still be fetching;
- t43: big draws in one call each (three 15,000-index grids that cycle the vertex area's halves, a 2,002-vertex strip,
  a 1,002-vertex fan, a textured grid through an affine texture matrix), against OSMesa.

Affine texture matrices are taken by the engine path since (s and t made while the vertices are copied); a
projective one still takes the CPU's path. Fifth Wheel's HIGH (texel coordinates scaled by 1/512) is drawn so.

**The second hang (2026-11-25, 14:31).** With the 300-index pieces (main 7a71642), Echoes' fly scene hung the P3
again at frame 372: a 700 ms stall between its HUD and the next tick (the chip already stuck), then the missed fence;
the engine switched itself off, but the card didn't recover and the PC went down. So the pieces' size was not the
(whole) cause. T&L is opt-in (`DGL_TNL=1`) until it is found. The suspect now: the fly scene interleaves the engine's
draws with window-space draws inline in the stream (`3D_DRAW_IMMD`: its HUD lines, and batches too big for half the
vertex area), many times a frame, with the matrix sent 20-30 times a frame; GLQuake, which never hung, draws nearly
everything through the engine, and Mesa's driver never interleaves the two that way.

## X3: the hangs found and cured, and T&L on by default (2026-11-25/26)

**Finding it.** On a missed fence or a stalled wait the back end dumps the ring around the read index, its last 32
draws, and in full the draw holding the index and the two engine draws before it (`DGL-TNLDIAG`); `DGL_TNL_TRACE`
traces chosen frames. `CP_CSQ_STAT`'s second field (Linux r100's debugfs layout) gives where the microcode reads, mod
128: in every dump it sat at the end of a draw or in the state after it, so the draw the engine never finished was the
one before the read index, not the one at it. Replayed alone from the P3's own dump in a stress program (DTMIX mode R,
`~/bench-archive/dtmix`, outside the repository), Echoes' far terrain (945 indices in four pieces) stopped the engine
at five placements in sixteen of its vertices in memory, with either packet (Mesa 7's shipped `3D_RNDR_GEN_INDX_PRIM`
or `LOAD_VBPNTR` + `DRAW_INDX`); none of the triangles alone, and only after the draw's earlier pieces.

**The cause and the cure.** The chip takes a split draw's pieces back to back, or state written while a draw is in
flight, badly: the engine stops for good, through every reset. A `WAIT_UNTIL 3D_IDLE` between the pieces of one draw
cures the first (a 60 s sweep of placements and vertex shifts clean, against a hang at the first frame without), and
one after an engine draw before the next register really written other than a matrix (matrices go through the
engine's own state flush) cures the second (Echoes' fly scene stopped there with only the first: a 96-index draw
then a textured one). `radeon_tcl_sync` = 6 is the default; the Linux DRM waits for 3D idle between repeated batches
for a lockup it couldn't explain either. Ruled out on the way: the sub-pixel mesh and the atmosphere's limb (only
queued behind the stuck draw), 64-byte alignment and 32-byte vertex slots (they moved the stall), the packet type.

**Safety.** If the engine sticks anyway: its recovery (Linux KMS r100's order: bus mastering off, the engines' reset
held, 2 s for it to come back); if it doesn't, the layer touches none of the engine's registers again (`radeon_dead`:
a write to a full FIFO holds the PCI bus, which froze the P3 twice) and the program ends with `DGL-ERR`, the PC alive;
GLOS's guard then skips the BIOS's mode set. A wait of half a second or more that runs out counts as a stall.

**Speed.** The card was slower than the P3's CPU at first for DOS-GL's own reasons, not the chip's: a sparse draw's
whole vertex range was converted and written (now only the vertices named); the engine fetched its vertices from
system memory through the PCI GART, which made it the bottleneck (now video memory: DTMIX's textured stress mode 25.5
fps against 65.0); each immediate-mode primitive was a draw of its own (immediate mode now stays on the CPU:
`DGL_TNL_IMM=n` to send it); draws of a few vertices cost more than they save (fewer than 8 now go to the CPU,
`DGL_TNL_MINV`). On the P3, T&L on against off (Echoes and Fifth Wheel on `7969d3c`, the rest on the final build):

| Program | T&L on | T&L off | |
|---|---|---|---|
| Echoes, fly scene Low / Medium / High | 13.70 / 17.49 / 22.01 ms | 15.32 / 20.12 / 26.79 ms | +11 / +13 / +18% |
| Fifth Wheel FW1 / FW1H | 74.9 / 69.3 fps | 65.1 / 62.4 fps | +15 / +11% |
| GLQuake demo1 (immediate mode) | 85.7 fps | 85.8 fps | level |
| Quake 2 demo1 (immediate mode) | 45.9 fps | 45.9 fps | level |
| PrBoom-plus demo1 (draws of four vertices: all the CPU's) | 82.1 fps | 82.2 fps | level |

Half-Life wasn't run on the P3 (its data's long names can't be copied there yet). The rest of the engine's gains are
X3b's: lighting on the engine and static geometry kept in video memory (below).


## X3: engine draws back to back, and a card left stuck (2026-11-26)

**The hazard.** Two of the engine's draw packets straight after each other, with neither a wait nor a matrix between,
stop the 7500 as a split draw's pieces did. X3b's GLQuake, its polygons sent to the engine one draw each, stopped it
within a second in every run, batched or not (the dumps: the setup engine busy, the rasteriser idle, the command FIFO
full behind the next state). A wait before a matrix too didn't help; a wait after every draw, or before every engine
draw that follows another, did. On `551cc82` the programs measured never sent two such draws in a row (Echoes' and
Fifth Wheel's are a matrix apart, GLQuake's and Quake 2's immediate mode is the CPU's, PrBoom-plus's quads too small),
but any program drawing several arrays of eight vertices or more under one matrix would. Mode 6 now also waits before
an engine draw that follows another with no matrix between (`radeon_tcl_adjacent`); draws a matrix keeps apart keep
their overlap.

**Strips and fans in one packet.** The HAL sends a strip, a fan or a line strip as one packet whatever its length (the
back end makes a longer one a list first): cut into pieces, a fan's later pieces lose their centre and a piece can be
one or two vertices long, which stopped the engine every time (found through `DGL_TNL_PIECE=6`, a test setting that
used to leave strips of up to 300 indices to the HAL).

**A card an earlier program left stuck.** No reset this layer knows frees an engine stopped this way; a warm reboot
(`glos reboot`) does. A second program used to freeze the PC on such a card at its mode set or first write (the P3
twice). The device's open now waits half a second for the engine, tries the recovery, and if the engine is still
stuck fails with "the card's engine is stuck, left so by an earlier program ... reboot the PC", touching nothing that
could hold the bus. Tested on the P3 with `DGL_TEST_STICK=1` (tests only: the first window-space draw claims three
vertices more than it sends): the first program ends with `DGL-ERR`, GLOS's guard skips the BIOS's mode set, a second
program refuses the card, `glos reboot` clears it. `DGL_TEST_NOCHECK=1` (tests only) skips the open's check, so that
GLOS's own refusal of mode sets on a dead card can be tried.

## X3b: lighting on the 7500's engine, buffer objects kept in video memory (2026-11-26)

**Lighting.** A lit draw goes through the engine as Mesa 7.0.3's radeon driver sends one: eye-space lighting, the
modelview in matrix slot 1 and its inverse (as the rows) in slot 2 for the normals, each vertex's normal straight after
its position (radeon_maos_vbtmp.h's order), the front material in registers 2210h-2250h, each of the eight lights in the
vector memory from 64 + i at a stride of 8 and the scalar memory from i at a stride of 8, the light model's ambient and
the eye vector in vectors 122 and 124, colour material as the vertex's colour for the components it names. Directional,
point and spot lights, attenuation, the local viewer, GL_NORMALIZE and GL_RESCALE_NORMAL_EXT; the specular is added
into the colour (OpenGL 1.1 has one). Two-sided lighting stays on the CPU's path. Conformance t40 passes with its lit
draws on the engine (and with its immediate-mode ones forced there, `DGL_TNL_IMM=3 DGL_TNL_MINV=0`). `DGL_TNL_LIGHT=0`
keeps every lit draw on the CPU.

**Buffer objects in video memory.** A draw from the arrays whose every enabled array is in a `GL_STATIC_DRAW` buffer
object has its vertices, 0 to its highest index, converted once into a 4 MB region of video memory and drawn from there,
indices only, while the buffers' generations (bumped by `glBufferData`, `glBufferSubData` and a writing
`glUnmapBuffer`), the arrays' layout, the current values standing for disabled arrays, the texture matrices and the
vertex's format are the same. t24 (a buffer changed both ways and drawn again) passes. `DGL_TNL_VBO=0` converts every
draw.

**What they gain.** DLIT (a stress program outside the repository: a lit, colour-material terrain, 8 copies of a 24 x
24 grid, 9,216 triangles a frame, a directional and a point light, from a static buffer object) on the P3:

| | fps |
|---|---|
| Everything on the CPU (`DGL_TNL=0`), or lighting on the CPU (`DGL_TNL_LIGHT=0`) | 25.9 / 26.1 |
| Lighting on the engine, the vertices converted each draw (`DGL_TNL_VBO=0`, or client arrays) | 171.3 |
| Lighting on the engine, the vertices in video memory | 328.5 |
| The same unlit | 436.0 |

**Fog on the engine.** Mesa's set-up (the mode in UCP_VERT_BLEND_CTL bits 8-9, the factor computed into the specular
alpha with the lighting on and no light, the modelview in slot 1) is right, but not its fog vector: Mesa writes (c, d,
0, 0) to vector 123, and on the 7500 that fogs by the vertex's eye x. DFOG2 (a probe outside the repository: 64 small
quads at eight depths and eight screen columns, each read back, the vector set by hand) found how the engine reads the
vector (x, y, z, w) against the eye-space position (ex, ey, ez), clamped at each vertex:

| Mode | The engine's factor | GL's fog on eye z |
|---|---|---|
| Linear | y + x ex + z ez (w unused) | (0, end/(end - start), 1/(end - start), 0) |
| Exp | exp(x ex + y ey + z ez) | (0, 0, density, 0) |
| Exp2 | exp(x ex² + y ey² + z ez²) | (0, 0, -density², 0) |

(eye z is negative ahead of the eye). With those, all 192 of DFOG2's values (three modes, 64 quads) are the CPU path's
to the byte at 32 bits. Range-based fog (UCP_VERT_BLEND_CTL bit 10), TCL_VTX_FP_FOG, the pixel pipeline's
FOG_USE_DEPTH, the lighting's enable and the eye vector changed nothing that GL needs.

The engine's factor is per vertex, clamped there and interpolated across the screen, not in perspective: one long
linear-fogged triangle drifts as much as an exp one (t44's first version, one draw from depth 2 to 15, 8,718 pixels
out on the P3). The CPU's path splits a triangle whose factor changes by more than a tenth across it. So a fogged draw
goes to the engine only where both give the same picture: the factor changing by a tenth at most over its depths (its
box's corners through the modelview, kept with a buffer object's vertices in video memory). Others (t10's floor
quads, 1.5 to 20 deep) go to the CPU's path; conformance t44 draws the same floors in bands that the engine takes. `DGL_TNL_FOG=0` keeps every
fogged draw there.

**Texture-coordinate generation and clip planes on the engine.** As Mesa's `radeon_validate_texgen`: a unit whose s
and t are both generated by one linear mode (object or eye), r and q not, on a 2D texture: the planes under the unit's
texture matrix (affine, its q divided out) in TEXMAT_n (slot 3 + n, rows s, t, q, r), SE_TCL_TEXTURE_PROC_CTL's
TEXGEN_TEXMAT_n and TEXMAT_n with the input (object or eye: the eye-space position needs the modelview in slot 1),
and the unit's output in SE_TCL_OUTPUT_VTX_SEL the computed coordinates (8 + n). Sphere maps stay on the CPU's path,
as in Mesa, and so do reflection and normal maps (cube maps: the engine's vertices carry no r). User clip planes: GL's
eye-space planes times the inverse projection (the CPU's clip-space planes) at vectors 116 to 121, their enables in
UCP_VERT_BLEND_CTL bits 2 to 7 (UCP_IN_CLIP_SPACE). `DGL_TNL_TEXGEN=0` and `DGL_TNL_CLIP=0` keep them on the CPU.

**Batching, tried and taken out.** Holding an engine draw for the next of the same state to join never joined one in the
programs measured (GLQuake's immediate mode and PrBoom-plus's quads stay on the CPU, Echoes' and Fifth Wheel's draws are
a matrix apart), and its checks in the CPU path's hot functions cost GLQuake 3% on the P3 (the instruction cache).
Immediate mode through the engine stays opt-in (`DGL_TNL_IMM=n`): GLQuake's polygons ran 72.5 fps there against 84.6 on
the CPU, batched or not.

**The other programs.** In clean builds on the P3 against main: PrBoom-plus level, Quake 2 0.7% and GLQuake 1.8% below,
the CPU's path's code the same (its hot sections main's size, the profiling builds level): the placement of the rest of
the program, which has moved GLQuake by several percent before with no change in work.
