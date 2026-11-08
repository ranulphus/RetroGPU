#!/usr/bin/env python3
"""Half-Life game data for Loop A runs (a local fixture, never committed).

  fixtures.py            check ~/VALVE against fixtures.json and install it
  fixtures.py --list     what goes where
  fixtures.py --update   rewrite fixtures.json from ~/VALVE (a new copy of the data)

Source: the owner's Half-Life CD (WON), its valve/ directory copied to
~/VALVE (HL_VALVE). Installed into MGA-Glide's fixture cache, which the dev
container mounts, as $MGA_CACHE/fixtures/games/halflife/VALVE; Loop A puts it
on D: as D:\\HL\\VALVE (games.json). Every file is checked against
tools/halflife/fixtures.json (sizes and sha256 only).

Left out: the Windows game libraries (dlls/*.dll, cl_dlls/*.dll: the DOS
build links its own), the owner's config.cfg, and media/ (the intro videos,
which the engine cannot play without ffmpeg).

Added where the data has no file of that name, loose or in pak0.pak:
tools/halflife/valve/, DOS-GL's own supplements for data older than
Half-Life 1.1 (delta.lst from mkdelta.py, placeholder event scripts) and
without the Steam release's resource/*_english.txt (our own English strings
for the menu, resource/mainui_english.txt).
Standard library only.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "fixtures.json")
CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
DEST = os.path.join(CACHE, "fixtures", "games", "halflife")
SRC = os.environ.get("HL_VALVE", os.path.expanduser("~/VALVE"))
CYLINDER = 63 * 16 * 512               # bytes per cylinder of Loop A's disk geometry
SUPPLEMENTS = os.path.join(HERE, "valve")


def pak_names(path):
    """Lower-case file names in a Quake/Half-Life PAK archive."""
    with open(path, "rb") as f:
        ident, off, size = struct.unpack("<4sii", f.read(12))
        if ident != b"PACK":
            return set()
        f.seek(off)
        return {f.read(64)[:56].split(b"\0")[0].decode("latin-1").lower() for _ in range(size // 64)}


def left_out(rel):
    parts = rel.replace("\\", "/").lower().split("/")
    return (parts[0] in ("dlls", "cl_dlls") and parts[-1].endswith(".dll")) or rel.lower() == "config.cfg" \
        or parts[0] == "media"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def tree(src):
    for root, _, files in os.walk(src):
        for f in sorted(files):
            p = os.path.join(root, f)
            rel = os.path.relpath(p, src).replace(os.sep, "/")
            if not left_out(rel):
                yield rel, p


def update():
    want = {rel: {"size": os.path.getsize(p), "sha256": sha256(p)} for rel, p in tree(SRC)}
    doc = {"_about": "Sizes and sha256 of the Half-Life (WON) files fixtures.py installs; the data itself is never "
                     "committed.", "halflife": want}
    with open(MANIFEST, "w") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write("\n")
    print("fixtures: %s: %d files, %.1f MB" % (MANIFEST, len(want), sum(w["size"] for w in want.values()) / 1e6))


def install():
    want = json.load(open(MANIFEST))["halflife"]
    out = os.path.join(DEST, "VALVE")
    tmp = out + ".part"
    shutil.rmtree(tmp, ignore_errors=True)
    seen, bad = set(), []
    for rel, p in tree(SRC):
        w = want.get(rel)
        if not w or w["size"] != os.path.getsize(p) or w["sha256"] != sha256(p):
            bad.append(rel)
            continue
        seen.add(rel)
        dst = os.path.join(tmp, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(p, dst)
    missing = sorted(set(want) - seen)
    if bad or missing:
        shutil.rmtree(tmp, ignore_errors=True)
        raise SystemExit("fixtures: %s: %d files differ from fixtures.json %s, %d missing %s"
                         % (SRC, len(bad), bad[:5], len(missing), missing[:5]))
    have = {rel.lower() for rel in seen}
    for pak in sorted(r for r in seen if r.lower().endswith(".pak")):
        have |= pak_names(os.path.join(tmp, pak))
    added = []
    for rel, p in ((os.path.relpath(os.path.join(r, f), SUPPLEMENTS).replace(os.sep, "/"), os.path.join(r, f))
                   for r, _, fs in os.walk(SUPPLEMENTS) for f in sorted(fs)):
        if rel.lower() not in have:
            os.makedirs(os.path.dirname(os.path.join(tmp, rel)), exist_ok=True)
            shutil.copyfile(p, os.path.join(tmp, rel))
            added.append(rel)
    if added:
        print("fixtures: added DOS-GL's %s" % ", ".join(sorted(set(a.split("/")[0] for a in added))))
    shutil.rmtree(out, ignore_errors=True)
    os.replace(tmp, out)
    total = sum(w["size"] for w in want.values())
    print("fixtures: halflife -> %s (%d files, %.1f MB; D: needs about %d cylinders)"
          % (out, len(seen), total / 1e6, total // CYLINDER + 1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--update", action="store_true", help="rewrite fixtures.json from the source")
    a = ap.parse_args()
    if not os.path.isdir(SRC):
        raise SystemExit("fixtures: no %s (copy the Half-Life CD's valve directory there, or set HL_VALVE)" % SRC)
    if a.update:
        update()
        return 0
    if a.list:
        print("halflife %s -> %s (%d files)" % (SRC, os.path.join(DEST, "VALVE"),
                                               len(json.load(open(MANIFEST))["halflife"])))
        return 0
    install()
    return 0


if __name__ == "__main__":
    sys.exit(main())
