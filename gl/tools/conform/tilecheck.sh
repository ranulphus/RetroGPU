#!/bin/sh
# tilecheck.sh [OUT]: the software renderer's paths against each other, byte for byte (gl/docs/b2.md B2.7,
# B2.8). Every conformance test's rig build (make rig-build) at 16, 32 and 8 bits, in four sets of the core's
# spans: fast (DGL_SOFT_EXACT=0, MMX where the CPU has it), ref (1: the reference spans), gen (2: the general
# integer spans) and nommx (0 with DGL_SOFT_MMX=0); each drawn with DGL_SOFT_TILE=0 and through tiles of 64, 16
# and 7 pixels. Tiles must draw what drawing at once does, in every set; fast and nommx must draw the same (MMX
# against C), and nommx and gen (the specialised runs against the general spans). The programs are 32-bit static
# Linux ones: they run here, outside the dev container. Exit status 0 when every frame matches. Make target:
# soft-tilecheck.
gl=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-$gl/out/tilecheck}
rm -rf "$out"
bad=0 n=0 fail=0

# compare A B WHAT: every frame under $out/A against the same one under $out/B.
compare() {
  for f in "$out/$1"/*/*.ppm; do
    rel=${f#"$out/$1/"}
    n=$((n + 1))
    if ! cmp -s "$f" "$out/$2/$rel"; then
      echo "tilecheck: $rel differs: $3"
      bad=$((bad + 1))
    fi
  done
}

for set in fast:0:1 ref:1:1 gen:2:1 nommx:0:0; do
  name=${set%%:*} rest=${set#*:}
  exact=${rest%%:*} mmx=${rest#*:}
  for bpp in 16 32 8; do
    for tile in 0 64 16 7; do
      for t in "$gl"/build/rig/conform/t[0-9]*; do
        test=$(basename "$t") d="$out/$name-$bpp-$tile/$(basename "$t")"
        mkdir -p "$d"
        extra=
        [ "$test" = t16 ] && extra=DGL_TEXHEAP_KB=600     # (the manifest's "pre" for t16)
        if ! env CT_OUT="$d" DGL_DEVICE=mem CT_BPP=$bpp DGL_SOFT_TILE=$tile DGL_SOFT_EXACT=$exact \
             DGL_SOFT_MMX=$mmx $extra "$t" >"$d/log" 2>&1; then
          echo "tilecheck: $test failed to run ($name, $bpp bits, tile $tile; log: $d/log)"
          fail=$((fail + 1))
        fi
      done
    done
    for tile in 64 16 7; do
      compare "$name-$bpp-0" "$name-$bpp-$tile" "$name spans at $bpp bits, tiles of $tile against at once"
    done
  done
done
for bpp in 16 32 8; do
  compare "fast-$bpp-0" "nommx-$bpp-0" "MMX against C at $bpp bits"
  compare "nommx-$bpp-0" "gen-$bpp-0" "the specialised runs against the general spans at $bpp bits"
done
echo "tilecheck: $n frames compared, $bad differ, $fail runs failed"
[ "$bad" = 0 ] && [ "$fail" = 0 ] && [ "$n" -gt 0 ]
