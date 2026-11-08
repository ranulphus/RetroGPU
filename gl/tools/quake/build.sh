#!/usr/bin/env bash
# Build the pinned Quake ports against build/lib/libGL.a.
#
#   build.sh [--work] [--soft]
#
# Sources: the checkouts $QDOS_DIR and $Q2DOS_DIR (default ~/qdos-dosgl and
# ~/q2dos-dosgl), cloned from the forks in tools/quake/deps.mk when missing.
# The pinned commits are exported with git archive into build/quake/src and
# built there; --work builds the checkouts as they are instead (tracked and
# new files, not ignored ones).
# Output in build/quake:
#   QDOSDGL.EXE   Quake on DOS-GL             Q2DGL.EXE    Quake 2 on DOS-GL
#   GAMEX86.DXE   Quake 2's game module       DOSLFN.COM   long file names (q2dos's doslfn.zip)
#   QDOS.EXE, Q2.EXE   the software renderers (--soft)
set -euo pipefail
# Called from make (make quake): the games' own makefiles must not see our
# command-line variables (q2dos's game makefile has its own GAME).
unset MAKEFLAGS MFLAGS MAKELEVEL GAME CARD
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
DJGPP_PREFIX=${DJGPP_PREFIX:-$HOME/.local/opt/djgpp-gcc1220}
work=0 soft=0
for a in "$@"; do
  case $a in
    --work) work=1 ;;
    --soft) soft=1 ;;
    *) echo "usage: build.sh [--work] [--soft]" >&2; exit 2 ;;
  esac
done
pin() { sed -n "s/^$1 *:= *//p" "$here/deps.mk"; }
out=$root/build/quake
mkdir -p "$out"

# export NAME DIR URL COMMIT -> build/quake/src/NAME
export_tree() {
  local name=$1 dir=$2 url=$3 commit=$4 dst=$out/src/$1
  [ -d "$dir/.git" ] || git clone -q "$url" "$dir"
  rm -rf "$dst"; mkdir -p "$dst"
  if [ $work = 1 ]; then
    (cd "$dir" && git ls-files -z -co --exclude-standard -x '*.exe' -x '*.EXE' -x '*.dxe' | tar --null -T - -cf -) | tar -x -C "$dst"
    echo "quake: $name from the working tree of $dir ($(git -C "$dir" describe --always --dirty))"
  else
    git -C "$dir" cat-file -e "$commit^{commit}" 2>/dev/null || git -C "$dir" fetch -q "$url" "$commit"
    git -C "$dir" archive "$commit" | tar -x -C "$dst"
    echo "quake: $name at ${commit:0:10}"
  fi
}
export_tree qdos "${QDOS_DIR:-$HOME/qdos-dosgl}" "$(pin QDOS_URL)" "$(pin QDOS_COMMIT)"
export_tree q2dos "${Q2DOS_DIR:-$HOME/q2dos-dosgl}" "$(pin Q2DOS_URL)" "$(pin Q2DOS_COMMIT)"

export PATH=$DJGPP_PREFIX/bin:$DJGPP_PREFIX/i586-pc-msdosdjgpp/bin:$PATH LD_LIBRARY_PATH=$DJGPP_PREFIX/hostlib
export DXE_LD_LIBRARY_PATH=$DJGPP_PREFIX/i586-pc-msdosdjgpp/lib      # dxe3gen (gamex86.dxe)
CC=i586-pc-msdosdjgpp-gcc
log=$out/build.log
: > "$log"
run() { "$@" >> "$log" 2>&1 || { tail -30 "$log"; echo "quake: failed: $*" >&2; exit 1; }; }
Q1=(CC=$CC USE_WATT32=no USE_GAMESPY=0 USE_OGG=no USE_SNDPCI=0)
Q2=(CC=$CC USE_WATT32=0 USE_GAMESPY=0 USE_CURL=0 USE_OGG=0 USE_SNDPCI=0 REF_DXE=0)
j=-j$(nproc)

# Quake: one build directory per renderer (the objects differ).
run make -C "$out/src/qdos/quake" $j "${Q1[@]}" GLQUAKE=1 GLDRIVER=dosgl DOSGL="$root"
cp "$out/src/qdos/quake/qdosdgl.exe" "$out/QDOSDGL.EXE"
# Quake 2: the game module, then the executables (clean between renderers).
run make -C "$out/src/q2dos/game" -f Makefile.dj $j CC=$CC
cp "$out/src/q2dos/game/gamex86.dxe" "$out/GAMEX86.DXE"
run make -C "$out/src/q2dos" -f Makefile.dj $j "${Q2[@]}" REF_STATIC_GL=1 REFGL_DRIVER=dosgl DOSGL="$root"
cp "$out/src/q2dos/q2dgl.exe" "$out/Q2DGL.EXE"
if [ $soft = 1 ]; then
  run make -C "$out/src/qdos/quake" clean
  run make -C "$out/src/qdos/quake" $j "${Q1[@]}"
  cp "$out/src/qdos/quake/qdos.exe" "$out/QDOS.EXE"
  run make -C "$out/src/q2dos" -f Makefile.dj clean
  run make -C "$out/src/q2dos" -f Makefile.dj $j "${Q2[@]}"
  cp "$out/src/q2dos/q2.exe" "$out/Q2.EXE"
fi
unzip -p "$out/src/q2dos/doslfn.zip" doslfn.com > "$out/DOSLFN.COM"
(cd "$out" && ls -l -- *.EXE *.DXE *.COM) | awk '{print "quake: build/quake/" $NF " " $5 " bytes"}'
