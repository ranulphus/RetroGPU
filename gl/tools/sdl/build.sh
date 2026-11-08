#!/usr/bin/env bash
# Build SDL3 (third_party/sdl, pinned upstream) as a static library, with the
# patch series in tools/sdl/patches/*.patch applied to a copy of the source
# (the OpenGL path through DOS-GL and its companions; docs/sdl.md).
#
#   tools/sdl/build.sh          DOS: DJGPP, SDL's own toolchain file.
#                               Output: build/sdl/dos/{include/SDL3,lib/libSDL3.a}
#   tools/sdl/build.sh --host   Linux, the same pinned source, for programs that
#                               also build natively (Fifth Wheel). Output: build/sdl/host
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
DJGPP_PREFIX=${DJGPP_PREFIX:-$HOME/.local/opt/djgpp-gcc1220}
target=dos
[ "${1:-}" = --host ] && target=host
src=$root/third_party/sdl
work=$root/build/sdl/src-$target
prefix=$root/build/sdl/$target
[ -f "$src/CMakeLists.txt" ] || { echo "sdl: submodule missing (git submodule update --init third_party/sdl)" >&2; exit 1; }
# The installed library stays in place until the new one is ready: programs
# that link against it while it rebuilds (Fifth Wheel builds against ~/DOSGL)
# see the old or the new, never none. Installed into $prefix.new, then swapped.
rm -rf "$work" "$prefix.new"; mkdir -p "$work"
(cd "$src" && git archive HEAD) | tar -x -C "$work"
for p in "$root"/tools/sdl/patches/*.patch; do
  [ -e "$p" ] || continue
  (cd "$work" && patch -p1 --no-backup-if-mismatch -s < "$p")
done
log=$root/build/sdl/build-$target.log
common=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF)
if [ $target = dos ]; then
  export PATH=$DJGPP_PREFIX/bin:$PATH LD_LIBRARY_PATH=$DJGPP_PREFIX/hostlib
  extra=(-DCMAKE_TOOLCHAIN_FILE="$work/build-scripts/i586-pc-msdosdjgpp.cmake")
  # The OpenGL path through DOS-GL (tools/sdl/patches/0001) once it exists.
  if grep -q SDL_DOS_DOSGL "$work/CMakeLists.txt" 2>/dev/null; then
    extra+=(-DSDL_DOS_DOSGL=ON -DDOSGL_DIR="$root")
  fi
else
  # Extensions a game does not need, whose headers the dev container lacks.
  extra=(-DSDL_X11_XTEST=OFF -DSDL_X11_XSCRNSAVER=OFF)
fi
{ cmake -S "$work" -B "$work/build" "${common[@]}" "${extra[@]}" &&
  cmake --build "$work/build" -j"$(nproc)" && cmake --install "$work/build" --prefix "$prefix.new"; } > "$log" 2>&1 \
  || { tail -40 "$log"; echo "sdl: build failed ($log)" >&2; exit 1; }
rm -rf "$prefix.old"
[ -d "$prefix" ] && mv "$prefix" "$prefix.old"
mv "$prefix.new" "$prefix"
rm -rf "$prefix.old"
echo "sdl: $prefix/lib/libSDL3.a ($(git -C "$src" rev-parse --short HEAD))"
