#!/usr/bin/env bash
# A ready-to-boot 86Box machine with PrBoom-plus on DOS-GL, for MGA-Glide's
# patched 86Box (the Windows kit, or make 86box on Linux).
#
#   winvm.sh [CARD]          (default g450) -> dist/doom-CARD-vm.zip
#
# The machine: Pentium II 350, 64 MB, the Matrox CARD, Sound Blaster 16,
# PS/2 mouse with CuteMouse; D: holds PRBOOMP.EXE and PRBOOM.WAD
# (build/doom, from the pinned fork) and the owner's IWADs
# (tools/doom/fixtures.py). The zip contains retail data: it is for the
# owner's machine only, never to be shared or committed.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
card=${1:-g450}
hal=${MGAHAL_DIR:-${MGA_GLIDE:-$HOME/MGA-Glide}}
b=$root/build/doom
data=${MGA_CACHE:-$HOME/.cache/mga-glide}/fixtures/games/doom/DOOM
for f in "$b/PRBOOMP.EXE" "$b/PRBOOM.WAD" "$data/DOOM.WAD" "$hal/build/ow/dos/UTEXIT.COM"; do
  [ -e "$f" ] || { echo "winvm: no $f (tools/doom/build.sh, fixtures.py, make dostools)" >&2; exit 1; }
done
stage=$root/build/doom-winvm
rm -rf "$stage"; mkdir -p "$stage" "$root/dist"
bat() {  # NAME IWAD: a batch file that starts the game on that IWAD
  printf '%s\r\n' "@ECHO OFF" "REM $1 [options]: PrBoom-plus on $2 (options go to the game, e.g. -vidmode gl)" \
    "D:" "CD \\DOOM" "PRBOOMP.EXE -iwad $2 %1 %2 %3 %4 %5 %6 %7 %8 %9" "C:" "CD \\" > "$stage/$1.BAT"
}
bat DOOM DOOM.WAD
iwads=(--d-dir "$data=/DOOM" --d-file "$stage/DOOM.BAT=/DOOM.BAT")
if [ -e "$data/DOOM2.WAD" ]; then
  bat DOOM2 DOOM2.WAD
  iwads+=(--d-file "$stage/DOOM2.BAT=/DOOM2.BAT")
fi
cat > "$stage/notes.txt" <<'NOTES'
What is on D: (D:\ is on PATH)
  DOOM.BAT      Type DOOM to play The Ultimate Doom (your DOOM.WAD) in
                PrBoom-plus, DOOM2 for Doom II. Options go to the game:
                  -vidmode gl         the OpenGL renderer, on the Matrox card
                                      through DOS-GL (the default draws in
                                      software into an 8-bit VESA mode)
                  -width 320 -height 200 (or 640x400, 640x480, 800x600 ...)
                  -nosound, -nomusic, -warp 1 1, -skill 4
  D:\DOOM       PRBOOMP.EXE (the game, SDL3 and DOS-GL in one DJGPP
                program), PRBOOM.WAD (its own data), your IWADs, and the
                PRBOOM.CFG and PRBSAVn.DSG files it writes.

Playing: PrBoom-plus's defaults: the arrow keys to move, Ctrl to fire, Space
to open, Alt to strafe, Shift to run, 1-7 for weapons, Tab for the map; the
mouse to turn and fire. Escape for the menu, F2 save, F3 load, F10 quit.
Sound effects and OPL2 music (emulated in software) come from the Sound
Blaster 16 (BLASTER in RUN.BAT); mus_opl_gain in PRBOOM.CFG sets the
music's level. Settings are kept in D:\DOOM\PRBOOM.CFG.

Known gaps: the OpenGL renderer runs in its compatibility mode (one texture
unit, the sky as a flat backdrop), without the sky dome, hardware gamma or
multisampling. 86Box runs the Matrox 3D engine much slower than the real
card: the software renderer is the faster one here, which says nothing
about a Pentium II with a G450.

This zip holds retail game data: keep it on your own machine.
NOTES
"$hal/tools/dev" python3 "$hal/tools/86box/mkwinvm.py" --name "doom-$card" --card "$card" --no-voodoo \
  --mem 64 --d-cylinders 120 --readme "$stage/notes.txt" --out "$root/dist/doom-$card-vm.zip" \
  "${iwads[@]}" --d-file "$b/PRBOOMP.EXE=/DOOM/PRBOOMP.EXE" --d-file "$b/PRBOOM.WAD=/DOOM/PRBOOM.WAD" \
  --run "ECHO PrBoom-plus: type DOOM or DOOM2 to play (README.txt has the rest)."
