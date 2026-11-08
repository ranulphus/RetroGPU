#!/usr/bin/env bash
# Build ClassiCube (third_party/classicube, pinned) for DOS with its GL 1.1
# backend on DOS-GL (PRD §11.6, M5): copy the source, apply
# tools/classicube/patches/*.patch, link against build/lib/libGL.a.
# Output: build/cc/CCDOS.EXE
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
DJGPP_PREFIX=${DJGPP_PREFIX:-$HOME/.local/opt/djgpp-gcc1220}
src=$root/third_party/classicube
work=$root/build/cc/src
[ -f "$src/Makefile" ] || { echo "classicube: submodule missing (git submodule update --init)" >&2; exit 1; }
rm -rf "$work"; mkdir -p "$work"
(cd "$src" && git archive HEAD) | tar -x -C "$work"
for p in "$root"/tools/classicube/patches/*.patch; do
  (cd "$work" && patch -p1 --binary --no-backup-if-mismatch -s < "$p")
done
export PATH=$DJGPP_PREFIX/bin:$PATH LD_LIBRARY_PATH=$DJGPP_PREFIX/hostlib
make -C "$work" dos -j"$(nproc)" \
  EXTRA_CFLAGS="-DCC_GFX_BACKEND=CC_GFX_BACKEND_GL11 -DCC_BUILD_GL11 -I$root/include" \
  EXTRA_LIBS="-L$root/build/lib -lGL -lm" > "$root/build/cc/build.log" 2>&1 || { tail -30 "$root/build/cc/build.log"; exit 1; }
cp "$work/CCDOS.EXE" "$root/build/cc/CCDOS.EXE"
echo "classicube: build/cc/CCDOS.EXE"
