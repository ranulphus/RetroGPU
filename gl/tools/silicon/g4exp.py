#!/usr/bin/env python3
"""Silicon experiments E1-E7 (examples/g4exp; how to read them:
docs/silicon-experiments.md).

  g4exp.py loopa [--card g450,g400,g200]   emulator baselines in out/g4exp-CARD/
  g4exp.py bench --pc NAME                 a bench PC through MGA-Glide's
                                           tools/bench/run.py (vbench-* for a dry run)
  g4exp.py report DIR...                   the verdict table from each DIR/serial.log

One job runs G4EXP.EXE several times, each under one DOS-GL switch:

  D  defaults
  C  DGL_COMBINER=1      single textures through the G400 combiner (E2)
  T  DGL_TC2_EXTRA=8000  TEXCTL2 bit 15 on every draw (E3: pictures compared with D)
  L  DGL_TLUT=1          paletted textures through the LUT, also on the G400 (E5)
  S  DGL_TLUT=0          paletted textures expanded in software (E5 reference)
  P  DGL_ILOAD=0         sub-images written by the CPU, not the engine (E6)

A PASS says the card does what DOS-GL assumes. The bench job itself passes
only if every run does, so on silicon a FAIL job is an answer, not an error.
"""
import argparse
import concurrent.futures
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MGA = os.environ.get("MGA_GLIDE", os.path.expanduser("~/MGA-Glide"))
HAL = os.environ.get("MGAHAL_DIR", MGA)
EXE = os.path.join(ROOT, "build", "exe", "G4EXP.EXE")

VARIANTS = [("D", None, "124567"), ("C", "DGL_COMBINER=1", "1246"), ("T", "DGL_TC2_EXTRA=8000", "12456"),
            ("L", "DGL_TLUT=1", "5"), ("S", "DGL_TLUT=0", "5"), ("P", "DGL_ILOAD=0", "6")]


def commands():
    cmds = []
    for tag, env, only in VARIANTS:
        if env:
            cmds.append("SET " + env)
        cmds.append("G4EXP.EXE --tag %s --only %s --noexit" % (tag, only))
        if env:
            cmds.append("SET %s=" % env.split("=")[0])
    return cmds


def cmd_loopa(a):
    if not os.path.exists(os.path.join(HAL, "build", "ow", "dos", "UTEXIT.COM")):
        subprocess.run(["make", "-C", ROOT, "-s", "dostools"], check=True)   # C:\HX helpers
    def one(card):
        out = os.path.join(ROOT, "out", "g4exp-" + card)
        cmd = [os.path.join(HAL, "tools", "dev"), "python3", os.path.join(HAL, "tools", "loopa", "run.py"),
               "--name", "g4exp-" + card, "--card", card, "--out", out, "--file", EXE, "--timeout", "600"]
        for c in commands():
            cmd += ["--cmd", c]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return card, out, r.stdout.strip().splitlines()[-1:] or ["(no output)"]
    cards = a.card.split(",")
    with concurrent.futures.ThreadPoolExecutor(len(cards)) as ex:
        done = list(ex.map(one, cards))
    for card, out, last in done:
        print("%s: %s" % (card, last[0]))
    return report([out for _, out, _ in done])


def cmd_bench(a):
    cmd = [sys.executable, os.path.join(MGA, "tools", "bench", "run.py"), "--pc", a.pc, "--name", "g4exp",
           "--file", EXE, "--timeout", "900"]
    for c in commands():
        cmd += ["--cmd", c]
    print(" ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=MGA)
    jobs = sorted(glob.glob(os.path.join(MGA, "out", "bench", a.pc, "g4exp*")), key=os.path.getmtime)
    if not jobs:
        print("g4exp: no job directory under %s/out/bench/%s" % (MGA, a.pc), file=sys.stderr)
        return 1
    return report([jobs[-1]])


def parse(path):
    """Per run tag: tests {name: (result, detail)}, images {name: crc}, stats lines."""
    runs, tag = {}, "?"
    for line in open(path, "rb").read().decode("latin-1").splitlines():
        line = line.strip()
        if line.startswith("HX-STAT g4exp run "):
            tag = dict(kv.split("=", 1) for kv in line.split()[3:] if "=" in kv).get("tag", "?")
            runs[tag] = {"tests": {}, "images": {}, "stats": []}
        elif tag not in runs:
            continue
        elif line.startswith("HX-TEST "):
            parts = line.split(" ", 3)
            runs[tag]["tests"][parts[1]] = (parts[2] if len(parts) > 2 else "?", parts[3] if len(parts) > 3 else "")
        elif line.startswith("HX-IMG "):
            parts = line.split()
            name = parts[1][:-len(tag)] if parts[1].endswith(tag) else parts[1]
            runs[tag]["images"][name] = parts[-1]
        elif line.startswith("HX-STAT g4exp "):
            runs[tag]["stats"].append(line[len("HX-STAT g4exp "):])
    return runs


def report(dirs):
    bad = 0
    for d in dirs:
        serial = os.path.join(d, "serial.log")
        if not os.path.exists(serial):
            print("%s: no serial.log" % d)
            bad += 1
            continue
        runs = parse(serial)
        tags = [t for t, _, _ in VARIANTS if t in runs]
        print("\n== %s" % d)
        for s in runs.get("D", {}).get("stats", []):
            if s.startswith("e7 chip") or s.startswith("e7 cfg40") or s.startswith("e7 cfg50"):
                print("   " + s)
        names = []
        for t in tags:
            names += [n for n in runs[t]["tests"] if n not in names and n != "gl-errors"]
        print("   %-16s %s" % ("", "  ".join("%-4s" % t for t in tags)))
        for n in names:
            cells = [runs[t]["tests"].get(n, ("-", ""))[0] for t in tags]
            print("   %-16s %s" % (n, "  ".join("%-4s" % c for c in cells)))
        for t in tags:
            for n, (res, detail) in runs[t]["tests"].items():
                if res != "PASS" or n.startswith(("e2", "e5", "e6")):
                    print("   %s/%s %s: %s" % (t, n, res, detail))
        if "D" in runs and "T" in runs:
            same = [n for n in runs["T"]["images"] if runs["D"]["images"].get(n) == runs["T"]["images"][n]]
            diff = [n for n in runs["T"]["images"] if n not in same]
            print("   E3 bit 15: %d pictures identical to D%s" % (len(same), ", differ: " + " ".join(diff) if diff
                                                                   else ""))
        missing = [t for t, _, _ in VARIANTS if t not in runs]
        if missing:
            print("   runs missing: %s" % " ".join(missing))
            bad += 1
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="what", required=True)
    p = sp.add_parser("loopa")
    p.add_argument("--card", default="g450,g400,g200")
    p = sp.add_parser("bench")
    p.add_argument("--pc", required=True)
    p = sp.add_parser("report")
    p.add_argument("dirs", nargs="+")
    a = ap.parse_args()
    if a.what == "report":
        return report(a.dirs)
    if not os.path.exists(EXE):
        print("g4exp: build %s first (make examples)" % EXE, file=sys.stderr)
        return 1
    return cmd_loopa(a) if a.what == "loopa" else cmd_bench(a)


if __name__ == "__main__":
    sys.exit(main())
