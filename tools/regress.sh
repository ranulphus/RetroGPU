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
# Each step's log is out/regress/TIER-STAMP/STEP.log; the summary, with each
# step's time, is printed and kept there as summary.txt. Exit status 0 when
# every step passes. Steps run one at a time (Loop A's idle timeouts are
# wall-clock, and a loaded host reads as a hang). A step whose fixtures are
# missing (the replays' traces in ~/.cache/mga-glide) is SKIP, not PASS.
set -u
tier=${1:-}
case "$tier" in host|smoke|merge) ;; *) sed -n '2,19p' "$0" >&2; exit 2 ;; esac
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root" || exit 1
unset BOX86_DIR MGAHAL_DIR MGA_GLIDE
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}
out=$root/out/regress/$tier-$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"
summary=$out/summary.txt
fails=0
echo "regress $tier: $(git log -1 --format='%h %s' | cut -c1-70)$(git diff --quiet HEAD -- || echo ' (dirty)')" | tee "$summary"

# step NAME COMMAND...: run, time, log and record PASS/FAIL.
step() {
    local name=$1; shift
    local s=$SECONDS rc
    "$@" > "$out/$name.log" 2>&1; rc=$?
    local t=$(( SECONDS - s )) res=PASS
    if [ $rc -ne 0 ]; then res="FAIL rc=$rc"; fails=$((fails + 1)); fi
    printf '  %-28s %-10s %5ds\n' "$name" "$res" "$t" | tee -a "$summary"
    [ $rc -eq 0 ] || tail -3 "$out/$name.log" | sed 's/^/      /' | tee -a "$summary"
}
skip() { printf '  %-28s %-10s %s\n' "$1" SKIP "$2" | tee -a "$summary"; }

# Loop A's status file for a run.py --name: PASS wanted.
loopa_pass() {
    local name=$1; shift
    tools/dev python3 tools/loopa/run.py --name "$name" "$@" || true
    [ "$(cat "out/$name/status" 2>/dev/null)" = PASS ]
}

# ---- host: builds and host tests of every part -----------------------------
step build-platform-glide tools/dev make -s runtime runtime-prof gltrace dostests hal-ow hal-djgpp hal-host \
                               smoke-djgpp dostests-djgpp hreplay rig v86test build/djgpp/WARPTRI.EXE build/djgpp/CLOCKMON.EXE
step glide-checks          tools/dev make -s check-header check-exports check-clib
step no-3dfx               python3 tools/ci/no_3dfx.py
step loopa-cfgcheck        make -s loopa-cfgcheck
step tests-host            tools/dev make -s tests-host
step gl-build              make -s -C gl lib examples conform-dos
step gl-tests-host         make -s -C gl tests-host
step gl-conform-host       tools/dev make -s -C gl conform-host

# ---- smoke: a short Loop A run of each part ---------------------------------
if [ "$tier" = smoke ]; then
    step loopa-hello       loopa_pass smoke-hello --exe build/ow/dos/HELLO.EXE --idle 25 --boot-grace 20
    step glide-conform-g400 env MGA_CARD=g400 tools/dev python3 glide/tools/conform/run.py check t01 t13 t27
    step gl-conform-g400   make -s -C gl conform CARD=g400 TESTS="t01 t16 t32"
fi

# ---- merge: everything ------------------------------------------------------
if [ "$tier" = merge ]; then
    step loopa-selftest     tools/dev make -s loopa-selftest
    step loopa-selftest-ext tools/dev make -s loopa-selftest-ext
    step loopa-twodtest     tools/dev make -s loopa-twodtest
    step hreplay-check      tools/dev make -s hreplay-check
    step glide-conform-ci   tools/dev make -s conform-ci
    for card in g200 g400 g450; do
        if [ -f "$cache/traces/gta-voodoo-300.bin" ] && [ -f "$cache/fixtures/ovl/gta.ovl" ]; then
            step replay-gta-$card env MGA_CARD=$card timeout 3000 tools/dev python3 tools/games/replay.py \
                "$cache/traces/gta-voodoo-300.bin" --ovl-ref "$cache/fixtures/ovl/gta.ovl" \
                --frames 60,150,240,295 --name regress-gta-$card
        else skip replay-gta-$card "no GTA trace or OVL in $cache"; fi
        if [ -f "$cache/traces/sr-389.bin" ] && [ -f "$cache/fixtures/ovl/sr.ovl" ]; then
            step replay-sr-$card env MGA_CARD=$card timeout 3000 tools/dev python3 tools/games/replay.py \
                "$cache/traces/sr-389.bin" --ovl-ref "$cache/fixtures/ovl/sr.ovl" \
                --frames 60,150,250,300,340,380 --name regress-sr-$card
        else skip replay-sr-$card "no Screamer Rally trace or OVL in $cache"; fi
    done
    step gl-build-ports     make -s -C gl rig sdl sdl-examples sdl-host quake halflife doom classicube
    for card in g450 g400 g200; do
        step gl-conform-$card make -s -C gl conform CARD=$card
    done
    step gl-g4exp           python3 gl/tools/silicon/g4exp.py loopa --card g450,g400,g200
    step gl-sdl-g400        make -s -C gl loopa-sdl CARD=g400
    step gl-classicube-g400 make -s -C gl loopa-classicube CARD=g400
    step gl-quake-g400      make -s -C gl loopa-quake GAME=quake CARD=g400
    step gl-quake2-g400     make -s -C gl loopa-quake GAME=quake2 CARD=g400
fi

echo "regress $tier: $([ $fails -eq 0 ] && echo PASS || echo "$fails FAILED"), $((SECONDS / 60)) min (logs: $out)" | tee -a "$summary"
[ $fails -eq 0 ]
