#!/usr/bin/env bash
# regress-remote.sh - tools/regress.sh on another machine: devserver by default
# (the owner's choice, 2026-11-12: the regressions off the shared build host,
# at most 8 emulators at once, niced so that machine's own work comes first).
#
#   tools/regress-remote.sh TIER [COMMIT]
#
# It sends COMMIT (default HEAD; uncommitted changes are not sent), keeps in
# step on the other machine what the tiers read (the toolchains, the emulator
# build and its ROMs, the fixtures, traces and game images, DOS-GL's mirrors
# and its ports' local repositories, GLOS's headers: rsync, so only changes
# travel), runs the tier there inside the dev container with the whole home
# mounted (nothing is built on that machine's own system), and brings the
# logs back to out/regress-remote/. Exit status: the tier's.
#
# Environment: REGRESS_HOST (retro@devserver), REGRESS_JOBS (lanes, 4),
# LOOPA_MAX_VMS (emulators at once, 8), MGA_NICE (10), ONLY (as regress.sh).
set -euo pipefail
tier=${1:?usage: tools/regress-remote.sh host|smoke|merge [COMMIT]}
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
commit=$(git rev-parse --verify "${2:-HEAD}^{commit}")
host=${REGRESS_HOST:-retro@devserver}
jobs=${REGRESS_JOBS:-4} vms=${LOOPA_MAX_VMS:-8} niceness=${MGA_NICE:-10}
if [ -z "${2:-}" ] && [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "regress-remote: uncommitted changes are not sent; running $(git log -1 --format='%h' "$commit")" >&2
fi

# ---- what the tiers read, in step (paths relative to both homes) -------------
show() { git show "$commit:$1"; }
box86_commit=$(show tools/setup/versions.mk | sed -n 's/^BOX86_COMMIT *:= *//p')
key=$( { echo "$box86_commit"; show tools/86box/series;
         show tools/86box/series | grep -v '^#' | while read -r p; do [ -n "$p" ] && show "tools/86box/patches/$p"; done; } \
       | sha256sum | cut -c1-16)
optdir() { show config.mk | sed -n "s|^$1 *?= *\$(HOME)/||p"; }
paths=("$(optdir WATCOM)" "$(optdir DJGPP_PREFIX)"
       .cache/mga-glide/86box/roms .cache/mga-glide/fixtures .cache/mga-glide/traces .cache/mga-glide/loopa
       .cache/mga-glide/games .cache/mga-glide/dl .cache/dosgl/mirrors
       qdos-dosgl q2dos-dosgl xash3d-fwgs-dos hlsdk-portable-dos prboom-plus-dos GLOS/include DOSGAMES)
[ -x "$HOME/.cache/mga-glide/86box/bin/86Box-$key" ] && paths+=(".cache/mga-glide/86box/bin/86Box-$key")
present=()
for p in "${paths[@]}"; do [ -e "$HOME/$p" ] && present+=("$p"); done
echo "regress-remote: syncing ${#present[@]} paths to $host"
( cd "$HOME" && rsync -aR --delete --exclude __pycache__ "${present[@]}" "$host:" )

# ---- the commit, checked out there --------------------------------------------
ssh -o BatchMode=yes "$host" '[ -d rgpu-regress.git ] || git init -q --bare rgpu-regress.git
    git --git-dir=rgpu-regress.git symbolic-ref HEAD refs/heads/regress'
git push -q --force "$host:rgpu-regress.git" "$commit:refs/heads/regress"
echo "regress-remote: $tier at $(git log -1 --format='%h %s' "$commit" | cut -c1-70) on $host" \
     "(lanes $jobs, emulators $vms, nice $niceness)"
rc=0
ssh -o BatchMode=yes "$host" "set -e
    [ -d rgpu-regress ] || git clone -q rgpu-regress.git rgpu-regress
    cd rgpu-regress
    git fetch -q origin regress
    git checkout -q --detach $commit
    git clean -fdq
    git submodule update -q --init --depth 1 || git submodule update -q --init   # (older git: no --filter)
    export MGA_EXTRA_DIR=\$HOME MGA_NICE=$niceness LOOPA_MAX_VMS=$vms
    tools/dev tools/86box/build.sh > /dev/null
    tools/dev env REGRESS_JOBS=$jobs ONLY='${ONLY:-}' tools/regress.sh $tier" || rc=$?

# ---- the logs, back here ------------------------------------------------------
latest=$(ssh -o BatchMode=yes "$host" "ls -td rgpu-regress/out/regress/$tier-* 2>/dev/null | head -1")
if [ -n "$latest" ]; then
    dest=$root/out/regress-remote/${host#*@}-$(basename "$latest")
    mkdir -p "$dest"
    rsync -a "$host:$latest/" "$dest/"
    echo "regress-remote: logs in $dest"
fi
exit $rc
