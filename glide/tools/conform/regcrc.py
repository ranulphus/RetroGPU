#!/usr/bin/env python3
"""regcrc.py - Glide conformance runs compared exactly (the Glide back-end seam's check, after DOS-GL's B1).

    glide/tools/conform/regcrc.py snap DIR CARD     copy out/conform/<t>/ (serial.log, frames) to DIR/CARD/<t>/
    glide/tools/conform/regcrc.py snaprun SRC DIR CARD NAME   one run's directory (a replay: out/replay/gta/mga) to
                                                              DIR/CARD/NAME/
    glide/tools/conform/regcrc.py compare BASE NEW [CARD ...]

Run with REGCRC=1 builds (make runtime dostests REGCRC=1): each conformance run's serial log then carries an
MGL-REGCRC line at every buffer swap and at close, the running CRC of every register write and WARP vertex so far.
Glide's runner writes out/conform/<t>/ afresh for each card, so `snap` keeps a card's run. `compare` wants, for
every test of every card (or the cards named), the same MGL-REGCRC lines and every frame (t*_N.png) the same byte
for byte. Exit status 0 when everything matches.
"""
import json
import os
import re
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
LINE = re.compile(r"MGL-REGCRC (\S+) frame=(\d+) crc=([0-9a-f]+) n=(\d+)")
FRAME = re.compile(r"^(t\d+[a-z]*|glp)_\d+\.png$")
TEST = re.compile(r"^t\d+[a-z]*$")


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


def snap(dst, card):
    src = os.path.join(ROOT, "out", "conform")
    n = 0
    for t in sorted(os.listdir(src)):
        d = os.path.join(src, t)
        if not (TEST.match(t) and os.path.isdir(d)):
            continue
        o = os.path.join(dst, card, t)
        if os.path.isdir(o):
            shutil.rmtree(o)
        os.makedirs(o)
        for fn in os.listdir(d):
            if fn == "serial.log" or FRAME.match(fn):
                shutil.copy2(os.path.join(d, fn), o)
        n += 1
    print("regcrc: %d tests of %s kept in %s" % (n, card, os.path.join(dst, card)))
    return 0


def snaprun(src, dst, card, name):
    o = os.path.join(dst, card, name)
    if os.path.isdir(o):
        shutil.rmtree(o)
    os.makedirs(o)
    for fn in os.listdir(src):
        if fn == "serial.log" or FRAME.match(fn):
            shutil.copy2(os.path.join(src, fn), o)
    print("regcrc: %s kept in %s" % (src, o))
    return 0


def nocompare():
    """The manifest's tests not compared (t23, the throughput benchmark: it draws for a time, so its writes vary)."""
    m = json.load(open(os.path.join(ROOT, "glide", "tests", "conform", "manifest.json")))
    return {k for k, v in m.items() if isinstance(v, dict) and v.get("nocompare")}


def compare(base, new, cards):
    skip = nocompare()
    if not cards:
        cards = sorted(d for d in os.listdir(new) if os.path.isdir(os.path.join(new, d)))
    bad = 0
    for card in cards:
        tests = sorted(t for t in os.listdir(os.path.join(new, card)) if os.path.isdir(os.path.join(new, card, t)))
        for t in tests:
            if t in skip:
                print("%-5s %-6s not compared (the manifest's nocompare)" % (card, t))
                continue
            a, b = os.path.join(base, card, t), os.path.join(new, card, t)
            problems = []
            if not os.path.isdir(a):
                problems.append("not in BASE")
            else:
                ca, cb = crcs(a), crcs(b)
                if not ca or not cb:
                    problems.append("no MGL-REGCRC lines (not a REGCRC=1 build?)")
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


def main(argv):
    if len(argv) >= 4 and argv[1] == "snap":
        return snap(argv[2], argv[3])
    if len(argv) >= 6 and argv[1] == "snaprun":
        return snaprun(argv[2], argv[3], argv[4], argv[5])
    if len(argv) >= 4 and argv[1] == "compare":
        return compare(argv[2], argv[3], argv[4:])
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
