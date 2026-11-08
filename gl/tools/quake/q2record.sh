#!/usr/bin/env bash
# Record a Quake 2 benchmark demo in Loop A from a console script.
#
#   q2record.sh NAME [MAP] [CARD]      (defaults: base1, g450)
#
# Runs build/quake/Q2DGL.EXE on the retail data with td_script set to
# tools/quake/q2/NAME.cfg (the fork execs it once MAP is live); the script
# records demos/NAME.dm2 and quits. The demo is derived from the retail game,
# so it is kept as a local fixture, never committed:
#   $MGA_CACHE/fixtures/games/q2demos/NAME.DM2
# tools/quake/run.sh quake2 plays it back as a timedemo (DEMO=NAME).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
name=${1:?usage: q2record.sh NAME [MAP] [CARD]}
map=${2:-base1}
card=${3:-g450}
cfg=$here/q2/$name.cfg
[ -f "$cfg" ] || { echo "q2record: no $cfg" >&2; exit 1; }
up=$(echo "$name" | tr a-z A-Z)
hal=${MGAHAL_DIR:-${MGA_GLIDE:-$HOME/MGA-Glide}}
q=$root/build/quake
dest=${MGA_CACHE:-$HOME/.cache/mga-glide}/fixtures/games/q2demos
cd "$root"
"$hal/tools/dev" python3 "$hal/tools/loopa/run.py" --name "q2rec-$name" --out "$root/out/q2rec-$name" \
  --games-file "$here/games.json" --game quake2 --card "$card" --timeout 1800 --idle 300 \
  --file "$q/Q2DGL.EXE=D:/QUAKE2/Q2DGL.EXE" --file "$q/GAMEX86.DXE=D:/QUAKE2/BASEQ2/GAMEX86.DXE" \
  --file "$q/DOSLFN.COM=D:/QUAKE2/DOSLFN.COM" --file "$cfg=D:/QUAKE2/BASEQ2/$up.CFG" \
  --pre "SET DGL_STATS=1" --pre "SET DGL_EXIT_AFTER=0" \
  --cmd "D:" --cmd "CD \\QUAKE2" --cmd "DOSLFN" \
  --cmd "Q2DGL.EXE +set gl_mode 3 +set s_initsound 0 +set td_script $name.cfg +map $map" \
  --cmd "COPY BASEQ2\\DEMOS\\$up.DM2 C:\\OUT" || true      # judged by the demo coming back
demo=$(find "$root/out/q2rec-$name/files" -iname "$name.dm2" | head -1)
[ -n "$demo" ] || { echo "q2record: no demo came back (see out/q2rec-$name/)" >&2; exit 1; }
mkdir -p "$dest"
cp "$demo" "$dest/$up.DM2"
echo "q2record: $dest/$up.DM2 ($(stat -c %s "$demo") bytes)"
