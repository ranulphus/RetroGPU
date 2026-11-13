#!/usr/bin/env python3
"""regcrc.py - two conformance runs compared exactly (B1's check, docs/b1.md).

    tools/conform/regcrc.py BASE NEW [CARD ...]

BASE and NEW are copies of out/conform from runs of REGCRC=1 builds (make
conform CARD=... REGCRC=1). For every test of every card (or the cards
named), the DGL-REGCRC lines (the running CRC of every register write and
WARP vertex, at each swap and at exit) must be the same, and so must every
frame (t*_N.png), byte for byte. Exit status 0 when everything matches.
"""
import os
import re
import sys

LINE = re.compile(r"DGL-REGCRC (\S+) frame=(\d+) crc=([0-9a-f]+) n=(\d+)")
FRAME = re.compile(r"^t\d+[a-z]*_\d+\.png$")


def crcs(test_dir):
    try:
        with open(os.path.join(test_dir, "serial.log"), errors="replace") as f:
            return [m.groups() for m in LINE.finditer(f.read())]
    except OSError:
        return None


def frames(test_dir):
    return sorted(n for n in os.listdir(test_dir) if FRAME.match(n))


def same_file(a, b):
    with open(a, "rb") as fa, open(b, "rb") as fb:
        return fa.read() == fb.read()


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    base, new, cards = argv[1], argv[2], argv[3:]
    if not cards:
        cards = sorted(d for d in os.listdir(new) if os.path.isdir(os.path.join(new, d)) and d != "ref")
    bad = 0
    for card in cards:
        tests = sorted(t for t in os.listdir(os.path.join(new, card)) if os.path.isdir(os.path.join(new, card, t)))
        for t in tests:
            a, b = os.path.join(base, card, t), os.path.join(new, card, t)
            problems = []
            if not os.path.isdir(a):
                problems.append("not in BASE")
            else:
                ca, cb = crcs(a), crcs(b)
                if not ca or not cb:
                    problems.append("no DGL-REGCRC lines (not a REGCRC=1 build?)")
                elif ca != cb:
                    k = next((i for i, (x, y) in enumerate(zip(ca, cb)) if x != y), min(len(ca), len(cb)))
                    problems.append("CRC differs from line %d: %s / %s" % (
                        k, " ".join(ca[k]) if k < len(ca) else "(none)", " ".join(cb[k]) if k < len(cb) else "(none)"))
                fa, fb = frames(a), frames(b)
                if fa != fb:
                    problems.append("frames %s / %s" % (fa, fb))
                else:
                    diff = [n for n in fa if not same_file(os.path.join(a, n), os.path.join(b, n))]
                    if diff:
                        problems.append("frames differ: " + " ".join(diff))
            n = len(crcs(b) or [])
            print("%-5s %-6s %s" % (card, t, "; ".join(problems) if problems else "same (%d CRC lines, %d frames)"
                                    % (n, len(frames(b)))))
            bad += bool(problems)
    print("regcrc: %s" % ("everything the same" if not bad else "%d tests differ" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
