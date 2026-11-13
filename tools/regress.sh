#!/usr/bin/env bash
# regress.sh - the regression of every part of RetroGPU: the platform and
# HAL, the Glide runtime (glide/) and DOS-GL (gl/), in three tiers (the
# owner's test tiers, 2026-11-06: the host on every change, a short Loop A
# smoke, everything before a merge; the P4 bench is the silicon tier).
#
#   tools/regress.sh host     builds and host tests of every part (minutes)
#   tools/regress.sh smoke    host, then a Loop A boot and a few conformance
#                             tests of each API on the G400 (about 10 minutes)
#   tools/regress.sh merge    host, then every Loop A suite on every card, the
#                             Glide game replays and the DOS-GL game ports:
#                             before a merge to main; run it alone on the host
#
# ONLY="PATTERN ..." runs only the steps whose names match (shell patterns:
# ONLY="replay-*" after a replay failed), with the tier's other steps skipped.
#
# Each step's log is out/regress/TIER-STAMP/STEP.log; the summary, with each
# step's time, is printed and kept there as summary.txt. Exit status 0 when
# every step passes. A step whose fixtures are missing (the replays' traces
# in ~/.cache/mga-glide) is SKIP, not PASS.
#
# REGRESS_JOBS=N runs the merge tier's Loop A steps N at a time (default 1:
# one after another), after its builds; LOOPA_MAX_VMS caps the emulators
# they start between them (tools/loopa/run.py), as Loop A's idle timeouts
# are wall-clock and an overloaded host reads as a hang. On a machine of
# its own: tools/regress-remote.sh.
set -u
tier=${1:-}
case "$tier" in host|smoke|merge) ;; *) sed -n '2,22p' "$0" >&2; exit 2 ;; esac
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root" || exit 1
unset BOX86_DIR MGAHAL_DIR MGA_GLIDE
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}
out=$root/out/regress/$tier-$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"
summary=$out/summary.txt
jobs_max=${REGRESS_JOBS:-1}
mkdir -p "$out/.res"
echo "regress $tier: $(git log -1 --format='%h %s' | cut -c1-70)$(git diff --quiet HEAD -- || echo ' (dirty)')" | tee "$summary"

# step NAME COMMAND...: run, time, log and record PASS/FAIL (unless ONLY leaves it out).
step() {
    local name=$1; shift
    if [ -n "${ONLY:-}" ]; then
        local p hit=
        for p in $ONLY; do case $name in $p) hit=1;; esac; done
        [ -n "$hit" ] || return 0
    fi
    local s=$SECONDS rc
    "$@" > "$out/$name.log" 2>&1; rc=$?
    local t=$(( SECONDS - s )) res=PASS
    [ $rc -eq 0 ] || res="FAIL rc=$rc"
    echo "$res" > "$out/.res/$name"
    { printf '  %-28s %-10s %5ds\n' "$name" "$res" "$t"
      [ $rc -eq 0 ] || tail -3 "$out/$name.log" | sed 's/^/      /'; } | tee -a "$summary"
}
# pstep NAME COMMAND...: a step in a lane of its own, REGRESS_JOBS at a time.
pstep() {
    if [ "$jobs_max" -le 1 ]; then step "$@"; return; fi
    while [ "$(jobs -rp | wc -l)" -ge "$jobs_max" ]; do wait -n; done
    step "$@" &
}
skip() { printf '  %-28s %-10s %s\n' "$1" SKIP "$2" | tee -a "$summary"; }
pskip() { skip "$@"; }

# replays NAME TRACE OVL FRAMES: one game's trace on the G200, G400 and G450
# at once (each card's VM and its Voodoo reference run side by side; the
# cards do not slow each other on this host), each card's result in the log.
replays() {
    local name=$1 trace=$2 ovl=$3 frames=$4 card pids=() rc=0
    for card in g200 g400 g450; do
        MGA_CARD=$card timeout 3000 tools/dev python3 tools/games/replay.py "$trace" --ovl-ref "$ovl" \
            --frames "$frames" --name "regress-$name-$card" > "$out/replay-$name-$card.log" 2>&1 &
        pids+=($!)
    done
    for card in g200 g400 g450; do
        wait "${pids[0]}" || rc=1
        pids=("${pids[@]:1}")
        echo "$card: $(tail -1 "$out/replay-$name-$card.log")"
    done
    return $rc
}

# Loop A's status file for a run.py --name: PASS wanted.
loopa_pass() {
    local name=$1; shift
    tools/dev python3 tools/loopa/run.py --name "$name" "$@" || true
    [ "$(cat "out/$name/status" 2>/dev/null)" = PASS ]
}

# ---- host: builds and host tests of every part -----------------------------
step build-platform-glide tools/dev make -s runtime runtime-prof gltrace dostests hal-ow hal-djgpp hal-host \
                               smoke-djgpp dostests-djgpp hreplay rig v86test build/djgpp/WARPTRI.EXE build/djgpp/CLOCKMON.EXE \
                               build/djgpp/VPROBE.EXE build/djgpp/VTWOD.EXE build/djgpp/VTRI.EXE
step glide-checks          tools/dev make -s check-header check-exports check-clib
step no-3dfx               python3 tools/ci/no_3dfx.py
step loopa-cfgcheck        make -s loopa-cfgcheck
step tests-host            tools/dev make -s tests-host
step gl-build              make -s -C gl lib examples conform-dos
step gl-tests-host         make -s -C gl tests-host
step gl-layers             sh gl/tools/layers.sh
step gl-conform-host       tools/dev make -s -C gl conform-host

# ---- smoke: a short Loop A run of each part ---------------------------------
if [ "$tier" = smoke ]; then
    step loopa-hello       loopa_pass smoke-hello --exe build/ow/dos/HELLO.EXE --idle 25 --boot-grace 20
    step glide-conform-g400 env MGA_CARD=g400 tools/dev python3 glide/tools/conform/run.py check t01 t13 t27
    step gl-conform-g400   make -s -C gl conform CARD=g400 TESTS="t01 t16 t32"
    step virge-vtwod       loopa_pass smoke-vtwod --machine 486dx4 --card virge --exe build/djgpp/VTWOD.EXE --idle 120 --timeout 900
    step virge-vtri        loopa_pass smoke-vtri --machine bf6 --card virge --exe build/djgpp/VTRI.EXE --idle 180 --timeout 900
fi

# ---- merge: everything ------------------------------------------------------
# The DOS-GL ports' builds first; then every Loop A step, in REGRESS_JOBS lanes.
if [ "$tier" = merge ]; then
    step gl-build-ports     make -s -C gl rig sdl sdl-examples sdl-host quake halflife doom classicube
    pstep loopa-selftest     tools/dev make -s loopa-selftest
    pstep loopa-selftest-ext tools/dev make -s loopa-selftest-ext
    pstep loopa-twodtest     tools/dev make -s loopa-twodtest
    # The S3 ViRGE/DX's layer (docs/virge.md): its probe, and its 2D, cursor and display start on both boards.
    pstep virge-vprobe       loopa_pass regress-vprobe --machine 486dx4 --card virge --exe build/djgpp/VPROBE.EXE --idle 90 --timeout 400
    pstep virge-vtwod-486    loopa_pass regress-vtwod-486 --machine 486dx4 --card virge --exe build/djgpp/VTWOD.EXE --idle 120 --timeout 900
    pstep virge-vtwod-p2     loopa_pass regress-vtwod-p2 --machine bf6 --card virge --exe build/djgpp/VTWOD.EXE --idle 120 --timeout 900
    pstep virge-vtri-486     loopa_pass regress-vtri-486 --machine 486dx4 --card virge --exe build/djgpp/VTRI.EXE --idle 400 --timeout 1500
    pstep virge-vtri-p2      loopa_pass regress-vtri-p2 --machine bf6 --card virge --exe build/djgpp/VTRI.EXE --idle 180 --timeout 900
    pstep hreplay-check      tools/dev make -s hreplay-check
    pstep glide-conform-ci   tools/dev make -s conform-ci
    # The acceptance games' traces through Glide, three cards at once. Screamer
    # Rally's 389 frames stop at 240 (the owner's choice, 2026-11-09): loading
    # screens to frame 95, the race from 96; every call pattern of the trace has
    # appeared by frame 99, and its first textures written over older ones (the
    # runtime must see the change) are at frames 197-234. Frames 241-389 repeat
    # those with more track at about 3 s a frame in 86Box's Matrox model.
    if [ -f "$cache/traces/gta-voodoo-300.bin" ] && [ -f "$cache/fixtures/ovl/gta.ovl" ]; then
        pstep replay-gta replays gta "$cache/traces/gta-voodoo-300.bin" "$cache/fixtures/ovl/gta.ovl" 60,150,240,295
    else pskip replay-gta "no GTA trace or OVL in $cache"; fi
    if [ -f "$cache/traces/sr-389.bin" ] && [ -f "$cache/fixtures/ovl/sr.ovl" ]; then
        pstep replay-sr replays sr "$cache/traces/sr-389.bin" "$cache/fixtures/ovl/sr.ovl" 60,150,200,240
    else pskip replay-sr "no Screamer Rally trace or OVL in $cache"; fi
    for card in g450 g400 g200; do
        pstep gl-conform-$card make -s -C gl conform CARD=$card
    done
    pstep gl-g4exp           python3 gl/tools/silicon/g4exp.py loopa --card g450,g400,g200
    pstep gl-sdl-g400        make -s -C gl loopa-sdl CARD=g400
    pstep gl-classicube-g400 make -s -C gl loopa-classicube CARD=g400
    pstep gl-quake-g400      make -s -C gl loopa-quake GAME=quake CARD=g400
    pstep gl-quake2-g400     make -s -C gl loopa-quake GAME=quake2 CARD=g400
fi
wait

fails=$(grep -l "^FAIL" "$out"/.res/* 2>/dev/null | wc -l)
echo "regress $tier: $([ $fails -eq 0 ] && echo PASS || echo "$fails FAILED"), $((SECONDS / 60)) min (logs: $out)" | tee -a "$summary"
[ $fails -eq 0 ]
