#!/usr/bin/env bash
# wt-add.sh - a worktree of this repository beside it, ready to build every
# part: ~/wt-rgpu-NAME on branch NAME (a new branch from main, or BRANCH if
# it exists), with gl/'s submodules (SDL 3, ClassiCube) borrowing the main
# checkout's objects instead of a fresh clone each (DOS-GL's 14 worktrees
# once held 915 MB of submodule copies).
#
#   tools/wt-add.sh NAME [BRANCH]
#
# Remove one with: git worktree remove ~/wt-rgpu-NAME
set -euo pipefail
name=${1:?usage: tools/wt-add.sh NAME [BRANCH]}
branch=${2:-$name}
main=$(git -C "$(dirname "$0")/.." rev-parse --path-format=absolute --git-common-dir)
top=$(dirname "$main")
wt=${WT_DIR:-$HOME}/wt-rgpu-$name
if git -C "$top" show-ref --verify --quiet "refs/heads/$branch"; then
    git -C "$top" worktree add "$wt" "$branch"
else
    git -C "$top" worktree add -b "$branch" "$wt" main
fi
for sub in gl/third_party/sdl gl/third_party/classicube; do
    ref=$main/modules/$sub
    if [ -d "$ref" ]; then
        git -C "$wt" submodule update --init --filter=blob:none --reference "$ref" -- "$sub"
    else
        git -C "$wt" submodule update --init --filter=blob:none -- "$sub"
    fi
done
echo "wt-add: $wt on $branch"
