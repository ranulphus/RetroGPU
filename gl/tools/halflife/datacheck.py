#!/usr/bin/env python3
"""What Half-Life's game code asks for that the owner's data lacks.

  datacheck.py [--hlsdk DIR] [--data DIR] [--json FILE]

The game code (hlsdk-portable, default ~/hlsdk-portable-dos) names resources
in string literals: models, sprites, sounds, event scripts, HUD and VGUI
images. The data (default the installed fixture, VALVE under
$MGA_CACHE/fixtures/games/halflife) holds loose files and pak0.pak. Each
referenced path missing from both is listed, by kind and by which library
(server dlls/, client cl_dll/) names it. Paths built at run time (with %d)
and sentences (!NAME) are not checked.

It also compares the skill cvars (sk_*) the server registers with those
skill.cfg sets: one it does not set reads as zero (a monster with no
health, a weapon with no damage).

Exit status 0 always: this reports; tools/halflife/valve/ supplies what
DOS-GL decides to supply.
"""
import argparse
import json
import os
import re
import struct
import sys

CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
LIT = re.compile(r'"([^"\n]{3,120})"')
KINDS = {".mdl": "model", ".spr": "sprite", ".wav": "sound", ".sc": "event", ".tga": "image", ".bmp": "image",
         ".txt": "text", ".bsp": "map"}


def pak_entries(path):
    with open(path, "rb") as f:
        ident, off, size = struct.unpack("<4sii", f.read(12))
        if ident != b"PACK":
            return {}
        f.seek(off)
        out = {}
        for _ in range(size // 64):
            e = f.read(64)
            name = e[:56].split(b"\0")[0].decode("latin-1").lower()
            out[name] = struct.unpack("<ii", e[56:64])
        return out


def data_files(data):
    have, paks = set(), {}
    for root, _, files in os.walk(data):
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), data).replace(os.sep, "/").lower()
            have.add(rel)
            if rel.endswith(".pak"):
                paks[os.path.join(root, f)] = pak_entries(os.path.join(root, f))
    for entries in paks.values():
        have |= set(entries)
    return have, paks


def read_data_file(data, paks, rel):
    p = os.path.join(data, rel)
    if os.path.exists(p):
        return open(p, "rb").read().decode("latin-1")
    for pak, entries in paks.items():
        if rel in entries:
            off, size = entries[rel]
            with open(pak, "rb") as f:
                f.seek(off)
                return f.read(size).decode("latin-1")
    return ""


def referenced(hlsdk):
    """{path: set of libraries} for resource-like string literals."""
    refs = {}
    for lib, sub in (("server", "dlls"), ("client", "cl_dll"), ("shared", "pm_shared"), ("shared", "game_shared")):
        for root, _, files in os.walk(os.path.join(hlsdk, sub)):
            for f in files:
                if not f.endswith((".cpp", ".c", ".h")):
                    continue
                for m in LIT.finditer(open(os.path.join(root, f), errors="replace").read()):
                    s = m.group(1).strip().replace("\\", "/")
                    ext = os.path.splitext(s)[1].lower()
                    if ext not in KINDS or "%" in s or s.startswith("!") or " " in s:
                        continue
                    if ext == ".wav" and not s.lower().startswith("sound/"):
                        s = "sound/" + s                    # PRECACHE_SOUND paths are under sound/
                    refs.setdefault(s.lower(), set()).add(lib)
    return refs


def skill_cvars(hlsdk):
    regs = set()
    for f in ("dlls/game.cpp",):
        p = os.path.join(hlsdk, f)
        if os.path.exists(p):
            code = re.sub(r"//[^\n]*", "", open(p, errors="replace").read())   # not commented-out ones
            regs |= set(re.findall(r'\{\s*"(sk_[A-Za-z0-9_]+)"', code))
    return regs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hlsdk", default=os.path.expanduser("~/hlsdk-portable-dos"))
    ap.add_argument("--data", default=os.path.join(CACHE, "fixtures", "games", "halflife", "VALVE"))
    ap.add_argument("--json", help="also write the findings here")
    a = ap.parse_args()
    have, paks = data_files(a.data)
    refs = referenced(a.hlsdk)
    missing = {p: sorted(libs) for p, libs in refs.items() if p not in have}
    by_kind = {}
    for p, libs in sorted(missing.items()):
        by_kind.setdefault(KINDS[os.path.splitext(p)[1]], []).append((p, libs))
    print("datacheck: %d resource paths in the game code, %d missing from %s" % (len(refs), len(missing), a.data))
    for kind, items in sorted(by_kind.items()):
        print("  %s (%d):" % (kind, len(items)))
        for p, libs in items:
            print("    %-48s %s" % (p, "+".join(libs)))
    regs = skill_cvars(a.hlsdk)
    cfg = read_data_file(a.data, paks, "skill.cfg")
    set_in_cfg = set(re.findall(r"^\s*(sk_[A-Za-z0-9_]+)", cfg, re.M))
    unset = sorted(regs - set_in_cfg)
    print("datacheck: %d skill cvars registered, %d set by skill.cfg, %d never set%s" %
          (len(regs), len(regs & set_in_cfg), len(unset), (": " + " ".join(unset)) if unset else ""))
    if a.json:
        json.dump({"missing": missing, "skill_unset": unset}, open(a.json, "w"), indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
