#!/usr/bin/env bash
# SDL3 on DOS-GL in Loop A on one card (default g450): each check's outcome on
# one line, exit status 0 when all pass (docs/sdl.md, "Tests").
#   tools/sdl/loopa.sh [CARD] [CHECK...]     checks: info keys beep busy badirq gl modes crash exit joy
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
card=${1:-g450}; shift || true
checks=${*:-info keys beep busy badirq gl modes crash exit joy}
harness=${MGAHAL_DIR:-${MGA_GLIDE:-$HOME/MGA-Glide}}
out=$root/out/sdl-$card
mkdir -p "$out"
blaster='SET BLASTER=A220 I5 D1 H5 T6'
fail=0
# run NAME [run.py options...]: the run's status (PASS, FAIL, TIMEOUT, ...)
run() { local name=$1; shift
  "$harness/tools/dev" python3 "$harness/tools/loopa/run.py" --name "$name" --card "$card" \
    --out "$out/$name" --idle 90 --timeout 600 "$@" > /dev/null 2>&1
  cat "$out/$name/status" 2>/dev/null || echo NO-STATUS; }
log() { cat "$out/$1/serial.log" 2>/dev/null; }
say() { printf '  %-7s %s\n' "$1" "$2"; case $2 in PASS*) ;; *) fail=1;; esac; }
bad() { say "$1" "FAIL (run $2) $3"; }
# The machine after a program: interrupts as before, Sound Blaster silent, keyboard the BIOS's.
after=(--cmd "SBCHK" --cmd "VECCHK check" --cmd "KEYWAIT 20" --cmd "SERSAY HX-DONE 0")
after_ok() { log "$1" | grep -q "HX-SBCHK .* stopped" && log "$1" | grep -q "HX-VECCHK ok" &&
             log "$1" | grep -q "HX-KEY scan=39"; }
after_why() { log "$1" | grep -h "HX-SBCHK\|HX-VECCHK changed\|HX-KEY " | tr '\n' ' '; }

for c in $checks; do case $c in
info)
  st=$(run info --exe "$root/build/exe/SDLINFO.EXE" --sound sb16 --pre "$blaster")
  if [ "$st" = PASS ]; then say info "PASS ($(log info | grep -o 'drivers.*' | head -1))"
  else bad info "$st" "$(log info | grep 'FAIL' | head -2 | tr '\n' ' ')"; fi;;
keys)
  st=$(run keys --exe "$root/build/exe/SDLKEYS.EXE" --cmd "SERSAY HX-START keys" --cmd "VECCHK save" \
         --cmd "C:\\TEST\\SDLKEYS.EXE --noexit" --cmd "VECCHK check" --cmd "KEYWAIT 20" --cmd "SERSAY HX-DONE 0" \
         --keys '@HX-TEST ready,1:0x1c,2:0x1e,3:0x01,@HX-KEYWAIT ready,1:0x39')
  if log keys | grep -q "HX-TEST escape PASS" && log keys | grep -q "HX-VECCHK ok" && log keys | grep -q "HX-KEY scan=39"
  then say keys "PASS (keys through SDL, then through the BIOS; vectors unchanged)"
  else bad keys "$st" "$(log keys | grep -h 'HX-TEST .* FAIL\|HX-VECCHK changed\|HX-KEY ' | tr '\n' ' ')"; fi;;
beep)
  st=$(run beep --exe "$root/build/exe/SDLBEEP.EXE" --sound sb16 --pre "$blaster" --wav)
  w=$(python3 "$harness/tools/loopa/wavcheck.py" "$out/beep/audio.wav" --tone 440 2>&1 | tail -1)
  if [ "$st" = PASS ] && [[ $w == *found* ]]; then say beep "PASS (440 Hz recorded)"; else bad beep "$st" "$w"; fi;;
busy)
  # The main thread yields once every 37 ms (a game's frame): the audio thread
  # must fill the ring in its turns. The driver counts the chunks the card had
  # to play as silence (SDL patch 0005); a WAV gap cannot tell, since 86Box
  # does not keep pace with the recorder.
  st=$(run busy --exe "$root/build/exe/SDLBEEP.EXE" --args="--busy 37" --sound sb16 --pre "$blaster" --wav)
  w=$(python3 "$harness/tools/loopa/wavcheck.py" "$out/busy/audio.wav" --tone 440 2>&1 | tail -1)
  u=$(log busy | grep -o 'HX-TEST underruns [A-Z]* .*' | cut -d' ' -f4-)
  if [ "$st" = PASS ] && [[ $w == *found* ]]; then say busy "PASS ($u; 440 Hz recorded)"
  else bad busy "$st" "$u; $w"; fi;;
badirq)
  # BLASTER names the wrong IRQ (the SB16 is on 5): the card never asks for
  # audio. SDL must give the device up (patch 0007) and the program end,
  # not hang closing it.
  st=$(run badirq --exe "$root/build/exe/SDLBEEP.EXE" --sound sb16 --pre "SET BLASTER=A220 I7 D1 H5 T6")
  g=$(log badirq | grep -o 'Giving it up' | head -1)
  if [ "$st" = PASS ] && [ -n "$g" ]; then say badirq "PASS (the device given up; the program ended)"
  else bad badirq "$st" "$(log badirq | grep -h 'HX-TEST .* FAIL\|SoundBlaster' | tr '\n' ' ')"; fi;;
gl)
  # SDLGL draws TEXCUBE's scene through SDL (audio playing): the same last frame.
  run texcube --exe "$root/build/exe/TEXCUBE.EXE" --args="--frames 150" > /dev/null
  st=$(run gl --exe "$root/build/exe/SDLGL.EXE" --args="--frames 150" --sound sb16 --pre "$blaster" \
         --pre "SET DGL_STATS=1")
  a=$(log texcube | grep -o 'crc=[0-9a-f]*'); b=$(log gl | grep -o 'crc=[0-9a-f]*')
  hooks=$(log gl | grep -o 'hooks=[0-9]*' | tail -1)
  if [ "$st" = PASS ] && [ -n "$a" ] && [ "$a" = "$b" ] && [ "${hooks#hooks=}" -gt 0 ] 2>/dev/null
  then say gl "PASS (frame = TEXCUBE's, $a; wait hook ran: $hooks)"
  else bad gl "$st" "(TEXCUBE $a, SDLGL $b, $hooks)"; fi;;
modes)
  st=$(run modes --exe "$root/build/exe/SDLGL.EXE" --args=--modes --pre "SET SDL_AUDIO_DRIVER=dummy")
  m=$(log modes | grep -o 'HX-TEST modes [A-Z]* .*')
  if [ "$st" = PASS ] && [[ $m == *PASS* ]]; then say modes "PASS ($(echo "$m" | cut -d' ' -f4-))"
  else bad modes "$st" "$m"; fi;;
crash)
  # A #UD with SDL's keyboard hooked, its Sound Blaster playing and DOS-GL on screen.
  st=$(run crash --exe "$root/build/exe/SDLCRASH.EXE" --sound sb16 --cmd "$blaster" --cmd "SERSAY HX-START crash" \
         --cmd "VECCHK save" --cmd "C:\\TEST\\SDLCRASH.EXE --noexit" "${after[@]}" --keys '@HX-KEYWAIT ready,1:0x39')
  if log crash | grep -q "DGL-FAULT" && log crash | grep -q "HX-VMODE bios=03" && after_ok crash
  then say crash "PASS (text mode, SB stopped, vectors and keyboard back)"
  else bad crash "$st" "$(after_why crash)"; fi;;
exit)
  # DGL_EXIT_AFTER: DOS-GL calls exit() inside a swap, with SDL still running.
  st=$(run exit --exe "$root/build/exe/SDLGL.EXE" --sound sb16 --cmd "$blaster" --cmd "SET DGL_EXIT_AFTER=30" \
         --cmd "SERSAY HX-START exit" --cmd "VECCHK save" --cmd "C:\\TEST\\SDLGL.EXE --frames 300 --noexit" \
         --cmd "SET DGL_EXIT_AFTER=" "${after[@]}" --keys '@HX-KEYWAIT ready,1:0x39')
  if log exit | grep -q "DGL-EXIT frames=30" && after_ok exit
  then say exit "PASS (exit in a swap: SB stopped, vectors and keyboard back)"
  else bad exit "$st" "$(after_why exit)"; fi;;
joy)
  keys='@HX-TEST ready'; t=2
  for a in 0 1 2 3; do keys="$keys,$t:joy:axis:$a:-32767,$((t+1)):joy:axis:$a:32767,$((t+2)):joy:axis:$a:0"; t=$((t+3)); done
  keys="$keys,$t:joy:button:0:1,$((t+1)):joy:button:0:0,$((t+2)):joy:button:3:1,$((t+3)):joy:button:3:0"
  st=$(run joy --exe "$root/build/exe/SDLGL.EXE" --args="--frames 1500" --keys "$keys" \
         --pre "SET SDL_AUDIO_DRIVER=dummy")
  r=$(log joy | python3 -c '
import sys
lo, hi, btn, axes = {}, {}, set(), 0
for l in sys.stdin:
    p = l.split()
    if p[:1] != ["HX-SDLJOY"]: continue
    if p[1] == "axis": a, v = int(p[2]), int(p[3]); lo[a] = min(lo.get(a, 0), v); hi[a] = max(hi.get(a, 0), v)
    elif p[1] == "button": btn.add(p[2] + p[3])
    elif "axes=" in l: axes = int(l.split("axes=")[1].split()[0])
full = [a for a in range(4) if lo.get(a, 0) <= -32000 and hi.get(a, 0) >= 32000]
ok = axes == 4 and len(full) == 4 and {"0down", "0up", "3down", "3up"} <= btn
print(("PASS" if ok else "FAIL") + " (%d axes, full scale on %s, buttons %s)" % (axes, full, sorted(btn)))')
  if [ "$st" = PASS ]; then say joy "$r"; else bad joy "$st" "$r"; fi;;
*) echo "loopa.sh: unknown check $c" >&2; fail=1;;
esac; done
exit $fail
