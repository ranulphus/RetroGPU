#!/usr/bin/env python3
"""attr.py EXE SAMPLES: softmesh's PROF=1 samples (one instruction address a
line, hex) mapped onto EXE's functions by nm; the share of each, largest
first (gl/docs/b2.md B2.9)."""
import bisect, collections, subprocess, sys
exe, samp = sys.argv[1], sys.argv[2]
syms = []
for line in subprocess.run(["nm", "-n", "-C", exe], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) >= 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
c = collections.Counter()
n = 0
for l in open(samp):
    a = int(l, 16); n += 1
    i = bisect.bisect_right(addrs, a) - 1
    c[syms[i][1] if i >= 0 else "?"] += 1
for name, k in c.most_common(28):
    print("%6.2f%%  %s" % (100.0 * k / n, name))
print(n, "samples")
