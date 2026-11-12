#!/usr/bin/env bash
# Build the pinned, locally patched 86Box used by Loop A, and fetch the ROMs.
# Runs inside the dev container (tools/dev). Output:
#   $BOX86_DIR/bin/86Box        -> current build (symlink)
#   $BOX86_DIR/roms/            sparse checkout of the ROM set
# The build is keyed by the pin plus the patch series, so it is a no-op when
# nothing changed.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
eval "$(sed -n 's/^\(BOX86_[A-Z]*\|ROMS_[A-Z]*\|MATROX_[A-Z0-9_]*\) *:= *\(.*\)$/\1=\2/p' "$root/tools/setup/versions.mk")"
: "${BOX86_DIR:=${MGA_CACHE:-$HOME/.cache/mga-glide}/86box}"
mkdir -p "$BOX86_DIR/bin"

key=$( { echo "$BOX86_COMMIT"; cat "$here/series";
         grep -v '^#' "$here/series" | while read -r p; do [ -n "$p" ] && cat "$here/patches/$p"; done; } \
       | sha256sum | cut -c1-16)
out=$BOX86_DIR/bin/86Box-$key

if [ ! -x "$out" ]; then
  src=$BOX86_DIR/src-$BOX86_COMMIT
  if [ ! -d "$src/.git" ]; then
    git init -q "$src"
    git -C "$src" remote add origin "$BOX86_REPO"
    git -C "$src" fetch -q --depth 1 origin "$BOX86_COMMIT"
    git -C "$src" checkout -q FETCH_HEAD
  fi
  work=$BOX86_DIR/work-$key
  rm -rf "$work"; mkdir -p "$work"
  git -C "$src" archive "$BOX86_COMMIT" | tar -x -C "$work"
  grep -v '^#' "$here/series" | while read -r p; do
    [ -n "$p" ] || continue
    echo "86box: applying $p"
    (cd "$work" && patch -p1 --no-backup-if-mismatch -s < "$here/patches/$p")
  done
  cmake -S "$work" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DDEV_BRANCH=ON -DQT=OFF -DSDL2=ON -DDYNAREC=ON \
        -DDISCORD=OFF -DMUNT=OFF -DFLUIDSYNTH=OFF -DRTMIDI=OFF -DOPENAL=ON \
        -DGDBSTUB=OFF -DNEW_DYNAREC=OFF > "$work/cmake.log"
  ninja -C "$work/build" > "$work/ninja.log" || { tail -40 "$work/ninja.log"; exit 1; }
  bin=$(find "$work/build" -type f -name 86Box -perm -u+x | head -1)
  cp "$bin" "$out"
  rm -rf "$work"
  echo "86box: built $out"
fi
ln -sfn "$(basename "$out")" "$BOX86_DIR/bin/86Box"

roms=$BOX86_DIR/roms
# The BF6 and Matrox ROMs for the Pentium II profile; the Shuttle HOT-433A and
# the S3 Trio64V2/DX for the 486 profiles (run.py --machine, --card vbe); the
# Crystal CS423x cards' Plug and Play data (run.py --sound cs4232, cs4235,
# cs4236b: 86Box offers them only when it is there; GLOS's crystal.c); the S3
# ViRGE/DX's generic BIOS (run.py --card virge: bench C's card, the first non-Matrox
# back end, gl/docs/backends.md).
rom_dirs="/machines/bf6/ /video/matrox/ /machines/hot433/ /video/s3/ /sound/crystal/ /video/s3virge/"
if [ "$(cat "$roms/.commit" 2>/dev/null)" != "$ROMS_COMMIT" ]; then
  rm -rf "$roms"
  git init -q "$roms"
  git -C "$roms" remote add origin "$ROMS_REPO"
  git -C "$roms" config core.sparseCheckout true
  printf '%s\n' $rom_dirs > "$roms/.git/info/sparse-checkout"
  git -C "$roms" fetch -q --depth 1 --filter=blob:none origin "$ROMS_COMMIT"
  git -C "$roms" checkout -q FETCH_HEAD
  echo "$ROMS_COMMIT" > "$roms/.commit"
  echo "86box: roms at $ROMS_COMMIT"
fi
# A checkout made with a shorter list gains the missing directories in place
# (other jobs may be using it, so it is never removed for this).
added=0
for d in $rom_dirs; do
  grep -qxF "$d" "$roms/.git/info/sparse-checkout" || { echo "$d" >> "$roms/.git/info/sparse-checkout"; added=1; }
done
if [ "$added" = 1 ]; then
  git -C "$roms" read-tree -mu HEAD
  echo "86box: roms gained $rom_dirs"
fi
# Genuine Matrox BIOSes for the emulated G200/G400/G450 (local patches 0004+).
mbios=$roms/video/matrox/mgaglide
if [ ! -s "$mbios/900-33.bin" ] || [ ! -s "$mbios/897-21.bin" ] || [ ! -s "$mbios/935-20.bin" ] || [ ! -s "$mbios/899-33.bin" ]; then
  mkdir -p "$mbios"
  "$root/tools/setup/fetch.sh" "$MATROX_BIOS_URL" "$MATROX_BIOS_SHA256" "$mbios/setup257.exe"
  unzip -o -q "$mbios/setup257.exe" 900-33.bin 899-33.bin 897-21.bin 935-20.bin -d "$mbios"   # (899-33: the PCI G200's, 0116)
  rm -f "$mbios/setup257.exe"
  echo "86box: Matrox BIOSes in $mbios"
fi
for f in machines/bf6/Beh_70.bin video/matrox/productiva8mbsdr.BIN video/matrox/mgaglide/900-33.bin \
         machines/hot433/2A4X5H21.BIN video/s3/86c775_2.bin video/s3virge/86c375_1.bin; do
  [ -s "$roms/$f" ] || { echo "86box: missing ROM $f" >&2; exit 1; }
done
echo "86box: ready ($BOX86_DIR/bin/86Box, key $key)"
