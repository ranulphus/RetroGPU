#!/usr/bin/env bash
# A ready-to-boot 86Box machine with Half-Life on DOS-GL, for MGA-Glide's
# patched 86Box (the Windows kit, or make 86box on Linux).
#
#   winvm.sh [CARD]          (default g450) -> dist/halflife-CARD-vm.zip
#
# The machine: Pentium II 350, 128 MB, the Matrox CARD, Sound Blaster 16,
# PS/2 mouse with CuteMouse; D: holds HLDGL.EXE (build/halflife, from the
# pinned forks) and the owner's WON data (tools/halflife/fixtures.py). The
# zip contains retail data: it is for the owner's machine only, never to be
# shared or committed.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
card=${1:-g450}
hal=${MGAHAL_DIR:-${MGA_GLIDE:-$HOME/MGA-Glide}}
b=$root/build/halflife
data=${MGA_CACHE:-$HOME/.cache/mga-glide}/fixtures/games/halflife/VALVE
for f in "$b/HLDGL.EXE" "$b/EXTRAS.PK3" "$b/DOSLFN.COM" "$data/pak0.pak" "$hal/build/ow/dos/UTEXIT.COM"; do
  [ -e "$f" ] || { echo "winvm: no $f (tools/halflife/build.sh, fixtures.py, make dostools)" >&2; exit 1; }
done
stage=$root/build/halflife-winvm
rm -rf "$stage"; mkdir -p "$stage" "$root/dist"
printf '%s\r\n' "@ECHO OFF" "REM HL [options]: Half-Life on DOS-GL (options go to the game, e.g. -width 800 -height 600)" \
  "D:" "CD \\HL" "DOSLFN > NUL" "HLDGL.EXE -game valve %1 %2 %3 %4 %5 %6 %7 %8 %9" "C:" "CD \\" > "$stage/HL.BAT"
cat > "$stage/notes.txt" <<'NOTES'
What is on D: (D:\ is on PATH)
  HL.BAT        Type HL to play Half-Life (your WON copy, D:\HL\VALVE) in
                Xash3D FWGS on DOS-GL, on the Matrox card. Options go to the
                game: -width 800 -height 600 (or 320x240 ... 1280x1024), -nosound, and on
                the G400/G450 +gl_vbo 1 (lightmaps in one pass: faster in open
                areas, about even in fights).
  D:\HL         HLDGL.EXE (the engine, the game code and DOS-GL in one DJGPP
                program), DOSLFN.COM (long file names; HL.BAT loads it).

Playing: the WON defaults apply: W A S D or the arrow keys, the mouse to look
and fire, F6 quick save, F7 quick load (not F9), ` for the console, Escape
for the menu. Sound comes from the Sound Blaster 16 (BLASTER in RUN.BAT).
A video mode chosen in the menu applies at the next start.

Known gaps: VGUI text (the multiplayer scoreboard) is missing, since the WON
data has no VGUI fonts; there are no intro videos; multiplayer is local only.
86Box runs the Matrox 3D engine much slower than the real card: expect a few
frames a second here, not what a Pentium II with a G450 does.

This zip holds retail game data: keep it on your own machine.
NOTES
"$hal/tools/dev" python3 "$hal/tools/86box/mkwinvm.py" --name "halflife-$card" --card "$card" --no-voodoo \
  --mem 128 --d-cylinders 800 --readme "$stage/notes.txt" --out "$root/dist/halflife-$card-vm.zip" \
  --d-dir "$data=/HL/VALVE" --d-file "$b/HLDGL.EXE=/HL/HLDGL.EXE" --d-file "$b/EXTRAS.PK3=/HL/VALVE/EXTRAS.PK3" \
  --d-file "$b/DOSLFN.COM=/HL/DOSLFN.COM" --d-file "$stage/HL.BAT=/HL.BAT" \
  --run "ECHO Half-Life: type HL to play (README.txt has the rest)."
