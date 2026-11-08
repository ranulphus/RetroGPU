#!/usr/bin/env python3
"""Open Watcom 2026-09-01 miscompiles one loop shape at -oxt (the runtime's
release optimisation; found 2026-09-29, docs/toolchain.md):

    if (!n) for (n = 0; n < 10; n++) body;      /* n reads 0 afterwards */

A condition that bounds a variable (!n, n == 0, n < 1), directly guarding a
for loop that assigns that variable, leaves the variable at its guarded
value after the loop, although the body runs. `while` loops and a separate
loop counter are fine. This check fails on that shape in any C file Open
Watcom builds (glide/src/, hal/, tests/, glide/tests/, tools/dos/).

  ow_loops.py [ROOT]
"""
import glob
import os
import re
import sys

GUARD = re.compile(r'\bif\s*\(([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*)\)\s*\{?\s*for\s*\(\s*(\w+)\s*=', re.S)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    files = []
    for d in ("glide/src", "hal", "tests", "glide/tests", "tools/dos"):
        files += glob.glob(os.path.join(root, d, "**", "*.c"), recursive=True)
    bad = 0
    for f in sorted(files):
        s = open(f, errors="replace").read()
        for m in GUARD.finditer(s):
            cond, var = m.group(1), m.group(2)
            if re.search(r'\b%s\b' % re.escape(var), cond):
                print("%s:%d: 'if (%s) for (%s = ...': Open Watcom -oxt miscompiles this (tools/ci/ow_loops.py)"
                      % (os.path.relpath(f, root), s[:m.start()].count("\n") + 1, cond.strip(), var))
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
