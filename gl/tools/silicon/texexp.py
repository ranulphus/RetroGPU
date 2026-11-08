#!/usr/bin/env python3
"""Silicon experiment E9 (examples/texexp; docs/silicon-experiments.md):
texture coordinates through the CPU's triangle setup against the G400's WARP.

  texexp.py loopa [--card g400]   86Box (BOX86_DIR: an emulator with the WARP
                                  model) in out/texexp-CARD/
  texexp.py bench --pc NAME       a bench PC through MGA-Glide's tools/bench/run.py
  texexp.py report CARD_DIR [REF_DIR]
                                  per cell, the share of pixels more than 32
                                  apart: CPU against WARP, and each against REF_DIR
                                  (86Box) when given

One job runs TEXEXP.EXE four times: parts 1 and 2 with DGL_WARP=0 (tag C) and
DGL_WARP=1 (tag W). The pictures are E9C, E9W, E9BC and E9BW.
"""
import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MGA = os.environ.get("MGA_GLIDE", os.path.expanduser("~/MGA-Glide"))
HAL = os.environ.get("MGAHAL_DIR", MGA)
EXE = os.path.join(ROOT, "build", "exe", "TEXEXP.EXE")

CELLS = {
    "e9": ["flat near0", "flat +8", "flat +100", "flat bilin", "floor x4", "floor x32", "floor mip", "floor bilin",
           "atlas flat", "atlas persp", "16 clamp", "8 clamp", "256", "32x8", "edge-on", "huge tri"],
    "e9b": ["atlas in", "atlas L", "atlas R", "atlas LR", "grid in", "grid L", "grid R", "grid LR",
            "flat in", "flat L", "flat TB", "flat farL", "wall scrL", "wall in", "wall L", "wall R"],
}


def commands():
    cmds = []
    for warp, tag in (("0", "C"), ("1", "W")):
        cmds.append("SET DGL_WARP=" + warp)
        cmds.append("TEXEXP.EXE --tag %s --noexit" % tag)
        cmds.append("TEXEXP.EXE --part 2 --tag %s --noexit" % tag)
    cmds.append("SET DGL_WARP=")
    return cmds


def cmd_loopa(a):
    out = os.path.join(ROOT, "out", "texexp-" + a.card)
    cmd = [os.path.join(HAL, "tools", "dev"), "python3", os.path.join(HAL, "tools", "loopa", "run.py"),
           "--name", "texexp-" + a.card, "--card", a.card, "--out", out, "--file", EXE, "--timeout", "600"]
    for c in commands():
        cmd += ["--cmd", c]
    return subprocess.run(cmd).returncode


def cmd_bench(a):
    cmd = [sys.executable, os.path.join(MGA, "tools", "bench", "run.py"), "--pc", a.pc, "--name", "texexp",
           "--file", EXE, "--timeout", "600"]
    for c in commands():
        cmd += ["--cmd", c]
    return subprocess.run(cmd, cwd=MGA).returncode


def cells(a, b):
    from PIL import ImageChops
    out = []
    for r in range(4):
        for c in range(4):
            box = (c * 160, r * 120, c * 160 + 160, r * 120 + 120)
            h = ImageChops.difference(a.crop(box), b.crop(box)).convert("L").histogram()
            out.append(sum(h[33:]) / sum(h))
    return out


def cmd_report(a):
    from PIL import Image
    o = lambda d, n: Image.open(os.path.join(d, n + ".png")).convert("RGB")
    for part, names in CELLS.items():
        if not os.path.exists(os.path.join(a.dir, part + "c.png")):
            continue
        cw = cells(o(a.dir, part + "c"), o(a.dir, part + "w"))
        cols = [cw]
        head = "%-11s %8s" % (part, "C vs W")
        if a.ref:
            cols += [cells(o(a.dir, part + "c"), o(a.ref, part + "c")), cells(o(a.dir, part + "w"), o(a.ref, part + "w"))]
            head += " %9s %9s" % ("C vs ref", "W vs ref")
        print(head)
        for i, n in enumerate(names):
            print("%-11s" % n + "".join(" %8.3f" % c[i] if k == 0 else " %9.3f" % c[i] for k, c in enumerate(cols)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("loopa")
    p.add_argument("--card", default="g400")
    p = sub.add_parser("bench")
    p.add_argument("--pc", required=True)
    p = sub.add_parser("report")
    p.add_argument("dir")
    p.add_argument("ref", nargs="?")
    a = ap.parse_args()
    return {"loopa": cmd_loopa, "bench": cmd_bench, "report": cmd_report}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
