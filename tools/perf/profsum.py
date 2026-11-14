#!/usr/bin/env python3
"""Sum the stage timers of a profiling build (hal/include/mga/prof.h).

  profsum.py SERIAL.LOG... [--skip N]

Reads MGL-PROF lines (MGA-Glide's build/ow-prof runtime, MGAGLIDE stats=N;
the triangles come from the MGL-STAT line before each) and DGL-PROF lines
(DOS-GL's PROF=1 or PROF=2 build, DGL_STATS=2; tris= on the line itself),
with the DGL-PFINE (PROF=2's stages) and DGL-PCNT lines (entries into each
stage, calls counted) after each. Stage values
are in units of 1024 cycles (in 86Box, the emulated CPU's cycles). The first
--skip lines (default 2) are dropped: they hold start-up and loading. Prints
each stage's share of the time and its cycles per triangle, the library's
total without the waits (drain, vsync, fifo), and register writes,
FIFOSTATUS reads and stage switches per triangle with the timers' own
share. 86Box's speed is not real hardware's: compare builds with each other,
on the same card and demo.
"""
import argparse
import re
import sys

COUNTERS = {"tris", "wr", "fiford", "sw", "ovh", "total"}
WAITS = {"drain", "vsync", "fifo"}
PAIR = re.compile(r"(\w+)=(\d+)")


def read(path, skip):
    lines, tris_next = [], None
    for line in open(path, errors="replace"):
        if "MGL-STAT" in line:
            m = re.search(r"tris=(\d+)", line)
            tris_next = int(m.group(1)) if m else None
        elif "MGL-PROF" in line or "DGL-PROF" in line:
            kv = {k: int(v) for k, v in PAIR.findall(line.split("PROF", 1)[1])}
            if "tris" not in kv and tris_next is not None:
                kv["tris"] = tris_next
            lines.append(kv)
        elif "DGL-PFINE" in line and lines:         # PROF=2's stages, for the DGL-PROF line before it
            lines[-1].update({k: int(v) for k, v in PAIR.findall(line.split("PFINE", 1)[1])})
        elif "DGL-PCNT" in line and lines:          # entries and calls, for the DGL-PROF line before them
            lines[-1].setdefault("_n", {}).update({k: int(v) for k, v in PAIR.findall(line.split("PCNT", 1)[1])})
    return lines[skip:]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--skip", type=int, default=2)
    ap.add_argument("--raw", action="store_true", help="leave the timers' own cost in each stage")
    a = ap.parse_args()
    rc = 0
    for path in a.logs:
        lines = read(path, a.skip)
        if not lines:
            print("%s: no PROF lines after skipping %d" % (path, a.skip))
            rc = 1
            continue
        tot, order = {}, []
        for kv in lines:
            for k, v in kv.items():
                if k == "_n":
                    continue
                if k not in tot:
                    order.append(k)
                tot[k] = tot.get(k, 0) + v if k != "ovh" else v
        stages = [k for k in order if k not in COUNTERS and k != "_n"]
        cyc = {s: tot[s] * 1024.0 for s in stages}
        all_cyc = sum(cyc.values()) or 1.0
        tris = tot.get("tris", 0) or 1
        # DGL-PCNT: each entry into a stage costs it about one switch (ovh
        # cycles, measured at start): taken out unless --raw.
        n = {}
        for kv in lines:
            for k, v in kv.get("_n", {}).items():
                n[k] = n.get(k, 0) + v
        ovh = tot.get("ovh", 0)
        taken = 0.0
        if n and not a.raw:
            for s in stages:
                t = min(cyc[s], ovh * n.get(s, 0))
                cyc[s] -= t
                taken += t
        print("== %s: %d lines, %.0f triangles each" % (path, len(lines), tris / len(lines)))
        if taken:
            print("  (the timers' entries taken out: %.1f%% of the time)" % (100 * taken / all_cyc))
        for s in stages:
            print("  %-8s %5.1f%%  %8.0f cyc/tri  %s" % (s, 100 * cyc[s] / all_cyc, cyc[s] / tris,
                                                       ("%7.1f entries/tri" % (n[s] / tris)) if s in n else ""))
        lib = sum(cyc[s] for s in stages if s != "app" and s not in WAITS)
        print("  library without waits: %.0f cyc/tri (%.1f%%)" % (lib / tris, 100 * lib / all_cyc))
        if "sw" in tot:
            print("  writes/tri %.1f  FIFOSTATUS reads/tri %.2f  switches/tri %.1f  (timers %.1f%% of time)"
                  % (tot.get("wr", 0) / tris, tot.get("fiford", 0) / tris, tot["sw"] / tris,
                     100.0 * tot["sw"] * tot.get("ovh", 0) / all_cyc))
        if n:
            frames = n.get("swap", 0) or 1
            valid = n.get("valid", 0)                   # (one a draw; vidle's are among them)
            print("  a frame: %.0f triangles, %.0f draws (begins), %.0f glVertex, %.0f binds; validations %.0f"
                  " (%.0f%% with nothing to write)" % (tris / frames, n.get("valid", 0) / frames,
                                                      n.get("c_verts", 0) / frames, n.get("c_binds", 0) / frames,
                                                      valid / frames, 100.0 * n.get("vidle", 0) / (valid or 1)))
            print("  a triangle: %.2f vertices transformed (general %.2f, fast %.2f)"
                  % ((n.get("c_xverts", 0) + n.get("c_fverts", 0)) / tris, n.get("c_xverts", 0) / tris,
                     n.get("c_fverts", 0) / tris))
    return rc


if __name__ == "__main__":
    sys.exit(main())
