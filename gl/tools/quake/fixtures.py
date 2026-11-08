#!/usr/bin/env python3
"""Game data for the Quake ports' Loop A runs (local fixtures, never committed).

  fixtures.py            check and install every fixture
  fixtures.py --list     what goes where

Sources:
  quake   retail Quake 1.06 (registered), the owner's install:  ~/ID1   (QUAKE_ID1)
  quake2  retail Quake 2, the owner's install:                   ~/BASEQ2 (QUAKE2_BASEQ2)
  lq      LibreQuake v0.09-beta lite.zip (BSD-3 art, GPL-2 progs), fetched and pinned

Installed into MGA-Glide's fixture cache, which the dev container mounts:
  $MGA_CACHE/fixtures/games/{quake/ID1, quake2/BASEQ2, lq/ID1}
Every file is checked against tools/quake/fixtures.json (sizes and sha256 only).
Files the ports do not use (the owner's CONFIG.CFG, Quake 2's Windows GAMEX86.DLL)
are left out. Quake 2 multiplayer skin icons copied without long file names
(BRIAN~12.PCX) get their real names back (brianna_i.pcx), since q2dos runs with LFN.
Standard library only.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "fixtures.json")
CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
DEST = os.path.join(CACHE, "fixtures", "games")
LQ_URL = "https://github.com/lavenderdotpet/LibreQuake/releases/download/v0.09-beta/lite.zip"
LQ_SHA256 = "428e736b2f01d953e09a08c60bee975bdc4a0ac2219e97fa095c8af41754da83"
LQ_CACHED = os.path.expanduser("~/.cache/dosbench/dl/lite.zip")
SKIP = {"CONFIG.CFG", "GAMEX86.DLL"}
# Quake 2 skin icons whose 8.3 aliases were copied instead of the long names.
Q2_LONG = {"BRIAN~12.PCX": "brianna_i.pcx", "STILE~36.PCX": "stiletto_i.pcx", "JEZEB~24.PCX": "jezebel_i.pcx",
           "CLAYM~12.PCX": "claymore_i.pcx", "NIGHT~32.PCX": "nightops_i.pcx", "POINT~36.PCX": "pointman_i.pcx",
           "RAMPA~44.PCX": "rampage_i.pcx", "HOWIT~24.PCX": "howitzer_i.pcx"}


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
            yield os.path.relpath(p, src), p


def install_tree(key, src, subdir, manifest):
    """Copy src into DEST/key/subdir after checking every file against the manifest."""
    want = manifest[key]
    seen, bad = set(), []
    out = os.path.join(DEST, key, subdir)
    tmp = out + ".part"
    shutil.rmtree(tmp, ignore_errors=True)
    for rel, path in tree(src):
        if os.path.basename(rel).upper() in SKIP:
            continue
        w = want.get(rel)
        if not w or w["size"] != os.path.getsize(path) or w["sha256"] != sha256(path):
            bad.append(rel)
            continue
        seen.add(rel)
        name = Q2_LONG.get(os.path.basename(rel).upper()) if key == "quake2" else None
        dst = os.path.join(tmp, os.path.dirname(rel), name or os.path.basename(rel))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(path, dst)
    missing = sorted(set(k for k in want if os.path.basename(k).upper() not in SKIP) - seen)
    if bad or missing:
        shutil.rmtree(tmp, ignore_errors=True)
        raise SystemExit("fixtures: %s: %d files differ from fixtures.json %s, %d missing %s"
                         % (key, len(bad), bad[:5], len(missing), missing[:5]))
    shutil.rmtree(out, ignore_errors=True)
    os.replace(tmp, out)
    print("fixtures: %s -> %s (%d files)" % (key, out, len(seen)))


def install_lq():
    zpath = LQ_CACHED if os.path.exists(LQ_CACHED) else os.path.join(CACHE, "dl", "lite.zip")
    if not os.path.exists(zpath) or sha256(zpath) != LQ_SHA256:
        os.makedirs(os.path.dirname(zpath), exist_ok=True)
        print("fixtures: fetching %s" % LQ_URL)
        urllib.request.urlretrieve(LQ_URL, zpath + ".part")
        if sha256(zpath + ".part") != LQ_SHA256:
            raise SystemExit("fixtures: lite.zip does not match its pinned sha256")
        os.replace(zpath + ".part", zpath)
    out = os.path.join(DEST, "lq", "ID1")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    with zipfile.ZipFile(zpath) as z:
        for n in ("lite/id1/pak0.pak", "lite/id1/pak1.pak"):
            with z.open(n) as s, open(os.path.join(out, os.path.basename(n).upper()), "wb") as d:
                shutil.copyfileobj(s, d)
    print("fixtures: lq -> %s" % out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    manifest = json.load(open(MANIFEST))
    srcs = {"quake": os.environ.get("QUAKE_ID1", os.path.expanduser("~/ID1")),
            "quake2": os.environ.get("QUAKE2_BASEQ2", os.path.expanduser("~/BASEQ2"))}
    if a.list:
        for k, s in srcs.items():
            print("%-7s %s -> %s (%d files)" % (k, s, os.path.join(DEST, k), len(manifest[k])))
        print("%-7s %s -> %s" % ("lq", LQ_URL, os.path.join(DEST, "lq")))
        return 0
    install_tree("quake", srcs["quake"], "ID1", manifest)
    install_tree("quake2", srcs["quake2"], "BASEQ2", manifest)
    install_lq()
    return 0


if __name__ == "__main__":
    sys.exit(main())
