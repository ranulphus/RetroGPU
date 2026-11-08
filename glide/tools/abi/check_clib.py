#!/usr/bin/env python3
"""Fail if GLIDE2X.OVL pulls C-library modules that need C startup.

The DLL has no C-library initialisation (PRD §6, src/rt/rt.h), so only
self-contained routines may be linked from clib3s. Reads the wlink map.
"""
import re
import sys

# Module-name prefixes that are safe without C startup.
ALLOWED = re.compile(r"^_?(mem|str|_?7f|_?i8|_?u8|i4|u4|__?I8|__?U8|fdiv|chipbug|cstrt|xmsg|"
                     r"i8d|u8d|i8m|u8m|i8s|u8s|i4d|u4d|i4m|fpu|flda|fsta|_?chk8087|"
                     r"inp|outp|int386|intx386|segread|intr|chipa|abs|labs|__stos)")


def main(path):
    txt = open(path, errors="replace").read()
    mods = set()
    for m in re.finditer(r"^Module:\s*(\S+\.lib)\(([^)]+)\)", txt, re.M):
        lib, mod = m.group(1), m.group(2)
        mods.add((lib.split("/")[-1].lower(), mod.split("/")[-1].lower()))
    bad = sorted((lib, mod) for lib, mod in mods
                 if lib.startswith("clib") and not ALLOWED.match(mod))
    print("check-clib: %d library modules linked" % len(mods))
    for lib, mod in sorted(mods):
        print("  %s(%s)%s" % (lib, mod, "  <-- not allowed" if (lib, mod) in bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
