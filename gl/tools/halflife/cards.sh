#!/usr/bin/env bash
# The same Half-Life frames on every card (H2): tools/halflife/run.sh map on
# each CARD with DGL_SNAP at the frames SNAP names, then each card's frames
# against the first card's (the conformance tolerances: RGB565, 24 per
# channel on at most 0.5% of pixels, edges masked).
#
#   cards.sh [CARD...]          default g450 g400 g200
#
# Environment: MAP (default c1a0), SNAP (default 300,500), FRAMES (default
# 550), WIDTH, HEIGHT, NAME (result directories out/NAME-CARD, default
# hl-cards). Frames of retail data stay local.
set -euo pipefail
# COMPARE_ONLY=1: compare the results already in out/ without running again
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
cards=("$@")
[ ${#cards[@]} -gt 0 ] || cards=(g450 g400 g200)
name=${NAME:-hl-cards}
export SNAP=${SNAP:-300,500} FRAMES=${FRAMES:-550}
rc=0
for c in "${cards[@]}"; do
  [ -n "${COMPARE_ONLY:-}" ] && break
  NAME=$name-$c TAIL=0 "$here/run.sh" map "$c" > "$root/out/$name-$c.log" 2>&1 || rc=1
  echo "$c: $(cat "$root/out/$name-$c/status" 2>/dev/null)"
done
python3 - "$root" "$name" "${cards[@]}" <<'EOF' || rc=1
import glob, os, sys
root, name, cards = sys.argv[1], sys.argv[2], sys.argv[3:]
sys.path.insert(0, os.path.join(root, "third_party", "mgahal", "tools"))
import imgcmp
base = os.path.join(root, "out", "%s-%s" % (name, cards[0]))   # Loop A's PNGs of the F*.PPM snapshots
frames = sorted(glob.glob(os.path.join(base, "f*.png")))
if not frames:
    sys.exit("cards: no snapshots from %s" % cards[0])
bad = 0
for c in cards[1:]:
    for f in frames:
        got = os.path.join(root, "out", "%s-%s" % (name, c), os.path.basename(f))
        if not os.path.exists(got):
            print("  %s %s missing" % (c, os.path.basename(f)))
            bad += 1
            continue
        r = imgcmp.compare(f, got, diff_path=got[:-4] + ".diff.png")
        print("  %s %s vs %s: %s frac=%s worst=%s" % (c, os.path.basename(f), cards[0],
              "ok" if r["ok"] else "DIFFERENT", r.get("frac"), r.get("worst")))
        bad += not r["ok"]
sys.exit(1 if bad else 0)
EOF
exit $rc
