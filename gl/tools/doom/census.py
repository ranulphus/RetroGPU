#!/usr/bin/env python3
"""What PrBoom-plus's GL renderer needs from DOS-GL: the P2 work list.

  census.py [--src DIR] [--json OUT]

Reads the renderer (src/gl_*.c, the GL parts of e6y.c and SDL/i_video.c in
the fork: by default the tree tools/doom/build.sh staged in
build/doom/src/prboom2, else the checkout ~/prboom-plus-dos) and DOS-GL's
sources and headers:

  called    the renderer calls it directly (core GL 1.1)
  missing   called, but DOS-GL only stubs it (a logged no-op), with the files
            that call it; a call compiled out on DOS (#ifndef __DJGPP__) is
            still listed, so read the file before acting on it

Extension entry points go through GLEXT_ pointers that the renderer loads
only when DOS-GL advertises the extension, so they are not listed. At run
time DOS-GL logs DGL-STUB for each stub called and DGL-GLERR for refused
calls, which is the check that counts (run.sh timedemo with VIDMODE=gl:
none), and a -DDOS_GLCHECK build (DOOM_CFLAGS) names the file and line of
each refused call.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "quake"))
from census import code, dosgl_have  # noqa: E402


def prboom_needs(src):
    calls = {}
    d = os.path.join(src, "src")
    names = [n for n in sorted(os.listdir(d)) if n.startswith("gl_") and n.endswith((".c", ".h"))]
    names += ["e6y.c", os.path.join("SDL", "i_video.c")]
    for name in names:
        text = code(os.path.join(d, name))
        for m in re.finditer(r"(?<![\w.>])(gl[A-Z]\w*)\s*\(", text):
            calls.setdefault(m.group(1), set()).add(name)
    return calls


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    staged = os.path.join(ROOT, "build", "doom", "src", "prboom2")
    ap.add_argument("--src", default=staged if os.path.isdir(staged) else os.path.expanduser("~/prboom-plus-dos/prboom2"))
    ap.add_argument("--json", help="also write the lists here")
    a = ap.parse_args()
    have_funcs, _ = dosgl_have()
    calls = {f: w for f, w in prboom_needs(a.src).items() if not f.startswith(("gld_", "glu"))}
    calls = {f: w for f, w in calls.items() if f in have_funcs or re.match(r"gl[A-Z][a-z]", f)}
    missing = {f: sorted(files) for f, files in calls.items() if f not in have_funcs}
    print("census: the renderer (%s) calls %d GL functions; DOS-GL implements %d of them"
          % (a.src, len(calls), len(calls) - len(missing)))
    for f, where in sorted(missing.items()):
        print("  missing %-28s %s" % (f, " ".join(where)))
    if a.json:
        json.dump({"called": sorted(calls), "missing": missing}, open(a.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
