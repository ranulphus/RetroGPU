#!/usr/bin/env python3
"""What the Quake ports need from DOS-GL: the Q1 work list.

  census.py [--qdos DIR] [--q2dos DIR] [--json OUT]

Reads the forks (by default the pinned trees tools/quake/build.sh exported to
build/quake/src, else the checkouts ~/qdos-dosgl and ~/q2dos-dosgl) and
DOS-GL's own headers and sources, and sorts every GL function and token the
two renderers mention:

  called   a renderer calls it (qdos glX_fp(...), Quake 2 qglX(...)): must work
  linked   only referenced so the static link resolves (Quake 2's qgl table
           binds all ~336 GL 1.1 functions): a logged stub is enough
  have     DOS-GL already exports or defines it

Functions reached only through GetProcAddress (the ARB, EXT and SGIS
extensions) are listed separately with the extension that advertises them.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
HOME = os.path.expanduser("~")


def read(path):
    with open(path, encoding="latin-1") as f:
        return f.read()


def code(path):
    """Source without comments or string literals (token scans should see code only)."""
    text = re.sub(r"/\*.*?\*/", " ", read(path), flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)


def files(root, subdirs, exts=(".c", ".h")):
    for sd in subdirs:
        d = os.path.join(root, sd)
        for name in sorted(os.listdir(d)):
            if name.endswith(exts):
                yield os.path.join(d, name)


def dosgl_have():
    funcs = set()
    for p in files(ROOT, ["src/gl", "src/dgl"], (".c",)):
        funcs |= set(re.findall(r"APIENTRY\s+(gl[A-Z]\w*)\s*\(", read(p)))
    tokens = set()
    for p in ("include/GL/gl.h", "include/GL/glext.h"):
        tokens |= set(re.findall(r"#define\s+(GL_\w+)", read(os.path.join(ROOT, p))))
    return funcs, tokens


def qdos_needs(root):
    common = os.path.join(root, "common")
    func_h = read(os.path.join(common, "gl_func.h"))
    linked = set(re.findall(r"GL_FUNCTION\(\s*[^,]+,\s*(gl\w+)", func_h))
    linked |= set(re.findall(r"#define\s+gl\w+_fp\s+(gl\w+)", func_h))
    optional = set(re.findall(r"GL_FUNCTION_OPT\(\s*[^,]+,\s*(gl\w+)", func_h))
    called, tokens = set(), set()
    for p in files(root, ["common", "quake"]):
        name = os.path.basename(p)
        if not (name.startswith("gl_") or name in ("r_part.c", "dos_dosgl.c", "glquake.h")):
            continue
        text = code(p)
        called |= set(re.findall(r"\b(gl[A-Z]\w*?)_fp\s*\(", text))
        tokens |= set(re.findall(r"\b(GL_[A-Z0-9_]+)\b", text))
    return linked - optional, called, optional, tokens


def q2dos_needs(root):
    qgl = read(os.path.join(root, "dos", "qgl_dos.c"))
    linked = set(re.findall(r"GPA\((gl\w+)\)", qgl))
    called, tokens = set(), set()
    for p in files(root, ["ref_gl"]):
        text = code(p)
        called |= set("gl" + m for m in re.findall(r"\bqgl([A-Z]\w*)\s*\(", text))
        tokens |= set(re.findall(r"\b(GL_[A-Z0-9_]+)\b", text))
    ext = set("gl" + m for m in re.findall(r"\bqgl([A-Z]\w*(?:ARB|EXT|SGIS))\s*=", read(os.path.join(root, "ref_gl", "gl_rmain.c"))))
    return linked, called - ext, ext, tokens


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = os.path.join(ROOT, "build", "quake", "src")
    pick = lambda name, fork: os.path.join(src, name) if os.path.isdir(os.path.join(src, name)) else os.path.join(HOME, fork)
    ap.add_argument("--qdos", default=pick("qdos", "qdos-dosgl"))
    ap.add_argument("--q2dos", default=pick("q2dos", "q2dos-dosgl"))
    ap.add_argument("--json")
    a = ap.parse_args()
    have_f, have_t = dosgl_have()
    q1_link, q1_call, q1_ext, q1_tok = qdos_needs(a.qdos)
    q2_link, q2_call, q2_ext, q2_tok = q2dos_needs(a.q2dos)
    # Tokens a renderer defines itself are not DOS-GL's to provide.
    own = set()
    for root, sub in ((a.qdos, ["common"]), (a.q2dos, ["ref_gl"])):
        for p in files(root, sub):
            text = code(p)
            own |= set(re.findall(r"#define\s+(GL_\w+)", text))
            own |= set(re.findall(r"\b(GL_\w+)\s*=[^=]", text))       # enum members
    own |= {"GL_DLSYM"}                                                 # a build switch
    called = (q1_call | q2_call) - (q1_ext | q2_ext)
    linked = (q1_link | q2_link) - called - (q1_ext | q2_ext)
    report = {
        "called_missing": sorted(called - have_f),
        "linked_missing": sorted(linked - have_f),
        "extensions": sorted((q1_ext | q2_ext) - have_f),
        "tokens_missing": sorted((q1_tok | q2_tok) - have_t - own),
        "counts": {"called": len(called), "linked": len(linked), "have": len(have_f)},
        "by_game": {"qdos_called_missing": sorted(q1_call - have_f - q1_ext),
                    "q2dos_called_missing": sorted(q2_call - have_f - q2_ext)},
    }
    for key in ("called_missing", "extensions", "tokens_missing", "linked_missing"):
        print("%s (%d):" % (key, len(report[key])))
        print("  " + " ".join(report[key]))
    for k, v in report["by_game"].items():
        print("%s: %s" % (k, " ".join(v)))
    print("counts: %s" % report["counts"])
    if a.json:
        json.dump(report, open(a.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
