#!/usr/bin/env python3
"""Extract the Glide names a game EXE asks its loader to resolve.

Game EXEs built against the Glide 2.x import library carry each imported
name as a NUL-terminated, uppercase, __stdcall-decorated string such as
"_GRGLIDEINIT@0". Usage:

    extract_imports.py GAME.EXE            -> names on stdout
    extract_imports.py ZIP::member.exe     -> read from inside a zip
"""
import re
import sys
import zipfile

NAME = re.compile(rb"(?<![A-Z0-9_@])(_G[RU][A-Z0-9]+@\d+)\x00")


def read(spec):
    if "::" in spec:
        zpath, member = spec.split("::", 1)
        with zipfile.ZipFile(zpath) as z:
            for info in z.infolist():
                if info.filename.lower() == member.lower():
                    return z.read(info)
        raise SystemExit("%s not found in %s" % (member, zpath))
    with open(spec, "rb") as f:
        return f.read()


def imports(data):
    return sorted({m.group(1).decode() for m in NAME.finditer(data)})


if __name__ == "__main__":
    for n in imports(read(sys.argv[1])):
        print(n)
