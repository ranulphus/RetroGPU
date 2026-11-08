#!/usr/bin/env bash
# Run a Quake port on DOS-GL in Loop A (86Box, MGA-Glide's runner).
#
#   run.sh GAME [CARD] [-- GAME ARGS...]
#
#   GAME   quake | lq    Quake on the retail or the LibreQuake data: timedemo
#                        DEMO (default demo1) with -fixedtime, to its end
#          quake2        Quake 2: with DEMO (demo1 or demo2, id's own in the
#                        retail pak0.pak, or one recorded by q2record.sh), that
#                        timedemo with fixedtime 14; without, map base1
#                        for FRAMES swaps
#   CARD   g450 (default), g400 or g200
# The timedemo result ("demo frames seconds fps", or "frames seconds fps" for
# Quake 2) is printed at the end (out/NAME/files/TD.TXT).
# Environment: DEMO, FRAMES (DGL_EXIT_AFTER; default 0 = no limit for
# timedemos, 600 for the Quake 2 map), STATS (DGL_STATS, default 1), SNAP
# (DGL_SNAP: frames to capture as out/NAME/files/F*.PPM, the same pictures on
# every run), PRE (one more RUN.BAT line, e.g. "SET DGL_TEXHEAP_KB=2048"),
# SHOTS (screenshot seconds after boot), NAME (result directory
# out/NAME, default GAME-gl-CARD), MGAHAL_DIR (another MGA-Glide checkout; default $MGA_GLIDE or ~/MGA-Glide).
# Needs build/quake (tools/quake/build.sh) and the fixtures
# (tools/quake/fixtures.py). Screenshots of retail data stay local.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
game=${1:?usage: run.sh GAME [CARD] [-- ARGS...]}; shift
card=g450
if [ $# -gt 0 ] && [ "$1" != "--" ]; then card=$1; shift; fi
[ "${1:-}" = "--" ] && shift
hal=${MGAHAL_DIR:-${MGA_GLIDE:-$HOME/MGA-Glide}}
# The DOS helpers Loop A puts in C:\HX (built in MGA-Glide's checkout).
[ -f "$hal/build/ow/dos/UTEXIT.COM" ] || make -C "$root" -s dostools
q=$root/build/quake
name=${NAME:-$game-gl-$card}
demos=${MGA_CACHE:-$HOME/.cache/mga-glide}/fixtures/games/q2demos
common=(--games-file "$here/games.json" --game "$game" --card "$card" --out "$root/out/$name"
        --pre "SET DGL_STATS=${STATS:-1}" --timeout 3600 --idle 300)
[ -n "${SNAP:-}" ] && common+=(--pre "SET DGL_SNAP=$SNAP")
[ -n "${PRE:-}" ] && common+=(--pre "$PRE")
case $game in
  quake|lq)
    args=${*:-"-nosound -nocdaudio -nolan -width 640 -height 480 -fixedtime -tdresult C:\\OUT\\TD.TXT -tdquit +timedemo ${DEMO:-demo1}"}
    set -- "${common[@]}" --file "$q/QDOSDGL.EXE=D:/QUAKE/QDOSDGL.EXE" \
      --pre "SET DGL_EXIT_AFTER=${FRAMES:-0}" --shots "${SHOTS:-40,70,100,140}" \
      --cmd "D:" --cmd "CD \\QUAKE" --cmd "QDOSDGL.EXE $args" ;;
  quake2)
    extra=()
    if [ -n "${DEMO:-}" ]; then
      up=$(echo "$DEMO" | tr a-z A-Z)
      case $DEMO in
        demo1|demo2) ;;                 # id's, in the retail pak0.pak (release protocol 26: q2dos plays them)
        *) [ -f "$demos/$up.DM2" ] || { echo "run.sh: no $demos/$up.DM2 (tools/quake/q2record.sh $DEMO)" >&2; exit 1; }
           extra=(--file "$demos/$up.DM2=D:/QUAKE2/BASEQ2/DEMOS/$up.DM2") ;;
      esac
      def="+set gl_mode 3 +set s_initsound 0 +set timedemo 1 +set fixedtime 14 +set td_result C:/OUT/TD.TXT +set td_quit 1 +demomap $DEMO.dm2"
      frames=${FRAMES:-0}
    else
      def="+set gl_mode 3 +set s_initsound 0 +map base1"
      frames=${FRAMES:-600}
    fi
    args=${*:-$def}
    set -- "${common[@]}" "${extra[@]}" --file "$q/Q2DGL.EXE=D:/QUAKE2/Q2DGL.EXE" \
      --file "$q/GAMEX86.DXE=D:/QUAKE2/BASEQ2/GAMEX86.DXE" --file "$q/DOSLFN.COM=D:/QUAKE2/DOSLFN.COM" \
      --pre "SET DGL_EXIT_AFTER=$frames" --shots "${SHOTS:-35,50,65}" \
      --cmd "D:" --cmd "CD \\QUAKE2" --cmd "DOSLFN" --cmd "Q2DGL.EXE $args" ;;
  *) echo "run.sh: unknown game $game" >&2; exit 2 ;;
esac
cd "$root"
rc=0
"$hal/tools/dev" python3 "$hal/tools/loopa/run.py" --name "$name" "$@" || rc=$?
td=$(find "$root/out/$name/files" -iname td.txt 2>/dev/null | head -1)
[ -n "$td" ] && sed "s/^/timedemo: /" "$td"
exit $rc
