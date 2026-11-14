#!/bin/sh
# tilecheck.sh [OUT]: the software renderer's paths against each other, byte for byte (gl/docs/b2.md B2.7,
# B2.8). Every conformance test's rig build (make rig-build) at 16, 32 and 8 bits, on each of the core's span
# modes (DGL_SOFT_EXACT 0: all fast paths, 1: the reference spans, 2: the general integer spans), drawn with
# DGL_SOFT_TILE=0 and through tiles of 64, 16 and 7 pixels. Tiles must draw what drawing at once does, in every
# mode; and mode 0's specialised runs what mode 2's general spans do. The programs are 32-bit static Linux ones:
# they run here, outside the dev container. Exit status 0 when every frame matches. Make target: soft-tilecheck.
gl=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-$gl/out/tilecheck}
rm -rf "$out"
bad=0 n=0 fail=0
for mode in 0 1 2; do
for bpp in 16 32 8; do
  set=$mode-$bpp
  for tile in 0 64 16 7; do
    for t in "$gl"/build/rig/conform/t[0-9]*; do
      name=$(basename "$t") d="$out/$set-$tile/$(basename "$t")"
      mkdir -p "$d"
      extra=
      [ "$name" = t16 ] && extra=DGL_TEXHEAP_KB=600     # (the manifest's "pre" for t16)
      if ! env CT_OUT="$d" DGL_DEVICE=mem CT_BPP=$bpp DGL_SOFT_TILE=$tile DGL_SOFT_EXACT=$mode $extra "$t" >"$d/log" 2>&1; then
        echo "tilecheck: $name failed to run at $bpp bits, tile $tile, exact $mode (log: $d/log)"
        fail=$((fail + 1))
      fi
    done
  done
  for tile in 64 16 7; do
    for f in "$out/$set-0"/*/*.ppm; do
      rel=${f#"$out/$set-0/"}
      n=$((n + 1))
      if ! cmp -s "$f" "$out/$set-$tile/$rel"; then
        echo "tilecheck: $rel differs at $bpp bits with tiles of $tile, exact $mode"
        bad=$((bad + 1))
      fi
    done
  done
done
done
for bpp in 16 32 8; do                  # (the specialised runs against the general integer spans)
  for f in "$out/0-$bpp-0"/*/*.ppm; do
    rel=${f#"$out/0-$bpp-0/"}
    n=$((n + 1))
    if ! cmp -s "$f" "$out/2-$bpp-0/$rel"; then
      echo "tilecheck: $rel differs at $bpp bits between the specialised runs and the general spans"
      bad=$((bad + 1))
    fi
  done
done
echo "tilecheck: $n frames compared, $bad differ, $fail runs failed"
[ "$bad" = 0 ] && [ "$fail" = 0 ] && [ "$n" -gt 0 ]
