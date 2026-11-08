#!/usr/bin/env bash
# Build Half-Life for DOS: Xash3D FWGS with Half-Life's client and server
# linked statically, on DOS-GL (build/lib/libGL.a).
#
#   build.sh [--work]
#
# Sources: the checkouts $XASH_DIR and $HLSDK_DIR (default ~/xash3d-fwgs-dos
# and ~/hlsdk-portable-dos), cloned from the forks in tools/halflife/deps.mk
# when missing. The pinned commits are exported (git archive) into
# build/halflife/src/xash3d, hlsdk-portable inside it, with each submodule the
# build uses at the commit its gitlink pins (fetched through bare mirrors in
# ~/.cache/dosgl/mirrors) and the forks' patch series for those submodules
# (scripts/djgpp/patches/<path>/*.patch) applied in order. --work builds the
# checkouts as they are instead: tracked and new files, submodules with their
# working-tree changes (which carry the patches).
# Output in build/halflife:
#   HLDGL.EXE    the game (stripped)          HLDGL.SYM   the same, with symbols
#   EXTRAS.PK3   the engine's extra data      DOSLFN.COM  long file names (q2dos's doslfn.zip)
#   STAGED.TXT   the commits and patches the build used
set -euo pipefail
unset MAKEFLAGS MFLAGS MAKELEVEL
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
D=${DJGPP_PREFIX:-$HOME/.local/opt/djgpp-gcc1220}
work=0
for a in "$@"; do
  case $a in
    --work) work=1 ;;
    *) echo "usage: build.sh [--work]" >&2; exit 2 ;;
  esac
done
pin() { sed -n "s/^$1 *:= *//p" "$here/deps.mk"; }
out=$root/build/halflife
src=$out/src/xash3d
mirrors=${DOSGL_MIRRORS:-$HOME/.cache/dosgl/mirrors}
xash_dir=${XASH_DIR:-$HOME/xash3d-fwgs-dos}
hlsdk_dir=${HLSDK_DIR:-$HOME/hlsdk-portable-dos}
# Submodules the DOS build uses (engine: path; mainui's own miniutl; hlsdk's FreeVGUI).
ENGINE_SUBS="3rdparty/mainui 3rdparty/extras/xash-extras 3rdparty/library_suffix 3rdparty/bzip2/bzip2
  3rdparty/opus/opus 3rdparty/libogg/libogg 3rdparty/vorbis/vorbis-src 3rdparty/opusfile/opusfile
  3rdparty/MultiEmulator 3rdparty/libbacktrace/libbacktrace 3rdparty/mbedtls/mbedtls"
mkdir -p "$out" "$mirrors"
staged=$out/STAGED.TXT
: > "$staged"

# A submodule of REPO_DIR (a checkout, whose .gitmodules and gitlinks at
# COMMIT say what to fetch) into DEST, at its pinned commit.
export_sub() {
  local repo=$1 commit=$2 path=$3 dest=$4 url sub name
  url=$(git -C "$repo" show "$commit:.gitmodules" | git config -f - --get-regexp '^submodule\..*\.path$' |
        awk -v p="$path" '$2 == p {sub(/\.path$/, "", $1); print $1}' | head -1)
  url=$(git -C "$repo" show "$commit:.gitmodules" | git config -f - --get "$url.url")
  sub=$(git -C "$repo" ls-tree "$commit" "$path" | awk '{print $3}')
  [ -n "$sub" ] || { echo "halflife: no gitlink for $path" >&2; exit 1; }
  name=$(basename "${url%.git}")-$(printf %s "$url" | sha256sum | cut -c1-8)
  if [ ! -d "$mirrors/$name.git" ]; then
    git clone -q --bare "$url" "$mirrors/$name.git"
  fi
  git -C "$mirrors/$name.git" cat-file -e "$sub^{commit}" 2>/dev/null ||
    git -C "$mirrors/$name.git" fetch -q "$url" '+refs/heads/*:refs/heads/*' "$sub" 2>/dev/null ||
    git -C "$mirrors/$name.git" fetch -q "$url" "$sub"
  rm -rf "$dest"; mkdir -p "$dest"
  git -C "$mirrors/$name.git" archive "$sub" | tar -x -C "$dest"
  echo "submodule $path $sub ($url)" >> "$staged"
  printf '%s\n' "$mirrors/$name.git"
}

# The patch series of the fork at FORK_DIR for PATH, applied to DEST.
apply_patches() {
  local fork=$1 path=$2 dest=$3 p
  for p in "$fork/scripts/djgpp/patches/$path"/*.patch; do
    [ -f "$p" ] || continue
    patch -d "$dest" -p1 -s --no-backup-if-mismatch < "$p"
    echo "patch $path/$(basename "$p") $(sha256sum < "$p" | cut -c1-12)" >> "$staged"
  done
}

rm -rf "$src"; mkdir -p "$src"
if [ $work = 1 ]; then
  # Working trees as they are, submodules included (their changes are the patches).
  (cd "$xash_dir" && git ls-files -z -co --exclude-standard --recurse-submodules 2>/dev/null ||
                     git ls-files -z -co --exclude-standard) | (cd "$xash_dir" && tar --null -T - -cf -) | tar -x -C "$src"
  for s in $ENGINE_SUBS 3rdparty/mainui/miniutl; do
    [ -d "$xash_dir/$s" ] && (cd "$xash_dir/$s" && git ls-files -z -co --exclude-standard | tar --null -T - -cf -) |
      (mkdir -p "$src/$s" && tar -x -C "$src/$s")
  done
  mkdir -p "$src/hlsdk-portable"
  (cd "$hlsdk_dir" && git ls-files -z -co --exclude-standard | tar --null -T - -cf -) | tar -x -C "$src/hlsdk-portable"
  (cd "$hlsdk_dir/freevgui" && git ls-files -z -co --exclude-standard | tar --null -T - -cf -) |
    (mkdir -p "$src/hlsdk-portable/freevgui" && tar -x -C "$src/hlsdk-portable/freevgui")
  echo "work engine $(git -C "$xash_dir" describe --always --dirty) hlsdk $(git -C "$hlsdk_dir" describe --always --dirty)" >> "$staged"
  export XASH_GIT_VERSION=$(git -C "$xash_dir" describe --always --dirty --abbrev=8)
  export XASH_GIT_BRANCH=$(git -C "$xash_dir" rev-parse --abbrev-ref HEAD)
  export XASH_GIT_COMMIT_DATE=$(git -C "$xash_dir" log -1 --format=%ci HEAD)
  echo "halflife: working trees of $xash_dir and $hlsdk_dir"
else
  xc=$(pin XASH_COMMIT) hc=$(pin HLSDK_COMMIT)
  [ -d "$xash_dir/.git" ] || git clone -q "$(pin XASH_URL)" "$xash_dir"
  [ -d "$hlsdk_dir/.git" ] || git clone -q "$(pin HLSDK_URL)" "$hlsdk_dir"
  git -C "$xash_dir" cat-file -e "$xc^{commit}" 2>/dev/null || git -C "$xash_dir" fetch -q "$(pin XASH_URL)" "$xc"
  git -C "$hlsdk_dir" cat-file -e "$hc^{commit}" 2>/dev/null || git -C "$hlsdk_dir" fetch -q "$(pin HLSDK_URL)" "$hc"
  git -C "$xash_dir" archive "$xc" | tar -x -C "$src"
  echo "engine $xc" >> "$staged"
  for s in $ENGINE_SUBS; do
    m=$(export_sub "$xash_dir" "$xc" "$s" "$src/$s")
    if [ "$s" = 3rdparty/mainui ]; then  # mainui's own submodule, pinned by mainui's commit
      mc=$(git -C "$xash_dir" ls-tree "$xc" "$s" | awk '{print $3}')
      git clone -q "$m" "$out/src/mainui.git" 2>/dev/null || git -C "$out/src/mainui.git" fetch -q
      export_sub "$out/src/mainui.git" "$mc" miniutl "$src/$s/miniutl" > /dev/null
      apply_patches "$xash_dir" "$s/miniutl" "$src/$s/miniutl"
    fi
    apply_patches "$xash_dir" "$s" "$src/$s"
  done
  mkdir -p "$src/hlsdk-portable"
  git -C "$hlsdk_dir" archive "$hc" | tar -x -C "$src/hlsdk-portable"
  echo "hlsdk $hc" >> "$staged"
  export_sub "$hlsdk_dir" "$hc" freevgui "$src/hlsdk-portable/freevgui" > /dev/null
  apply_patches "$hlsdk_dir" freevgui "$src/hlsdk-portable/freevgui"
  echo "halflife: engine ${xc:0:10}, hlsdk ${hc:0:10}"
  export XASH_GIT_VERSION=${xc:0:8} XASH_GIT_BRANCH=dos
  export XASH_GIT_COMMIT_DATE=$(git -C "$xash_dir" log -1 --format=%ci "$xc")
fi

# Build: the DJGPP target, every library static, OpenGL from this DOS-GL.
# (The staged tree has no .git: XASH_GIT_* above give the engine its version.)
[ -f "$root/build/lib/libGL.a" ] || { echo "halflife: no build/lib/libGL.a (make lib)" >&2; exit 1; }
export DJGPP_PREFIX=$D LD_LIBRARY_PATH=$D/hostlib
export LD=$D/bin/i586-pc-msdosdjgpp-ld OBJCOPY=$D/bin/i586-pc-msdosdjgpp-objcopy NM=$D/bin/i586-pc-msdosdjgpp-nm
log=$out/build.log
: > "$log"
run() { "$@" >> "$log" 2>&1 || { grep -E "error|Error" "$log" | tail -20; echo "halflife: failed: $*" >&2; exit 1; }; }
cd "$src"
run ./waf configure -o "$out/waf" --djgpp -T release --disable-mbedtls --hlsdk=hlsdk-portable --dosgl="$root" \
  --static-linking=filesystem_stdio,ref_gl,menu,client,server
run ./waf build -j"$(nproc)"
cp "$out/waf/engine/xash.exe" "$out/HLDGL.SYM"
"$D/bin/i586-pc-msdosdjgpp-strip" -o "$out/HLDGL.EXE" "$out/HLDGL.SYM"
cp "$out/waf/3rdparty/extras/extras.pk3" "$out/EXTRAS.PK3"
# DOSLFN (freeware, Henrik Haftmann), as q2dos ships it.
q2=${Q2DOS_DIR:-$HOME/q2dos-dosgl}
q2c=$(sed -n 's/^Q2DOS_COMMIT *:= *//p' "$root/tools/quake/deps.mk")
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
git -C "$q2" show "$q2c:doslfn.zip" > "$tmp/doslfn.zip"
unzip -p "$tmp/doslfn.zip" doslfn.com > "$out/DOSLFN.COM"
(cd "$out" && ls -l HLDGL.EXE EXTRAS.PK3 DOSLFN.COM) | awk '{print "halflife: build/halflife/" $NF " " $5 " bytes"}'
