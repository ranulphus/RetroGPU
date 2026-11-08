#!/usr/bin/env bash
# Run PrBoom-plus on DOS-GL in Loop A (86Box, MGA-Glide's runner).
#
#   run.sh MODE [CARD] [-- PRBOOMP ARGS...]
#
#   MODE   nodraw   every built-in demo of the IWADs (DEMOS, default
#                   "DOOM:1 DOOM:2 DOOM:3 DOOM:4 DOOM2:1 DOOM2:2 DOOM2:3") as
#                   -timedemo -nodraw -nosound -checksum, one program run
#                   each; afterwards each one's gametics and final checksum
#                   are compared with tools/doom/reference.json, and the
#                   config written at exit and the absence of any video
#                   set-up are checked
#          timedemo the same demos shown and heard (SOUND defaults to sb16),
#                   each also written as DOSBench test <D1|D2>DEMO<N>
#                   (C:\OUT\RESULTS.TXT; frame DBSHOT, default 200, as
#                   L<ID>.PPM; <D1|D2>GL<N> with VIDMODE=gl, the OpenGL
#                   renderer); GEOM=WxH picks the resolution; checked as
#                   nodraw, plus each test's DOSBench line
#          play     keys mode with tools/doom/keys/PLAY.keys (default p3play:
#                   new game, save, load, idclev12, quit, checked from the log)
#          keys     PRBOOMP.EXE (ARGS, default -iwad DOOM.WAD) with KEYS typed
#                   into it; then VECCHK, SBCHK and KEYWAIT
#          cmd      PRBOOMP.EXE with ARGS (default -iwad DOOM.WAD)
#   CARD   g450 (default), g400 or g200
# The program's output is in out/NAME/files/*.LOG and on the serial log
# (DOOM_SERIAL: DOOM-LOG lines between HX-START and HX-DONE).
# Environment: DEMOS, MEM (the PC's RAM in MB, default 64), SOUND (86Box
# sound card, default none; sb16 also sets BLASTER), MOUSE (none by default;
# ps2 with CuteMouse), WAV=1 (record the sound card to out/NAME/audio.wav),
# SHOTS (screenshot seconds after boot), PRE (one more
# RUN.BAT line), NAME (result directory out/NAME, default doom-MODE-CARD),
# NORUN=1 (check out/NAME from an earlier run without running again),
# MGAHAL_DIR (another MGA-Glide checkout; default $MGA_GLIDE or ~/MGA-Glide), DOOM_BUILD (another build
# directory holding PRBOOMP.EXE and PRBOOM.WAD). DJGPP's long file names are off
# (LFN=n): the program must live with 8.3 names.
# Needs build/doom (tools/doom/build.sh) and the fixture (tools/doom/fixtures.py).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
mode=${1:?usage: run.sh MODE [CARD] [-- ARGS...]}; shift
card=g450
if [ $# -gt 0 ] && [ "$1" != "--" ]; then card=$1; shift; fi
[ "${1:-}" = "--" ] && shift
hal=${MGAHAL_DIR:-${MGA_GLIDE:-$HOME/MGA-Glide}}
# The DOS helpers Loop A puts in C:\HX (built in MGA-Glide's checkout).
[ -f "$hal/build/ow/dos/UTEXIT.COM" ] || make -C "$root" -s dostools
b=${DOOM_BUILD:-$root/build/doom}
[ -f "$b/PRBOOMP.EXE" ] || { echo "run.sh: no $b/PRBOOMP.EXE (tools/doom/build.sh)" >&2; exit 2; }
name=${NAME:-doom-$mode-$card}
common=(--games-file "$here/games.json" --game doom --card "$card" --out "$root/out/$name"
        --pre "SET DOOM_SERIAL=1" --pre "SET LFN=n"
        --file "$b/PRBOOMP.EXE=D:/DOOM/PRBOOMP.EXE" --file "$b/PRBOOM.WAD=D:/DOOM/PRBOOM.WAD"
        --mem "${MEM:-64}" --timeout 2400 --idle 300)
case $mode in timedemo|play|keys) SOUND=${SOUND:-sb16} ;; esac
if [ "${SOUND:-none}" != none ]; then
  common+=(--sound "$SOUND" --pre "SET BLASTER=A220 I5 D1 H5 T6")
else
  common+=(--sound "")
fi
common+=(--mouse "${MOUSE:-none}")
[ -n "${WAV:-}" ] && common+=(--wav)
[ -n "${PRE:-}" ] && common+=(--pre "$PRE")
[ -n "${SHOTS:-}" ] && common+=(--shots "$SHOTS")
start=(--cmd "D:" --cmd "CD \\DOOM")
case $mode in
  nodraw)
    demos=${DEMOS:-DOOM:1 DOOM:2 DOOM:3 DOOM:4 DOOM2:1 DOOM2:2 DOOM2:3}
    cmds=()
    for d in $demos; do
      w=${d%:*} n=${d#*:}
      id=$( [ "$w" = DOOM2 ] && echo D2 || echo D1 )DEMO$n
      cmds+=(--cmd "PRBOOMP.EXE -iwad $w.WAD -timedemo demo$n -nodraw -nosound -checksum C:\\OUT\\$id.SUM $* > C:\\OUT\\$id.LOG")
    done
    set -- "${common[@]}" "${start[@]}" "${cmds[@]}" --cmd "COPY PRBOOM.CFG C:\\OUT > NUL" ;;
  timedemo)
    # every demo in DEMOS shown and heard (SOUND defaults to sb16 here) as
    # a timedemo, written as DOSBench test <IWAD><N> (C:\OUT\RESULTS.TXT,
    # frame DBSHOT (default 200) as L<ID>.PPM), with the game-state checksum
    demos=${DEMOS:-DOOM:1 DOOM:2 DOOM:3 DOOM:4 DOOM2:1 DOOM2:2 DOOM2:3}
    cmds=()
    for d in $demos; do
      w=${d%:*} n=${d#*:}
      id=$( [ "$w" = DOOM2 ] && echo D2 || echo D1 )$( [ "${VIDMODE:-}" = gl ] && echo GL || echo DEMO )$n
      cmds+=(--cmd "PRBOOMP.EXE -iwad $w.WAD ${GEOM:+-geom $GEOM }${VIDMODE:+-vidmode $VIDMODE }-timedemo demo$n -dosbench C:\\OUT $id -dbshot ${DBSHOT:-200} -checksum C:\\OUT\\$id.SUM $* > C:\\OUT\\$id.LOG")
    done
    set -- "${common[@]}" "${start[@]}" "${cmds[@]}" ;;
  play|keys)
    # KEYS typed into PRBOOMP.EXE (Loop A --keys: SECONDS:SCANCODE,...,
    # @TEXT anchors); play: tools/doom/keys/PLAY.keys (default p3play, the
    # P3 exit, checked from the log afterwards). After the program: VECCHK
    # (interrupt vectors as before it), SBCHK (the Sound Blaster stopped),
    # KEYWAIT (a key through the BIOS) and the config copied out.
    if [ "$mode" = play ]; then
      KEYS=${KEYS:-$(sed 's/#.*//; s/^[[:space:]]*//; s/[[:space:]]*$//' "$here/keys/${PLAY:-p3play}.keys" | grep -v '^$' | paste -sd, -)}
    fi
    args=${*:-"-iwad DOOM.WAD ${VIDMODE:+-vidmode $VIDMODE}"}
    set -- "${common[@]}" --keys "${KEYS:?KEYS=SECONDS:SCANCODE,...}" "${start[@]}" --cmd "VECCHK save" \
      --cmd "PRBOOMP.EXE $args > C:\\OUT\\DOOM.LOG" --cmd "VECCHK check" --cmd "SBCHK" --cmd "KEYWAIT 20" \
      --cmd "COPY PRBOOM.CFG C:\\OUT > NUL" --cmd "COPY PRBSAV0.DSG C:\\OUT > NUL" ;;
  cmd)
    args=${*:-"-iwad DOOM.WAD"}
    set -- "${common[@]}" "${start[@]}" --cmd "PRBOOMP.EXE $args > C:\\OUT\\DOOM.LOG" \
      --cmd "COPY PRBOOM.CFG C:\\OUT > NUL" ;;
  *) echo "run.sh: unknown mode $mode" >&2; exit 2 ;;
esac
cd "$root"
rc=0
# NORUN=1: only check an earlier run's results in out/NAME
[ -n "${NORUN:-}" ] || "$hal/tools/dev" python3 "$hal/tools/loopa/run.py" --name "$name" "$@" || rc=$?
out=$root/out/$name
if [ "$mode" = nodraw ] || [ "$mode" = timedemo ]; then
  MODE=$mode VIDMODE=${VIDMODE:-} python3 - "$here/reference.json" "$out" $demos <<'EOF' || rc=1
import json, os, re, sys
ref = json.load(open(sys.argv[1]))["demos"]
out, demos = sys.argv[2], sys.argv[3:]
files = {f.upper(): os.path.join(dp, f) for dp, _, fs in os.walk(os.path.join(out, "files")) for f in fs}
serial = open(os.path.join(out, "serial.log"), "rb").read().decode("latin-1").replace("\r", "")
ok = True
def check(what, good, detail=""):
    global ok
    ok &= good
    print("%s: %-4s %s%s" % (os.environ["MODE"], "ok" if good else "FAIL", what, "  " + detail if detail else ""))
# one HX-START .. HX-DONE block per program run, in order
blocks = re.split(r"^(?=HX-START PRBOOMP)", serial, flags=re.M)[1:]
for i, d in enumerate(demos):
    w, n = d.split(":")
    key = "%s demo%s" % (w, n)
    sid = ("D2" if w == "DOOM2" else "D1") + ("GL" if os.environ.get("VIDMODE") == "gl" else "DEMO") + n
    blk = blocks[i] if i < len(blocks) else ""
    m = re.search(r"^DOOM-TIMED gametics=(\d+)", blk, re.M)
    tics = int(m.group(1)) if m else None
    s = files.get(sid + ".SUM")
    final = None
    if s:
        for line in open(s, errors="replace"):
            if line.startswith("final: "):
                final = line.split()[1]
    m = re.search(r"^HX-DONE (-?\d+)", blk, re.M)
    done = int(m.group(1)) if m else None
    want = ref.get(key, {})
    check("%-11s gametics %s, final %s, exit %s" % (key, tics, final, done),
          tics == want.get("gametics") and final == want.get("final") and done == 0,
          "" if tics == want.get("gametics") and final == want.get("final")
          else "want %s %s" % (want.get("gametics"), want.get("final")))
if os.environ["MODE"] == "nodraw":
    check("config written (PRBOOM.CFG)", "PRBOOM.CFG" in files)
    check("no video set-up", not re.search(r"I_UpdateVideoMode|I_InitGraphics", serial))
else:
    # the DOSBench lines: frames, fps and the captured frame's CRC per test
    res = files.get("RESULTS.TXT")
    tl = [l.split() for l in open(res) if l.startswith("T ")] if res else []
    for d in demos:
        w, n = d.split(":")
        sid = ("D2" if w == "DOOM2" else "D1") + ("GL" if os.environ.get("VIDMODE") == "gl" else "DEMO") + n
        t = [dict(x.split("=", 1) for x in l[1:] if "=" in x) for l in tl if "test=" + sid in l]
        if t:
            t = t[-1]
            print("timedemo: %-8s frames=%s fps=%s p99_ms=%s crc=%s" % (sid, t.get("frames"), t.get("fps"), t.get("p99_ms"), t.get("crc")))
        check("%s DOSBench line" % sid, bool(t))
    if os.environ.get("VIDMODE") == "gl":
        # DOS-GL's own log: a stub reached or a GL error raised
        bad = sorted(set(re.findall(r"^(DGL-STUB \S+|DGL-GLERR \S+)", serial, re.M)))
        check("no DOS-GL stubs or GL errors", not bad, " ".join(bad))
sys.exit(0 if ok else 1)
EOF
fi
if [ "$mode" = play ] && [ "${PLAY:-p3play}" = p3play ]; then
  # the P3 exit: new game, save, load, a level change, quit; then the
  # machine as it was: vectors, Sound Blaster, keyboard, text mode
  log=$(tr -d '\r' < "$out/serial.log" 2>/dev/null)
  check() { if grep -qE "$2" <<<"$log"; then echo "play: ok   $1"; else echo "play: FAIL $1"; rc=1; fi; }
  check "new game (E1M1)" "^DOOM-LEVEL E1M1"
  check "saved to slot 1" "^DOOM-LOG G_DoSaveGame: \[1\]"
  check "loaded slot 1" "^DOOM-LOG G_DoLoadGame: \[1\]"
  check "idclev12 (E1M2)" "^DOOM-LEVEL E1M2"
  check "clean exit" "^HX-DONE 0"
  check "vectors restored" "^HX-VECCHK ok"
  check "Sound Blaster stopped" "^HX-SBCHK .*stopped"
  check "keyboard through the BIOS" "^HX-KEY scan=39"
  check "text mode" "^HX-VMODE bios=03"
  for f in PRBOOM.CFG PRBSAV0.DSG; do
    if find "$out/files" -iname "$f" | grep -q .; then echo "play: ok   $f written"; else echo "play: FAIL $f"; rc=1; fi
  done
fi
for f in DOOM.LOG; do
  p=$(find "$out/files" -iname "$f" 2>/dev/null | head -1)
  if [ -n "$p" ] && [ "${TAIL:-12}" -gt 0 ]; then
    echo "== $f"; grep -v '^\s*$' "$p" | tail -"${TAIL:-12}" || true
  fi
done
exit $rc
