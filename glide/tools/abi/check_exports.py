#!/usr/bin/env python3
"""Verify GLIDE2X.OVL's module name and export table.

usage: check_exports.py GLIDE2X.OVL abi/glide2x.api [game.names ...]

Fails if the module is not a DLL named glide2x, if any function in the API
table is not exported under its decorated uppercase name, or if any name a
game imports is missing.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from api import load_api  # noqa: E402
from ledump import LEModule  # noqa: E402


def main(argv):
    ovl, api_path, games = argv[1], argv[2], argv[3:]
    m = LEModule(open(ovl, "rb").read())
    exported = set(m.exports())
    errors = []
    if m.module_name() != "glide2x":
        errors.append("module name is %r, want 'glide2x'" % m.module_name())
    if not m.is_library():
        errors.append("module flags %#x: library bit not set" % m.module_flags)
    if m.num_import_modules:
        errors.append("DLL imports %d modules; it must be self-contained" % m.num_import_modules)
    want = {f.decorated for f in load_api(api_path)}
    for n in sorted(want - exported):
        errors.append("missing export " + n)
    for g in games:
        for n in open(g).read().split():
            if n not in exported:
                errors.append("%s imports %s, not exported" % (os.path.basename(g), n))
    extra = sorted(n for n in exported - want if n.startswith(("_GR", "_GU")))
    for n in extra:
        errors.append("unexpected Glide export " + n)
    print("check-exports: module=%s flags=%#x exports=%d" % (m.module_name(), m.module_flags, len(exported)))
    for e in errors:
        print("  ERROR", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
