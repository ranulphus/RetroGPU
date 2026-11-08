#!/usr/bin/env python3
"""Doom IWADs for Loop A runs (a local fixture, never committed).

  fixtures.py            check ~/DOOM against fixtures.json and install it
  fixtures.py --list     what goes where
  fixtures.py --update   rewrite fixtures.json from ~/DOOM (a new copy of the data)

Source: the owner's IWADs (DOOM.WAD, DOOM2.WAD, TNT.WAD, PLUTONIA.WAD...) in
~/DOOM (DOOM_IWADS). Installed into MGA-Glide's fixture cache, which the dev
container mounts, as $MGA_CACHE/fixtures/games/doom/DOOM; Loop A puts it on D:
as D:\\DOOM (games.json). Every file is checked against
tools/doom/fixtures.json (sizes and sha256 only). Standard library only.
"""
import argparse
import hashlib
import json
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "fixtures.json")
CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
DEST = os.path.join(CACHE, "fixtures", "games", "doom")
SRC = os.environ.get("DOOM_IWADS", os.path.expanduser("~/DOOM"))
CYLINDER = 63 * 16 * 512               # bytes per cylinder of Loop A's disk geometry

# Known IWAD releases by size (the sha256 in fixtures.json is what is checked).
KNOWN = {12408292: "The Ultimate Doom 1.9", 11159840: "Doom 1.9 (registered)", 4196020: "Doom shareware 1.9",
         14604584: "Doom II 1.9", 18195736: "Final Doom: TNT Evilution", 17420824: "Final Doom: The Plutonia Experiment"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def wads(src):
    for f in sorted(os.listdir(src)):
        p = os.path.join(src, f)
        if os.path.isfile(p) and f.lower().endswith(".wad"):
            yield f.upper(), p


def update():
    want = {}
    for name, p in wads(SRC):
        size = os.path.getsize(p)
        want[name] = {"size": size, "sha256": sha256(p), "what": KNOWN.get(size, "unknown release")}
    doc = {"_about": "Sizes and sha256 of the Doom IWADs fixtures.py installs; the data itself is never committed.",
           "doom": want}
    with open(MANIFEST, "w") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write("\n")
    for name, w in sorted(want.items()):
        print("fixtures: %-12s %9d  %s" % (name, w["size"], w["what"]))


def install():
    want = json.load(open(MANIFEST))["doom"]
    out = os.path.join(DEST, "DOOM")
    tmp = out + ".part"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    have = dict(wads(SRC))
    bad = [n for n, w in want.items() if n not in have or os.path.getsize(have[n]) != w["size"]
           or sha256(have[n]) != w["sha256"]]
    if bad:
        shutil.rmtree(tmp, ignore_errors=True)
        raise SystemExit("fixtures: %s: %s missing or different from fixtures.json" % (SRC, ", ".join(bad)))
    for n in sorted(want):
        shutil.copyfile(have[n], os.path.join(tmp, n))
    shutil.rmtree(out, ignore_errors=True)
    os.replace(tmp, out)
    total = sum(w["size"] for w in want.values())
    print("fixtures: doom -> %s (%s; %.1f MB, D: needs about %d cylinders)"
          % (out, ", ".join(sorted(want)), total / 1e6, total // CYLINDER + 1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--update", action="store_true", help="rewrite fixtures.json from the source")
    a = ap.parse_args()
    if not os.path.isdir(SRC):
        raise SystemExit("fixtures: no %s (copy your IWADs there, or set DOOM_IWADS)" % SRC)
    if a.update:
        update()
        return 0
    if a.list:
        print("doom %s -> %s (%s)" % (SRC, os.path.join(DEST, "DOOM"), ", ".join(sorted(json.load(open(MANIFEST))["doom"]))))
        return 0
    install()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
