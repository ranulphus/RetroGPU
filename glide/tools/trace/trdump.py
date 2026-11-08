#!/usr/bin/env python3
"""Read an MGA-Glide call trace (docs/trace.md).

  trdump.py TRACE            one line per call
  trdump.py --stats TRACE    call counts per function and per frame
"""
import collections
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))   # the repository (build/)
TR_BREF, TR_BMIN, TR_BCACHE = 0x40000000, 64, 512
TR_MAGIC, TR_NULL, TR_VREF, OP_PSEUDO, OP_LFBSPANS, OP_END = 0x5254474D, 0xFFFFFFFF, 0x80000000, 0xF000, 0xF001, 0xFFFF


def api_table():
    return {f["id"]: f for f in json.load(open(os.path.join(ROOT, "build", "gen", "api.json")))}


def records(path):
    data = open(path, "rb").read()
    magic, ver, count, _r = struct.unpack_from("<4I", data, 0)
    if magic != TR_MAGIC:
        raise SystemExit("%s: not an MGTR trace" % path)
    off = 16
    while off + 4 <= len(data):
        (rh,) = struct.unpack_from("<I", data, off)
        off += 4
        if rh == OP_END:
            return
        op, na, nb = rh & 0xFFFF, (rh >> 16) & 0xFF, rh >> 24
        args = list(struct.unpack_from("<%dI" % na, data, off))
        off += 4 * na
        blobs = []
        for _ in range(nb):
            (ln,) = struct.unpack_from("<I", data, off)
            off += 4
            if ln == TR_NULL:
                blobs.append(None)
            elif (ln & 0xC0000000) == TR_BREF:
                blobs.append(("bref", ln & 0xFFFF))
            elif ln & TR_VREF:
                blobs.append(("vref", ln & 0xFFFF))
            else:
                blobs.append(data[off:off + ln])
                off += (ln + 3) & ~3
        yield op, args, blobs


def fmt_arg(t, v):
    if t.strip() == "float":
        return "%g" % struct.unpack("<f", struct.pack("<I", v))[0]
    return str(v) if v < 0x10000 else "0x%x" % v


def main(argv):
    stats = "--stats" in argv
    path = [a for a in argv[1:] if not a.startswith("--")][0]
    api = api_table()
    counts, frame, per_frame = collections.Counter(), 0, collections.Counter()
    for op, args, blobs in records(path):
        if op >= OP_PSEUDO:
            name = "LFB-SPANS" if op == OP_LFBSPANS else "PSEUDO-%x" % op
            params = []
        else:
            name = api[op]["name"]
            params = api[op]["params"]
        counts[name] += 1
        per_frame[frame] += 1
        if name == "grBufferSwap":
            frame += 1
        if not stats:
            a = ", ".join(fmt_arg(params[i][0] if i < len(params) else "", v) for i, v in enumerate(args))
            b = " ".join("NULL" if x is None else ("%s#%d" % (x[0][0], x[1]) if isinstance(x, tuple) else "%dB" % len(x))
                         for x in blobs)
            print("%5d %s(%s)%s" % (frame, name, a, (" [" + b + "]") if b else ""))
    if stats:
        print("frames: %d  calls: %d" % (frame, sum(counts.values())))
        for name, n in counts.most_common():
            print("%8d  %s" % (n, name))


if __name__ == "__main__":
    main(sys.argv)
