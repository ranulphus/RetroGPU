#!/usr/bin/env python3
"""Mark API functions implemented: set_status.py STATUS name [name ...]
or --from-src to mark every function defined with GR_ENTRY in src/ as
'partial' (unless already 'done')."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
API = os.path.join(ROOT, "abi", "glide2x.api")


def defined_in_src():
    names = set()
    for dp, _dn, fns in os.walk(os.path.join(ROOT, "src")):
        for fn in fns:
            if fn.endswith(".c"):
                for m in re.finditer(r"GR_ENTRY\(\s*[^,]+,\s*(\w+)\s*,", open(os.path.join(dp, fn)).read()):
                    names.add(m.group(1))
    return names


def main(argv):
    lines = open(API).read().splitlines()
    if argv[1] == "--from-src":
        impl = defined_in_src()
        out = []
        for l in lines:
            if not l.startswith("#") and "|" in l:
                parts = [p.strip() for p in l.split("|")]
                if parts[0] in impl and parts[5] == "stub":
                    parts[5] = "partial"
                elif parts[0] not in impl and parts[5] != "stub":
                    parts[5] = "stub"
                l = " | ".join(parts)
            out.append(l)
    else:
        status, names = argv[1], set(argv[2:])
        out = []
        for l in lines:
            if not l.startswith("#") and "|" in l:
                parts = [p.strip() for p in l.split("|")]
                if parts[0] in names:
                    parts[5] = status
                l = " | ".join(parts)
            out.append(l)
    open(API, "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main(sys.argv)
