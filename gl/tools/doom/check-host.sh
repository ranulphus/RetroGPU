#!/usr/bin/env bash
# The Linux build of the fork (tools/doom/build.sh --host) against
# tools/doom/reference.json: every built-in demo of the IWADs as
# -timedemo -nodraw -nosound -checksum; gametics and the final checksum
# must equal the reference (recorded with upstream's SDL2 build).
#
#   check-host.sh [IWAD:N ...]      default: DOOM:1-4 and DOOM2:1-3
#
# IWADs from the fixture ($MGA_CACHE/fixtures/games/doom/DOOM, fixtures.py).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
b=$root/build/doom/host
[ -x "$b/prboom-plus" ] || { echo "check-host: no $b/prboom-plus (build.sh --host)" >&2; exit 2; }
wads=${MGA_CACHE:-$HOME/.cache/mga-glide}/fixtures/games/doom/DOOM
[ -f "$wads/DOOM.WAD" ] || { echo "check-host: no IWADs in $wads (fixtures.py)" >&2; exit 2; }
demos=${*:-DOOM:1 DOOM:2 DOOM:3 DOOM:4 DOOM2:1 DOOM2:2 DOOM2:3}
o=$root/out/doom-host; rm -rf "$o"; mkdir -p "$o"
cd "$b"   # prboom-plus.wad is found in the working directory
for d in $demos; do
  w=${d%:*} n=${d#*:}
  # a timedemo ends through I_Error (exit status 255) on Linux
  HOME=$o DOOMWADDIR=$wads SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
    ./prboom-plus -iwad "$w.WAD" -timedemo "demo$n" -nodraw -nosound -checksum "$o/$w-$n.sum" > "$o/$w-$n.log" 2>&1 || true
done
python3 - "$here/reference.json" "$o" $demos <<'EOF'
import json, re, sys
ref = json.load(open(sys.argv[1]))["demos"]
o, ok = sys.argv[2], True
for d in sys.argv[3:]:
    w, n = d.split(":")
    key = "%s demo%s" % (w, n)
    log = open("%s/%s-%s.log" % (o, w, n), errors="replace").read()
    m = re.search(r"Timed (\d+) gametics", log)
    tics = int(m.group(1)) if m else None
    final = None
    try:
        for line in open("%s/%s-%s.sum" % (o, w, n)):
            if line.startswith("final: "):
                final = line.split()[1]
    except OSError:
        pass
    want = ref.get(key, {})
    good = tics == want.get("gametics") and final == want.get("final")
    ok &= good
    print("check-host: %-4s %-11s gametics %s, final %s" % ("ok" if good else "FAIL", key, tics, final))
sys.exit(0 if ok else 1)
EOF
