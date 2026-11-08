#!/usr/bin/env bash
# Build PrBoom-plus for DOS on DOS-GL's SDL3 (build/sdl/dos) with DJGPP.
#
#   build.sh [--work] [--host]
#
# Source: the fork's commit pinned in tools/doom/deps.mk, exported (git
# archive) from $PRBOOM_DIR (default ~/prboom-plus-dos; a local repository,
# not published) into build/doom/src. --work builds the checkout's working
# tree as it is (tracked and new files) instead.
#
# The data WAD (prboom-plus.wad) is made by the fork's rdatawad, built for
# this host first (build/doom/host); a cross build imports it from there.
# --host also builds the whole game for Linux there, against build/sdl/host
# (desktop SDL3): the build whose demo checksums must equal reference.json.
#
# DOOM_CFLAGS adds C flags to the DOS build (e.g. -DDOS_GLCHECK: GL calls
# DOS-GL refuses logged with their file and line).
#
# Output in build/doom:
#   PRBOOMP.EXE  the game (stripped)    PRBOOMP.SYM  the same, with symbols
#   PRBOOM.WAD   the data WAD           STAGED.TXT   the commit the build used
set -euo pipefail
unset MAKEFLAGS MFLAGS MAKELEVEL
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
D=${DJGPP_PREFIX:-$HOME/.local/opt/djgpp-gcc1220}
work=0 host=0
for a in "$@"; do
  case $a in
    --work) work=1 ;;
    --host) host=1 ;;
    *) echo "usage: build.sh [--work] [--host]" >&2; exit 2 ;;
  esac
done
pin() { sed -n "s/^$1 *:= *//p" "$here/deps.mk"; }
out=$root/build/doom
src=$out/src
fork=${PRBOOM_DIR:-$(pin PRBOOM_DIR)}
fork=${fork/#\~/$HOME}
[ -d "$fork/.git" ] || { echo "doom: no fork checkout at $fork (PRBOOM_DIR)" >&2; exit 1; }
for f in "$root/build/sdl/dos/lib/libSDL3.a" "$root/build/lib/libGL.a"; do
  [ -f "$f" ] || { echo "doom: no $f (make sdl lib)" >&2; exit 1; }
done
mkdir -p "$out"
rm -rf "$src"; mkdir -p "$src"
if [ $work = 1 ]; then
  (cd "$fork" && git ls-files -z -co --exclude-standard | tar --null -T - -cf -) | tar -x -C "$src"
  commit=$(git -C "$fork" rev-parse --short=8 HEAD)
  git -C "$fork" diff --quiet HEAD || commit=$commit-dirty
  echo "work $commit" > "$out/STAGED.TXT"
  echo "doom: working tree of $fork ($commit)"
else
  c=$(pin PRBOOM_COMMIT)
  git -C "$fork" cat-file -e "$c^{commit}" 2>/dev/null || { echo "doom: $fork lacks $c" >&2; exit 1; }
  git -C "$fork" archive "$c" | tar -x -C "$src"
  commit=${c:0:8}
  echo "fork $c" > "$out/STAGED.TXT"
  echo "doom: fork $commit"
fi
log=$out/build.log
: > "$log"
run() { "$@" >> "$log" 2>&1 || { grep -E "error|Error" "$log" | tail -20; echo "doom: failed: $*" >&2; exit 1; }; }
# The fork's code must not lean on implicit declarations or pointer/int mixups
# (SDL2 -> SDL3 changed many signatures).
strict="-Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types"
nolibs=(-DWITH_IMAGE=OFF -DWITH_MIXER=OFF -DWITH_NET=OFF -DWITH_PCRE=OFF -DWITH_ZLIB=OFF -DWITH_MAD=OFF
        -DWITH_FLUIDSYNTH=OFF -DWITH_DUMB=OFF -DWITH_VORBISFILE=OFF -DWITH_PORTMIDI=OFF -DWITH_ALSA=OFF
        -DBUILD_SERVER=OFF)

# Host: rdatawad (and with --host the game) against desktop SDL3.
[ -f "$root/build/sdl/host/lib/libSDL3.a" ] || { echo "doom: no build/sdl/host (make sdl-host)" >&2; exit 1; }
run cmake -S "$src/prboom2" -B "$out/host" -DCMAKE_BUILD_TYPE=Release -DSDL3_DIR="$root/build/sdl/host/lib/cmake/SDL3" \
  -DBUILD_GL=OFF "${nolibs[@]}" -DCMAKE_C_FLAGS="$strict"
if [ $host = 1 ]; then
  run cmake --build "$out/host" -j"$(nproc)"
  echo "doom: build/doom/host/prboom-plus (Linux)"
else
  run cmake --build "$out/host" -j"$(nproc)" --target prboomwad
fi

# DOS: DJGPP with SDL's toolchain file, SDL3 and DOS-GL from this tree.
export PATH=$D/bin:$PATH LD_LIBRARY_PATH=$D/hostlib
# Both renderers: software (the default, -vidmode 8) and OpenGL through
# DOS-GL (-vidmode gl), without GLU.
run cmake -S "$src/prboom2" -B "$out/dos" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$root/third_party/sdl/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DIMPORT_EXECUTABLES="$out/host/ImportExecutables.cmake" \
  -DSDL3_DIR="$root/build/sdl/dos/lib/cmake/SDL3" \
  -DBUILD_GL=ON "${nolibs[@]}" \
  -DCMAKE_C_FLAGS="$strict -march=pentium -DDOS_BUILD_COMMIT=\\\"$commit\\\" -I$root/include ${DOOM_CFLAGS:-}" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$root/build/lib"
run cmake --build "$out/dos" -j"$(nproc)"
cp "$out/dos/prboom-plus.exe" "$out/PRBOOMP.SYM"
"$D/bin/i586-pc-msdosdjgpp-strip" -o "$out/PRBOOMP.EXE" "$out/PRBOOMP.SYM"
cp "$out/dos/prboom-plus.wad" "$out/PRBOOM.WAD"
(cd "$out" && ls -l PRBOOMP.EXE PRBOOM.WAD) | awk '{print "doom: build/doom/" $NF " " $5 " bytes"}'
