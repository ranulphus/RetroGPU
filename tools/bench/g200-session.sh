#!/usr/bin/env bash
# g200-session.sh - docs/g200-warp.md's G6: the G200's WARP on a real card,
# as one prepared batch, so the card is swapped in once.
#
#   tools/bench/g200-session.sh PC [PHASE...]     (default: all phases, in order)
#
# PC is bench-g400 with the G200 swapped in (or vbench-g200 for the dry run
# in 86Box). Runs on the build host. Builds come from this tree (RetroGPU:
# the HAL, Glide and DOS-GL in gl/) and these checkouts (environment
# overrides): DOSBENCH, FIFTHWHEEL, DGK; DOSBench and Fifth Wheel are built
# against this tree (RETROGPU; no pins, no copies). Each phase prints its outcomes on lines of its
# own; the results stay under out/bench/PC/ and out/g200-session/.
#
# Phases (each answers questions in docs/g200-warp.md, "Open, for the card"):
#   warptri   the PIO load and start; pipe 7; the vertices by the CPU; the
#             third load with and without the soft reset; cycles a triangle
#   conform   DOS-GL's conformance with the WARP (DGL_WARP=1) and without
#   scenes    DOSBench's scenes, OpenGL (DGL_WARP=1, 0) and Glide (warp=2, 0)
#   fifth     Fifth Wheel's tour with the WARP and without: FW-STAT, frame times
#   glide     the acceptance games' traces through Glide, warp=2 against 0
#   agp       last (a hang needs a person): AGP 1x and 2x by sideband
# Phases before agp keep the DMA on PCI (WARPTRI pci, DGL_AGP=0, MGAGLIDE
# agp=0): the HAL otherwise turns AGP on wherever the card and bridge have it.
# SCENES_TESTS, SCENES_SECS and SCENES_APIS (gl, glide) narrow the scenes
# phase (the dry run: G1ARNA, 1).
set -uo pipefail
pc=${1:?PC}; shift
phases=${*:-warptri conform scenes fifth glide agp}
MGA=$(cd "$(dirname "$0")/../.." && pwd)
DOSGL=$MGA/gl
DOSBENCH=${DOSBENCH:-$HOME/DOSBench}
FW=${FIFTHWHEEL:-$HOME/FifthWheel}
DGK=${DGK:-$HOME/dgk}
export RETROGPU=$MGA
unset MGA_GLIDE
out=$MGA/out/g200-session/$pc
mkdir -p "$out"
say() { printf '%-9s %s\n' "$1" "$2" | tee -a "$out/summary.txt"; }
bench() {  # NAME [run.py options...]: the job's status
    local name=$1; shift
    python3 "$MGA/tools/bench/run.py" --pc "$pc" --name "g200-$name" "$@" > "$out/$name.log" 2>&1
    local dir; dir=$(ls -dt "$MGA/out/bench/$pc/g200-$name"* 2>/dev/null | head -1)
    echo "$dir" > "$out/$name.dir"
    if [ -n "$dir" ] && [ -f "$dir/result.json" ]; then
        python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['status'])" "$dir/result.json"
    else
        echo NO-RESULT
    fi
}
serial() { cat "$(cat "$out/$1.dir")/serial.log" 2>/dev/null | tr -d '\r'; }
ppmdiff() {  # A B: pixels that differ, the largest difference, and how many by more than 32 levels
    python3 - "$1" "$2" <<'PY'
import sys
def ppm(p):
    d = open(p, 'rb').read(); parts = d.split(b'\n', 3); return parts[1], parts[3]
(sa, a), (sb, b) = ppm(sys.argv[1]), ppm(sys.argv[2])
if sa != sb:
    print("sizes differ"); sys.exit()
n = mx = big = 0
for i in range(0, len(a), 3):
    d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
    if d:
        n += 1; mx = max(mx, d); big += d > 32
print("%d pixels differ, at most %d levels, %d by more than 32" % (n, mx, big))
PY
}

build() {
    (cd "$MGA" && make -s hal-djgpp runtime build/djgpp/WARPTRI.EXE dostests) >> "$out/build.log" 2>&1 || return 1
    (cd "$DOSGL" && make -s lib conform-dos) >> "$out/build.log" 2>&1 || return 1
    [ -f "$DOSGL/build/sdl/dos/lib/libSDL3.a" ] || (cd "$DOSGL" && make -s sdl) >> "$out/build.log" 2>&1 || return 1
    (cd "$DOSBENCH" && make -s RETROGPU="$MGA") >> "$out/build.log" 2>&1 || return 1
    (cd "$FW" && make -s RETROGPU="$MGA" DGK="$DGK" dos) >> "$out/build.log" 2>&1 || return 1
}

phase_warptri() {
    local st
    for args in "" "pipe=7" "pio" "starts=30 reload" "starts=30 reload noreset" "n=20000"; do
        local n=warptri-$(echo "${args:-dma}" | tr ' =' '-_')
        st=$(bench "$n" --exe "$MGA/build/djgpp/WARPTRI.EXE" --args "$args pci" --timeout 300)
        say warptri "$args: $st; $(serial "$n" | grep -ao 'HX-TEST [a-z0-9-]* [A-Z]*\|per_tri=[0-9]*\|dead\|start [0-9]* pipe' \
            | sort | uniq -c | tr '\n' ' ' | tr -s ' ')"
    done
}

phase_conform() {
    local w
    # (make conform-bench's steps: its PRE is one line, and these need two)
    (cd "$DOSGL" && make -s conform-dos && "$MGA/tools/dev" sh -c 'make -s conform-host && python3 tools/conform/run.py --refs') \
        > "$out/conform-refs.log" 2>&1
    for w in 1 0; do
        (cd "$DOSGL" && python3 tools/conform/run.py --bench "$pc" --pre "SET DGL_WARP=$w" --pre "SET DGL_AGP=0") > "$out/conform-$w.log" 2>&1
        say conform "DGL_WARP=$w: $(grep -c ' PASS ' "$out/conform-$w.log") pass, $(grep -c ' FAIL ' "$out/conform-$w.log") fail"
        cp "$DOSGL/out/conform/summary-$pc.json" "$out/conform-$w.json" 2>/dev/null
        [ -d "$DOSGL/out/conform/$pc" ] && rm -rf "$out/conform-$w" && cp -r "$DOSGL/out/conform/$pc" "$out/conform-$w"
    done
}

phase_scenes() {
    local st common="--tests ${SCENES_TESTS:-all} --data C:\\DOSBENCH\\DATA --secs ${SCENES_SECS:-5}"
    st=$(bench scenes-data --extender dos4gw $(for f in "$DOSBENCH"/build/data/*.DBS; do echo --file "$f=$(basename "$f")"; done) \
        --cmd "IF NOT EXIST C:\\DOSBENCH\\NUL MD C:\\DOSBENCH" --cmd "IF NOT EXIST C:\\DOSBENCH\\DATA\\NUL MD C:\\DOSBENCH\\DATA" \
        --cmd "COPY C:\\TEST\\*.DBS C:\\DOSBENCH\\DATA > NUL" --cmd "SERSAY HX-DONE 0" --timeout 1200)
    say scenes "data: $st"
    for w in 1 0; do
        case " ${SCENES_APIS:-gl glide} " in *" gl "*) ;; *) continue;; esac
        st=$(bench "scenes-gl-$w" --file "$DOSBENCH/build/dos/BENCHGL.EXE" --set "DGL_WARP=$w" --set DGL_AGP=0 \
            --cmd "BENCHGL.EXE $common" --timeout 3600 --idle 900)
        say scenes "OpenGL DGL_WARP=$w: $st"
        serial "scenes-gl-$w" | grep -a 'HX-STAT bench T ' | sed -n 's/.*test=\([A-Z0-9]*\).* fps=\([0-9.]*\).* tris_frame=\([0-9]*\).* ktris_s=\([0-9.]*\).*/  \1 \2 fps, \3 tris, \4 ktris\/s/p' \
            | tee -a "$out/summary.txt"
    done
    for w in 2 0; do
        case " ${SCENES_APIS:-gl glide} " in *" glide "*) ;; *) continue;; esac
        st=$(bench "scenes-glide-$w" --file "$DOSBENCH/build/dos/BENCHG.EXE" --ovl "$MGA/build/ow/GLIDE2X.OVL" \
            --set "MGAGLIDE=warp=$w agp=0" --cmd "BENCHG.EXE $common --glide=C:\\TEST\\GLIDE2X.OVL" --timeout 3600 --idle 900)
        say scenes "Glide warp=$w: $st"
        serial "scenes-glide-$w" | grep -a 'HX-STAT bench T ' | sed -n 's/.*test=\([A-Z0-9]*\).* fps=\([0-9.]*\).*/  \1 \2 fps/p' | tee -a "$out/summary.txt"
    done
}

phase_fifth() {
    local st w
    for w in 1 0; do
        st=$(bench "fifth-$w" --exe "$FW/build/dos/FWHEEL.EXE" --file "$FW/build/data/WORLD.PAK" \
            --args "-test -fixed -nosound -autopilot -frames 1800 -hash" --set "DGL_WARP=$w" --set DGL_AGP=0 --set DGL_STATS=1 \
            --timeout 1800)
        say fifth "DGL_WARP=$w: $st; $(serial "fifth-$w" | grep -ao 'FW-STAT[^\n]*\|DGL-WARP tris=[0-9]* failures=[0-9]*' | tr '\n' ' ')"
        serial "fifth-$w" | grep -a 'FW-HASH' > "$out/fifth-$w.hashes"
    done
    cmp -s "$out/fifth-1.hashes" "$out/fifth-0.hashes" && say fifth "the tour's state hashes agree both ways" \
        || say fifth "the tour's state hashes DIFFER (the simulation must not depend on the setup path)"
}

phase_glide() {
    local st w g frames
    for g in gta:300:100,200,290 sr:150:50,100,140; do
        local key=${g%%:*} rest=${g#*:}; frames=${rest#*:}
        for w in 2 0; do
            st=$(bench "glide-$key-$w" --replay "$HOME/.cache/mga-glide/traces/$key-voodoo-${rest%%:*}.bin" --frames "$frames" \
                --ovl "$MGA/build/ow/GLIDE2X.OVL" --set "MGAGLIDE=warp=$w agp=0" --timeout 1800)
            say glide "$key warp=$w: $st; $(serial "glide-$key-$w" | grep -ao 'warp_tris=[0-9]* warp_failures=[0-9]*' | head -1)"
        done
        for f in "$(cat "$out/glide-$key-0.dir")"/files/GLP_*.PPM; do
            [ -f "$f" ] || continue
            say glide "$key $(basename "$f"): $(ppmdiff "$f" "$(cat "$out/glide-$key-2.dir")/files/$(basename "$f")")"
        done
    done
}

phase_agp() {
    local st a r
    for a in "agpinfo" "agp=1 agpdraw" "agp=2 agpdraw" "agp=2 dmabw"; do
        local n=agp-$(echo "$a" | tr ' =' '-_')
        st=$(bench "$n" --exe "$MGA/build/djgpp/WARPTRI.EXE" --args "$a" --timeout 300)
        say agp "$a: $st; $(serial "$n" | grep -ao 'HX-TEST [a-z0-9-]* [A-Z]*\|agp ring=[0-9]* xfer=[0-9]*\|agp_pll=[0-9a-f]*\|per_tri=[0-9]*\|mb_s=[0-9]*' | tr '\n' ' ')"
    done
    for r in 1 2; do                    # (DGL_AGP=r: at most r x)
        st=$(bench "agp-scenes-gl-$r" --file "$DOSBENCH/build/dos/BENCHGL.EXE" --set DGL_WARP=1 --set "DGL_AGP=$r" \
            --cmd "BENCHGL.EXE --tests G --data C:\\DOSBENCH\\DATA --secs 5" --timeout 3600 --idle 900)
        say agp "OpenGL scenes by AGP ${r}x: $st; $(serial "agp-scenes-gl-$r" | grep -ao 'DGL-WARP tris=[0-9]* failures=[0-9]*\|ring=[0-9/K]*' | tr '\n' ' ')"
        serial "agp-scenes-gl-$r" | grep -a 'HX-STAT bench T ' | sed -n 's/.*test=\([A-Z0-9]*\).* fps=\([0-9.]*\).*/  \1 \2 fps/p' | tee -a "$out/summary.txt"
    done
}

: > "$out/summary.txt"
say build "RetroGPU $(git -C "$MGA" describe --always --dirty)"
build || { say build "FAILED: $out/build.log"; exit 1; }
for p in $phases; do "phase_$p"; done
say done "$out/summary.txt"
