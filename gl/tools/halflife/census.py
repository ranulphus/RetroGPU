#!/usr/bin/env python3
"""What Xash3D FWGS's GL renderer needs from DOS-GL: the H2 work list.

  census.py [--xash DIR] [--json OUT]

Reads the renderer (ref/gl in the engine fork: by default the tree
tools/halflife/build.sh staged in build/halflife/src/xash3d, else the
checkout ~/xash3d-fwgs-dos) and DOS-GL's sources and headers:

  called    the renderer calls it (pglName(...) through its function table)
  missing   called, but DOS-GL only stubs it (a logged no-op), with the
            renderer's function list it is in: opengl_110funcs is loaded
            always, the others only when their extension is advertised

The renderer defines its own GL constants (gl_export.h), so tokens do not
matter. At run time DOS-GL logs DGL-STUB for each stub called, which is the
check that counts (tools/halflife/run.sh map: none on c1a0).
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


def func_lists(root):
    """{GL function: the renderer's dllfunc_t list naming it} (gl_opengl.c)."""
    text = code(os.path.join(root, "ref", "gl", "gl_opengl.c"))
    out = {}
    for m in re.finditer(r"dllfunc_t\s+(\w+)\[\][^=]*=\s*\{(.*?)\n\};", text, re.S):
        for f in re.findall(r"GL_CALL\(\s*(gl\w+)\s*\)", m.group(2)):
            out.setdefault(f, m.group(1))
    return out


def xash_needs(root):
    calls, tokens = {}, set()
    d = os.path.join(root, "ref", "gl")
    for name in sorted(os.listdir(d)):
        if not name.endswith((".c", ".h")):
            continue
        text = code(os.path.join(d, name))
        for m in re.finditer(r"\bpgl([A-Z]\w*)\s*\(", text):
            calls.setdefault("gl" + m.group(1), set()).add(name)
        tokens |= set(re.findall(r"\b(GL_[A-Z0-9_]+)\b", text))
    return calls, tokens


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    staged = os.path.join(ROOT, "build", "halflife", "src", "xash3d")
    ap.add_argument("--xash", default=staged if os.path.isdir(staged) else os.path.expanduser("~/xash3d-fwgs-dos"))
    ap.add_argument("--json", help="also write the lists here")
    a = ap.parse_args()
    have_funcs, have_tokens = dosgl_have()
    calls, tokens = xash_needs(a.xash)
    lists = func_lists(a.xash)
    missing = {f: sorted(files) for f, files in calls.items() if f not in have_funcs}
    print("census: the renderer (%s) calls %d GL functions; DOS-GL implements %d of them"
          % (a.xash, len(calls), len(calls) - len(missing)))
    for f, where in sorted(missing.items(), key=lambda kv: (lists.get(kv[0], "?") != "opengl_110funcs", kv[0])):
        print("  missing %-32s %-28s %s" % (f, lists.get(f, "?"), " ".join(where)))
    if a.json:
        json.dump({"called": sorted(calls), "missing": missing, "lists": lists}, open(a.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
